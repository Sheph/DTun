#include "DMasterClient.h"
#include "Logger.h"
#include "RendezvousFastSession.h"
#include "RendezvousSymmConnSession.h"
#include "RendezvousSymmAccSession.h"
#include "DTun/Utils.h"
#include "DTun/SConnector.h"
#include "DTun/SConnection.h"
#include "udt.h"
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>
#include <fcntl.h>

namespace DNode
{
    DMasterClient* theMasterClient = NULL;

    DMasterClient::DMasterClient(DTun::SManager& remoteMgr, DTun::SManager& localMgr,
        const boost::shared_ptr<DTun::AppConfig>& appConfig)
    : remoteMgr_(remoteMgr)
    , localMgr_(localMgr)
    , closing_(false)
    , probedIp_(0)
    , probedPort_(0)
    , nextConnIdx_(0)
    {
        address_ = appConfig->getString("server.address");
        port_ = appConfig->getUInt32("server.port");
        if (!appConfig->isPresent("server.probeAddress") || !appConfig->isPresent("server.probePort")) {
            LOG4CPLUS_WARN(logger(), "No probe server, assuming we're behind non-symmetrical NAT");
            probePort_ = 0;
        } else {
            probeAddress_ = appConfig->getString("server.probeAddress");
            probePort_ = appConfig->getUInt32("server.probePort");
            if (probeAddress_ == address_) {
                LOG4CPLUS_WARN(logger(), "Probe server is the same as master server, assuming we're behind non-symmetrical NAT");
                probeAddress_.clear();
                probePort_ = 0;
            } else {
                LOG4CPLUS_INFO(logger(), "Probe server used: " << probeAddress_ << ":" << probePort_);
            }
        }
        nodeId_ = appConfig->getUInt32("node.id");

        LOG4CPLUS_INFO(logger(), "Server used: " << address_ << ":" << port_ << ", this nodeId: " << nodeId_);

        std::vector<std::string> routeKeys = appConfig->getSubKeys("node.route");

        routes_.resize(routeKeys.size());

        for (std::vector<std::string>::const_iterator it = routeKeys.begin();
             it != routeKeys.end(); ++it) {
            int i = ::atoi(it->c_str());
            if ((i < 0) || (i >= (int)routes_.size())) {
                LOG4CPLUS_WARN(logger(), "Bad route index: " << i);
                continue;
            }
            std::string ipStr = appConfig->getString("node.route." + *it + ".ip");
            if (!DTun::stringToIp(ipStr, routes_[i].ip)) {
                LOG4CPLUS_WARN(logger(), "Cannot parse ip address: " << ipStr);
            }
            std::string maskStr = appConfig->getString("node.route." + *it + ".mask");
            if (!DTun::stringToIp(maskStr, routes_[i].mask)) {
                LOG4CPLUS_WARN(logger(), "Cannot parse ip address: " << ipStr);
            }
            int nodeId = appConfig->getSInt32("node.route." + *it + ".node");
            if (nodeId >= 0) {
                routes_[i].nodeId = nodeId;
            }
        }

        LOG4CPLUS_INFO(logger(), "Routes:");
        for (size_t i = 0; i < routes_.size(); ++i) {
            LOG4CPLUS_INFO(logger(), "#" << i << ": " << DTun::ipToString(routes_[i].ip) << "/" << DTun::ipToString(routes_[i].mask)
                << " -> " << (routes_[i].nodeId ? (int)*routes_[i].nodeId : -1));
        }
    }

    DMasterClient::~DMasterClient()
    {
        boost::mutex::scoped_lock lock(m_);
        closing_ = true;
    }

    bool DMasterClient::start()
    {
        boost::shared_ptr<DTun::SHandle> handle = remoteMgr_.createStreamSocket();
        if (!handle) {
           return false;
        }

        connector_ = handle->createConnector();

        if (probeAddress_.empty()) {
            std::ostringstream os;
            os << port_;

            if (!connector_->connect(address_, os.str(), boost::bind(&DMasterClient::onConnect, this, _1), DTun::SConnector::ModeNormal)) {
                return false;
            }
        } else {
            std::ostringstream os;
            os << probePort_;

            if (!connector_->connect(probeAddress_, os.str(), boost::bind(&DMasterClient::onProbeConnect, this, _1), DTun::SConnector::ModeNormal)) {
                return false;
            }
        }

        return true;
    }

    DTun::ConnId DMasterClient::registerConnection(DTun::UInt32 remoteIp,
        DTun::UInt16 remotePort,
        const RegisterConnectionCallback& callback)
    {
        DTun::UInt32 dstNodeId = 0;

        if (!getDstNodeId(remoteIp, dstNodeId)) {
            LOG4CPLUS_ERROR(logger(), "No route to " << DTun::ipToString(remoteIp));
            return DTun::ConnId();
        }

        boost::mutex::scoped_lock lock(m_);

        if (!conn_ || closing_) {
            return DTun::ConnId();
        }

        boost::shared_ptr<ConnState> connState =
            boost::make_shared<ConnState>();

        DTun::UInt32 connIdx = nextConnIdx_++;
        if (connIdx == 0) {
            connIdx = nextConnIdx_++;
        }

        connState->connId = DTun::ConnId(nodeId_, connIdx);
        connState->remoteIp = remoteIp;
        connState->remotePort = remotePort;
        connState->callback = callback;

        connStates_[connState->connId] = connState;
        rendezvousConnIds_.push_back(connState->connId);

        bool running = false;

        for (ConnStateMap::const_iterator it = connStates_.begin(); it != connStates_.end(); ++it) {
            if (it->second->rSess) {
                running = true;
                break;
            }
        }

        if (!running && (rendezvousConnIds_.size() == 1)) {
            connState->status = ConnStatusPending;

            DTun::DProtocolMsgConnCreate msg;

            msg.connId = DTun::toProtocolConnId(connState->connId);
            msg.dstNodeId = dstNodeId;
            msg.remoteIp = remoteIp;
            msg.remotePort = remotePort;
            msg.fastOnly = 0;

            sendMsg(DPROTOCOL_MSG_CONN_CREATE, &msg, sizeof(msg));
        }

        return connState->connId;
    }

    void DMasterClient::closeConnection(const DTun::ConnId& connId)
    {
        boost::mutex::scoped_lock lock(m_);

        if (closing_) {
            return;
        }

        ConnStateMap::iterator it = connStates_.find(connId);
        if (it == connStates_.end()) {
            return;
        }

        boost::shared_ptr<ConnState> tmp = it->second;

        connStates_.erase(it);
        rendezvousConnIds_.remove(connId);

        assert(!tmp->proxySession);

        if ((tmp->status == ConnStatusPending) && (tmp->mode != RendezvousModeUnknown)) {
            DTun::DProtocolMsgConnClose msg;

            msg.connId = DTun::toProtocolConnId(connId);
            msg.established = 0;

            sendMsg(DPROTOCOL_MSG_CONN_CLOSE, &msg, sizeof(msg));
        }

        lock.unlock();
    }

    void DMasterClient::dump()
    {
        int fdMax = static_cast<int>(::sysconf(_SC_OPEN_MAX));
        int numFds = 0;
        for (int i = 0; i < fdMax; ++i) {
            if (::fcntl(i, F_GETFL) != -1) {
                ++numFds;
            }
        }

        boost::mutex::scoped_lock lock(m_);

        int totStates = connStates_.size();
        int connSess = 0;
        int accSess = 0;
        int prx = 0;
        int numOut = 0;
        for (ConnStateMap::const_iterator it = connStates_.begin(); it != connStates_.end(); ++it) {
            if (it->second->rSess) {
                if (it->second->callback) {
                    ++connSess;
                } else {
                    ++accSess;
                }
            }
            if (it->second->proxySession) {
                ++prx;
            }
            if (it->second->callback) {
                ++numOut;
            }
        }

        LOG4CPLUS_INFO(logger(), "totStates=" << totStates << ", connSess=" << connSess
            << ", accSess=" << accSess << ", prx=" << prx << ", numOut=" << numOut
            << ", " << remoteMgr_.reactor().dump()
            << ", numFds=" << numFds << ", maxFds=" << fdMax);
    }

    bool DMasterClient::getDstNodeId(DTun::UInt32 remoteIp, DTun::UInt32& dstNodeId) const
    {
        for (size_t i = 0; i < routes_.size(); ++i) {
            if ((remoteIp & routes_[i].mask) == (routes_[i].ip & routes_[i].mask)) {
                if (routes_[i].nodeId) {
                    dstNodeId = *routes_[i].nodeId;
                    return true;
                } else {
                    return false;
                }
            }
        }
        return false;
    }

    void DMasterClient::onProbeConnect(int err)
    {
        LOG4CPLUS_TRACE(logger(), "DMasterClient::onProbeConnect(" << err << ")");

        boost::mutex::scoped_lock lock(m_);

        boost::shared_ptr<DTun::SHandle> handle = connector_->handle();

        connector_->close();

        if (err) {
            LOG4CPLUS_ERROR(logger(), "No connection to probe server");

            handle->close();
        } else {
            LOG4CPLUS_INFO(logger(), "Connected to probe server");

            conn_ = handle->createConnection();

            DTun::DProtocolHeader header;

            header.msgCode = DPROTOCOL_MSG_HELLO_PROBE;

            buff_.resize(sizeof(header));
            memcpy(&buff_[0], &header, sizeof(header));

            conn_->write(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&DMasterClient::onProbeSend, this, _1));
        }
    }

    void DMasterClient::onProbeSend(int err)
    {
        LOG4CPLUS_TRACE(logger(), "DMasterClient::onProbeSend(" << err << ")");

        boost::mutex::scoped_lock lock(m_);

        if (err) {
            boost::shared_ptr<DTun::SConnection> tmp = conn_;
            conn_.reset();
            lock.unlock();
            LOG4CPLUS_ERROR(logger(), "Connection to probe server lost");
            return;
        }

        buff_.resize(sizeof(DTun::DProtocolHeader) + sizeof(DTun::DProtocolMsgProbe));
        conn_->read(&buff_[0], &buff_[0] + buff_.size(),
            boost::bind(&DMasterClient::onProbeRecv, this, _1, _2),
            true);
    }

    void DMasterClient::onProbeRecv(int err, int numBytes)
    {
        LOG4CPLUS_TRACE(logger(), "DMasterClient::onProbeRecv(" << err << ", " << numBytes << ")");

        boost::mutex::scoped_lock lock(m_);

        if (err) {
            boost::shared_ptr<DTun::SConnection> tmp = conn_;
            conn_.reset();
            lock.unlock();
            LOG4CPLUS_ERROR(logger(), "Connection to probe server lost");
            return;
        }

        DTun::DProtocolHeader header;
        DTun::DProtocolMsgProbe msg;
        assert(numBytes == (sizeof(header) + sizeof(msg)));
        memcpy(&header, &buff_[0], sizeof(header));
        memcpy(&msg, &buff_[0] + sizeof(header), sizeof(msg));

        if (header.msgCode != DPROTOCOL_MSG_PROBE) {
            boost::shared_ptr<DTun::SConnection> tmp = conn_;
            conn_.reset();
            lock.unlock();
            LOG4CPLUS_ERROR(logger(), "Bad probe response");
            return;
        }

        probedIp_ = msg.srcIp;
        probedPort_ = msg.srcPort;

        LOG4CPLUS_INFO(logger(), "Probed addr = " << DTun::ipPortToString(probedIp_, probedPort_));

        SYSSOCKET sock = conn_->handle()->duplicate();
        if (sock == SYS_INVALID_SOCKET) {
            boost::shared_ptr<DTun::SConnection> tmp = conn_;
            conn_.reset();
            lock.unlock();
            return;
        }

        conn_->close();

        boost::shared_ptr<DTun::SHandle> handle = remoteMgr_.createStreamSocket();
        if (!handle) {
            DTun::closeSysSocketChecked(sock);
            return;
        }

        if (!handle->bind(sock)) {
            DTun::closeSysSocketChecked(sock);
            return;
        }

        connector_ = handle->createConnector();

        std::ostringstream os;
        os << port_;

        if (!connector_->connect(address_, os.str(), boost::bind(&DMasterClient::onConnect, this, _1), DTun::SConnector::ModeNormal)) {
            return;
        }
    }

    void DMasterClient::onConnect(int err)
    {
        LOG4CPLUS_TRACE(logger(), "DMasterClient::onConnect(" << err << ")");

        boost::mutex::scoped_lock lock(m_);

        boost::shared_ptr<DTun::SHandle> handle = connector_->handle();

        connector_->close();

        if (err) {
            LOG4CPLUS_ERROR(logger(), "No connection to server");

            handle->close();
        } else {
            LOG4CPLUS_INFO(logger(), "Connected to server");

            conn_ = handle->createConnection();

            DTun::DProtocolHeader header;
            DTun::DProtocolMsgHello msg;

            header.msgCode = DPROTOCOL_MSG_HELLO;
            msg.nodeId = nodeId_;
            msg.probeIp = probedIp_;
            msg.probePort = probedPort_;

            buff_.resize(sizeof(header) + sizeof(msg));
            memcpy(&buff_[0], &header, sizeof(header));
            memcpy(&buff_[0] + sizeof(header), &msg, sizeof(msg));

            conn_->write(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&DMasterClient::onHelloSend, this, _1));
        }
    }

    void DMasterClient::onHelloSend(int err)
    {
        LOG4CPLUS_TRACE(logger(), "DMasterClient::onHelloSend(" << err << ")");

        boost::mutex::scoped_lock lock(m_);

        if (err) {
            boost::shared_ptr<DTun::SConnection> tmp = conn_;
            conn_.reset();
            lock.unlock();
            LOG4CPLUS_ERROR(logger(), "Connection to server lost");
            return;
        }

        buff_.resize(sizeof(DTun::DProtocolHeader));
        conn_->read(&buff_[0], &buff_[0] + buff_.size(),
            boost::bind(&DMasterClient::onRecvHeader, this, _1, _2),
            true);
    }

    void DMasterClient::onSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff)
    {
        LOG4CPLUS_TRACE(logger(), "DMasterClient::onSend(" << err << ")");

        boost::mutex::scoped_lock lock(m_);

        if (err && conn_) {
            boost::shared_ptr<DTun::SConnection> tmp = conn_;
            conn_.reset();
            lock.unlock();
            LOG4CPLUS_ERROR(logger(), "Connection to server lost");
        }
    }

    void DMasterClient::onRecvHeader(int err, int numBytes)
    {
        LOG4CPLUS_TRACE(logger(), "DMasterClient::onRecvHeader(" << err << ", " << numBytes << ")");

        boost::mutex::scoped_lock lock(m_);

        if (err) {
            boost::shared_ptr<DTun::SConnection> tmp = conn_;
            conn_.reset();
            lock.unlock();
            LOG4CPLUS_ERROR(logger(), "Connection to server lost");
            return;
        }

        DTun::DProtocolHeader header;
        assert(numBytes == sizeof(header));
        memcpy(&header, &buff_[0], numBytes);

        switch (header.msgCode) {
        case DPROTOCOL_MSG_CONN:
            buff_.resize(sizeof(DTun::DProtocolMsgConn));
            conn_->read(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&DMasterClient::onRecvMsgConn, this, _1, _2),
                true);
            break;
        case DPROTOCOL_MSG_CONN_STATUS:
            buff_.resize(sizeof(DTun::DProtocolMsgConnStatus));
            conn_->read(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&DMasterClient::onRecvMsgConnStatus, this, _1, _2),
                true);
            break;
        case DPROTOCOL_MSG_FAST:
            buff_.resize(sizeof(DTun::DProtocolMsgFast));
            conn_->read(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&DMasterClient::onRecvMsgOther, this, _1, _2, header.msgCode),
                true);
            break;
        case DPROTOCOL_MSG_SYMM:
            buff_.resize(sizeof(DTun::DProtocolMsgSymm));
            conn_->read(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&DMasterClient::onRecvMsgOther, this, _1, _2, header.msgCode),
                true);
            break;
        case DPROTOCOL_MSG_SYMM_NEXT:
            buff_.resize(sizeof(DTun::DProtocolMsgSymmNext));
            conn_->read(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&DMasterClient::onRecvMsgOther, this, _1, _2, header.msgCode),
                true);
            break;
        default:
            LOG4CPLUS_ERROR(logger(), "bad msg code: " << static_cast<int>(header.msgCode));
            boost::shared_ptr<DTun::SConnection> tmp = conn_;
            conn_.reset();
            lock.unlock();
            break;
        }
    }

    void DMasterClient::onRecvMsgConn(int err, int numBytes)
    {
        LOG4CPLUS_TRACE(logger(), "DMasterClient::onRecvMsgConn(" << err << ", " << numBytes << ")");

        boost::mutex::scoped_lock lock(m_);

        if (err) {
            boost::shared_ptr<DTun::SConnection> tmp = conn_;
            conn_.reset();
            lock.unlock();
            LOG4CPLUS_ERROR(logger(), "Connection to server lost");
            return;
        }

        DTun::DProtocolMsgConn msg;
        assert(numBytes == sizeof(msg));
        memcpy(&msg, &buff_[0], numBytes);

        LOG4CPLUS_TRACE(logger(), "Proxy request: connId = " << DTun::fromProtocolConnId(msg.connId) << ", remote_addr = " << DTun::ipPortToString(msg.ip, msg.port)
            << ", mode = " << (int)msg.mode);

        boost::shared_ptr<ConnState> connState =
            boost::make_shared<ConnState>();

        connState->connId = DTun::fromProtocolConnId(msg.connId);
        connState->remoteIp = msg.ip;
        connState->remotePort = msg.port;

        switch (msg.mode) {
        case DPROTOCOL_RMODE_FAST:
            connState->mode = RendezvousModeFast;
            break;
        case DPROTOCOL_RMODE_SYMM_CONN:
            connState->mode = RendezvousModeSymmConn;
            break;
        case DPROTOCOL_RMODE_SYMM_ACC:
            connState->mode = RendezvousModeSymmAcc;
            break;
        default:
            LOG4CPLUS_ERROR(logger(), "Bad rmode = " << msg.mode);
            connState->mode = RendezvousModeFast;
            break;
        }

        connState->status = ConnStatusPending;

        if (connStates_.count(connState->connId) > 0) {
            LOG4CPLUS_ERROR(logger(), "Conn " << connState->connId << " already exists");

            buff_.resize(sizeof(DTun::DProtocolHeader));
            conn_->read(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&DMasterClient::onRecvHeader, this, _1, _2),
                true);
        } else {
            connStates_[connState->connId] = connState;
            rendezvousConnIds_.push_back(connState->connId);

            buff_.resize(sizeof(DTun::DProtocolHeader));
            conn_->read(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&DMasterClient::onRecvHeader, this, _1, _2),
                true);

            while (processRendezvous(lock)) {}
        }
    }

    void DMasterClient::onRecvMsgConnStatus(int err, int numBytes)
    {
        LOG4CPLUS_TRACE(logger(), "DMasterClient::onRecvMsgConnStatus(" << err << ", " << numBytes << ")");

        boost::mutex::scoped_lock lock(m_);

        if (err) {
            boost::shared_ptr<DTun::SConnection> tmp = conn_;
            conn_.reset();
            lock.unlock();
            LOG4CPLUS_ERROR(logger(), "Connection to server lost");
            return;
        }

        DTun::DProtocolMsgConnStatus msg;
        assert(numBytes == sizeof(msg));
        memcpy(&msg, &buff_[0], numBytes);

        DTun::ConnId connId = DTun::fromProtocolConnId(msg.connId);

        buff_.resize(sizeof(DTun::DProtocolHeader));
        conn_->read(&buff_[0], &buff_[0] + buff_.size(),
            boost::bind(&DMasterClient::onRecvHeader, this, _1, _2),
            true);

        ConnStateMap::iterator it = connStates_.find(connId);
        if (it == connStates_.end()) {
            LOG4CPLUS_WARN(logger(), "Conn " << connId << " not found");
            return;
        }

        switch (msg.statusCode) {
        case DPROTOCOL_STATUS_NONE:
            // fastOnly response in case of symm rmode, no connection created.
            assert(it->second->mode == RendezvousModeUnknown);
            assert(it->second->status == ConnStatusPending);
            assert(it->second->triedFastOnly);
            it->second->status = ConnStatusNone;
            break;
        case DPROTOCOL_STATUS_PENDING:
            // connection created, set mode.
            assert(it->second->mode == RendezvousModeUnknown);
            assert(it->second->status == ConnStatusPending);
            switch (msg.mode) {
            case DPROTOCOL_RMODE_FAST:
                it->second->mode = RendezvousModeFast;
                break;
            case DPROTOCOL_RMODE_SYMM_CONN:
                it->second->mode = RendezvousModeSymmConn;
                break;
            case DPROTOCOL_RMODE_SYMM_ACC:
                it->second->mode = RendezvousModeSymmAcc;
                break;
            default:
                LOG4CPLUS_ERROR(logger(), "Bad rmode = " << msg.mode);
                it->second->mode = RendezvousModeFast;
                break;
            }
            break;
        case DPROTOCOL_STATUS_ESTABLISHED:
            if (it->second->rSess) {
                assert(it->second->mode != RendezvousModeUnknown);
                it->second->status = ConnStatusEstablished;
                boost::shared_ptr<RendezvousSession> rSess = it->second->rSess;
                lock.unlock();
                rSess->onEstablished();
                lock.lock();
            }
            break;
        default: {
            boost::shared_ptr<ConnState> tmp = it->second;
            connStates_.erase(it);
            rendezvousConnIds_.remove(connId);
            lock.unlock();
            RegisterConnectionCallback cb = tmp->callback;
            tmp.reset();
            if (cb) {
                cb(msg.statusCode, boost::shared_ptr<DTun::SHandle>(), 0, 0);
                cb = RegisterConnectionCallback();
            }
            lock.lock();
            break;
        }
        }

        while (processRendezvous(lock)) {}
    }

    void DMasterClient::onRecvMsgOther(int err, int numBytes, DTun::UInt8 msgId)
    {
        LOG4CPLUS_TRACE(logger(), "DMasterClient::onRecvMsgOther(" << err << ", " << numBytes << ", " << (int)msgId << ")");

        boost::mutex::scoped_lock lock(m_);

        if (err) {
            boost::shared_ptr<DTun::SConnection> tmp = conn_;
            conn_.reset();
            lock.unlock();
            LOG4CPLUS_ERROR(logger(), "Connection to server lost");
            return;
        }

        DTun::DProtocolConnId dConnId;
        assert(numBytes >= (int)sizeof(dConnId));
        memcpy(&dConnId, &buff_[0], sizeof(dConnId));

        DTun::ConnId connId = DTun::fromProtocolConnId(dConnId);

        ConnStateMap::iterator it = connStates_.find(connId);
        if (it == connStates_.end()) {
            LOG4CPLUS_WARN(logger(), "Conn " << connId << " not found");
            buff_.resize(sizeof(DTun::DProtocolHeader));
            conn_->read(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&DMasterClient::onRecvHeader, this, _1, _2),
                true);
            return;
        }

        if (it->second->rSess && (it->second->status != ConnStatusEstablished)) {
            boost::shared_ptr<RendezvousSession> rSess = it->second->rSess;
            lock.unlock();
            rSess->onMsg(msgId, &buff_[0]);
            lock.lock();
            if (conn_) {
                buff_.resize(sizeof(DTun::DProtocolHeader));
                conn_->read(&buff_[0], &buff_[0] + buff_.size(),
                    boost::bind(&DMasterClient::onRecvHeader, this, _1, _2),
                    true);
            }
        }
    }

    void DMasterClient::onProxyDone(const DTun::ConnId& connId)
    {
        LOG4CPLUS_TRACE(logger(), "DMasterClient::onProxyDone(" << connId << ")");

        boost::mutex::scoped_lock lock(m_);

        if (closing_) {
            return;
        }

        ConnStateMap::iterator it = connStates_.find(connId);
        if (it == connStates_.end()) {
            return;
        }

        boost::shared_ptr<ConnState> tmp = it->second;

        connStates_.erase(it);

        lock.unlock();
    }

    void DMasterClient::onRendezvous(const DTun::ConnId& connId, int err, SYSSOCKET s, DTun::UInt32 ip, DTun::UInt16 port)
    {
        LOG4CPLUS_TRACE(logger(), "DMasterClient::onRendezvous(" << connId << ", err=" << err << ", s=" << s << ", " << DTun::ipPortToString(ip, port) << ")");

        boost::mutex::scoped_lock lock(m_);

        if (closing_) {
            if (s != SYS_INVALID_SOCKET) {
                DTun::closeSysSocketChecked(s);
            }
            return;
        }

        ConnStateMap::iterator it = connStates_.find(connId);
        assert(it != connStates_.end());
        if (it == connStates_.end()) {
            if (s != SYS_INVALID_SOCKET) {
                DTun::closeSysSocketChecked(s);
            }
            return;
        }

        boost::shared_ptr<ConnState> tmp = it->second;

        it->second->rSess.reset();

        bool sendClose = true;

        boost::shared_ptr<DTun::SHandle> handle;

        if (!err) {
            handle = remoteMgr_.createStreamSocket();
            if (!handle || !handle->bind(s)) {
                err = DPROTOCOL_STATUS_ERR_UNKNOWN;
                if (s != SYS_INVALID_SOCKET) {
                    DTun::closeSysSocketChecked(s);
                }
            }
            s = SYS_INVALID_SOCKET;
        }

        if (err) {
            connStates_.erase(it);
        } else {
            sendClose = (it->second->status != ConnStatusEstablished);
            tmp->status = it->second->status = ConnStatusEstablished;
            it->second->boundHandle = handle;
        }

        if (sendClose) {
            DTun::DProtocolMsgConnClose msg;

            msg.connId = DTun::toProtocolConnId(connId);
            msg.established = (tmp->status == ConnStatusEstablished);

            sendMsg(DPROTOCOL_MSG_CONN_CLOSE, &msg, sizeof(msg));
        }

        lock.unlock();

        tmp->rSess.reset();

        if (tmp->callback) {
            tmp->callback(err, handle, ip, port);
            tmp->callback = RegisterConnectionCallback();
        } else if (!err) {
            boost::shared_ptr<ProxySession> proxySession =
                boost::make_shared<ProxySession>(boost::ref(remoteMgr_), boost::ref(localMgr_));
            bool res = proxySession->start(handle, tmp->remoteIp, tmp->remotePort, ip, port,
                boost::bind(&DMasterClient::onProxyDone, this, connId));
            lock.lock();
            it = connStates_.find(connId);
            if (it != connStates_.end()) {
                if (res) {
                    it->second->proxySession = proxySession;
                } else {
                    connStates_.erase(it);
                }
            }
            lock.unlock();
            proxySession.reset();
        }

        lock.lock();

        while (processRendezvous(lock)) {}
    }

    void DMasterClient::sendMsg(DTun::UInt8 msgCode, const void* msg, int msgSize)
    {
        DTun::DProtocolHeader header;

        header.msgCode = msgCode;

        boost::shared_ptr<std::vector<char> > sndBuff =
            boost::make_shared<std::vector<char> >(sizeof(header) + msgSize);

        memcpy(&(*sndBuff)[0], &header, sizeof(header));
        memcpy(&(*sndBuff)[0] + sizeof(header), msg, msgSize);

        conn_->write(&(*sndBuff)[0], &(*sndBuff)[0] + sndBuff->size(),
            boost::bind(&DMasterClient::onSend, this, _1, sndBuff));
    }

    bool DMasterClient::processRendezvous(boost::mutex::scoped_lock& lock)
    {
        bool running = false;

        for (ConnStateMap::const_iterator it = connStates_.begin(); it != connStates_.end(); ++it) {
            if (it->second->rSess) {
                if (it->second->mode > RendezvousModeFast) {
                    // Symm rendezvous session is in progress, don't do anything until its done.
                    return false;
                } else {
                    assert(it->second->mode == RendezvousModeFast);
                    running = true;
                    break;
                }
            }
        }

        for (ConnIdList::iterator it = rendezvousConnIds_.begin(); it != rendezvousConnIds_.end();) {
            DTun::ConnId connId = *it;
            ConnStateMap::iterator jt = connStates_.find(connId);
            if (jt == connStates_.end()) {
                rendezvousConnIds_.erase(it++);
                continue;
            }

            assert(!jt->second->rSess);
            assert(jt->second->status != ConnStatusEstablished);
            assert(!jt->second->boundHandle);
            assert(!jt->second->proxySession);

            if ((jt->second->mode == RendezvousModeUnknown) && (jt->second->status == ConnStatusNone)) {
                if (running) {
                    if (jt->second->triedFastOnly) {
                        break;
                    } else {
                        jt->second->triedFastOnly = true;
                    }
                }

                jt->second->status = ConnStatusPending;

                DTun::DProtocolMsgConnCreate msg;

                msg.connId = DTun::toProtocolConnId(connId);
                bool res = getDstNodeId(jt->second->remoteIp, msg.dstNodeId);
                assert(res);
                msg.remoteIp = jt->second->remoteIp;
                msg.remotePort = jt->second->remotePort;
                msg.fastOnly = running ? 1 : 0;

                sendMsg(DPROTOCOL_MSG_CONN_CREATE, &msg, sizeof(msg));
                break;
            } else if (jt->second->mode >= RendezvousModeFast) {
                assert(jt->second->status == ConnStatusPending);
                rendezvousConnIds_.erase(it++);

                bool res = false;

                switch (jt->second->mode) {
                case RendezvousModeFast: {
                    boost::shared_ptr<RendezvousFastSession> rSess =
                        boost::make_shared<RendezvousFastSession>(boost::ref(localMgr_), boost::ref(remoteMgr_), nodeId_, connId);
                    jt->second->rSess = rSess;
                    res = rSess->start(address_, port_, boost::bind(&DMasterClient::onRendezvous, this, connId, _1, _2, _3, _4));
                    break;
                }
                case RendezvousModeSymmConn: {
                    /*boost::shared_ptr<RendezvousSymmConnSession> rSess =
                        boost::make_shared<RendezvousSymmConnSession>(connId, !!jt->second->callback);
                    jt->second->rSess = rSess;
                    res = rSess->start(boost::bind(&DMasterClient::onRendezvous, this, connId, _1, _2, _3, _4));*/
                    break;
                }
                case RendezvousModeSymmAcc: {
                    /*boost::shared_ptr<RendezvousSymmAccSession> rSess =
                        boost::make_shared<RendezvousSymmAccSession>(connId, !!jt->second->callback);
                    jt->second->rSess = rSess;
                    res = rSess->start(boost::bind(&DMasterClient::onRendezvous, this, connId, _1, _2, _3, _4));*/
                    break;
                }
                default:
                    assert(false);
                    break;
                }

                if (!res) {
                    boost::shared_ptr<RendezvousSession> rSess = jt->second->rSess;
                    RegisterConnectionCallback cb = jt->second->callback;
                    connStates_.erase(jt);

                    DTun::DProtocolMsgConnClose msg;

                    msg.connId = DTun::toProtocolConnId(connId);
                    msg.established = 0;

                    sendMsg(DPROTOCOL_MSG_CONN_CLOSE, &msg, sizeof(msg));
                    lock.unlock();
                    rSess.reset();
                    if (cb) {
                        cb(DPROTOCOL_STATUS_ERR_CANCELED, boost::shared_ptr<DTun::SHandle>(), 0, 0);
                    }
                    return true;
                }
            } else {
                break;
            }
        }

        return false;
    }
}

#include "DMasterClient.h"
#include "Logger.h"
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

    DMasterClient::SysSocketHolder::SysSocketHolder()
    : sock(SYS_INVALID_SOCKET)
    {
    }

    DMasterClient::SysSocketHolder::SysSocketHolder(SYSSOCKET sock)
    : sock(sock)
    {
    }

    DMasterClient::SysSocketHolder::~SysSocketHolder()
    {
        if (sock != SYS_INVALID_SOCKET) {
            DTun::closeSysSocketChecked(sock);
            sock = SYS_INVALID_SOCKET;
        }
    }

    DMasterClient::DMasterClient(DTun::SManager& remoteMgr, DTun::SManager& localMgr,
        const boost::shared_ptr<DTun::AppConfig>& appConfig)
    : remoteMgr_(remoteMgr)
    , localMgr_(localMgr)
    , closing_(false)
    , nextConnId_(0)
    , numOutConnections_(0)
    {
        address_ = appConfig->getString("server.address");
        port_ = appConfig->getUInt32("server.port");
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

        std::ostringstream os;
        os << port_;

        if (!connector_->connect(address_, os.str(), boost::bind(&DMasterClient::onConnect, this, _1), DTun::SConnector::ModeNormal)) {
            return false;
        }

        return true;
    }

    void DMasterClient::changeNumOutConnections(int diff)
    {
        boost::mutex::scoped_lock lock(m_);
        numOutConnections_ += diff;
    }

    DTun::UInt32 DMasterClient::registerConnection(SYSSOCKET s,
        DTun::UInt32 remoteIp,
        DTun::UInt16 remotePort,
        const RegisterConnectionCallback& callback)
    {
        DTun::UInt32 dstNodeId = 0;

        if (!getDstNodeId(remoteIp, dstNodeId)) {
            LOG4CPLUS_ERROR(logger(), "No route to " << DTun::ipToString(remoteIp));
            return 0;
        }

        boost::mutex::scoped_lock lock(m_);

        if (!conn_ || closing_) {
            DTun::closeSysSocketChecked(s);
            return 0;
        }

        boost::shared_ptr<DMasterSession> sess =
            boost::make_shared<DMasterSession>(boost::ref(remoteMgr_), address_, port_);

        DTun::UInt32 connId = nextConnId_++;
        if (connId == 0) {
            connId = nextConnId_++;
        }

        if (!sess->startConnector(s, nodeId_, dstNodeId, connId, remoteIp, remotePort,
            boost::bind(&DMasterClient::onRegisterConnection, this, _1, connId))) {
            return 0;
        }

        connMasterSessions_[connId].sess = sess;
        connMasterSessions_[connId].callback = callback;

        return connId;
    }

    void DMasterClient::cancelConnection(DTun::UInt32 connId)
    {
        boost::mutex::scoped_lock lock(m_);

        if (closing_) {
            return;
        }

        ConnMasterSessionMap::iterator it = connMasterSessions_.find(connId);
        if (it == connMasterSessions_.end()) {
            return;
        }

        ConnMasterSession tmp = it->second;

        connMasterSessions_.erase(it);

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
        LOG4CPLUS_INFO(logger(), "connSess=" << connMasterSessions_.size()
            << ", accSess=" << accMasterSessions_.size() << ", prx=" << proxySessions_.size() << ", numOut=" << numOutConnections_
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

            buff_.resize(sizeof(header) + sizeof(msg));
            memcpy(&buff_[0], &header, sizeof(header));
            memcpy(&buff_[0] + sizeof(header), &msg, sizeof(msg));

            conn_->write(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&DMasterClient::onSend, this, _1));
        }
    }

    void DMasterClient::onSend(int err)
    {
        LOG4CPLUS_TRACE(logger(), "DMasterClient::onSend(" << err << ")");

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
        case DPROTOCOL_MSG_CONN_OK:
            buff_.resize(sizeof(DTun::DProtocolMsgConnOK));
            conn_->read(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&DMasterClient::onRecvMsgConnOK, this, _1, _2),
                true);
            break;
        case DPROTOCOL_MSG_CONN_ERR:
            buff_.resize(sizeof(DTun::DProtocolMsgConnErr));
            conn_->read(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&DMasterClient::onRecvMsgConnErr, this, _1, _2),
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

        LOG4CPLUS_TRACE(logger(), "Proxy request: src = " << msg.srcNodeId
            << ", src_addr = " << DTun::ipPortToString(msg.srcNodeIp, msg.srcNodePort)
            << ", connId = " << msg.connId << ", remote_addr = " << DTun::ipPortToString(msg.ip, msg.port));

        startAccMasterSession(msg.srcNodeId, msg.connId, msg.ip, msg.port, msg.srcNodeIp, msg.srcNodePort);

        buff_.resize(sizeof(DTun::DProtocolHeader));
        conn_->read(&buff_[0], &buff_[0] + buff_.size(),
            boost::bind(&DMasterClient::onRecvHeader, this, _1, _2),
            true);
    }

    void DMasterClient::onRecvMsgConnOK(int err, int numBytes)
    {
        LOG4CPLUS_TRACE(logger(), "DMasterClient::onRecvMsgConnOK(" << err << ", " << numBytes << ")");

        boost::mutex::scoped_lock lock(m_);

        if (err) {
            boost::shared_ptr<DTun::SConnection> tmp = conn_;
            conn_.reset();
            lock.unlock();
            LOG4CPLUS_ERROR(logger(), "Connection to server lost");
            return;
        }

        DTun::DProtocolMsgConnOK msg;
        assert(numBytes == sizeof(msg));
        memcpy(&msg, &buff_[0], numBytes);

        ConnMasterSessionMap::iterator it = connMasterSessions_.find(msg.connId);
        if (it == connMasterSessions_.end()) {
            LOG4CPLUS_ERROR(logger(), "connId " << msg.connId << " not found");
            return;
        }

        ConnMasterSession tmp = it->second;

        connMasterSessions_.erase(it);

        buff_.resize(sizeof(DTun::DProtocolHeader));
        conn_->read(&buff_[0], &buff_[0] + buff_.size(),
            boost::bind(&DMasterClient::onRecvHeader, this, _1, _2),
            true);

        lock.unlock();

        tmp.sess.reset();
        tmp.callback(0, msg.dstNodeIp, msg.dstNodePort);
    }

    void DMasterClient::onRecvMsgConnErr(int err, int numBytes)
    {
        LOG4CPLUS_TRACE(logger(), "DMasterClient::onRecvMsgConnErr(" << err << ", " << numBytes << ")");

        boost::mutex::scoped_lock lock(m_);

        if (err) {
            boost::shared_ptr<DTun::SConnection> tmp = conn_;
            conn_.reset();
            lock.unlock();
            LOG4CPLUS_ERROR(logger(), "Connection to server lost");
            return;
        }

        DTun::DProtocolMsgConnErr msg;
        assert(numBytes == sizeof(msg));
        memcpy(&msg, &buff_[0], numBytes);

        ConnMasterSessionMap::iterator it = connMasterSessions_.find(msg.connId);
        if (it == connMasterSessions_.end()) {
            LOG4CPLUS_ERROR(logger(), "connId " << msg.connId << " not found");
            return;
        }

        ConnMasterSession tmp = it->second;

        connMasterSessions_.erase(it);

        buff_.resize(sizeof(DTun::DProtocolHeader));
        conn_->read(&buff_[0], &buff_[0] + buff_.size(),
            boost::bind(&DMasterClient::onRecvHeader, this, _1, _2),
            true);

        lock.unlock();

        tmp.sess.reset();
        LOG4CPLUS_ERROR(logger(), "connId " << msg.connId << " failed with errCode " << msg.errCode);
        tmp.callback(msg.errCode, 0, 0);
    }

    void DMasterClient::onRegisterConnection(int err, DTun::UInt32 connId)
    {
        boost::mutex::scoped_lock lock(m_);

        ConnMasterSessionMap::iterator it = connMasterSessions_.find(connId);
        if (it == connMasterSessions_.end()) {
            return;
        }

        ConnMasterSession tmp = it->second;

        it->second.sess.reset();

        if (err) {
            connMasterSessions_.erase(it);
        }

        lock.unlock();

        if (err) {
            tmp.sess.reset();
            tmp.callback(err, 0, 0);
        }
    }

    void DMasterClient::onAcceptConnection(int err, const boost::weak_ptr<DMasterSession>& sess,
        DTun::UInt32 localIp,
        DTun::UInt16 localPort,
        DTun::UInt32 remoteIp,
        DTun::UInt16 remotePort)
    {
        LOG4CPLUS_TRACE(logger(), "DMasterClient::onAcceptConnection(" << err << ")");

        boost::shared_ptr<DMasterSession> sess_shared = sess.lock();
        if (!sess_shared) {
            return;
        }

        boost::mutex::scoped_lock lock(m_);

        AccMasterSessions::iterator it = accMasterSessions_.find(sess_shared);
        if (it == accMasterSessions_.end()) {
            assert(false);
            return;
        }

        SYSSOCKET boundSock = it->second->sock;
        it->second->sock = SYS_INVALID_SOCKET;

        accMasterSessions_.erase(it);

        lock.unlock();

        sess_shared.reset();

        if (!err) {
            boost::shared_ptr<ProxySession> proxySess =
                boost::make_shared<ProxySession>(boost::ref(remoteMgr_), boost::ref(localMgr_));

            lock.lock();
            if (proxySess->start(boundSock, localIp, localPort, remoteIp, remotePort,
                boost::bind(&DMasterClient::onProxyDone, this, boost::weak_ptr<ProxySession>(proxySess)))) {
                proxySessions_.insert(proxySess);
            }
            lock.unlock();
        } else {
            DTun::closeSysSocketChecked(boundSock);
        }
    }

    void DMasterClient::onProxyDone(const boost::weak_ptr<ProxySession>& sess)
    {
        LOG4CPLUS_TRACE(logger(), "DMasterClient::onProxyDone()");

        boost::shared_ptr<ProxySession> sess_shared = sess.lock();
        if (!sess_shared) {
            return;
        }

        boost::mutex::scoped_lock lock(m_);

        if (closing_) {
            return;
        }

        proxySessions_.erase(sess_shared);

        lock.unlock();
    }

    void DMasterClient::startAccMasterSession(DTun::UInt32 srcNodeId,
        DTun::UInt32 srcConnId,
        DTun::UInt32 localIp,
        DTun::UInt16 localPort,
        DTun::UInt32 remoteIp,
        DTun::UInt16 remotePort)
    {
        SYSSOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == SYS_INVALID_SOCKET) {
            LOG4CPLUS_ERROR(logger(), "Cannot create UDP socket");
            return;
        }

        SYSSOCKET boundSock = dup(s);
        if (boundSock == -1) {
            LOG4CPLUS_ERROR(logger(), "Cannot dup UDP socket");
            DTun::closeSysSocketChecked(s);
            return;
        }

        struct sockaddr_in addr;

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        int res = ::bind(s, (const struct sockaddr*)&addr, sizeof(addr));

        if (res == SYS_SOCKET_ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot bind UDP socket");
            DTun::closeSysSocketChecked(s);
            DTun::closeSysSocketChecked(boundSock);
            return;
        }

        boost::shared_ptr<DMasterSession> sess =
            boost::make_shared<DMasterSession>(boost::ref(remoteMgr_), address_, port_);

        if (!sess->startAcceptor(s, srcNodeId, nodeId_, srcConnId,
            boost::bind(&DMasterClient::onAcceptConnection, this, _1, boost::weak_ptr<DMasterSession>(sess),
                localIp, localPort, remoteIp, remotePort))) {
            DTun::closeSysSocketChecked(boundSock);
            return;
        }

        accMasterSessions_.insert(std::make_pair(sess, boost::make_shared<SysSocketHolder>(boundSock)));
    }
}

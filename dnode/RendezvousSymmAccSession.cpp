#include "RendezvousSymmAccSession.h"
#include "Logger.h"
#include "DTun/Utils.h"
#include <boost/make_shared.hpp>

namespace DNode
{
    RendezvousSymmAccSession::RendezvousSymmAccSession(DTun::SManager& localMgr, DTun::SManager& remoteMgr,
        DTun::UInt32 nodeId, const DTun::ConnId& connId, const std::string& serverAddr, int serverPort,
        const std::string& probeAddr, int probePort,
        DTun::UInt32 destIp)
    : RendezvousSession(nodeId, connId)
    , localMgr_(localMgr)
    , remoteMgr_(remoteMgr)
    , serverAddr_(serverAddr)
    , serverPort_(serverPort)
    , probeAddr_(probeAddr)
    , probePort_(probePort)
    , windowSize_(601)
    , owner_(connId.nodeId == nodeId)
    , stepIdx_(0)
    , numPingSent_(0)
    , destIp_(destIp)
    , destDiscoveredPort_(0)
    , srcPort_(0)
    , watch_(boost::make_shared<DTun::OpWatch>(boost::ref(localMgr.reactor())))
    {
        assert(destIp_ != 0);
    }

    RendezvousSymmAccSession::~RendezvousSymmAccSession()
    {
        watch_->close();
    }

    bool RendezvousSymmAccSession::start(const boost::shared_ptr<DTun::SConnection>& serverConn,
        const HandleKeepaliveList& keepalive,
        const Callback& callback)
    {
        setStarted();

        boost::mutex::scoped_lock lock(m_);

        callback_ = callback;
        serverConn_ = serverConn;
        keepalive_ = keepalive;

        if (owner_) {
            return true;
        }

        SYSSOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == SYS_INVALID_SOCKET) {
            LOG4CPLUS_ERROR(logger(), "Cannot create UDP socket");
            return false;
        }

        SYSSOCKET boundSock = ::dup(s);
        if (boundSock == -1) {
            LOG4CPLUS_ERROR(logger(), "Cannot dup UDP socket");
            DTun::closeSysSocketChecked(s);
            return false;
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
            return false;
        }

        boost::shared_ptr<DTun::SHandle> handle = localMgr_.createDatagramSocket(boundSock);
        if (!handle) {
            DTun::closeSysSocketChecked(s);
            DTun::closeSysSocketChecked(boundSock);
            return false;
        }

        pingConn_ = handle->createConnection();
        masterSession_ =
            boost::make_shared<DMasterSession>(boost::ref(remoteMgr_), serverAddr_, serverPort_);
        bool startRes = masterSession_->startSymm(s, nodeId(), connId(),
            boost::bind(&RendezvousSymmAccSession::onHelloSend, this, _1, _2));

        lock.unlock();

        localMgr_.reactor().post(
            watch_->wrap(boost::bind(&RendezvousSymmAccSession::onCheckStartTimeout, this)));

        return startRes;
    }

    void RendezvousSymmAccSession::onMsg(DTun::UInt8 msgId, const void* msg)
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousSymmAccSession::onMsg(" << (int)msgId << ")");

        if (msgId != DPROTOCOL_MSG_SYMM_NEXT) {
            return;
        }

        boost::mutex::scoped_lock lock(m_);

        if (!callback_) {
            return;
        }

        if (pingConn_) {
            SYSSOCKET s = pingConn_->handle()->duplicate();
            lock.unlock();
            pingConn_->close();
            lock.lock();
            if (!callback_) {
                DTun::closeSysSocketChecked(s);
                return;
            }
            masterSession_ =
                boost::make_shared<DMasterSession>(boost::ref(remoteMgr_), probeAddr_, probePort_);
            if (!masterSession_->startSymm(s, nodeId(), connId(),
                boost::bind(&RendezvousSymmAccSession::onByeSend, this, _1, _2), true)) {
                Callback cb = callback_;
                callback_ = Callback();
                lock.unlock();
                cb(1, SYS_INVALID_SOCKET, 0, 0, 0);
            }

            return;
        }

        lock.unlock();

        localMgr_.reactor().post(
            watch_->wrap(boost::bind(&RendezvousSymmAccSession::onSymmNextTimeout, this)),
            ((stepIdx_ == 0) ? 0 : 1000));
    }

    void RendezvousSymmAccSession::onEstablished()
    {
        // may come after sending final ping, but we don't care, we'll
        // complete very soon...
    }

    void RendezvousSymmAccSession::onSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff)
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousSymmAccSession::onSend(" << err << ")");
    }

    void RendezvousSymmAccSession::onHelloSend(int err, DTun::UInt16 srcPort)
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousSymmAccSession::onHelloSend(" << err << ", " << srcPort << ")");

        boost::shared_ptr<DTun::SHandle> masterHandle;

        if (!err) {
            srcPort_ = srcPort;

            for (size_t i = 0; i < keepalive_.size(); ++i) {
                if (keepalive_[i].srcPort == srcPort) {
                    LOG4CPLUS_ERROR(logger(), "Port stolen!");
                }
            }

            masterHandle = masterSession_->conn()->handle();
            masterSession_->conn()->close(true);
        }

        boost::mutex::scoped_lock lock(m_);

        if (callback_ && !err) {
            masterHandle_ = masterHandle;
        }

        if (!callback_ || !err) {
            return;
        }

        Callback cb = callback_;
        callback_ = Callback();
        lock.unlock();
        cb(err, SYS_INVALID_SOCKET, 0, 0, 0);
    }

    void RendezvousSymmAccSession::onByeSend(int err, DTun::UInt16 srcPort)
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousSymmAccSession::onByeSend(" << err << ", " << srcPort << ")");

        boost::mutex::scoped_lock lock(m_);

        if (!callback_) {
            return;
        }

        if (err) {
            Callback cb = callback_;
            callback_ = Callback();
            lock.unlock();
            cb(err, SYS_INVALID_SOCKET, 0, 0, 0);
            return;
        }

        if (srcPort != srcPort_) {
            LOG4CPLUS_ERROR(logger(), "Port changed!");
        }

        srcPort_ = srcPort;

        for (size_t i = 0; i < keepalive_.size(); ++i) {
            if (keepalive_[i].srcPort == srcPort) {
                LOG4CPLUS_ERROR(logger(), "Port stolen!");
            }
        }

        boost::shared_ptr<DTun::SHandle> handle = localMgr_.createDatagramSocket(masterSession_->conn()->handle()->duplicate());
        pingConn_ = handle->createConnection();

        lock.unlock();

        localMgr_.reactor().post(
            watch_->wrap(boost::bind(&RendezvousSymmAccSession::onSymmNextTimeout, this)),
            ((stepIdx_ == 0) ? 0 : 1000));
    }

    void RendezvousSymmAccSession::onPingSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff)
    {
        boost::mutex::scoped_lock lock(m_);

        if (!callback_) {
            return;
        }

        if (err) {
            LOG4CPLUS_TRACE(logger(), "RendezvousSymmAccSession::onPingSend(" << err << ")");
            Callback cb = callback_;
            callback_ = Callback();
            lock.unlock();
            cb(err, SYS_INVALID_SOCKET, 0, 0, 0);
            return;
        }

        ++numPingSent_;

        DTun::UInt16 port = getCurrentPort();

        if (port && (numPingSent_ < windowSize_)) {
            pingConn_->writeTo(&(*sndBuff)[0], &(*sndBuff)[0] + sndBuff->size(),
                destIp_, port,
                boost::bind(&RendezvousSymmAccSession::onPingSend, this, _1, sndBuff));
            return;
        }

        lock.unlock();

        for (size_t i = 0; i < keepalive_.size(); ++i) {
            keepalive_[i].handle->ping(keepalive_[i].destIp, keepalive_[i].destPort);
        }

        lock.lock();

        if (!callback_) {
            return;
        }

        ++stepIdx_;

        sendSymmNext();
    }

    void RendezvousSymmAccSession::onRecvPing(int err, int numBytes, DTun::UInt32 ip, DTun::UInt16 port, const boost::shared_ptr<std::vector<char> >& rcvBuff)
    {
        boost::mutex::scoped_lock lock(m_);

        if (!callback_) {
            LOG4CPLUS_TRACE(logger(), "RendezvousSymmAccSession::onRecvPing(" << err << ", " << numBytes << ", src=" << DTun::ipPortToString(ip, port) << ")");
            return;
        }

        if (err) {
            LOG4CPLUS_TRACE(logger(), "RendezvousSymmAccSession::onRecvPing(" << err << ", " << numBytes << ", src=" << DTun::ipPortToString(ip, port) << ")");
            Callback cb = callback_;
            callback_ = Callback();
            lock.unlock();
            cb(err, SYS_INVALID_SOCKET, 0, 0, 0);
            return;
        }

        if (stepIdx_ == -1) {
            return;
        }

        LOG4CPLUS_TRACE(logger(), "RendezvousSymmAccSession::onRecvPing(" << err << ", " << numBytes << ", src=" << DTun::ipPortToString(ip, port) << ")");

        if (numBytes != 4) {
            if (ip != destIp_) {
                LOG4CPLUS_WARN(logger(), "RendezvousSymmAccSession::onRecvPing bad ping len: "
                    << numBytes);
            }
            pingConn_->readFrom(&(*rcvBuff)[0], &(*rcvBuff)[0] + rcvBuff->size(),
                boost::bind(&RendezvousSymmAccSession::onRecvPing, this, _1, _2, _3, _4, rcvBuff));
            return;
        } else {
            uint8_t a = (*rcvBuff)[0];
            uint8_t b = (*rcvBuff)[1];
            uint8_t c = (*rcvBuff)[2];
            uint8_t d = (*rcvBuff)[3];
            if ((a != 0xAA) || (b != 0xBB) || (c != 0xCC) || (d != 0xDD)) {
                LOG4CPLUS_WARN(logger(), "RendezvousSymmAccSession::onRecvPing bad ping: "
                    << (int)a << "," << (int)b << "," << (int)c << "," << (int)d);
                pingConn_->readFrom(&(*rcvBuff)[0], &(*rcvBuff)[0] + rcvBuff->size(),
                    boost::bind(&RendezvousSymmAccSession::onRecvPing, this, _1, _2, _3, _4, rcvBuff));
                return;
            }
            if (ip != destIp_) {
                LOG4CPLUS_WARN(logger(), "RendezvousSymmAccSession::onRecvPing bad source ip");
                pingConn_->readFrom(&(*rcvBuff)[0], &(*rcvBuff)[0] + rcvBuff->size(),
                    boost::bind(&RendezvousSymmAccSession::onRecvPing, this, _1, _2, _3, _4, rcvBuff));
                return;
            }
        }

        destDiscoveredPort_ = port;

        pingConn_->readFrom(&(*rcvBuff)[0], &(*rcvBuff)[0] + rcvBuff->size(),
            boost::bind(&RendezvousSymmAccSession::onRecvPing, this, _1, _2, _3, _4, rcvBuff));
    }

    void RendezvousSymmAccSession::onSymmNextTimeout()
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousSymmAccSession::onSymmNextTimeout()");

        masterSession_.reset();
        masterHandle_.reset();

        boost::mutex::scoped_lock lock(m_);

        if (!callback_) {
            return;
        }

        if (destDiscoveredPort_ != 0) {
            stepIdx_ = -1;
            lock.unlock();

            localMgr_.reactor().post(
                watch_->wrap(boost::bind(&RendezvousSymmAccSession::onSendFinalTimeout, this, 3)), 0);

            return;
        }

        lock.unlock();

        pingConn_.reset();

        lock.lock();

        if (!callback_) {
            return;
        }

        DTun::UInt16 port = getCurrentPort();

        if (!port) {
            LOG4CPLUS_WARN(logger(), "RendezvousSymmAccSession::onSymmNextTimeout(" << connId() << ", no more ports to try)");
            Callback cb = callback_;
            callback_ = Callback();
            lock.unlock();
            cb(1, SYS_INVALID_SOCKET, 0, 0, 0);
            return;
        }

        SYSSOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == SYS_INVALID_SOCKET) {
            LOG4CPLUS_ERROR(logger(), "Cannot create UDP socket");
            Callback cb = callback_;
            callback_ = Callback();
            lock.unlock();
            cb(1, SYS_INVALID_SOCKET, 0, 0, 0);
            return;
        }

        SYSSOCKET boundSock = ::dup(s);
        if (boundSock == -1) {
            LOG4CPLUS_ERROR(logger(), "Cannot dup UDP socket");
            DTun::closeSysSocketChecked(s);
            Callback cb = callback_;
            callback_ = Callback();
            lock.unlock();
            cb(1, SYS_INVALID_SOCKET, 0, 0, 0);
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
            Callback cb = callback_;
            callback_ = Callback();
            lock.unlock();
            cb(1, SYS_INVALID_SOCKET, 0, 0, 0);
            return;
        }

        boost::shared_ptr<DTun::SHandle> handle = localMgr_.createDatagramSocket(boundSock);
        if (!handle) {
            DTun::closeSysSocketChecked(s);
            DTun::closeSysSocketChecked(boundSock);
            Callback cb = callback_;
            callback_ = Callback();
            lock.unlock();
            cb(1, SYS_INVALID_SOCKET, 0, 0, 0);
            return;
        }

        pingConn_ = handle->createConnection();
        masterSession_ =
            boost::make_shared<DMasterSession>(boost::ref(remoteMgr_), serverAddr_, serverPort_);
        if (!masterSession_->startSymm(s, nodeId(), connId(),
            boost::bind(&RendezvousSymmAccSession::onHelloSend, this, _1, _2))) {
            Callback cb = callback_;
            callback_ = Callback();
            lock.unlock();
            cb(1, SYS_INVALID_SOCKET, 0, 0, 0);
            return;
        }

        lock.unlock();

        localMgr_.reactor().post(
            watch_->wrap(boost::bind(&RendezvousSymmAccSession::onCheckStartTimeout, this)));
    }

    void RendezvousSymmAccSession::onCheckStartTimeout()
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousSymmAccSession::onCheckStartTimeout(" << connId() << ")");

        boost::mutex::scoped_lock lock(m_);

        if (!callback_) {
            return;
        }

        if (masterHandle_ && masterHandle_->canReuse()) {
            boost::shared_ptr<std::vector<char> > rcvBuff =
                boost::make_shared<std::vector<char> >(1024);
            pingConn_->readFrom(&(*rcvBuff)[0], &(*rcvBuff)[0] + rcvBuff->size(),
                boost::bind(&RendezvousSymmAccSession::onRecvPing, this, _1, _2, _3, _4, rcvBuff));

            boost::shared_ptr<std::vector<char> > sndBuff =
                boost::make_shared<std::vector<char> >(4);

            (*sndBuff)[0] = 0xAA;
            (*sndBuff)[1] = 0xBB;
            (*sndBuff)[2] = 0xCC;
            (*sndBuff)[3] = 0xDD;

            DTun::UInt16 port = getCurrentPort();
            assert(port);

            numPingSent_= 0;

            pingConn_->writeTo(&(*sndBuff)[0], &(*sndBuff)[0] + sndBuff->size(),
                destIp_, port,
                boost::bind(&RendezvousSymmAccSession::onPingSend, this, _1, sndBuff));

            return;
        }

        lock.unlock();

        localMgr_.reactor().post(
            watch_->wrap(boost::bind(&RendezvousSymmAccSession::onCheckStartTimeout, this)), 250);
    }

    void RendezvousSymmAccSession::onSendFinalTimeout(int cnt)
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousSymmAccSession::onSendFinalTimeout(" << connId() << ", " << cnt << ")");

        boost::mutex::scoped_lock lock(m_);

        if (!callback_) {
            return;
        }

        if (cnt == 0) {
            Callback cb = callback_;
            callback_ = Callback();
            lock.unlock();
            SYSSOCKET s = pingConn_->handle()->duplicate();
            pingConn_->close();
            cb(0, s, destIp_, destDiscoveredPort_, srcPort_);
            return;
        }

        boost::shared_ptr<std::vector<char> > sndBuff =
            boost::make_shared<std::vector<char> >(4);

        (*sndBuff)[0] = 0xAA;
        (*sndBuff)[1] = 0xBB;
        (*sndBuff)[2] = 0xCC;
        (*sndBuff)[3] = 0xEE; // This is final ping.

        pingConn_->writeTo(&(*sndBuff)[0], &(*sndBuff)[0] + sndBuff->size(),
            destIp_, destDiscoveredPort_,
            boost::bind(&RendezvousSymmAccSession::onSend, _1, sndBuff));

        lock.unlock();

        localMgr_.reactor().post(
            watch_->wrap(boost::bind(&RendezvousSymmAccSession::onSendFinalTimeout, this, cnt - 1)), 150);
    }

    DTun::UInt16 RendezvousSymmAccSession::getCurrentPort()
    {
        int port = 1024 + (stepIdx_ * windowSize_) + numPingSent_;
        return htons((port > 65535) ? 0 : port);
    }

    void RendezvousSymmAccSession::sendSymmNext()
    {
        DTun::DProtocolHeader header;
        DTun::DProtocolMsgSymmNext msg;

        header.msgCode = DPROTOCOL_MSG_SYMM_NEXT;
        msg.connId = DTun::toProtocolConnId(connId());

        boost::shared_ptr<std::vector<char> > sndBuff =
            boost::make_shared<std::vector<char> >(sizeof(header) + sizeof(msg));

        memcpy(&(*sndBuff)[0], &header, sizeof(header));
        memcpy(&(*sndBuff)[0] + sizeof(header), &msg, sizeof(msg));

        serverConn_->write(&(*sndBuff)[0], &(*sndBuff)[0] + sndBuff->size(),
            boost::bind(&RendezvousSymmAccSession::onSend, _1, sndBuff));
    }
}

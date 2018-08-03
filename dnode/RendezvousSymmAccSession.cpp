#include "RendezvousSymmAccSession.h"
#include "Logger.h"
#include "DTun/Utils.h"
#include <boost/make_shared.hpp>

namespace DNode
{
    RendezvousSymmAccSession::RendezvousSymmAccSession(DTun::SManager& localMgr, DTun::SManager& remoteMgr,
        DTun::UInt32 nodeId, const DTun::ConnId& connId, const std::string& serverAddr, int serverPort,
        DTun::UInt32 destIp, const boost::shared_ptr<PortAllocator>& portAllocator, bool bestEffort)
    : RendezvousSession(nodeId, connId)
    , localMgr_(localMgr)
    , remoteMgr_(remoteMgr)
    , serverAddr_(serverAddr)
    , serverPort_(serverPort)
    , windowSize_(299)
    , owner_(connId.nodeId == nodeId)
    , portAllocator_(portAllocator)
    , bestEffort_(bestEffort)
    , ready_(false)
    , stepIdx_(0)
    , numPingSent_(0)
    , destIp_(destIp)
    , destDiscoveredPort_(0)
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
            if (bestEffort_) {
                portReservation_ =
                    portAllocator_->reserveSymmPortsBestEffort(windowSize_ + 1,
                        watch_->wrap(boost::bind(&RendezvousSymmAccSession::onPortReservation, this)));
                return true;
            } else {
                portReservation_ = portAllocator_->reserveSymmPorts(windowSize_ + 1);
                if (!portReservation_) {
                    return false;
                }

                sendReady();

                return true;
            }
        }

        if (!ready_) {
            return true;
        }

        if (bestEffort_) {
            portReservation_ =
                portAllocator_->reserveSymmPortsBestEffort(windowSize_ + 1,
                    watch_->wrap(boost::bind(&RendezvousSymmAccSession::onPortReservation, this)));
            return true;
        }

        portReservation_ = portAllocator_->reserveSymmPorts(windowSize_ + 1);
        if (!portReservation_) {
            return false;
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

        portReservation_->use();

        pingConn_ = handle->createConnection();
        masterSession_ =
            boost::make_shared<DMasterSession>(boost::ref(remoteMgr_), serverAddr_, serverPort_);
        bool startRes = masterSession_->startSymm(s, nodeId(), connId(),
            boost::bind(&RendezvousSymmAccSession::onHelloSend, this, _1));

        lock.unlock();

        localMgr_.reactor().post(
            watch_->wrap(boost::bind(&RendezvousSymmAccSession::onCheckStartTimeout, this)));

        return startRes;
    }

    void RendezvousSymmAccSession::onMsg(DTun::UInt8 msgId, const void* msg)
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousSymmAccSession::onMsg(" << (int)msgId << ")");

        boost::mutex::scoped_lock lock(m_);

        if (msgId == DPROTOCOL_MSG_READY) {
            ready_ = true;
            if (!started()) {
                return;
            }

            if (!callback_) {
                return;
            }

            if (portReservation_) {
                lock.unlock();
                onPortReservation();
            } else {
                if (bestEffort_) {
                    portReservation_ =
                        portAllocator_->reserveSymmPortsBestEffort(windowSize_ + 1,
                            watch_->wrap(boost::bind(&RendezvousSymmAccSession::onPortReservation, this)));
                } else {
                    portReservation_ = portAllocator_->reserveSymmPorts(windowSize_ + 1);
                    if (!portReservation_) {
                        Callback cb = callback_;
                        callback_ = Callback();
                        lock.unlock();
                        cb(1, SYS_INVALID_SOCKET, 0, 0, boost::shared_ptr<PortReservation>());
                        return;
                    }
                    lock.unlock();
                    onPortReservation();
                }
            }
        } else if (msgId == DPROTOCOL_MSG_SYMM_NEXT) {
            if (!callback_) {
                return;
            }

            lock.unlock();

            localMgr_.reactor().post(
                watch_->wrap(boost::bind(&RendezvousSymmAccSession::onSymmNextTimeout, this)),
                ((stepIdx_ == 0) ? 0 : 1000));
        }
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

    void RendezvousSymmAccSession::onPortReservation()
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousSymmAccSession::onPortReservation()");

        boost::mutex::scoped_lock lock(m_);

        if (!callback_) {
            return;
        }

        if (!ready_) {
            assert(owner_);
            if (owner_) {
                ready_ = true;
                sendReady();
            }
            return;
        }

        SYSSOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == SYS_INVALID_SOCKET) {
            LOG4CPLUS_ERROR(logger(), "Cannot create UDP socket");
            Callback cb = callback_;
            callback_ = Callback();
            lock.unlock();
            cb(1, SYS_INVALID_SOCKET, 0, 0, boost::shared_ptr<PortReservation>());
            return;
        }

        SYSSOCKET boundSock = ::dup(s);
        if (boundSock == -1) {
            LOG4CPLUS_ERROR(logger(), "Cannot dup UDP socket");
            DTun::closeSysSocketChecked(s);
            Callback cb = callback_;
            callback_ = Callback();
            lock.unlock();
            cb(1, SYS_INVALID_SOCKET, 0, 0, boost::shared_ptr<PortReservation>());
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
            cb(1, SYS_INVALID_SOCKET, 0, 0, boost::shared_ptr<PortReservation>());
            return;
        }

        boost::shared_ptr<DTun::SHandle> handle = localMgr_.createDatagramSocket(boundSock);
        if (!handle) {
            DTun::closeSysSocketChecked(s);
            DTun::closeSysSocketChecked(boundSock);
            Callback cb = callback_;
            callback_ = Callback();
            lock.unlock();
            cb(1, SYS_INVALID_SOCKET, 0, 0, boost::shared_ptr<PortReservation>());
            return;
        }

        portReservation_->use();

        pingConn_ = handle->createConnection();
        masterSession_ =
            boost::make_shared<DMasterSession>(boost::ref(remoteMgr_), serverAddr_, serverPort_);
        if (!masterSession_->startSymm(s, nodeId(), connId(),
            boost::bind(&RendezvousSymmAccSession::onHelloSend, this, _1))) {
            Callback cb = callback_;
            callback_ = Callback();
            lock.unlock();
            cb(1, SYS_INVALID_SOCKET, 0, 0, boost::shared_ptr<PortReservation>());
            return;
        }

        lock.unlock();

        localMgr_.reactor().post(
            watch_->wrap(boost::bind(&RendezvousSymmAccSession::onCheckStartTimeout, this)));
    }

    void RendezvousSymmAccSession::onHelloSend(int err)
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousSymmAccSession::onHelloSend(" << err << ")");

        boost::shared_ptr<DTun::SHandle> masterHandle;

        if (!err) {
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
        cb(err, SYS_INVALID_SOCKET, 0, 0, boost::shared_ptr<PortReservation>());
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
            cb(err, SYS_INVALID_SOCKET, 0, 0, boost::shared_ptr<PortReservation>());
            return;
        }

        ++numPingSent_;

        DTun::UInt16 port = getCurrentPort();

        if (port && (numPingSent_ < windowSize_)) {
            pingConn_->writeTo(&(*sndBuff)[0], &(*sndBuff)[0] + sndBuff->size(),
                destIp_, port,
                boost::bind(&RendezvousSymmAccSession::onPingSend, this, _1, sndBuff));
            portReservation_->use();
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
            cb(err, SYS_INVALID_SOCKET, 0, 0, boost::shared_ptr<PortReservation>());
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
        portReservation_.reset();

        lock.lock();

        if (!callback_) {
            return;
        }

        DTun::UInt16 port = getCurrentPort();

        if (!port || (stepIdx_ >= 3)) {
            LOG4CPLUS_WARN(logger(), "RendezvousSymmAccSession::onSymmNextTimeout(" << connId() << ", no more ports to try)");
            Callback cb = callback_;
            callback_ = Callback();
            lock.unlock();
            cb(1, SYS_INVALID_SOCKET, 0, 0, boost::shared_ptr<PortReservation>());
            return;
        }

        sendReady();
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
            portReservation_->use();

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
            portReservation_->keepalive();
            Callback cb = callback_;
            callback_ = Callback();
            lock.unlock();
            SYSSOCKET s = pingConn_->handle()->duplicate();
            pingConn_->close();
            cb(0, s, destIp_, destDiscoveredPort_, portReservation_);
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
        portReservation_->use();

        lock.unlock();

        localMgr_.reactor().post(
            watch_->wrap(boost::bind(&RendezvousSymmAccSession::onSendFinalTimeout, this, cnt - 1)), 150);
    }

    DTun::UInt16 RendezvousSymmAccSession::getCurrentPort()
    {
        int port = 1024 + (stepIdx_ * windowSize_) + numPingSent_;
        return htons((port > 65535) ? 0 : port);
    }

    void RendezvousSymmAccSession::sendReady()
    {
        DTun::DProtocolHeader header;
        DTun::DProtocolMsgReady msg;

        header.msgCode = DPROTOCOL_MSG_READY;
        msg.connId = DTun::toProtocolConnId(connId());

        boost::shared_ptr<std::vector<char> > sndBuff =
            boost::make_shared<std::vector<char> >(sizeof(header) + sizeof(msg));

        memcpy(&(*sndBuff)[0], &header, sizeof(header));
        memcpy(&(*sndBuff)[0] + sizeof(header), &msg, sizeof(msg));

        serverConn_->write(&(*sndBuff)[0], &(*sndBuff)[0] + sndBuff->size(),
            boost::bind(&RendezvousSymmAccSession::onSend, _1, sndBuff));
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

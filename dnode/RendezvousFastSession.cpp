#include "RendezvousFastSession.h"
#include "Logger.h"
#include "DTun/Utils.h"
#include <boost/make_shared.hpp>

namespace DNode
{
    RendezvousFastSession::RendezvousFastSession(DTun::SManager& localMgr, DTun::SManager& remoteMgr, DTun::UInt32 nodeId, const DTun::ConnId& connId,
        const std::string& serverAddr, int serverPort,
        const boost::shared_ptr<PortAllocator>& portAllocator, bool bestEffort)
    : RendezvousSession(nodeId, connId)
    , localMgr_(localMgr)
    , remoteMgr_(remoteMgr)
    , serverAddr_(serverAddr)
    , serverPort_(serverPort)
    , portAllocator_(portAllocator)
    , bestEffort_(bestEffort)
    , owner_(connId.nodeId == nodeId)
    , ready_(false)
    , stepIdx_(0)
    , origTTL_(255)
    , ttl_(2)
    , next_(true)
    , destIp_(0)
    , destPort_(0)
    , watch_(boost::make_shared<DTun::OpWatch>(boost::ref(localMgr.reactor())))
    {
    }

    RendezvousFastSession::~RendezvousFastSession()
    {
        watch_->close();
    }

    bool RendezvousFastSession::start(const boost::shared_ptr<DTun::SConnection>& serverConn, const Callback& callback)
    {
        setStarted();

        boost::mutex::scoped_lock lock(m_);

        callback_ = callback;
        serverConn_ = serverConn;

        if (owner_) {
            if (bestEffort_) {
                portReservation_ =
                    portAllocator_->reserveFastPortsBestEffort(2,
                        watch_->wrap(boost::bind(&RendezvousFastSession::onPortReservation, this)));
                return true;
            } else {
                portReservation_ = portAllocator_->reserveFastPorts(2);
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
                portAllocator_->reserveFastPortsBestEffort(2,
                    watch_->wrap(boost::bind(&RendezvousFastSession::onPortReservation, this)));
            return true;
        }

        portReservation_ = portAllocator_->reserveFastPorts(2);
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
        origTTL_ = pingConn_->handle()->getTTL();
        if (origTTL_ == 0)
            origTTL_ = 255;
        masterSession_ =
            boost::make_shared<DMasterSession>(boost::ref(remoteMgr_), serverAddr_, serverPort_);
        bool startRes = masterSession_->startFast(s, nodeId(), connId(),
            boost::bind(&RendezvousFastSession::onHelloSend, this, _1));

        lock.unlock();

        localMgr_.reactor().post(
            watch_->wrap(boost::bind(&RendezvousFastSession::onCheckStartTimeout, this)));

        return startRes;
    }

    void RendezvousFastSession::onMsg(DTun::UInt8 msgId, const void* msg)
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousFastSession::onMsg(" << (int)msgId << ")");

        boost::mutex::scoped_lock lock(m_);

        if (msgId == DPROTOCOL_MSG_READY) {
            ready_ = true;
            if (!started()) {
                return;
            }

            if (!callback_) {
                return;
            }

            if (!owner_ && portReservation_) {
                ++stepIdx_;
                ttl_ = 2;
                next_ = true;

                LOG4CPLUS_WARN(logger(), "RendezvousFastSession::onMsg(NEXT " << stepIdx_ << ", " << connId() << ")");

                lock.unlock();

                watch_->close();
                watch_ = boost::make_shared<DTun::OpWatch>(boost::ref(localMgr_.reactor()));

                masterSession_.reset();
                masterHandle_.reset();

                portReservation_.reset();
                pingConn_.reset();

                lock.lock();

                if (!callback_) {
                    return;
                }

                destIp_ = 0;
                destPort_ = 0;
            }

            if (bestEffort_) {
                portReservation_ =
                    portAllocator_->reserveFastPortsBestEffort(2,
                        watch_->wrap(boost::bind(&RendezvousFastSession::onPortReservation, this)));
            } else {
                portReservation_ = portAllocator_->reserveFastPorts(2);
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
        } else if (msgId == DPROTOCOL_MSG_FAST) {
            if (!callback_) {
                return;
            }

            const DTun::DProtocolMsgFast* msgFast = (const DTun::DProtocolMsgFast*)msg;

            destIp_ = msgFast->nodeIp;
            destPort_ = msgFast->nodePort;

            assert(portReservation_);

            if (owner_) {
                lock.unlock();
                onPortReservation();
            }
        } else if (msgId == DPROTOCOL_MSG_NEXT) {
            next_ = true;
        }
    }

    void RendezvousFastSession::onEstablished()
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousFastSession::onEstablished()");

        boost::mutex::scoped_lock lock(m_);

        if (!callback_) {
            return;
        }

        assert(destIp_ != 0);

        Callback cb = callback_;
        callback_ = Callback();
        portReservation_->keepalive();
        lock.unlock();
        pingConn_->handle()->setTTL(origTTL_);
        SYSSOCKET s = pingConn_->handle()->duplicate();
        pingConn_->close();
        cb(0, s, destIp_, destPort_, portReservation_);
    }

    void RendezvousFastSession::onSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff)
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousFastSession::onSend(" << err << ")");
    }

    void RendezvousFastSession::onPortReservation()
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousFastSession::onPortReservation()");

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

        if (owner_ && portReservationNext_) {
            ++stepIdx_;
            ttl_ = 2;
            next_ = true;

            lock.unlock();

            masterSession_.reset();
            masterHandle_.reset();

            portReservation_ = portReservationNext_;
            portReservationNext_.reset();
            pingConn_.reset();

            lock.lock();

            if (!callback_) {
                return;
            }

            portReservation_->use();
            sendReady();
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
        origTTL_ = pingConn_->handle()->getTTL();
        if (origTTL_ == 0)
            origTTL_ = 255;
        masterSession_ =
            boost::make_shared<DMasterSession>(boost::ref(remoteMgr_), serverAddr_, serverPort_);
        if (!masterSession_->startFast(s, nodeId(), connId(),
            boost::bind(&RendezvousFastSession::onHelloSend, this, _1))) {
            Callback cb = callback_;
            callback_ = Callback();
            lock.unlock();
            cb(1, SYS_INVALID_SOCKET, 0, 0, boost::shared_ptr<PortReservation>());
            return;
        }

        lock.unlock();

        localMgr_.reactor().post(
            watch_->wrap(boost::bind(&RendezvousFastSession::onCheckStartTimeout, this)));
    }

    void RendezvousFastSession::onHelloSend(int err)
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousFastSession::onHelloSend(" << err << ")");

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

    void RendezvousFastSession::onPingSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff)
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousFastSession::onPingSend(" << err << ")");

        boost::mutex::scoped_lock lock(m_);

        if (!callback_ || !err) {
            if (callback_ && !err)
                sendNext();
            return;
        }

        Callback cb = callback_;
        callback_ = Callback();
        lock.unlock();
        cb(err, SYS_INVALID_SOCKET, 0, 0, boost::shared_ptr<PortReservation>());
    }

    void RendezvousFastSession::onRecvPing(int err, int numBytes, DTun::UInt32 ip, DTun::UInt16 port)
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousFastSession::onRecvPing(" << err << ", " << numBytes << ", src=" << DTun::ipPortToString(ip, port) << ")");

        boost::mutex::scoped_lock lock(m_);

        if (!callback_ || (destIp_ == 0)) {
            return;
        }

        if (!err) {
            if (numBytes != 4) {
                if ((ip != destIp_) || (port != destPort_)) {
                    LOG4CPLUS_WARN(logger(), "RendezvousFastSession::onRecvPing bad ping len: "
                        << numBytes);
                } else {
                    // we might receive SYN here from LTUDP session that's already created by the peer,
                    // we'll reply to it later in our own LTUDP session...
                }
                pingConn_->readFrom(&rcvBuff_[0], &rcvBuff_[0] + rcvBuff_.size(),
                    boost::bind(&RendezvousFastSession::onRecvPing, this, _1, _2, _3, _4));
                return;
            } else {
                uint8_t a = rcvBuff_[0];
                uint8_t b = rcvBuff_[1];
                uint8_t c = rcvBuff_[2];
                uint8_t d = rcvBuff_[3];
                if ((a != 0xAA) || (b != 0xBB) || (c != 0xCC) || (d != 0xDD)) {
                    LOG4CPLUS_WARN(logger(), "RendezvousFastSession::onRecvPing bad ping: "
                        << (int)a << "," << (int)b << "," << (int)c << "," << (int)d);
                    pingConn_->readFrom(&rcvBuff_[0], &rcvBuff_[0] + rcvBuff_.size(),
                        boost::bind(&RendezvousFastSession::onRecvPing, this, _1, _2, _3, _4));
                    return;
                }
                if ((ip != destIp_) || (port != destPort_)) {
                    LOG4CPLUS_WARN(logger(), "RendezvousFastSession::onRecvPing bad source ip/port " << DTun::ipPortToString(ip, port)
                        << ", need " << DTun::ipPortToString(destIp_, destPort_));
                    pingConn_->readFrom(&rcvBuff_[0], &rcvBuff_[0] + rcvBuff_.size(),
                        boost::bind(&RendezvousFastSession::onRecvPing, this, _1, _2, _3, _4));
                    return;
                }
            }
        }

        Callback cb = callback_;
        callback_ = Callback();
        if (!err) {
            portReservation_->keepalive();
        }
        lock.unlock();
        if (err) {
            cb(err, SYS_INVALID_SOCKET, 0, 0, boost::shared_ptr<PortReservation>());
        } else {
            pingConn_->handle()->setTTL(origTTL_);
            SYSSOCKET s = pingConn_->handle()->duplicate();
            pingConn_->close();
            cb(0, s, ip, port, portReservation_);
        }
    }

    void RendezvousFastSession::onCheckStartTimeout()
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousFastSession::onCheckStartTimeout(" << connId() << ")");

        boost::mutex::scoped_lock lock(m_);

        if (!callback_) {
            return;
        }

        if ((destIp_ != 0) && masterHandle_ && masterHandle_->canReuse()) {
            rcvBuff_.resize(4096);
            pingConn_->readFrom(&rcvBuff_[0], &rcvBuff_[0] + rcvBuff_.size(),
                boost::bind(&RendezvousFastSession::onRecvPing, this, _1, _2, _3, _4));

            lock.unlock();

            localMgr_.reactor().post(
                watch_->wrap(boost::bind(&RendezvousFastSession::onPingTimeout, this)));

            return;
        }

        lock.unlock();

        localMgr_.reactor().post(
            watch_->wrap(boost::bind(&RendezvousFastSession::onCheckStartTimeout, this)), 250);
    }

    void RendezvousFastSession::onPingTimeout()
    {
        boost::mutex::scoped_lock lock(m_);

        if (!callback_) {
            LOG4CPLUS_TRACE(logger(), "RendezvousFastSession::onPingTimeout(" << connId() << ")");
            return;
        }

        assert(destIp_ != 0);
        assert(destPort_ != 0);

        if (!next_) {
            localMgr_.reactor().post(
                watch_->wrap(boost::bind(&RendezvousFastSession::onPingTimeout, this)), 25);
            return;
        }

        LOG4CPLUS_TRACE(logger(), "RendezvousFastSession::onPingTimeout(" << connId() << ", " << DTun::ipPortToString(destIp_, destPort_) << ", ttl=" << ttl_ << ")");

        boost::shared_ptr<std::vector<char> > sndBuff =
            boost::make_shared<std::vector<char> >(4);

        (*sndBuff)[0] = 0xAA;
        (*sndBuff)[1] = 0xBB;
        (*sndBuff)[2] = 0xCC;
        (*sndBuff)[3] = 0xDD;

        pingConn_->handle()->setTTL(ttl_);

        pingConn_->writeTo(&(*sndBuff)[0], &(*sndBuff)[0] + sndBuff->size(),
            destIp_, destPort_,
            boost::bind(&RendezvousFastSession::onPingSend, this, _1, sndBuff));
        portReservation_->use();

        next_ = false;

        ++ttl_;

        bool callPortRsvd = false;
        bool keepPosting = true;

        if (owner_ && (ttl_ == 65)) {
            keepPosting = false;
            if (stepIdx_ == 2) {
                LOG4CPLUS_WARN(logger(), "RendezvousFastSession::onPingTimeout(FAILED, " << connId() << ")");
                Callback cb = callback_;
                callback_ = Callback();
                lock.unlock();
                cb(1, SYS_INVALID_SOCKET, 0, 0, boost::shared_ptr<PortReservation>());
                return;
            } else {
                LOG4CPLUS_WARN(logger(), "RendezvousFastSession::onPingTimeout(NEXT " << stepIdx_ + 1 << ", " << connId() << ")");
            }
            if (bestEffort_) {
                portReservationNext_ =
                    portAllocator_->reserveFastPortsBestEffort(2,
                        watch_->wrap(boost::bind(&RendezvousFastSession::onPortReservation, this)));
            } else {
                portReservationNext_ = portAllocator_->reserveFastPorts(2);
                if (portReservationNext_) {
                    callPortRsvd = true;
                } else {
                    Callback cb = callback_;
                    callback_ = Callback();
                    lock.unlock();
                    cb(1, SYS_INVALID_SOCKET, 0, 0, boost::shared_ptr<PortReservation>());
                    return;
                }
            }
        }

        lock.unlock();

        if (callPortRsvd) {
            onPortReservation();
        }

        if (keepPosting) {
            localMgr_.reactor().post(
                watch_->wrap(boost::bind(&RendezvousFastSession::onPingTimeout, this)), 25);
        }
    }

    void RendezvousFastSession::sendReady()
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
            boost::bind(&RendezvousFastSession::onSend, _1, sndBuff));
    }

    void RendezvousFastSession::sendNext()
    {
        DTun::DProtocolHeader header;
        DTun::DProtocolMsgNext msg;

        header.msgCode = DPROTOCOL_MSG_NEXT;
        msg.connId = DTun::toProtocolConnId(connId());

        boost::shared_ptr<std::vector<char> > sndBuff =
            boost::make_shared<std::vector<char> >(sizeof(header) + sizeof(msg));

        memcpy(&(*sndBuff)[0], &header, sizeof(header));
        memcpy(&(*sndBuff)[0] + sizeof(header), &msg, sizeof(msg));

        serverConn_->write(&(*sndBuff)[0], &(*sndBuff)[0] + sndBuff->size(),
            boost::bind(&RendezvousFastSession::onSend, _1, sndBuff));
    }
}

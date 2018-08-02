#include "RendezvousSymmConnSession.h"
#include "Logger.h"
#include "DTun/Utils.h"
#include <boost/make_shared.hpp>

namespace DNode
{
    RendezvousSymmConnSession::RendezvousSymmConnSession(DTun::SManager& localMgr, DTun::SManager& remoteMgr,
        DTun::UInt32 nodeId, const DTun::ConnId& connId,
        const boost::shared_ptr<PortAllocator>& portAllocator, bool bestEffort)
    : RendezvousSession(nodeId, connId)
    , localMgr_(localMgr)
    , remoteMgr_(remoteMgr)
    , windowSize_(299)
    , owner_(connId.nodeId == nodeId)
    , portAllocator_(portAllocator)
    , bestEffort_(bestEffort)
    , ready_(false)
    , numPingSent_(0)
    , destIp_(0)
    , destPort_(0)
    , watch_(boost::make_shared<DTun::OpWatch>(boost::ref(localMgr.reactor())))
    {
    }

    RendezvousSymmConnSession::~RendezvousSymmConnSession()
    {
        watch_->close();
    }

    bool RendezvousSymmConnSession::start(const boost::shared_ptr<DTun::SConnection>& serverConn,
        const Callback& callback)
    {
        setStarted();

        std::vector<boost::shared_ptr<DTun::SConnection> > pingConns;

        boost::mutex::scoped_lock lock(m_);

        struct sockaddr_in addr;

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        for (int i = 0; i < windowSize_; ++i) {
            boost::shared_ptr<DTun::SHandle> handle = localMgr_.createDatagramSocket();
            if (!handle) {
                return false;
            }

            if (!handle->bind((const struct sockaddr*)&addr, sizeof(addr))) {
                return false;
            }

            pingConns.push_back(handle->createConnection());
        }

        pingConns_ = pingConns;
        serverConn_ = serverConn;
        callback_ = callback;

        for (size_t i = 0; i < pingConns.size(); ++i) {
            boost::shared_ptr<std::vector<char> > rcvBuff =
                boost::make_shared<std::vector<char> >(1024);
            pingConns[i]->readFrom(&(*rcvBuff)[0], &(*rcvBuff)[0] + rcvBuff->size(),
                boost::bind(&RendezvousSymmConnSession::onRecvPing, this, _1, _2, _3, _4, i, rcvBuff));
        }

        if (owner_) {
            if (bestEffort_) {
                portReservation_ =
                    portAllocator_->reserveSymmPortsBestEffort(windowSize_ + 1,
                        watch_->wrap(boost::bind(&RendezvousSymmConnSession::onPortReservation, this)));
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
                    watch_->wrap(boost::bind(&RendezvousSymmConnSession::onPortReservation, this)));
            return true;
        }

        portReservation_ = portAllocator_->reserveSymmPorts(windowSize_ + 1);
        if (!portReservation_) {
            return false;
        }

        sendReady();

        return true;
    }

    void RendezvousSymmConnSession::onMsg(DTun::UInt8 msgId, const void* msg)
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousSymmConnSession::onMsg(" << (int)msgId << ")");

        boost::mutex::scoped_lock lock(m_);

        if (msgId == DPROTOCOL_MSG_READY) {
            ready_ = true;
            if (!started()) {
                return;
            }

            if (!callback_) {
                return;
            }

            lock.unlock();

            portReservation_.reset();

            lock.lock();

            if (!callback_) {
                return;
            }

            if (bestEffort_) {
                portReservation_ =
                    portAllocator_->reserveSymmPortsBestEffort(windowSize_ + 1,
                        watch_->wrap(boost::bind(&RendezvousSymmConnSession::onPortReservation, this)));
            } else {
                portReservation_ = portAllocator_->reserveSymmPorts(windowSize_ + 1);
                if (!portReservation_) {
                    Callback cb = callback_;
                    callback_ = Callback();
                    lock.unlock();
                    cb(1, SYS_INVALID_SOCKET, 0, 0);
                    return;
                }
                lock.unlock();
                onPortReservation();
            }
        } else if (msgId == DPROTOCOL_MSG_SYMM) {
            const DTun::DProtocolMsgSymm* msgSymm = (const DTun::DProtocolMsgSymm*)msg;

            destIp_ = msgSymm->nodeIp;
            destPort_ = msgSymm->nodePort;
        } else if (msgId == DPROTOCOL_MSG_SYMM_NEXT) {
            if (!callback_) {
                return;
            }

            numPingSent_ = 0;

            boost::shared_ptr<std::vector<char> > sndBuff =
                boost::make_shared<std::vector<char> >(4);

            (*sndBuff)[0] = 0xAA;
            (*sndBuff)[1] = 0xBB;
            (*sndBuff)[2] = 0xCC;
            (*sndBuff)[3] = 0xDD;

            pingConns_[0]->writeTo(&(*sndBuff)[0], &(*sndBuff)[0] + sndBuff->size(),
                destIp_, destPort_,
                boost::bind(&RendezvousSymmConnSession::onPingSend, this, _1, sndBuff));
            portReservation_->use();
        }
    }

    void RendezvousSymmConnSession::onEstablished()
    {
        localMgr_.reactor().post(
            watch_->wrap(boost::bind(&RendezvousSymmConnSession::onEstablishedTimeout, this)), 1500);
    }

    void RendezvousSymmConnSession::onPortReservation()
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousSymmConnSession::onPortReservation()");

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

        sendReady();
    }

    void RendezvousSymmConnSession::onPingSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff)
    {
        boost::mutex::scoped_lock lock(m_);

        if (!callback_) {
            return;
        }

        if (err) {
            LOG4CPLUS_TRACE(logger(), "RendezvousSymmConnSession::onPingSend(" << err << ")");
            Callback cb = callback_;
            callback_ = Callback();
            lock.unlock();
            cb(err, SYS_INVALID_SOCKET, 0, 0);
            return;
        }

        ++numPingSent_;

        if (numPingSent_ < (int)pingConns_.size()) {
            pingConns_[numPingSent_]->writeTo(&(*sndBuff)[0], &(*sndBuff)[0] + sndBuff->size(),
                destIp_, destPort_,
                boost::bind(&RendezvousSymmConnSession::onPingSend, this, _1, sndBuff));
            portReservation_->use();
        } else {
            sendSymmNext();
        }
    }

    void RendezvousSymmConnSession::onServerSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff)
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousSymmConnSession::onServerSend(" << err << ")");
    }

    void RendezvousSymmConnSession::onRecvPing(int err, int numBytes, DTun::UInt32 ip, DTun::UInt16 port, int connIdx, const boost::shared_ptr<std::vector<char> >& rcvBuff)
    {
        boost::mutex::scoped_lock lock(m_);

        if (!callback_) {
            LOG4CPLUS_TRACE(logger(), "RendezvousSymmConnSession::onRecvPing(" << err << ", " << numBytes << ", i=" << connIdx << ", src=" << DTun::ipPortToString(ip, port) << ")");
            return;
        }

        if (err) {
            LOG4CPLUS_TRACE(logger(), "RendezvousSymmConnSession::onRecvPing(" << err << ", " << numBytes << ", i=" << connIdx << ", src=" << DTun::ipPortToString(ip, port) << ")");
            Callback cb = callback_;
            callback_ = Callback();
            lock.unlock();
            cb(err, SYS_INVALID_SOCKET, 0, 0);
            return;
        }

        uint8_t d = 0;

        if (numBytes != 4) {
            LOG4CPLUS_TRACE(logger(), "RendezvousSymmConnSession::onRecvPing(" << err << ", " << numBytes << ", i=" << connIdx << ", src=" << DTun::ipPortToString(ip, port) << ")");
            if (ip != destIp_) {
                LOG4CPLUS_WARN(logger(), "RendezvousSymmConnSession::onRecvPing bad ping len: "
                    << numBytes);
            }
            pingConns_[connIdx]->readFrom(&(*rcvBuff)[0], &(*rcvBuff)[0] + rcvBuff->size(),
                boost::bind(&RendezvousSymmConnSession::onRecvPing, this, _1, _2, _3, _4, connIdx, rcvBuff));
            return;
        } else {
            uint8_t a = (*rcvBuff)[0];
            uint8_t b = (*rcvBuff)[1];
            uint8_t c = (*rcvBuff)[2];
            d = (*rcvBuff)[3];
            if ((a != 0xAA) || (b != 0xBB) || (c != 0xCC) || ((d != 0xDD) && (d != 0xEE))) {
                LOG4CPLUS_TRACE(logger(), "RendezvousSymmConnSession::onRecvPing(" << err << ", " << numBytes << ", i=" << connIdx << ", src=" << DTun::ipPortToString(ip, port) << ")");
                LOG4CPLUS_WARN(logger(), "RendezvousSymmConnSession::onRecvPing bad ping: "
                    << (int)a << "," << (int)b << "," << (int)c << "," << (int)d);
                pingConns_[connIdx]->readFrom(&(*rcvBuff)[0], &(*rcvBuff)[0] + rcvBuff->size(),
                    boost::bind(&RendezvousSymmConnSession::onRecvPing, this, _1, _2, _3, _4, connIdx, rcvBuff));
                return;
            }
            if (ip != destIp_) {
                LOG4CPLUS_TRACE(logger(), "RendezvousSymmConnSession::onRecvPing(" << err << ", " << numBytes << ", i=" << connIdx << ", src=" << DTun::ipPortToString(ip, port) << ")");
                LOG4CPLUS_WARN(logger(), "RendezvousSymmConnSession::onRecvPing bad source ip, expected: " << DTun::ipToString(destIp_));
                pingConns_[connIdx]->readFrom(&(*rcvBuff)[0], &(*rcvBuff)[0] + rcvBuff->size(),
                    boost::bind(&RendezvousSymmConnSession::onRecvPing, this, _1, _2, _3, _4, connIdx, rcvBuff));
                return;
            }
        }

        if (d == 0xEE) {
            boost::shared_ptr<PortReservation> portReservation = portReservation_;
            if (portReservation) {
                LOG4CPLUS_TRACE(logger(), "RendezvousSymmConnSession::onRecvPing(FINAL, " << err << ", " << numBytes << ", i=" << connIdx << ", src=" << DTun::ipPortToString(ip, port) << ")");
                portReservation->keepalive();
                Callback cb = callback_;
                callback_ = Callback();
                lock.unlock();
                SYSSOCKET s = pingConns_[connIdx]->handle()->duplicate();
                pingConns_[connIdx]->close();
                cb(0, s, destIp_, destPort_);
            } else {
                LOG4CPLUS_TRACE(logger(), "RendezvousSymmConnSession::onRecvPing(FINAL other, " << err << ", " << numBytes << ", i=" << connIdx << ", src=" << DTun::ipPortToString(ip, port) << ")");

                pingConns_[connIdx]->readFrom(&(*rcvBuff)[0], &(*rcvBuff)[0] + rcvBuff->size(),
                    boost::bind(&RendezvousSymmConnSession::onRecvPing, this, _1, _2, _3, _4, connIdx, rcvBuff));
            }
        } else {
            LOG4CPLUS_TRACE(logger(), "RendezvousSymmConnSession::onRecvPing(" << err << ", " << numBytes << ", i=" << connIdx << ", src=" << DTun::ipPortToString(ip, port) << ")");

            pingConns_[connIdx]->readFrom(&(*rcvBuff)[0], &(*rcvBuff)[0] + rcvBuff->size(),
                boost::bind(&RendezvousSymmConnSession::onRecvPing, this, _1, _2, _3, _4, connIdx, rcvBuff));
        }
    }

    void RendezvousSymmConnSession::onEstablishedTimeout()
    {
        boost::mutex::scoped_lock lock(m_);

        if (!callback_) {
            LOG4CPLUS_TRACE(logger(), "RendezvousSymmConnSession::onEstablishedTimeout(" << connId() << ")");
            return;
        }

        LOG4CPLUS_WARN(logger(), "RendezvousSymmConnSession::onEstablishedTimeout(" << connId() << ")");

        Callback cb = callback_;
        callback_ = Callback();
        lock.unlock();
        cb(1, SYS_INVALID_SOCKET, 0, 0);
    }

    void RendezvousSymmConnSession::sendReady()
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
            boost::bind(&RendezvousSymmConnSession::onServerSend, _1, sndBuff));
    }

    void RendezvousSymmConnSession::sendSymmNext()
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
            boost::bind(&RendezvousSymmConnSession::onServerSend, _1, sndBuff));
    }
}

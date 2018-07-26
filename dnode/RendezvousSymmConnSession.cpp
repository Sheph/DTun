#include "RendezvousSymmConnSession.h"
#include "Logger.h"
#include "DTun/Utils.h"
#include <boost/make_shared.hpp>

namespace DNode
{
    RendezvousSymmConnSession::RendezvousSymmConnSession(DTun::SManager& localMgr, DTun::SManager& remoteMgr,
        DTun::UInt32 nodeId, const DTun::ConnId& connId)
    : RendezvousSession(nodeId, connId)
    , localMgr_(localMgr)
    , remoteMgr_(remoteMgr)
    , windowSize_(300)
    , owner_(connId.nodeId == nodeId)
    , numPingSent_(0)
    , numKeepaliveSent_(0)
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
        const std::vector<boost::shared_ptr<DTun::SHandle> >& keepalive, const Callback& callback)
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
        keepalive_ = keepalive;
        serverConn_ = serverConn;
        callback_ = callback;

        for (size_t i = 0; i < pingConns.size(); ++i) {
            boost::shared_ptr<std::vector<char> > rcvBuff =
                boost::make_shared<std::vector<char> >(1024);
            pingConns[i]->readFrom(&(*rcvBuff)[0], &(*rcvBuff)[0] + rcvBuff->size(),
                boost::bind(&RendezvousSymmConnSession::onRecvPing, this, _1, _2, _3, _4, i, rcvBuff));
        }

        if (!owner_) {
            sendSymmNext();
        }

        return true;
    }

    void RendezvousSymmConnSession::onMsg(DTun::UInt8 msgId, const void* msg)
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousSymmConnSession::onMsg(" << (int)msgId << ")");

        boost::mutex::scoped_lock lock(m_);

        if (msgId == DPROTOCOL_MSG_SYMM) {
            const DTun::DProtocolMsgSymm* msgSymm = (const DTun::DProtocolMsgSymm*)msg;

            destIp_ = msgSymm->nodeIp;
            destPort_ = msgSymm->nodePort;
        } else if (msgId == DPROTOCOL_MSG_SYMM_NEXT) {
            if (!callback_) {
                return;
            }

            numPingSent_ = 0;
            numKeepaliveSent_ = 0;

            boost::shared_ptr<std::vector<char> > sndBuff =
                boost::make_shared<std::vector<char> >(4);

            (*sndBuff)[0] = 0xAA;
            (*sndBuff)[1] = 0xBB;
            (*sndBuff)[2] = 0xCC;
            (*sndBuff)[3] = 0xDD;

            pingConns_[0]->writeTo(&(*sndBuff)[0], &(*sndBuff)[0] + sndBuff->size(),
                destIp_, destPort_,
                boost::bind(&RendezvousSymmConnSession::onPingSend, this, _1, sndBuff));
        }
    }

    void RendezvousSymmConnSession::onEstablished()
    {
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
        LOG4CPLUS_INFO(logger(), "RendezvousSymmConnSession::onRecvPing(" << err << ", " << numBytes << ", i=" << connIdx << ", src=" << DTun::ipPortToString(ip, port) << ")");

        boost::mutex::scoped_lock lock(m_);

        if (!callback_) {
            return;
        }

        pingConns_[connIdx]->readFrom(&(*rcvBuff)[0], &(*rcvBuff)[0] + rcvBuff->size(),
            boost::bind(&RendezvousSymmConnSession::onRecvPing, this, _1, _2, _3, _4, connIdx, rcvBuff));
    }

    void RendezvousSymmConnSession::onEstablishedTimeout()
    {
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

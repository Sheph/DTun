#include "RendezvousSymmConnSession.h"
#include "Logger.h"
#include "DTun/Utils.h"
#include <boost/make_shared.hpp>

namespace DNode
{
    RendezvousSymmConnSession::RendezvousSymmConnSession(DTun::SManager& mgr,
        DTun::UInt32 nodeId, const DTun::ConnId& connId,
        const boost::shared_ptr<DTun::SConnection>& serverConn,
        const std::vector<boost::shared_ptr<DTun::SHandle> >& keepalive)
    : RendezvousSession(nodeId, connId)
    , mgr_(mgr)
    , owner_(connId.nodeId == nodeId)
    , numPingSent_(0)
    , numKeepaliveSent_(0)
    , established_(false)
    , destIp_(0)
    , destPort_(0)
    , watch_(boost::make_shared<DTun::OpWatch>(boost::ref(mgr.reactor())))
    , keepalive_(keepalive)
    , serverConn_(serverConn)
    {
    }

    RendezvousSymmConnSession::~RendezvousSymmConnSession()
    {
        watch_->close();
    }

    bool RendezvousSymmConnSession::start(const Callback& callback)
    {
        std::vector<boost::shared_ptr<DTun::SConnection> > pingConns;

        boost::mutex::scoped_lock lock(m_);

        struct sockaddr_in addr;

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        for (int i = 0; i < 100; ++i) {
            boost::shared_ptr<DTun::SHandle> handle = mgr_.createDatagramSocket();
            if (!handle) {
                return false;
            }

            if (!handle->bind((const struct sockaddr*)&addr, sizeof(addr))) {
                return false;
            }

            pingConns.push_back(handle->createConnection());
        }

        pingConns_ = pingConns;
        callback_ = callback;

        for (size_t i = 0; i < pingConns.size(); ++i) {
            boost::shared_ptr<std::vector<char> > rcvBuff =
                boost::make_shared<std::vector<char> >(1024);
            pingConns[i]->readFrom(&(*rcvBuff)[0], &(*rcvBuff)[0] + rcvBuff->size(),
                boost::bind(&RendezvousSymmConnSession::onRecvPing, this, _1, _2, _3, _4, rcvBuff));
        }

        if (!owner_) {
            sendSymmNext();
        }

        return true;
    }

    void RendezvousSymmConnSession::onMsg(DTun::UInt8 msgId, const void* msg)
    {
    }

    void RendezvousSymmConnSession::onEstablished()
    {
    }

    void RendezvousSymmConnSession::onPingSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff)
    {
    }

    void RendezvousSymmConnSession::onServerSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff)
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousSymmConnSession::onServerSend(" << err << ")");
    }

    void RendezvousSymmConnSession::onRecvPing(int err, int numBytes, DTun::UInt32 ip, DTun::UInt16 port, const boost::shared_ptr<std::vector<char> >& rcvBuff)
    {
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

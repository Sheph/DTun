#include "RendezvousSymmAccSession.h"
#include "Logger.h"
#include "DTun/Utils.h"
#include <boost/make_shared.hpp>

namespace DNode
{
    RendezvousSymmAccSession::RendezvousSymmAccSession(DTun::SManager& localMgr, DTun::SManager& remoteMgr,
        DTun::UInt32 nodeId, const DTun::ConnId& connId,
        const std::vector<boost::shared_ptr<DTun::SHandle> >& keepalive)
    : RendezvousSession(nodeId, connId)
    , localMgr_(localMgr)
    , remoteMgr_(remoteMgr)
    , owner_(connId.nodeId == nodeId)
    , stepIdx_(0)
    , numPingSent_(0)
    , numKeepaliveSent_(0)
    , watch_(boost::make_shared<DTun::OpWatch>(boost::ref(localMgr.reactor())))
    , keepalive_(keepalive)
    {
    }

    RendezvousSymmAccSession::~RendezvousSymmAccSession()
    {
        watch_->close();
    }

    bool RendezvousSymmAccSession::start(const Callback& callback)
    {
        boost::mutex::scoped_lock lock(m_);

        if (!owner_) {
            callback_ = callback;
            return true;
        }

        struct sockaddr_in addr;

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        boost::shared_ptr<DTun::SHandle> handle = localMgr_.createDatagramSocket();
        if (!handle) {
            return false;
        }

        if (!handle->bind((const struct sockaddr*)&addr, sizeof(addr))) {
            return false;
        }

        pingConn_ = handle->createConnection();

        boost::shared_ptr<std::vector<char> > rcvBuff =
            boost::make_shared<std::vector<char> >(1024);
        pingConn_->readFrom(&(*rcvBuff)[0], &(*rcvBuff)[0] + rcvBuff->size(),
            boost::bind(&RendezvousSymmAccSession::onRecvPing, this, _1, _2, _3, _4, rcvBuff));

        return true;
    }

    void RendezvousSymmAccSession::onMsg(DTun::UInt8 msgId, const void* msg)
    {
    }

    void RendezvousSymmAccSession::onEstablished()
    {
    }

    void RendezvousSymmAccSession::onHelloSend(int err)
    {
    }

    void RendezvousSymmAccSession::onPingSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff)
    {
    }

    void RendezvousSymmAccSession::onRecvPing(int err, int numBytes, DTun::UInt32 ip, DTun::UInt16 port, const boost::shared_ptr<std::vector<char> >& rcvBuff)
    {
    }

    void RendezvousSymmAccSession::onSymmNextTimeout()
    {
    }
}

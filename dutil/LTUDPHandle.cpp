#include "DTun/LTUDPHandle.h"
#include "DTun/LTUDPManager.h"
#include "DTun/LTUDPAcceptor.h"
#include "DTun/LTUDPConnector.h"
#include "DTun/LTUDPConnection.h"
#include <boost/make_shared.hpp>

namespace DTun
{
    LTUDPHandle::LTUDPHandle(LTUDPManager& mgr)
    : mgr_(mgr)
    , impl_(boost::make_shared<LTUDPHandleImpl>(boost::ref(mgr)))
    , transportPort_(0)
    {
    }

    LTUDPHandle::LTUDPHandle(LTUDPManager& mgr,
        const boost::shared_ptr<SConnection>& conn, struct tcp_pcb* pcb)
    : mgr_(mgr)
    , impl_(boost::make_shared<LTUDPHandleImpl>(boost::ref(mgr), conn, pcb))
    , transportPort_(0)
    {
    }

    LTUDPHandle::~LTUDPHandle()
    {
        close();
    }

    SReactor& LTUDPHandle::reactor()
    {
        return mgr_.reactor();
    }

    void LTUDPHandle::ping(UInt32 ip, UInt16 port)
    {
        boost::shared_ptr<LTUDPHandleImpl> impl = impl_;
        if (!impl) {
            return;
        }

        impl->ping(ip, port);
    }

    bool LTUDPHandle::bind(SYSSOCKET s)
    {
        return impl_->bind(s);
    }

    bool LTUDPHandle::bind(const struct sockaddr* name, int namelen)
    {
        return impl_->bind(name, namelen);
    }

    bool LTUDPHandle::getSockName(UInt32& ip, UInt16& port) const
    {
        return impl_->getSockName(ip, port);
    }

    bool LTUDPHandle::getPeerName(UInt32& ip, UInt16& port) const
    {
        return impl_->getPeerName(ip, port);
    }

    SYSSOCKET LTUDPHandle::duplicate()
    {
        return impl_->duplicate();
    }

    void LTUDPHandle::close(bool immediate)
    {
        if (impl_) {
            transportPort_ = impl_->getTransportPort();
            impl_->mgr().addToKill(impl_, immediate);
            impl_.reset();
        }
    }

    bool LTUDPHandle::canReuse() const
    {
        return !impl_ && ((transportPort_ == 0) || !mgr_.haveTransportConnection(transportPort_));
    }

    boost::shared_ptr<SConnector> LTUDPHandle::createConnector()
    {
        return boost::make_shared<LTUDPConnector>(shared_from_this());
    }

    boost::shared_ptr<SAcceptor> LTUDPHandle::createAcceptor()
    {
        return boost::make_shared<LTUDPAcceptor>(shared_from_this());
    }

    boost::shared_ptr<SConnection> LTUDPHandle::createConnection()
    {
        return boost::make_shared<LTUDPConnection>(shared_from_this());
    }
}

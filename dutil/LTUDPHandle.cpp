#include "DTun/LTUDPHandle.h"
#include "DTun/LTUDPManager.h"
#include "DTun/LTUDPAcceptor.h"
#include "DTun/LTUDPConnector.h"
#include "DTun/LTUDPConnection.h"
#include <boost/make_shared.hpp>

namespace DTun
{
    LTUDPHandle::LTUDPHandle(LTUDPManager& mgr)
    : reactor_(mgr.reactor())
    , impl_(boost::make_shared<LTUDPHandleImpl>(boost::ref(mgr)))
    {
    }

    LTUDPHandle::LTUDPHandle(LTUDPManager& mgr,
        const boost::shared_ptr<SConnection>& conn, struct tcp_pcb* pcb)
    : reactor_(mgr.reactor())
    , impl_(boost::make_shared<LTUDPHandleImpl>(boost::ref(mgr), conn, pcb))
    {
    }

    LTUDPHandle::~LTUDPHandle()
    {
        close();
    }

    bool LTUDPHandle::bind(SYSSOCKET s)
    {
        assert(false);
        return false;
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

    void LTUDPHandle::close()
    {
        if (impl_) {
            impl_->mgr().addToKill(impl_);
            impl_.reset();
        }
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

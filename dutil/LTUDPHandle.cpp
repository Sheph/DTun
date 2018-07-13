#include "DTun/LTUDPHandle.h"
#include "DTun/LTUDPManager.h"
#include "DTun/LTUDPAcceptor.h"
#include "DTun/LTUDPConnector.h"
#include "DTun/LTUDPConnection.h"
#include <boost/make_shared.hpp>

namespace DTun
{
    LTUDPHandle::LTUDPHandle(LTUDPManager& mgr, const boost::shared_ptr<SConnection>& conn)
    : reactor_(mgr.reactor())
    , impl_(boost::make_shared<LTUDPHandleImpl>(boost::ref(mgr), conn))
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
        return impl_->conn()->handle()->bind(name, namelen);
    }

    bool LTUDPHandle::getSockName(UInt32& ip, UInt16& port) const
    {
        assert(false);
        return false;
    }

    bool LTUDPHandle::getPeerName(UInt32& ip, UInt16& port) const
    {
        assert(false);
        return false;
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
        assert(false);
        return boost::shared_ptr<SConnector>();
    }

    boost::shared_ptr<SAcceptor> LTUDPHandle::createAcceptor()
    {
        return boost::make_shared<LTUDPAcceptor>(shared_from_this());
    }

    boost::shared_ptr<SConnection> LTUDPHandle::createConnection()
    {
        assert(false);
        return boost::shared_ptr<SConnection>();
    }
}

#include "DTun/SysHandle.h"
#include "DTun/SysConnector.h"
#include "DTun/SysConnection.h"
#include "DTun/Utils.h"
#include <boost/make_shared.hpp>

namespace DTun
{
    SysHandle::SysHandle(SysReactor& reactor, SYSSOCKET sock)
    : reactor_(reactor)
    , sock_(sock)
    {
    }

    SysHandle::~SysHandle()
    {
        close();
    }

    bool SysHandle::bind(SYSSOCKET s)
    {
        assert(false);
        return false;
    }

    bool SysHandle::bind(const struct sockaddr* name, int namelen)
    {
        assert(false);
        return false;
    }

    bool SysHandle::getSockName(UInt32& ip, UInt16& port) const
    {
        assert(false);
        return false;
    }

    bool SysHandle::getPeerName(UInt32& ip, UInt16& port) const
    {
        assert(false);
        return false;
    }

    void SysHandle::close()
    {
        if (sock_ != SYS_INVALID_SOCKET) {
            DTun::closeSysSocketChecked(sock_);
            sock_ = SYS_INVALID_SOCKET;
        }
    }

    boost::shared_ptr<SConnector> SysHandle::createConnector()
    {
        return boost::make_shared<SysConnector>(boost::ref(reactor_), shared_from_this());
    }

    boost::shared_ptr<SAcceptor> SysHandle::createAcceptor()
    {
        assert(false);
        return boost::shared_ptr<SAcceptor>();
    }

    boost::shared_ptr<SConnection> SysHandle::createConnection()
    {
        return boost::make_shared<SysConnection>(boost::ref(reactor_), shared_from_this());
    }
}

#include "DTun/SysHandle.h"
#include "DTun/SysConnector.h"
#include "DTun/SysConnection.h"
#include "DTun/Utils.h"
#include "Logger.h"
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

    void SysHandle::ping(UInt32 ip, UInt16 port)
    {
        assert(false);
    }

    bool SysHandle::bind(SYSSOCKET s)
    {
        assert(false);
        return false;
    }

    bool SysHandle::bind(const struct sockaddr* name, int namelen)
    {
        if ((name->sa_family != AF_INET) ||
            (((const struct sockaddr_in*)name)->sin_addr.s_addr != htonl(INADDR_ANY))) {
            int optval = 1;
            if (::setsockopt(sock_, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
                LOG4CPLUS_ERROR(logger(), "cannot set sock reuse addr");
                return false;
            }
        }

        if (::bind(sock_, name, namelen) == SYS_SOCKET_ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot bind sys socket: " << strerror(errno));
            return false;
        }

        return true;
    }

    bool SysHandle::getSockName(UInt32& ip, UInt16& port) const
    {
        struct sockaddr_in addr;
        socklen_t addrLen = sizeof(addr);

        if (::getsockname(sock_, (struct sockaddr*)&addr, &addrLen) == SYS_SOCKET_ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot get sys sock name: " << strerror(errno));
            return false;
        }

        ip = addr.sin_addr.s_addr;
        port = addr.sin_port;

        return true;
    }

    bool SysHandle::getPeerName(UInt32& ip, UInt16& port) const
    {
        struct sockaddr_in addr;
        socklen_t addrLen = sizeof(addr);

        if (::getpeername(sock_, (struct sockaddr*)&addr, &addrLen) == SYS_SOCKET_ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot get sys peer name: " << strerror(errno));
            return false;
        }

        ip = addr.sin_addr.s_addr;
        port = addr.sin_port;

        return true;
    }

    SYSSOCKET SysHandle::duplicate()
    {
        return dup(sock_);
    }

    int SysHandle::getTTL() const
    {
        int result = 0;
        socklen_t resultLen = sizeof(result);
        if (::getsockopt(sock_, IPPROTO_IP, IP_TTL, &result, &resultLen) < 0) {
            LOG4CPLUS_ERROR(logger(), "Cannot getsockopt: " << strerror(errno));
            return 0;
        }
        if ((result <= 0) || (result > 255)) {
            LOG4CPLUS_ERROR(logger(), "bad TTL value: " << result);
            result = 0;
        }
        return result;
    }

    bool SysHandle::setTTL(int ttl)
    {
        if (::setsockopt(sock_, IPPROTO_IP, IP_TTL, &ttl, sizeof(ttl)) < 0) {
            LOG4CPLUS_ERROR(logger(), "cannot set TTL");
            return false;
        }
        return true;
    }

    void SysHandle::close(bool immediate)
    {
        if (sock_ != SYS_INVALID_SOCKET) {
            DTun::closeSysSocketChecked(sock_);
            sock_ = SYS_INVALID_SOCKET;
        }
    }

    bool SysHandle::canReuse() const
    {
        return (sock_ == SYS_INVALID_SOCKET);
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

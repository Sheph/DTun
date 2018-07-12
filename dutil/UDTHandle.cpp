#include "DTun/UDTHandle.h"
#include "DTun/UDTConnector.h"
#include "DTun/UDTConnection.h"
#include "DTun/UDTAcceptor.h"
#include "DTun/Utils.h"
#include "Logger.h"
#include <boost/make_shared.hpp>

namespace DTun
{
    UDTHandle::UDTHandle(UDTReactor& reactor, UDTSOCKET sock)
    : reactor_(reactor)
    , sock_(sock)
    {
    }

    UDTHandle::~UDTHandle()
    {
        close();
    }

    bool UDTHandle::bind(SYSSOCKET s)
    {
        bool optval = false;
        if (UDT::setsockopt(sock_, 0, UDT_REUSEADDR, &optval, sizeof(optval)) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot set reuseaddr for UDT socket: " << UDT::getlasterror().getErrorMessage());
            return false;
        }

        if (UDT::bind2(sock_, s) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot bind UDT socket: " << UDT::getlasterror().getErrorMessage());
            return false;
        }

        return true;
    }

    bool UDTHandle::bind(const struct sockaddr* name, int namelen)
    {
        if (UDT::bind(sock_, name, namelen) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot bind UDT socket: " << UDT::getlasterror().getErrorMessage());
            return false;
        }

        return true;
    }

    bool UDTHandle::getSockName(UInt32& ip, UInt16& port) const
    {
        struct sockaddr_in addr;
        int addrLen = sizeof(addr);

        if (UDT::getsockname(sock_, (struct sockaddr*)&addr, &addrLen) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot get UDT sock name: " << UDT::getlasterror().getErrorMessage());
            return false;
        }

        ip = addr.sin_addr.s_addr;
        port = addr.sin_port;

        return true;
    }

    bool UDTHandle::getPeerName(UInt32& ip, UInt16& port) const
    {
        struct sockaddr_in addr;
        int addrLen = sizeof(addr);

        if (UDT::getpeername(sock_, (struct sockaddr*)&addr, &addrLen) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot get UDT peer name: " << UDT::getlasterror().getErrorMessage());
            return false;
        }

        ip = addr.sin_addr.s_addr;
        port = addr.sin_port;

        return true;
    }

    void UDTHandle::close()
    {
        if (sock_ != UDT::INVALID_SOCK) {
            DTun::closeUDTSocketChecked(sock_);
            sock_ = UDT::INVALID_SOCK;
        }
    }

    boost::shared_ptr<SConnector> UDTHandle::createConnector()
    {
        return boost::make_shared<UDTConnector>(boost::ref(reactor_), shared_from_this());
    }

    boost::shared_ptr<SAcceptor> UDTHandle::createAcceptor()
    {
        return boost::make_shared<UDTAcceptor>(boost::ref(reactor_), shared_from_this());
    }

    boost::shared_ptr<SConnection> UDTHandle::createConnection()
    {
        return boost::make_shared<UDTConnection>(boost::ref(reactor_), shared_from_this());
    }
}

#include "DTun/UDTConnector.h"
#include "DTun/UDTReactor.h"
#include "DTun/Utils.h"
#include "Logger.h"
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <netdb.h>

namespace DTun
{
    UDTConnector::UDTConnector(UDTReactor& reactor, const boost::shared_ptr<UDTHandle>& handle)
    : UDTHandler(reactor, handle)
    , noCloseSock_(false)
    {
        reactor.add(this);
    }

    UDTConnector::~UDTConnector()
    {
        close();
    }

    bool UDTConnector::connect(const std::string& address, const std::string& port, const ConnectCallback& callback, Mode mode)
    {
        bool optval = false;
        if (UDT::setsockopt(udtHandle()->sock(), 0, UDT_RCVSYN, &optval, sizeof(optval)) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot setsockopt on UDT socket: " << UDT::getlasterror().getErrorMessage());
            return false;
        }

        if (UDT::setsockopt(udtHandle()->sock(), 0, UDT_SNDSYN, &optval, sizeof(optval)) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot setsockopt on UDT socket: " << UDT::getlasterror().getErrorMessage());
            return false;
        }

        if (mode != ModeNormal) {
            optval = true;
            if (UDT::setsockopt(udtHandle()->sock(), 0, UDT_RENDEZVOUS, &optval, sizeof(optval)) == UDT::ERROR) {
                LOG4CPLUS_ERROR(logger(), "Cannot set rendezvous mode on UDT socket: " << UDT::getlasterror().getErrorMessage());
                return false;
            }
        }

        addrinfo hints;

        memset(&hints, 0, sizeof(struct addrinfo));

        hints.ai_flags = AI_PASSIVE;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* res;

        if (::getaddrinfo(address.c_str(), port.c_str(), &hints, &res) != 0) {
            LOG4CPLUS_ERROR(logger(), "cannot resolve address/port");
            return false;
        }

        if (UDT::connect(udtHandle()->sock(), res->ai_addr, res->ai_addrlen) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot connect UDT socket: " << UDT::getlasterror().getErrorMessage());
            freeaddrinfo(res);
            return false;
        }

        freeaddrinfo(res);

        callback_ = callback;

        reactor().update(this);

        return true;
    }

    void UDTConnector::close(bool immediate)
    {
        boost::shared_ptr<UDTHandle> handle = reactor().remove(this);
        if (!noCloseSock_ && handle) {
            handle->close(immediate);
        }
    }

    int UDTConnector::getPollEvents() const
    {
        return callback_ ? UDT_EPOLL_OUT : 0;
    }

    void UDTConnector::handleRead()
    {
    }

    void UDTConnector::handleWrite()
    {
        ConnectCallback cb = callback_;
        callback_ = ConnectCallback();
        reactor().update(this);

        int state = UDT::getsockstate(udtHandle()->sock());
        noCloseSock_ = true;
        cb((state == CONNECTED) ? 0 : CUDTException::ECONNFAIL);
    }
}

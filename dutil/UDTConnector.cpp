#include "DTun/UDTConnector.h"
#include "DTun/UDTReactor.h"
#include "Logger.h"
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <netdb.h>

namespace DTun
{
    UDTConnector::UDTConnector(UDTReactor& reactor, UDTSOCKET sock)
    : UDTSocket(reactor, sock)
    , closed_(false)
    {
        reactor.add(this);
    }

    UDTConnector::~UDTConnector()
    {
    }

    bool UDTConnector::connect(const std::string& address, const std::string& port, const ConnectCallback& callback)
    {
        bool optval = false;
        if (UDT::setsockopt(sock(), 0, UDT_RCVSYN, &optval, sizeof(optval)) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot setsockopt on UDT socket: " << UDT::getlasterror().getErrorMessage());
            return false;
        }

        if (UDT::setsockopt(sock(), 0, UDT_SNDSYN, &optval, sizeof(optval)) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot setsockopt on UDT socket: " << UDT::getlasterror().getErrorMessage());
            return false;
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

        if (UDT::connect(sock(), res->ai_addr, res->ai_addrlen) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot connect UDT socket: " << UDT::getlasterror().getErrorMessage());
            freeaddrinfo(res);
            return false;
        }

        freeaddrinfo(res);

        callback_ = callback;

        reactor().update(this);

        return true;
    }

    void UDTConnector::close()
    {
        if (closed_) {
            return;
        }
        closed_ = true;
        if (sock() != UDT::INVALID_SOCK) {
            reactor().remove(this);
            resetSock();
        }
    }

    int UDTConnector::getPollEvents() const
    {
        return callback_ ? (UDT_EPOLL_OUT | UDT_EPOLL_ERR) : 0;
    }

    void UDTConnector::handleRead()
    {
    }

    void UDTConnector::handleWrite()
    {
        ConnectCallback cb = callback_;
        callback_ = ConnectCallback();
        reactor().update(this);

        int state = BROKEN;
        int optlen = sizeof(int);
        if (UDT::getsockopt(sock(), 0, UDT_STATE, &state, &optlen) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot getsockopt UDT socket: " << UDT::getlasterror().getErrorMessage());
            // UDT is not fucking conforming to POSIX at all, who knows, m.b. it can change the opt here...
            state = BROKEN;
        }

        cb((state == CONNECTED) ? 0 : CUDTException::ECONNFAIL);
    }

    void UDTConnector::handleClose()
    {
        if (callback_) {
            ConnectCallback cb = callback_;
            callback_ = ConnectCallback();
            cb(CUDTException::ENOCONN);
        }
    }
}

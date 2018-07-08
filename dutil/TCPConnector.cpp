#include "DTun/TCPConnector.h"
#include "DTun/TCPReactor.h"
#include "Logger.h"
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <sys/epoll.h>
#include <fcntl.h>

namespace DTun
{
    TCPConnector::TCPConnector(TCPReactor& reactor, SYSSOCKET sock)
    : TCPSocket(reactor, sock)
    , handedOut_(false)
    {
        reactor.add(this);
    }

    TCPConnector::~TCPConnector()
    {
        close();
    }

    bool TCPConnector::connect(const std::string& address, const std::string& port, const ConnectCallback& callback)
    {
        if (::fcntl(sock(), F_SETFL, O_NONBLOCK) < 0) {
            LOG4CPLUS_ERROR(logger(), "cannot set sock non-blocking");
            return false;
        }

        int optval = 1;
        if (::setsockopt(sock(), SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
            LOG4CPLUS_ERROR(logger(), "cannot set sock reuse addr");
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

        int conn_res = ::connect(sock(), res->ai_addr, res->ai_addrlen);

        freeaddrinfo(res);

        if ((conn_res < 0) && (errno != EINPROGRESS)) {
            LOG4CPLUS_ERROR(logger(), "Cannot connect TCP socket: " << strerror(errno));
            return false;
        }

        callback_ = callback;

        reactor().update(this);

        return true;
    }

    void TCPConnector::close()
    {
        SYSSOCKET s = reactor().remove(this);
        if (!handedOut_ && (s != SYS_INVALID_SOCKET)) {
            SYS_CLOSE_SOCKET(s);
        }
    }

    int TCPConnector::getPollEvents() const
    {
        return callback_ ? EPOLLOUT : 0;
    }

    void TCPConnector::handleRead()
    {
    }

    void TCPConnector::handleWrite()
    {
        ConnectCallback cb = callback_;
        callback_ = ConnectCallback();
        reactor().update(this);

        int result = 0;
        socklen_t resultLen = sizeof(result);
        if (::getsockopt(sock(), SOL_SOCKET, SO_ERROR, &result, &resultLen) < 0) {
            result = errno;
            LOG4CPLUS_ERROR(logger(), "Cannot getsockopt TCP socket: " << strerror(errno));
        }

        handedOut_ = true;
        cb(result);
    }
}

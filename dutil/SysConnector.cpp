#include "DTun/SysConnector.h"
#include "DTun/SysReactor.h"
#include "DTun/Utils.h"
#include "Logger.h"
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <netdb.h>
#include <sys/epoll.h>
#include <fcntl.h>

namespace DTun
{
    SysConnector::SysConnector(SysReactor& reactor, const boost::shared_ptr<SysHandle>& handle)
    : SysHandler(reactor, handle)
    , handedOut_(false)
    {
        reactor.add(this);
    }

    SysConnector::~SysConnector()
    {
        close();
    }

    bool SysConnector::connect(const std::string& address, const std::string& port, const ConnectCallback& callback, bool rendezvous)
    {
        assert(!rendezvous);
        if (rendezvous) {
            LOG4CPLUS_FATAL(logger(), "rendezvous not supported!");
            return false;
        }

        if (::fcntl(sysHandle()->sock(), F_SETFL, O_NONBLOCK) < 0) {
            LOG4CPLUS_ERROR(logger(), "cannot set sock non-blocking");
            return false;
        }

        int optval = 1;
        if (::setsockopt(sysHandle()->sock(), SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
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

        int conn_res = ::connect(sysHandle()->sock(), res->ai_addr, res->ai_addrlen);

        freeaddrinfo(res);

        if ((conn_res < 0) && (errno != EINPROGRESS)) {
            LOG4CPLUS_ERROR(logger(), "Cannot connect TCP socket: " << strerror(errno));
            return false;
        }

        callback_ = callback;

        reactor().update(this);

        return true;
    }

    void SysConnector::close()
    {
        boost::shared_ptr<SysHandle> handle = reactor().remove(this);
        if (!handedOut_ && handle) {
            handle->close();
        }
    }

    int SysConnector::getPollEvents() const
    {
        return callback_ ? EPOLLOUT : 0;
    }

    void SysConnector::handleRead()
    {
    }

    void SysConnector::handleWrite()
    {
        ConnectCallback cb = callback_;
        callback_ = ConnectCallback();
        reactor().update(this);

        int result = 0;
        socklen_t resultLen = sizeof(result);
        if (::getsockopt(sysHandle()->sock(), SOL_SOCKET, SO_ERROR, &result, &resultLen) < 0) {
            result = errno;
            LOG4CPLUS_ERROR(logger(), "Cannot getsockopt TCP socket: " << strerror(errno));
        }

        handedOut_ = true;
        cb(result);
    }
}

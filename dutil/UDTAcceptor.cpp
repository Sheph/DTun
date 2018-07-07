#include "DTun/UDTAcceptor.h"
#include "DTun/UDTReactor.h"
#include "Logger.h"

namespace DTun
{
    UDTAcceptor::UDTAcceptor(UDTReactor& reactor, UDTSOCKET sock)
    : UDTSocket(reactor, sock)
    {
        reactor.add(this);
    }

    UDTAcceptor::~UDTAcceptor()
    {
        close();
    }

    bool UDTAcceptor::listen(int backlog, const ListenCallback& callback)
    {
        if (UDT::listen(sock(), backlog) == UDT::ERROR) {
           LOG4CPLUS_ERROR(logger(), "Cannot listen UDT socket: " << UDT::getlasterror().getErrorMessage());
           return false;
        }

        callback_ = callback;

        reactor().update(this);

        return true;
    }

    void UDTAcceptor::close()
    {
        UDTSOCKET s = reactor().remove(this);
        if (s != UDT::INVALID_SOCK) {
            UDT::close(s);
        }
    }

    int UDTAcceptor::getPollEvents() const
    {
        return callback_ ? UDT_EPOLL_IN : 0;
    }

    void UDTAcceptor::handleRead()
    {
        sockaddr_storage clientaddr;
        int addrlen = sizeof(clientaddr);

        UDTSOCKET client;

        if ((client = UDT::accept(sock(), (sockaddr*)&clientaddr, &addrlen)) == UDT::INVALID_SOCK) {
            LOG4CPLUS_ERROR(logger(), "Cannot accept UDT socket: " << UDT::getlasterror().getErrorMessage());
            return;
        }

        bool optval = false;
        if (UDT::setsockopt(client, 0, UDT_RCVSYN, &optval, sizeof(optval)) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot setsockopt on UDT socket: " << UDT::getlasterror().getErrorMessage());
            UDT::close(client);
            return;
        }

        if (UDT::setsockopt(client, 0, UDT_SNDSYN, &optval, sizeof(optval)) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot setsockopt on UDT socket: " << UDT::getlasterror().getErrorMessage());
            UDT::close(client);
            return;
        }

        callback_(client);
    }

    void UDTAcceptor::handleWrite()
    {
    }
}

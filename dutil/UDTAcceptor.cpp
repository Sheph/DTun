#include "DTun/UDTAcceptor.h"
#include "DTun/UDTReactor.h"
#include "DTun/Utils.h"
#include "Logger.h"
#include <boost/make_shared.hpp>

namespace DTun
{
    UDTAcceptor::UDTAcceptor(UDTReactor& reactor, const boost::shared_ptr<UDTHandle>& handle)
    : UDTHandler(reactor, handle)
    {
        reactor.add(this);
    }

    UDTAcceptor::~UDTAcceptor()
    {
        close();
    }

    bool UDTAcceptor::listen(int backlog, const ListenCallback& callback)
    {
        if (UDT::listen(udtHandle()->sock(), backlog) == UDT::ERROR) {
           LOG4CPLUS_ERROR(logger(), "Cannot listen UDT socket: " << UDT::getlasterror().getErrorMessage());
           return false;
        }

        callback_ = callback;

        reactor().update(this);

        return true;
    }

    void UDTAcceptor::close()
    {
        boost::shared_ptr<UDTHandle> handle = reactor().remove(this);
        if (handle) {
            handle->close();
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

        if ((client = UDT::accept(udtHandle()->sock(), (sockaddr*)&clientaddr, &addrlen)) == UDT::INVALID_SOCK) {
            LOG4CPLUS_ERROR(logger(), "Cannot accept UDT socket: " << UDT::getlasterror().getErrorMessage());
            return;
        }

        bool optval = false;
        if (UDT::setsockopt(client, 0, UDT_RCVSYN, &optval, sizeof(optval)) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot setsockopt on UDT socket: " << UDT::getlasterror().getErrorMessage());
            DTun::closeUDTSocketChecked(client);
            return;
        }

        if (UDT::setsockopt(client, 0, UDT_SNDSYN, &optval, sizeof(optval)) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot setsockopt on UDT socket: " << UDT::getlasterror().getErrorMessage());
            DTun::closeUDTSocketChecked(client);
            return;
        }

        callback_(boost::make_shared<UDTHandle>(boost::ref(reactor()), client));
    }

    void UDTAcceptor::handleWrite()
    {
    }
}

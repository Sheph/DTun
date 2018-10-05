#include "DTun/UTPConnector.h"
#include "DTun/UTPManager.h"
#include "Logger.h"
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>

namespace DTun
{
    UTPConnector::UTPConnector(const boost::shared_ptr<UTPHandle>& handle)
    : handedOut_(false)
    , handle_(handle)
    , watch_(boost::make_shared<OpWatch>(boost::ref(handle->reactor())))
    {
    }

    UTPConnector::~UTPConnector()
    {
        close();
    }

    void UTPConnector::close(bool immediate)
    {
        if (watch_->close() && !handedOut_) {
            handle_->close(immediate);
        }
    }

    bool UTPConnector::connect(const std::string& address, const std::string& port, const ConnectCallback& callback, Mode mode)
    {
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

        UInt32 destIp = ((const struct sockaddr_in*)res->ai_addr)->sin_addr.s_addr;
        UInt16 destPort = ((const struct sockaddr_in*)res->ai_addr)->sin_port;

        freeaddrinfo(res);

        handle_->reactor().post(watch_->wrap(
            boost::bind(&UTPConnector::onStartConnect, this, address, port, callback, mode, destIp, destPort)));

        return true;
    }

    void UTPConnector::onStartConnect(const std::string& address, const std::string& port, const ConnectCallback& callback, Mode mode,
        UInt32 destIp, UInt16 destPort)
    {
        if (mode != ModeRendezvousAcc) {
            handle_->impl()->connect(address, port,
                watch_->wrap<int>(boost::bind(&UTPConnector::onConnect, this, _1, callback)));
        } else {
            handle_->impl()->listen(1,
                watch_->wrap<boost::shared_ptr<SHandle> >(boost::bind(&UTPConnector::onRendezvousAccept, this, _1, callback)));
            handle_->reactor().post(watch_->wrap(
                boost::bind(&UTPConnector::onRendezvousTimeout, this, callback)), 18000);
        }
    }

    void UTPConnector::onConnect(int err, const ConnectCallback& callback)
    {
        handedOut_ = true;
        callback(err);
    }

    void UTPConnector::onRendezvousAccept(const boost::shared_ptr<SHandle>& handle, const ConnectCallback& callback)
    {
        if (handedOut_) {
            return;
        }

        boost::shared_ptr<UTPHandle> utpHandle =
            boost::dynamic_pointer_cast<UTPHandle>(handle);
        assert(utpHandle);

        handle_.reset();
        handle_ = utpHandle;

        handedOut_ = true;
        callback(0);
    }

    void UTPConnector::onRendezvousTimeout(const ConnectCallback& callback)
    {
        LOG4CPLUS_TRACE(logger(), "UTPConnector::onRendezvousTimeout()");

        if (handedOut_) {
            return;
        }

        handedOut_ = true;
        callback(1);
    }
}

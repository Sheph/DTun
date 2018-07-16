#include "DTun/LTUDPConnector.h"
#include "DTun/LTUDPManager.h"
#include "Logger.h"
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>

namespace DTun
{
    LTUDPConnector::LTUDPConnector(const boost::shared_ptr<LTUDPHandle>& handle)
    : handedOut_(false)
    , handle_(handle)
    , watch_(boost::make_shared<OpWatch>(boost::ref(handle->reactor())))
    {
    }

    LTUDPConnector::~LTUDPConnector()
    {
        close();
    }

    void LTUDPConnector::close()
    {
        if (watch_->close() && !handedOut_) {
            handle_->close();
        }
    }

    bool LTUDPConnector::connect(const std::string& address, const std::string& port, const ConnectCallback& callback, Mode mode)
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
            boost::bind(&LTUDPConnector::onStartConnect, this, address, port, callback, mode, destIp, destPort)));

        return true;
    }

    void LTUDPConnector::onStartConnect(const std::string& address, const std::string& port, const ConnectCallback& callback, Mode mode,
        UInt32 destIp, UInt16 destPort)
    {
        if (mode != ModeRendezvousAcc) {
            handle_->impl()->connect(address, port,
                watch_->wrap<int>(boost::bind(&LTUDPConnector::onConnect, this, _1, callback)));
        } else {
            handle_->impl()->listen(1,
                watch_->wrap<boost::shared_ptr<SHandle> >(boost::bind(&LTUDPConnector::onRendezvousAccept, this, _1, callback)));
            onRendezvousTimeout(6, 3000, destIp, destPort, callback);
        }
    }

    void LTUDPConnector::onConnect(int err, const ConnectCallback& callback)
    {
        handedOut_ = true;
        callback(err);
    }

    void LTUDPConnector::onRendezvousAccept(const boost::shared_ptr<SHandle>& handle, const ConnectCallback& callback)
    {
        if (handedOut_) {
            return;
        }

        boost::shared_ptr<LTUDPHandle> ltudpHandle =
            boost::dynamic_pointer_cast<LTUDPHandle>(handle);
        assert(ltudpHandle);

        handle_.reset();
        handle_ = ltudpHandle;

        handedOut_ = true;
        callback(0);
    }

    void LTUDPConnector::onRendezvousTimeout(int count, int timeoutMs, UInt32 destIp, UInt16 destPort, const ConnectCallback& callback)
    {
        LOG4CPLUS_TRACE(logger(), "LTUDPConnector::onRendezvousTimeout(" << count << ")");

        if (handedOut_) {
            return;
        }

        if (count == 0) {
            handedOut_ = true;
            callback(ERR_TIMEOUT);
        } else {
            handle_->impl()->rendezvousPing(destIp, destPort);
            handle_->reactor().post(watch_->wrap(
                boost::bind(&LTUDPConnector::onRendezvousTimeout, this, count - 1, timeoutMs, destIp, destPort, callback)), timeoutMs);
        }
    }
}

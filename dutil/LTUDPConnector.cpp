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
        handle_->reactor().post(watch_->wrap(
            boost::bind(&LTUDPConnector::onStartConnect, this, address, port, callback, mode)));

        return true;
    }

    void LTUDPConnector::onStartConnect(const std::string& address, const std::string& port, const ConnectCallback& callback, Mode mode)
    {
        if (mode != ModeRendezvousAcc) {
            handle_->impl()->connect(address, port,
                watch_->wrap<int>(boost::bind(&LTUDPConnector::onConnect, this, _1, callback)));
        } else {
            handle_->impl()->listen(10,
                watch_->wrap<boost::shared_ptr<SHandle> >(boost::bind(&LTUDPConnector::onRendezvousAccept, this, _1, callback)));
        }
    }

    void LTUDPConnector::onConnect(int err, const ConnectCallback& callback)
    {
        handedOut_ = true;
        callback(err);
    }

    void LTUDPConnector::onRendezvousAccept(const boost::shared_ptr<SHandle>& handle, const ConnectCallback& callback)
    {
        LOG4CPLUS_TRACE(logger(), "LTUDPConnector::onRendezvousAccept");

        boost::shared_ptr<LTUDPHandle> ltudpHandle =
            boost::dynamic_pointer_cast<LTUDPHandle>(handle);
        assert(ltudpHandle);

        handle_.reset();
        handle_ = ltudpHandle;

        handedOut_ = true;
        callback(0);
    }
}

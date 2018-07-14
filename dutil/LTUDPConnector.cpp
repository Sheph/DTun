#include "DTun/LTUDPConnector.h"
#include "DTun/LTUDPManager.h"
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

    bool LTUDPConnector::connect(const std::string& address, const std::string& port, const ConnectCallback& callback, bool rendezvous)
    {
        handle_->reactor().dispatch(watch_->wrap(
            boost::bind(&LTUDPConnector::onStartConnect, this, address, port, callback, rendezvous)));

        return true;
    }

    void LTUDPConnector::onStartConnect(const std::string& address, const std::string& port, const ConnectCallback& callback, bool rendezvous)
    {
        handle_->impl()->connect(address, port,
            watch_->wrap<int>(boost::bind(&LTUDPConnector::onConnect, this, _1, callback)), rendezvous);
    }

    void LTUDPConnector::onConnect(int err, const ConnectCallback& callback)
    {
        handedOut_ = true;
        callback(err);
    }
}

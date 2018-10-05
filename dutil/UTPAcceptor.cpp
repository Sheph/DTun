#include "DTun/UTPAcceptor.h"
#include "DTun/UTPManager.h"
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>

namespace DTun
{
    UTPAcceptor::UTPAcceptor(const boost::shared_ptr<UTPHandle>& handle)
    : handle_(handle)
    , watch_(boost::make_shared<OpWatch>(boost::ref(handle->reactor())))
    {
    }

    UTPAcceptor::~UTPAcceptor()
    {
        close();
    }

    void UTPAcceptor::close(bool immediate)
    {
        if (watch_->close()) {
            handle_->close(immediate);
        }
    }

    bool UTPAcceptor::listen(int backlog, const ListenCallback& callback)
    {
        handle_->reactor().post(watch_->wrap(
            boost::bind(&UTPAcceptor::onStartListen, this, backlog, callback)));

        return true;
    }

    void UTPAcceptor::onStartListen(int backlog, const ListenCallback& callback)
    {
        handle_->impl()->listen(backlog, watch_->wrap(callback));
    }
}

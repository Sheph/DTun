#include "DTun/LTUDPAcceptor.h"
#include "DTun/LTUDPManager.h"
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>

namespace DTun
{
    LTUDPAcceptor::LTUDPAcceptor(const boost::shared_ptr<LTUDPHandle>& handle)
    : handle_(handle)
    , watch_(boost::make_shared<OpWatch>(boost::ref(handle->reactor())))
    {
    }

    LTUDPAcceptor::~LTUDPAcceptor()
    {
        close();
    }

    void LTUDPAcceptor::close()
    {
        if (watch_->close()) {
            handle_->close();
        }
    }

    bool LTUDPAcceptor::listen(int backlog, const ListenCallback& callback)
    {
        handle_->reactor().dispatch(watch_->wrap(
            boost::bind(&LTUDPAcceptor::onStartListen, this, backlog, callback)));

        return true;
    }

    void LTUDPAcceptor::onStartListen(int backlog, const ListenCallback& callback)
    {
        handle_->impl()->listen(backlog, watch_->wrap(callback));
    }
}

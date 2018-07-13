#include "DTun/OpWatch.h"
#include <boost/bind.hpp>

namespace DTun
{
    OpWatch::OpWatch()
    : state_(StateActive)
    , inCallback_(false)
    {
    }

    OpWatch::~OpWatch()
    {
    }

    void OpWatch::close()
    {
        boost::mutex::scoped_lock lock(m_);

        if (state_ == StateClosed) {
            return;
        }

        if (state_ == StateActive) {
            state_ = StateClosing;
        }

        while (inCallback_) {
            c_.wait(lock);
        }
        state_ = StateClosed;
    }

    OpWatch::Callback OpWatch::wrap(const Callback& callback)
    {
        return boost::bind(&OpWatch::onWrappedCallback, shared_from_this(), callback);
    }

    void OpWatch::onWrappedCallback(const Callback& callback)
    {
        boost::mutex::scoped_lock lock(m_);
        if (state_ != StateActive) {
            return;
        }
        inCallback_ = true;
        lock.unlock();
        callback();
        lock.lock();
        inCallback_ = false;
        bool signal = (state_ == StateClosing);
        lock.unlock();
        if (signal) {
            c_.notify_all();
        }
    }
}

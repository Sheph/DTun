#include "DTun/OpWatch.h"

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

    bool OpWatch::close()
    {
        boost::mutex::scoped_lock lock(m_);

        if (state_ == StateClosed) {
            return false;
        }

        bool res = false;

        if (state_ == StateActive) {
            state_ = StateClosing;
            res = true;
        }

        while (inCallback_) {
            c_.wait(lock);
        }
        state_ = StateClosed;

        return res;
    }
}

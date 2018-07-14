#include "DTun/OpWatch.h"

namespace DTun
{
    OpWatch::OpWatch(SReactor& reactor)
    : reactor_(reactor)
    , state_(StateActive)
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

        if (inCallback_ && !reactor_.isSameThread()) {
            while (inCallback_) {
                c_.wait(lock);
            }
        }

        state_ = StateClosed;

        return res;
    }
}

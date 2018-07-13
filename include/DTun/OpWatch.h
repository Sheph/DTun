#ifndef _DTUN_OPWATCH_H_
#define _DTUN_OPWATCH_H_

#include "DTun/Types.h"
#include <boost/function.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>

namespace DTun
{
    class OpWatch : boost::noncopyable,
        public boost::enable_shared_from_this<OpWatch>
    {
    public:
        typedef boost::function<void()> Callback;

        OpWatch();
        ~OpWatch();

        void close();

        Callback wrap(const Callback& callback);

    private:
        enum State
        {
            StateActive = 0,
            StateClosing,
            StateClosed,
        };

        void onWrappedCallback(const Callback& callback);

        boost::mutex m_;
        boost::condition_variable c_;
        State state_;
        bool inCallback_;
    };
}

#endif

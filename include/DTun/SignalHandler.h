#ifndef _DTUN_SIGNAL_HANDLER_H_
#define _DTUN_SIGNAL_HANDLER_H_

#include "DTun/Types.h"
#include <boost/noncopyable.hpp>
#include <boost/function.hpp>

namespace DTun
{
    class SignalHandler : boost::noncopyable
    {
    public:
        typedef boost::function<void (int)> Callback;

        SignalHandler(const Callback& callback);
        ~SignalHandler();
    };
}

#endif

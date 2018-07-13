#ifndef _DTUN_SREACTOR_H_
#define _DTUN_SREACTOR_H_

#include "DTun/Types.h"
#include <boost/noncopyable.hpp>
#include <boost/function.hpp>

namespace DTun
{
    class SReactor : boost::noncopyable
    {
    public:
        typedef boost::function<void ()> Callback;

        SReactor() {}
        virtual ~SReactor() {}

        virtual void post(const Callback& callback, UInt32 timeoutMs = 0) = 0;

        virtual void dispatch(const Callback& callback) = 0;
    };
}

#endif

#ifndef _DTUN_SREACTOR_H_
#define _DTUN_SREACTOR_H_

#include "DTun/Types.h"
#include <boost/noncopyable.hpp>
#include <boost/function.hpp>

namespace DTun
{
    class DTUN_API SReactor : boost::noncopyable
    {
    public:
        typedef boost::function<void ()> Callback;

        SReactor() {}
        virtual ~SReactor() {}

        virtual bool start() = 0;

        virtual void run() = 0;

        virtual void processUpdates() = 0;

        virtual void stop() = 0;

        virtual bool isSameThread() const = 0;

        virtual void post(const Callback& callback, UInt32 timeoutMs = 0) = 0;

        virtual void dispatch(const Callback& callback) = 0;

        virtual std::string dump() = 0;
    };
}

#endif

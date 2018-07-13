#ifndef _DTUN_OPWATCH_H_
#define _DTUN_OPWATCH_H_

#include "DTun/Types.h"
#include <boost/function.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>
#include <boost/bind.hpp>

namespace DTun
{
    class DTUN_API OpWatch : boost::noncopyable,
        public boost::enable_shared_from_this<OpWatch>
    {
    public:
        OpWatch();
        ~OpWatch();

        bool close();

        boost::function<void()> wrap(const boost::function<void()>& callback)
        {
            return boost::bind(&OpWatch::onWrappedCallback0, shared_from_this(), callback);
        }

        boost::function<bool()> wrapWithResult(const boost::function<void()>& callback)
        {
            return boost::bind(&OpWatch::onWrappedCallbackWithResult0, shared_from_this(), callback);
        }

        template <class A1>
        boost::function<void(A1)> wrap(const boost::function<void(A1)>& callback)
        {
            return boost::bind(&OpWatch::onWrappedCallback1<A1>, shared_from_this(), callback, _1);
        }

        template <class A1>
        boost::function<bool(A1)> wrapWithResult(const boost::function<void(A1)>& callback)
        {
            return boost::bind(&OpWatch::onWrappedCallbackWithResult1<A1>, shared_from_this(), callback, _1);
        }

    private:
        enum State
        {
            StateActive = 0,
            StateClosing,
            StateClosed,
        };

        void onWrappedCallback0(const boost::function<void()>& callback)
        {
            onWrappedCallbackWithResult0(callback);
        }

        bool onWrappedCallbackWithResult0(const boost::function<void()>& callback)
        {
            boost::mutex::scoped_lock lock(m_);
            if (state_ != StateActive) {
                return false;
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
            return true;
        }

        template <class A1>
        void onWrappedCallback1(const boost::function<void(A1)>& callback, const A1& a1)
        {
            onWrappedCallbackWithResult1(callback, a1);
        }

        template <class A1>
        bool onWrappedCallbackWithResult1(const boost::function<void(A1)>& callback, const A1& a1)
        {
            boost::mutex::scoped_lock lock(m_);
            if (state_ != StateActive) {
                return false;
            }
            inCallback_ = true;
            lock.unlock();
            callback(a1);
            lock.lock();
            inCallback_ = false;
            bool signal = (state_ == StateClosing);
            lock.unlock();
            if (signal) {
                c_.notify_all();
            }
            return true;
        }

        boost::mutex m_;
        boost::condition_variable c_;
        State state_;
        bool inCallback_;
    };
}

#endif

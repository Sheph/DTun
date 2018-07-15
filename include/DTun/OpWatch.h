#ifndef _DTUN_OPWATCH_H_
#define _DTUN_OPWATCH_H_

#include "DTun/Types.h"
#include "DTun/SReactor.h"
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
        explicit OpWatch(SReactor& reactor);
        ~OpWatch();

        bool close();

        boost::function<void()> wrap(const boost::function<void()>& callback)
        {
            return boost::bind(&OpWatch::onWrappedCallback0, shared_from_this(), callback);
        }

        template <class A1>
        boost::function<void(A1)> wrap(const boost::function<void(A1)>& callback)
        {
            return boost::bind(&OpWatch::onWrappedCallback1<A1>, shared_from_this(), callback, _1);
        }

        template <class A1, class A2>
        boost::function<void(A1, A2)> wrap(const boost::function<void(A1, A2)>& callback)
        {
            return boost::bind(&OpWatch::onWrappedCallback2<A1, A2>, shared_from_this(), callback, _1, _2);
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

        template <class A1>
        void onWrappedCallback1(const boost::function<void(A1)>& callback, const A1& a1)
        {
            boost::mutex::scoped_lock lock(m_);
            if (state_ != StateActive) {
                return;
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
        }

        template <class A1, class A2>
        void onWrappedCallback2(const boost::function<void(A1, A2)>& callback, const A1& a1, const A2& a2)
        {
            boost::mutex::scoped_lock lock(m_);
            if (state_ != StateActive) {
                return;
            }
            inCallback_ = true;
            lock.unlock();
            callback(a1, a2);
            lock.lock();
            inCallback_ = false;
            bool signal = (state_ == StateClosing);
            lock.unlock();
            if (signal) {
                c_.notify_all();
            }
        }

        SReactor& reactor_;
        boost::mutex m_;
        boost::condition_variable c_;
        State state_;
        bool inCallback_;
    };
}

#endif

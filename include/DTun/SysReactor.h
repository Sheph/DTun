#ifndef _DTUN_SYSREACTOR_H_
#define _DTUN_SYSREACTOR_H_

#include "DTun/SysHandler.h"
#include "DTun/SReactor.h"
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread.hpp>
#include <boost/chrono.hpp>
#include <map>

namespace DTun
{
    class SysReactor : public SReactor
    {
    public:
        explicit SysReactor();
        ~SysReactor();

        bool start();

        void run();

        void processUpdates();

        void stop();

        virtual void post(const Callback& callback, UInt32 timeoutMs = 0);

        void add(SysHandler* handler);
        boost::shared_ptr<SysHandle> remove(SysHandler* handler);
        void update(SysHandler* handler);

    private:
        struct HandlerInfo
        {
            HandlerInfo()
            : handler(NULL)
            , pollEvents(0) {}

            HandlerInfo(SysHandler* handler, int pollEvents)
            : handler(handler)
            , pollEvents(pollEvents) {}

            SysHandler* handler;
            int pollEvents;
        };

        struct PollHandlerInfo
        {
            PollHandlerInfo()
            : cookie(0)
            , pollEvents(0)
            , notInEpoll(false) {}

            PollHandlerInfo(uint64_t cookie, int pollEvents)
            : cookie(cookie)
            , pollEvents(pollEvents)
            , notInEpoll(false) {}

            uint64_t cookie;
            int pollEvents;
            bool notInEpoll;
        };

        struct DispatchToken
        {
            DispatchToken(const boost::chrono::steady_clock::time_point& scheduledTime,
                const Callback& callback, uint64_t id)
            : scheduledTime(scheduledTime)
            , callback(callback)
            , id(id) {}
            ~DispatchToken() {}

            inline bool operator<(const DispatchToken& rhs) const
            {
                if (scheduledTime < rhs.scheduledTime) {
                    return true;
                } else if (scheduledTime > rhs.scheduledTime) {
                    return false;
                }
                return id < rhs.id;
            }

            boost::chrono::steady_clock::time_point scheduledTime;
            Callback callback;
            uint64_t id;
        };

        typedef std::map<uint64_t, HandlerInfo> HandlerMap;
        typedef std::map<SYSSOCKET, PollHandlerInfo> PollHandlerMap;

        void reset();

        bool isSameThread();

        void signalWr();
        void signalRd();

        void processTokens();

        boost::thread::id runThreadId_;

        boost::mutex m_;
        boost::condition_variable c_;
        int eid_;
        bool stopping_;
        uint64_t nextCookie_;
        SYSSOCKET signalWrSock_;
        SYSSOCKET signalRdSock_;
        HandlerMap handlers_;
        PollHandlerMap pollHandlers_;
        bool inPoll_;
        uint64_t pollIteration_;
        SysHandler* currentlyHandling_;
        std::set<DispatchToken> tokens_;
        uint64_t nextTokenId_;
        boost::optional<boost::chrono::steady_clock::time_point> wakeupTime_;
    };
}

#endif

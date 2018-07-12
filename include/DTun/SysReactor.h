#ifndef _DTUN_SYSREACTOR_H_
#define _DTUN_SYSREACTOR_H_

#include "DTun/SysHandler.h"
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread.hpp>
#include <map>

namespace DTun
{
    class SysReactor : boost::noncopyable
    {
    public:
        explicit SysReactor();
        ~SysReactor();

        bool start();

        void run();

        void processUpdates();

        void stop();

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

        typedef std::map<uint64_t, HandlerInfo> HandlerMap;
        typedef std::map<SYSSOCKET, PollHandlerInfo> PollHandlerMap;

        void reset();

        bool isSameThread();

        void signalWr();
        void signalRd();

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
    };
}

#endif

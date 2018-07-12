#ifndef _DTUN_UDTREACTOR_H_
#define _DTUN_UDTREACTOR_H_

#include "DTun/UDTHandler.h"
#include "DTun/SReactor.h"
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread.hpp>
#include <map>

namespace DTun
{
    class UDTReactor : public SReactor
    {
    public:
        explicit UDTReactor();
        ~UDTReactor();

        bool start();

        void run();

        void processUpdates();

        void stop();

        virtual void post(const Callback& callback, UInt32 timeoutMs = 0);

        void add(UDTHandler* handler);
        boost::shared_ptr<UDTHandle> remove(UDTHandler* handler);
        void update(UDTHandler* handler);

    private:
        struct HandlerInfo
        {
            HandlerInfo()
            : handler(NULL)
            , pollEvents(0) {}

            HandlerInfo(UDTHandler* handler, int pollEvents)
            : handler(handler)
            , pollEvents(pollEvents) {}

            UDTHandler* handler;
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
        typedef std::map<UDTSOCKET, PollHandlerInfo> PollHandlerMap;

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
        UDTSOCKET signalWrSock_;
        UDTSOCKET signalRdSock_;
        HandlerMap handlers_;
        PollHandlerMap pollHandlers_;
        bool inPoll_;
        uint64_t pollIteration_;
        UDTHandler* currentlyHandling_;
    };
}

#endif

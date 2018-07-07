#ifndef _DTUN_UDTREACTOR_H_
#define _DTUN_UDTREACTOR_H_

#include "DTun/UDTSocket.h"
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread.hpp>
#include <map>

namespace DTun
{
    class UDTReactor : boost::noncopyable
    {
    public:
        explicit UDTReactor();
        ~UDTReactor();

        bool start();

        void run();

        void processUpdates();

        void stop();

        void add(UDTSocket* socket);
        UDTSOCKET remove(UDTSocket* socket);
        void update(UDTSocket* socket);

    private:
        struct SocketInfo
        {
            SocketInfo()
            : socket(NULL)
            , pollEvents(0) {}

            SocketInfo(UDTSocket* socket, int pollEvents)
            : socket(socket)
            , pollEvents(pollEvents) {}

            UDTSocket* socket;
            int pollEvents;
        };

        struct PollSocketInfo
        {
            PollSocketInfo()
            : cookie(0)
            , pollEvents(0)
            , notInEpoll(false) {}

            PollSocketInfo(uint64_t cookie, int pollEvents)
            : cookie(cookie)
            , pollEvents(pollEvents)
            , notInEpoll(false) {}

            uint64_t cookie;
            int pollEvents;
            bool notInEpoll;
        };

        typedef std::map<uint64_t, SocketInfo> SocketMap;
        typedef std::map<UDTSOCKET, PollSocketInfo> PollSocketMap;

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
        SocketMap sockets_;
        PollSocketMap pollSockets_;
        uint64_t pollIteration_;
        UDTSocket* currentlyHandling_;
    };
}

#endif

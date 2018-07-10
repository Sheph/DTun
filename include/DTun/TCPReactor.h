#ifndef _DTUN_TCPREACTOR_H_
#define _DTUN_TCPREACTOR_H_

#include "DTun/TCPSocket.h"
#include <boost/thread/mutex.hpp>
#include <boost/thread/condition.hpp>
#include <boost/thread.hpp>
#include <map>

namespace DTun
{
    class TCPReactor : boost::noncopyable
    {
    public:
        explicit TCPReactor();
        ~TCPReactor();

        bool start();

        void run();

        void processUpdates();

        void stop();

        void add(TCPSocket* socket);
        SYSSOCKET remove(TCPSocket* socket);
        void update(TCPSocket* socket);

    private:
        struct SocketInfo
        {
            SocketInfo()
            : socket(NULL)
            , pollEvents(0) {}

            SocketInfo(TCPSocket* socket, int pollEvents)
            : socket(socket)
            , pollEvents(pollEvents) {}

            TCPSocket* socket;
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
        typedef std::map<SYSSOCKET, PollSocketInfo> PollSocketMap;

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
        SocketMap sockets_;
        PollSocketMap pollSockets_;
        bool inPoll_;
        uint64_t pollIteration_;
        TCPSocket* currentlyHandling_;
    };
}

#endif

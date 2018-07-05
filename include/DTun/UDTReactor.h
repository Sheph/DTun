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

        void stop();

        void add(UDTSocket* socket);
        void remove(UDTSocket* socket);
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

        typedef std::map<UDTSOCKET, SocketInfo> SocketMap;

        void reset();

        bool isSameThread();

        void signalWr();
        void signalRd();

        void processUpdates();

        boost::thread::id runThreadId_;

        boost::mutex m_;
        int eid_;
        bool stopping_;
        UDTSOCKET signalWrSock_;
        UDTSOCKET signalRdSock_;
        SocketMap sockets_;

        boost::condition_variable c_;
        std::set<UDTSocket*> toAddSockets_;
        std::set<UDTSocket*> toRemoveSockets_;
        std::set<UDTSocket*> toUpdateSockets_;
    };
}

#endif

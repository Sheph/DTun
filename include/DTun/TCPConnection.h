#ifndef _DTUN_TCPCONNECTION_H_
#define _DTUN_TCPCONNECTION_H_

#include "DTun/TCPSocket.h"
#include <boost/thread/mutex.hpp>
#include <list>

namespace DTun
{
    class TCPConnection : public TCPSocket
    {
    public:
        typedef boost::function<void (int)> WriteCallback;
        typedef boost::function<void (int, int)> ReadCallback;

        TCPConnection(TCPReactor& reactor, SYSSOCKET sock);
        ~TCPConnection();

        void write(const char* first, const char* last, const WriteCallback& callback);

        void read(char* first, char* last, const ReadCallback& callback);

        virtual void close();

        virtual int getPollEvents() const;

        virtual void handleRead();
        virtual void handleWrite();

    private:
        struct WriteReq
        {
            const char* first;
            const char* last;
            WriteCallback callback;
        };

        struct ReadReq
        {
            char* first;
            char* last;
            ReadCallback callback;
        };

        mutable boost::mutex m_;
        std::list<WriteReq> writeQueue_;
        std::list<ReadReq> readQueue_;
    };
}

#endif

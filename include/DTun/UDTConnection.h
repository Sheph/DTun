#ifndef _DTUN_UDTCONNECTION_H_
#define _DTUN_UDTCONNECTION_H_

#include "DTun/UDTSocket.h"
#include <boost/thread/mutex.hpp>
#include <list>

namespace DTun
{
    class UDTConnection : public UDTSocket
    {
    public:
        typedef boost::function<void (int)> WriteCallback;
        typedef boost::function<void (int, int)> ReadCallback;

        UDTConnection(UDTReactor& reactor, UDTSOCKET sock);
        ~UDTConnection();

        bool write(const char* first, const char* last, const WriteCallback& callback);

        bool read(char* first, char* last, const ReadCallback& callback, bool readAll);

        virtual void close();

        virtual int getPollEvents() const;

        virtual void handleRead();
        virtual void handleWrite();
        virtual void handleClose();

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
            int total_read;
            ReadCallback callback;
            bool readAll;
        };

        mutable boost::mutex m_;
        std::list<WriteReq> writeQueue_;
        std::list<ReadReq> readQueue_;
        bool closed_;
    };
}

#endif

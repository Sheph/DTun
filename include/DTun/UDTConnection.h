#ifndef _DTUN_UDTCONNECTION_H_
#define _DTUN_UDTCONNECTION_H_

#include "DTun/UDTHandler.h"
#include "DTun/SConnection.h"
#include <boost/thread/mutex.hpp>
#include <list>

namespace DTun
{
    class UDTConnection : public UDTHandler, public SConnection
    {
    public:
        UDTConnection(UDTReactor& reactor, const boost::shared_ptr<UDTHandle>& handle);
        ~UDTConnection();

        virtual void write(const char* first, const char* last, const WriteCallback& callback);

        virtual void read(char* first, char* last, const ReadCallback& callback, bool readAll);

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
            int total_read;
            ReadCallback callback;
            bool readAll;
        };

        mutable boost::mutex m_;
        std::list<WriteReq> writeQueue_;
        std::list<ReadReq> readQueue_;
    };
}

#endif

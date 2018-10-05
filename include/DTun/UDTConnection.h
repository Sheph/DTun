#ifndef _DTUN_UDTCONNECTION_H_
#define _DTUN_UDTCONNECTION_H_

#include "DTun/UDTHandler.h"
#include "DTun/SConnection.h"
#include <boost/thread/mutex.hpp>
#include <list>

namespace DTun
{
    class DTUN_API UDTConnection : public UDTHandler, public SConnection
    {
    public:
        UDTConnection(UDTReactor& reactor, const boost::shared_ptr<UDTHandle>& handle);
        ~UDTConnection();

        virtual void write(const char* first, const char* last, const WriteCallback& callback);

        virtual void read(char* first, char* last, const ReadCallback& callback, bool readAll);

        virtual void writeTo(const char* first, const char* last, UInt32 destIp, UInt16 destPort, const WriteCallback& callback);

        virtual void readFrom(char* first, char* last, const ReadFromCallback& callback, bool drain = false);

        virtual void close(bool immediate = false);

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

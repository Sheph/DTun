#ifndef _DTUN_SYSCONNECTION_H_
#define _DTUN_SYSCONNECTION_H_

#include "DTun/SysHandler.h"
#include "DTun/SConnection.h"
#include <boost/thread/mutex.hpp>
#include <list>

namespace DTun
{
    class DTUN_API SysConnection : public SysHandler, public SConnection
    {
    public:
        SysConnection(SysReactor& reactor, const boost::shared_ptr<SysHandle>& handle);
        ~SysConnection();

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
            UInt32 destIp;
            UInt16 destPort;
            WriteCallback callback;
        };

        struct ReadReq
        {
            char* first;
            char* last;
            ReadCallback callback;
            ReadFromCallback fromCallback;
            bool drain;
        };

        void handleReadNormal(ReadReq* req);
        bool handleReadFrom(ReadReq* req);

        mutable boost::mutex m_;
        std::list<WriteReq> writeQueue_;
        std::list<ReadReq> readQueue_;
    };
}

#endif

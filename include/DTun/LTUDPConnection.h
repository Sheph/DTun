#ifndef _DTUN_LTUDPCONNECTION_H_
#define _DTUN_LTUDPCONNECTION_H_

#include "DTun/SConnection.h"
#include "DTun/LTUDPHandle.h"
#include "DTun/OpWatch.h"
#include <list>

namespace DTun
{
    class DTUN_API LTUDPConnection : public SConnection
    {
    public:
        explicit LTUDPConnection(const boost::shared_ptr<LTUDPHandle>& handle);
        ~LTUDPConnection();

        virtual boost::shared_ptr<SHandle> handle() const { return handle_; }

        virtual void close();

        virtual void write(const char* first, const char* last, const WriteCallback& callback);

        virtual void read(char* first, char* last, const ReadCallback& callback, bool readAll);

        virtual void writeTo(const char* first, const char* last, UInt32 destIp, UInt16 destPort, const WriteCallback& callback);

        virtual void readFrom(char* first, char* last, const ReadFromCallback& callback);

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

        typedef std::pair<const char*, const char*> WriteOutReq;

        void onWrite(const char* first, const char* last, const WriteCallback& callback);

        void onRead(char* first, char* last, const ReadCallback& callback, bool readAll);

        void onHandleWrite(int err, int numBytes);

        void onHandleRead();

        boost::shared_ptr<LTUDPHandle> handle_;
        boost::shared_ptr<OpWatch> watch_;
        std::list<WriteReq> writeQueue_;
        std::list<ReadReq> readQueue_;
        std::list<WriteOutReq> writeOutQueue_;
    };
}

#endif

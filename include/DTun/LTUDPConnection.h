#ifndef _DTUN_LTUDPCONNECTION_H_
#define _DTUN_LTUDPCONNECTION_H_

#include "DTun/SConnection.h"
#include "DTun/LTUDPHandle.h"
#include "DTun/OpWatch.h"

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

    private:
        //void onSend(int err);
        //void onRecv(int err, int numBytes);

        boost::shared_ptr<LTUDPHandle> handle_;
        //OpWatch watch_;
    };
}

#endif

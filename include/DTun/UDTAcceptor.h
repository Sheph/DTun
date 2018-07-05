#ifndef _DTUN_UDTACCEPTOR_H_
#define _DTUN_UDTACCEPTOR_H_

#include "DTun/UDTSocket.h"

namespace DTun
{
    class UDTAcceptor : public UDTSocket
    {
    public:
        typedef boost::function<void (UDTSOCKET)> ListenCallback;

        UDTAcceptor(UDTReactor& reactor, UDTSOCKET sock);
        ~UDTAcceptor();

        bool listen(int backlog, const ListenCallback& callback);

        virtual void close();

        virtual int getPollEvents() const;

        virtual void handleRead();
        virtual void handleWrite();
        virtual void handleClose();

    private:
        ListenCallback callback_;
    };
}

#endif

#ifndef _DTUN_UDTCONNECTOR_H_
#define _DTUN_UDTCONNECTOR_H_

#include "DTun/UDTSocket.h"

namespace DTun
{
    class UDTConnector : public UDTSocket
    {
    public:
        typedef boost::function<void (int)> ConnectCallback;

        UDTConnector(UDTReactor& reactor, UDTSOCKET sock);
        ~UDTConnector();

        bool connect(const std::string& address, const std::string& port, const ConnectCallback& callback);

        virtual void close();

        virtual int getPollEvents() const;

        virtual void handleRead();
        virtual void handleWrite();

    private:
        ConnectCallback callback_;
        bool noCloseSock_;
    };
}

#endif

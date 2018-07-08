#ifndef _DTUN_TCPCONNECTOR_H_
#define _DTUN_TCPCONNECTOR_H_

#include "DTun/TCPSocket.h"

namespace DTun
{
    class TCPConnector : public TCPSocket
    {
    public:
        typedef boost::function<void (int)> ConnectCallback;

        TCPConnector(TCPReactor& reactor, SYSSOCKET sock);
        ~TCPConnector();

        bool connect(const std::string& address, const std::string& port, const ConnectCallback& callback);

        virtual void close();

        virtual int getPollEvents() const;

        virtual void handleRead();
        virtual void handleWrite();

    private:
        ConnectCallback callback_;
        bool handedOut_;
    };
}

#endif

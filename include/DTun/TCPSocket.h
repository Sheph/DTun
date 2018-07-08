#ifndef _DTUN_TCPSOCKET_H_
#define _DTUN_TCPSOCKET_H_

#include "DTun/Types.h"
#include <boost/noncopyable.hpp>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include "udt.h"

namespace DTun
{
    class TCPReactor;

    class TCPSocket : boost::noncopyable
    {
    public:
        TCPSocket(TCPReactor& reactor, SYSSOCKET sock);
        virtual ~TCPSocket();

        inline TCPReactor& reactor() { return reactor_; }

        inline SYSSOCKET sock() const { return sock_; }

        inline void setCookie(uint64_t cookie) { cookie_ = cookie; }
        inline uint64_t cookie() const { return cookie_; }

        void resetSock() { sock_ = SYS_INVALID_SOCKET; }

        virtual void close() = 0;

        virtual int getPollEvents() const = 0;

        virtual void handleRead() = 0;
        virtual void handleWrite() = 0;

    private:
        TCPReactor& reactor_;
        SYSSOCKET sock_;
        uint64_t cookie_;
    };
}

#endif

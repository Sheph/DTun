#ifndef _DTUN_UDTSOCKET_H_
#define _DTUN_UDTSOCKET_H_

#include "DTun/Types.h"
#include <boost/noncopyable.hpp>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>
#include "udt.h"

namespace DTun
{
    class UDTReactor;

    class UDTSocket : boost::noncopyable
    {
    public:
        UDTSocket(UDTReactor& reactor, UDTSOCKET sock)
        : reactor_(reactor)
        , sock_(sock)
        , cookie_(0)
        {
        }

        virtual ~UDTSocket()
        {
        }

        inline UDTReactor& reactor() { return reactor_; }

        inline UDTSOCKET sock() const { return sock_; }

        inline void setCookie(uint64_t cookie) { cookie_ = cookie; }
        inline uint64_t cookie() const { return cookie_; }

        void resetSock() { sock_ = UDT::INVALID_SOCK; }

        virtual void close() = 0;

        virtual int getPollEvents() const = 0;

        virtual void handleRead() = 0;
        virtual void handleWrite() = 0;
        virtual void handleBroken(int err) = 0;

    private:
        UDTReactor& reactor_;
        UDTSOCKET sock_;
        uint64_t cookie_;
    };
}

#endif

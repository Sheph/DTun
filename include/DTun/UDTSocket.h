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
        , inDestructor_(false)
        {
        }

        virtual ~UDTSocket()
        {
        }

        inline UDTReactor& reactor() { return reactor_; }

        inline UDTSOCKET sock() const { return sock_; }

        inline bool inDestructor() const { return inDestructor_; }

        virtual void close() = 0;

        virtual int getPollEvents() const = 0;

        virtual void handleRead() = 0;
        virtual void handleWrite() = 0;
        virtual void handleClose() = 0;

    protected:
        void resetSock() { sock_ = UDT::INVALID_SOCK; }
        void setInDestructor() { inDestructor_ = true; }

    private:
        UDTReactor& reactor_;
        UDTSOCKET sock_;
        bool inDestructor_;
    };
}

#endif

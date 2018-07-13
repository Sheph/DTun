#ifndef _DTUN_UDTHANDLER_H_
#define _DTUN_UDTHANDLER_H_

#include "DTun/UDTHandle.h"
#include "DTun/SHandler.h"
#include <boost/noncopyable.hpp>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>

namespace DTun
{
    class UDTReactor;

    class DTUN_API UDTHandler : virtual public SHandler
    {
    public:
        UDTHandler(UDTReactor& reactor, const boost::shared_ptr<UDTHandle>& handle);
        virtual ~UDTHandler();

        inline UDTReactor& reactor() { return reactor_; }

        virtual boost::shared_ptr<SHandle> handle() const { return handle_; }
        const boost::shared_ptr<UDTHandle>& udtHandle() const { return handle_; }

        inline void setCookie(uint64_t cookie) { cookie_ = cookie; }
        inline uint64_t cookie() const { return cookie_; }

        void resetHandle() { handle_.reset(); }

        virtual int getPollEvents() const = 0;

        virtual void handleRead() = 0;
        virtual void handleWrite() = 0;

    private:
        UDTReactor& reactor_;
        boost::shared_ptr<UDTHandle> handle_;
        uint64_t cookie_;
    };
}

#endif

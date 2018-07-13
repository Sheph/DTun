#ifndef _DTUN_SYSHANDLER_H_
#define _DTUN_SYSHANDLER_H_

#include "DTun/SysHandle.h"
#include "DTun/SHandler.h"
#include <boost/noncopyable.hpp>
#include <boost/function.hpp>
#include <boost/shared_ptr.hpp>

namespace DTun
{
    class SysReactor;

    class DTUN_API SysHandler : public virtual SHandler
    {
    public:
        SysHandler(SysReactor& reactor, const boost::shared_ptr<SysHandle>& handle);
        ~SysHandler();

        inline SysReactor& reactor() { return reactor_; }

        virtual boost::shared_ptr<SHandle> handle() const { return handle_; }
        const boost::shared_ptr<SysHandle>& sysHandle() const { return handle_; }

        inline void setCookie(uint64_t cookie) { cookie_ = cookie; }
        inline uint64_t cookie() const { return cookie_; }

        void resetHandle() { handle_.reset(); }

        virtual int getPollEvents() const = 0;

        virtual void handleRead() = 0;
        virtual void handleWrite() = 0;

    private:
        SysReactor& reactor_;
        boost::shared_ptr<SysHandle> handle_;
        uint64_t cookie_;
    };
}

#endif

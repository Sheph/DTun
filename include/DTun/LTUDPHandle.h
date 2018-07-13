#ifndef _DTUN_LTUDPHANDLE_H_
#define _DTUN_LTUDPHANDLE_H_

#include "DTun/LTUDPHandleImpl.h"
#include "DTun/SReactor.h"
#include <boost/enable_shared_from_this.hpp>

namespace DTun
{
    class LTUDPManager;

    class LTUDPHandle : public SHandle,
        public boost::enable_shared_from_this<LTUDPHandle>
    {
    public:
        explicit LTUDPHandle(LTUDPManager& mgr);
        ~LTUDPHandle();

        inline const boost::shared_ptr<LTUDPHandleImpl>& impl() const { return impl_; }

        inline SReactor& reactor() { return reactor_; }

        virtual bool bind(SYSSOCKET s);

        virtual bool bind(const struct sockaddr* name, int namelen);

        virtual bool getSockName(UInt32& ip, UInt16& port) const;

        virtual bool getPeerName(UInt32& ip, UInt16& port) const;

        virtual void close();

        virtual boost::shared_ptr<SConnector> createConnector();

        virtual boost::shared_ptr<SAcceptor> createAcceptor();

        virtual boost::shared_ptr<SConnection> createConnection();

    private:
        SReactor& reactor_;
        boost::shared_ptr<LTUDPHandleImpl> impl_;
    };
}

#endif

#ifndef _DTUN_UTPHANDLE_H_
#define _DTUN_UTPHANDLE_H_

#include "DTun/UTPHandleImpl.h"
#include "DTun/SReactor.h"
#include <boost/enable_shared_from_this.hpp>

namespace DTun
{
    class UTPManager;

    class DTUN_API UTPHandle : public SHandle,
        public boost::enable_shared_from_this<UTPHandle>
    {
    public:
        explicit UTPHandle(UTPManager& mgr);
        UTPHandle(UTPManager& mgr,
            const boost::shared_ptr<SConnection>& conn, utp_socket* utpSock);
        ~UTPHandle();

        inline const boost::shared_ptr<UTPHandleImpl>& impl() const { return impl_; }

        SReactor& reactor();

        virtual void ping(UInt32 ip, UInt16 port);

        virtual bool bind(SYSSOCKET s);

        virtual bool bind(const struct sockaddr* name, int namelen);

        virtual bool getSockName(UInt32& ip, UInt16& port) const;

        virtual bool getPeerName(UInt32& ip, UInt16& port) const;

        virtual SYSSOCKET duplicate();

        virtual int getTTL() const;

        virtual bool setTTL(int ttl);

        virtual void close(bool immediate = false);

        virtual bool canReuse() const;

        virtual boost::shared_ptr<SConnector> createConnector();

        virtual boost::shared_ptr<SAcceptor> createAcceptor();

        virtual boost::shared_ptr<SConnection> createConnection();

    private:
        UTPManager& mgr_;
        boost::shared_ptr<UTPHandleImpl> impl_;
        UInt16 transportPort_;
    };
}

#endif

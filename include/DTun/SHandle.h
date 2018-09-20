#ifndef _DTUN_SHANDLE_H_
#define _DTUN_SHANDLE_H_

#include "DTun/Types.h"
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>

namespace DTun
{
    class SConnector;
    class SAcceptor;
    class SConnection;

    class DTUN_API SHandle : boost::noncopyable
    {
    public:
        SHandle() {}
        virtual ~SHandle() {}

        // used to ping handle from any place, use with care
        // it may run underlying transport connection's destructor...
        virtual void ping(UInt32 ip, UInt16 port) = 0;

        // consumes 'sock'
        virtual bool bind(SYSSOCKET s) = 0;

        virtual bool bind(const struct sockaddr* name, int namelen) = 0;

        virtual bool getSockName(UInt32& ip, UInt16& port) const = 0;

        virtual bool getPeerName(UInt32& ip, UInt16& port) const = 0;

        virtual SYSSOCKET duplicate() = 0;

        virtual int getTTL() const = 0;

        virtual bool setTTL(int ttl) = 0;

        virtual void close(bool immediate = false) = 0;

        virtual bool canReuse() const = 0;

        virtual boost::shared_ptr<SConnector> createConnector() = 0;

        virtual boost::shared_ptr<SAcceptor> createAcceptor() = 0;

        virtual boost::shared_ptr<SConnection> createConnection() = 0;
    };
}

#endif

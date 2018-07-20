#ifndef _DTUN_SHANDLE_H_
#define _DTUN_SHANDLE_H_

#include "DTun/Types.h"
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>

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

        // consumes 'sock'
        virtual bool bind(SYSSOCKET s) = 0;

        virtual bool bind(const struct sockaddr* name, int namelen) = 0;

        virtual bool getSockName(UInt32& ip, UInt16& port) const = 0;

        virtual bool getPeerName(UInt32& ip, UInt16& port) const = 0;

        virtual SYSSOCKET duplicate() = 0;

        virtual void close() = 0;

        virtual boost::shared_ptr<SConnector> createConnector() = 0;

        virtual boost::shared_ptr<SAcceptor> createAcceptor() = 0;

        virtual boost::shared_ptr<SConnection> createConnection() = 0;
    };
}

#endif

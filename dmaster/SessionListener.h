#ifndef _SESSIONLISTENER_H_
#define _SESSIONLISTENER_H_

#include "DTun/Types.h"
#include <boost/noncopyable.hpp>

namespace DMaster
{
    class SessionListener : boost::noncopyable
    {
    public:
        explicit SessionListener() {}
        ~SessionListener() {}

        virtual void onStartPersistent() = 0;

        virtual void onStartConnector(DTun::UInt32 dstNodeId,
            DTun::UInt32 connId,
            DTun::UInt32 remoteIp,
            DTun::UInt16 remotePort) = 0;

        virtual void onStartAcceptor(DTun::UInt32 connId) = 0;

        virtual void onError(int errCode) = 0;
    };
}

#endif

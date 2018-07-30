#ifndef _RENDEZVOUSSESSION_H_
#define _RENDEZVOUSSESSION_H_

#include "DTun/Types.h"
#include "DTun/SConnection.h"
#include <boost/noncopyable.hpp>
#include <boost/function.hpp>
#include <vector>

namespace DNode
{
    struct HandleKeepalive
    {
        HandleKeepalive()
        : destIp(0)
        , destPort(0)
        , srcPort(0) {}

        HandleKeepalive(const boost::shared_ptr<DTun::SHandle>& handle,
            DTun::UInt32 destIp,
            DTun::UInt16 destPort,
            DTun::UInt16 srcPort)
        : handle(handle)
        , destIp(destIp)
        , destPort(destPort)
        , srcPort(srcPort) {}

        boost::shared_ptr<DTun::SHandle> handle;
        DTun::UInt32 destIp;
        DTun::UInt16 destPort;
        DTun::UInt16 srcPort;
    };

    typedef std::vector<HandleKeepalive> HandleKeepaliveList;

    class RendezvousSession : boost::noncopyable
    {
    public:
        typedef boost::function<void (int, SYSSOCKET, DTun::UInt32, DTun::UInt16, DTun::UInt16)> Callback;

        RendezvousSession(DTun::UInt32 nodeId, const DTun::ConnId& connId)
        : nodeId_(nodeId)
        , connId_(connId)
        , started_(false) {}
        virtual ~RendezvousSession() {}

        virtual void onMsg(DTun::UInt8 msgId, const void* msg) = 0;

        virtual void onEstablished() = 0;

        inline DTun::UInt32 nodeId() const { return nodeId_; }

        inline const DTun::ConnId& connId() const { return connId_; }

        inline bool started() const { return started_; }
        inline void setStarted() { started_ = true; }

    private:
        DTun::UInt32 nodeId_;
        DTun::ConnId connId_;
        bool started_;
    };
}

#endif

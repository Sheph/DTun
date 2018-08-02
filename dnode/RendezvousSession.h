#ifndef _RENDEZVOUSSESSION_H_
#define _RENDEZVOUSSESSION_H_

#include "PortReservation.h"
#include "DTun/Types.h"
#include "DTun/SConnection.h"
#include <boost/noncopyable.hpp>
#include <boost/function.hpp>
#include <vector>

namespace DNode
{
    class RendezvousSession : boost::noncopyable
    {
    public:
        typedef boost::function<void (int, SYSSOCKET, DTun::UInt32, DTun::UInt16, const boost::shared_ptr<PortReservation>&)> Callback;

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

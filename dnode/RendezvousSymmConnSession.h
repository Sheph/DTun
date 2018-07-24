#ifndef _RENDEZVOUSSYMMCONNSESSION_H_
#define _RENDEZVOUSSYMMCONNSESSION_H_

#include "RendezvousSession.h"
#include "DTun/SManager.h"
#include <vector>

namespace DNode
{
    class RendezvousSymmConnSession : public RendezvousSession
    {
    public:
        RendezvousSymmConnSession(const DTun::ConnId& connId, bool owner);
        ~RendezvousSymmConnSession();

        //bool start(const boost::shared_ptr<DTun::SConnection>& serverConn, const std::vector<boost::shared_ptr<DTun::SHandle> >& keepalive, const Callback& callback);
        bool start(const Callback& callback);

        virtual void onMsg(DTun::UInt8 msgId, const void* msg);

        virtual void onEstablished();

    private:
        void onSymm(DTun::UInt32 nodeIp, DTun::UInt16 nodePort);
        void onPingSend(int err);

        DTun::SManager& mgr_;
        bool owner_;
        Callback callback_;
        std::vector<boost::shared_ptr<DTun::SConnection> > pingConns_;
        boost::shared_ptr<DTun::SConnection> serverConn_;
    };
}

#endif

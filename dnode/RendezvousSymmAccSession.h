#ifndef _RENDEZVOUSSYMMACCSESSION_H_
#define _RENDEZVOUSSYMMACCSESSION_H_

#include "RendezvousSession.h"
#include "DMasterSession.h"
#include "DTun/OpWatch.h"

namespace DNode
{
    class RendezvousSymmAccSession : public RendezvousSession
    {
    public:
        RendezvousSymmAccSession(const DTun::ConnId& connId, bool owner);
        ~RendezvousSymmAccSession();

        //bool start(const boost::shared_ptr<DTun::SConnection>& serverConn, const std::vector<boost::shared_ptr<DTun::SHandle> >& keepalive, const Callback& callback);
        bool start(const Callback& callback);

        virtual void onMsg(DTun::UInt8 msgId, const void* msg);

        virtual void onEstablished();

    private:
        void onSymmNext();
        void onPingSend(int err);
        void onHelloSend(int err);
        void onSymmTimeout();

        bool owner_;
        int stepIdx_;
        SYSSOCKET s_;
        Callback callback_;
        boost::shared_ptr<DMasterSession> masterSession_;
        boost::shared_ptr<DTun::SConnection> serverConn_;
        boost::shared_ptr<DTun::OpWatch> watch_;
    };
}

#endif

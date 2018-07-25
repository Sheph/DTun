#ifndef _RENDEZVOUSSYMMACCSESSION_H_
#define _RENDEZVOUSSYMMACCSESSION_H_

#include "RendezvousSession.h"
#include "DMasterSession.h"
#include "DTun/SManager.h"
#include "DTun/OpWatch.h"
#include <boost/thread/mutex.hpp>

namespace DNode
{
    class RendezvousSymmAccSession : public RendezvousSession
    {
    public:
        RendezvousSymmAccSession(DTun::SManager& localMgr, DTun::SManager& remoteMgr,
            DTun::UInt32 nodeId, const DTun::ConnId& connId,
            const std::vector<boost::shared_ptr<DTun::SHandle> >& keepalive);
        ~RendezvousSymmAccSession();

        bool start(const Callback& callback);

        virtual void onMsg(DTun::UInt8 msgId, const void* msg);

        virtual void onEstablished();

    private:
        void onHelloSend(int err);
        void onPingSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff);
        void onRecvPing(int err, int numBytes, DTun::UInt32 ip, DTun::UInt16 port, const boost::shared_ptr<std::vector<char> >& rcvBuff);
        void onSymmNextTimeout();

        DTun::SManager& localMgr_;
        DTun::SManager& remoteMgr_;
        bool owner_;
        boost::mutex m_;
        int stepIdx_;
        int numPingSent_;
        int numKeepaliveSent_;
        Callback callback_;
        boost::shared_ptr<DTun::OpWatch> watch_;
        std::vector<boost::shared_ptr<DTun::SHandle> > keepalive_;
        boost::shared_ptr<DTun::SConnection> pingConn_;
        boost::shared_ptr<DMasterSession> masterSession_;
    };
}

#endif

#ifndef _RENDEZVOUSSYMMCONNSESSION_H_
#define _RENDEZVOUSSYMMCONNSESSION_H_

#include "RendezvousSession.h"
#include "DTun/SManager.h"
#include "DTun/OpWatch.h"
#include "DTun/DProtocol.h"
#include <boost/thread/mutex.hpp>
#include <vector>

namespace DNode
{
    class RendezvousSymmConnSession : public RendezvousSession
    {
    public:
        RendezvousSymmConnSession(DTun::SManager& localMgr, DTun::SManager& remoteMgr,
            DTun::UInt32 nodeId, const DTun::ConnId& connId);
        ~RendezvousSymmConnSession();

        bool start(const boost::shared_ptr<DTun::SConnection>& serverConn,
            const HandleKeepaliveList& keepalive,
            const Callback& callback);

        virtual void onMsg(DTun::UInt8 msgId, const void* msg);

        virtual void onEstablished();

    private:
        static void onServerSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff);

        void onPingSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff);
        void onRecvPing(int err, int numBytes, DTun::UInt32 ip, DTun::UInt16 port, int connIdx, const boost::shared_ptr<std::vector<char> >& rcvBuff);
        void onEstablishedTimeout();

        void sendSymmNext();

        DTun::SManager& localMgr_;
        DTun::SManager& remoteMgr_;
        int windowSize_;
        bool owner_;
        boost::mutex m_;
        int numPingSent_;
        Callback callback_;
        DTun::UInt32 destIp_;
        DTun::UInt16 destPort_;
        boost::shared_ptr<DTun::OpWatch> watch_;
        HandleKeepaliveList keepalive_;
        boost::shared_ptr<DTun::SConnection> serverConn_;
        std::vector<boost::shared_ptr<DTun::SConnection> > pingConns_;
    };
}

#endif

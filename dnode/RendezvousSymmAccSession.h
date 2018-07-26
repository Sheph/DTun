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
            DTun::UInt32 nodeId, const DTun::ConnId& connId, const std::string& serverAddr, int serverPort,
            DTun::UInt32 destIp);
        ~RendezvousSymmAccSession();

        bool start(const boost::shared_ptr<DTun::SConnection>& serverConn,
            const std::vector<boost::shared_ptr<DTun::SHandle> >& keepalive,
            const Callback& callback);

        virtual void onMsg(DTun::UInt8 msgId, const void* msg);

        virtual void onEstablished();

    private:
        static void onSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff);

        void onHelloSend(int err);
        void onPingSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff);
        void onRecvPing(int err, int numBytes, DTun::UInt32 ip, DTun::UInt16 port, const boost::shared_ptr<std::vector<char> >& rcvBuff);
        void onSymmNextTimeout();
        void onCheckStartTimeout();
        void onSendFinalTimeout(int cnt);

        DTun::UInt16 getCurrentPort();

        void sendSymmNext();

        DTun::SManager& localMgr_;
        DTun::SManager& remoteMgr_;
        std::string serverAddr_;
        int serverPort_;
        int windowSize_;
        bool owner_;
        boost::mutex m_;
        int stepIdx_;
        int numPingSent_;
        int numKeepaliveSent_;
        Callback callback_;
        DTun::UInt32 destIp_;
        DTun::UInt16 destDiscoveredPort_;
        boost::shared_ptr<DTun::OpWatch> watch_;
        std::vector<boost::shared_ptr<DTun::SHandle> > keepalive_;
        boost::shared_ptr<DTun::SConnection> serverConn_;
        boost::shared_ptr<DTun::SConnection> pingConn_;
        boost::shared_ptr<DTun::SHandle> masterHandle_;
        boost::shared_ptr<DMasterSession> masterSession_;
    };
}

#endif

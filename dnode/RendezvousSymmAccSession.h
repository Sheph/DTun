#ifndef _RENDEZVOUSSYMMACCSESSION_H_
#define _RENDEZVOUSSYMMACCSESSION_H_

#include "RendezvousSession.h"
#include "DMasterSession.h"
#include "PortAllocator.h"
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
            DTun::UInt32 destIp, const boost::shared_ptr<PortAllocator>& portAllocator, bool bestEffort);
        ~RendezvousSymmAccSession();

        bool start(const boost::shared_ptr<DTun::SConnection>& serverConn,
            const Callback& callback);

        virtual void onMsg(DTun::UInt8 msgId, const void* msg);

        virtual void onEstablished();

    private:
        static void onSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff);

        void onPortReservation();
        void onHelloSend(int err);
        void onPingSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff);
        void onRecvPing(int err, int numBytes, DTun::UInt32 ip, DTun::UInt16 port, const boost::shared_ptr<std::vector<char> >& rcvBuff);
        void onSymmNextTimeout();
        void onCheckStartTimeout();
        void onSendFinalTimeout(int cnt);

        DTun::UInt16 getCurrentPort();

        void sendReady();

        void sendSymmNext();

        DTun::SManager& localMgr_;
        DTun::SManager& remoteMgr_;
        std::string serverAddr_;
        int serverPort_;
        int windowSize_;
        bool owner_;
        boost::shared_ptr<PortAllocator> portAllocator_;
        bool bestEffort_;
        std::vector<DTun::UInt16> ports_;

        boost::mutex m_;
        bool ready_;
        int stepIdx_;
        int numPingSent_;
        Callback callback_;
        DTun::UInt32 destIp_;
        DTun::UInt16 destDiscoveredPort_;
        boost::shared_ptr<DTun::OpWatch> watch_;
        boost::shared_ptr<PortReservation> portReservation_;
        boost::shared_ptr<DTun::SConnection> serverConn_;
        boost::shared_ptr<DTun::SConnection> pingConn_;
        boost::shared_ptr<DTun::SHandle> masterHandle_;
        boost::shared_ptr<DMasterSession> masterSession_;
    };
}

#endif

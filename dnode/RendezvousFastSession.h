#ifndef _RENDEZVOUSFASTSESSION_H_
#define _RENDEZVOUSFASTSESSION_H_

#include "RendezvousSession.h"
#include "DMasterSession.h"
#include "PortAllocator.h"
#include "DTun/OpWatch.h"
#include "DTun/SManager.h"

namespace DNode
{
    class RendezvousFastSession : public RendezvousSession
    {
    public:
        RendezvousFastSession(DTun::SManager& localMgr, DTun::SManager& remoteMgr, DTun::UInt32 nodeId, const DTun::ConnId& connId,
            const std::string& serverAddr, int serverPort,
            const boost::shared_ptr<PortAllocator>& portAllocator, bool bestEffort);
        ~RendezvousFastSession();

        bool start(const boost::shared_ptr<DTun::SConnection>& serverConn,
            const Callback& callback);

        virtual void onMsg(DTun::UInt8 msgId, const void* msg);

        virtual void onEstablished();

    private:
        static void onSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff);

        void onPortReservation();
        void onHelloSend(int err);
        void onPingSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff);
        void onRecvPing(int err, int numBytes, DTun::UInt32 ip, DTun::UInt16 port);
        void onCheckStartTimeout();
        void onPingTimeout();

        void sendReady();

        void sendNext();

        DTun::SManager& localMgr_;
        DTun::SManager& remoteMgr_;
        std::string serverAddr_;
        int serverPort_;
        boost::shared_ptr<PortAllocator> portAllocator_;
        bool bestEffort_;
        bool owner_;

        boost::mutex m_;
        bool ready_;
        int stepIdx_;
        int origTTL_;
        int ttl_;
        bool next_;
        std::vector<char> rcvBuff_;
        Callback callback_;
        DTun::UInt32 destIp_;
        DTun::UInt16 destPort_;
        boost::shared_ptr<DTun::OpWatch> watch_;
        boost::shared_ptr<PortReservation> portReservation_;
        boost::shared_ptr<PortReservation> portReservationNext_;
        boost::shared_ptr<DTun::SConnection> serverConn_;
        boost::shared_ptr<DTun::SHandle> masterHandle_;
        boost::shared_ptr<DMasterSession> masterSession_;
        boost::shared_ptr<DTun::SConnection> pingConn_;
    };
}

#endif

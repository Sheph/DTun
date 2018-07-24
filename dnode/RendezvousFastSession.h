#ifndef _RENDEZVOUSFASTSESSION_H_
#define _RENDEZVOUSFASTSESSION_H_

#include "RendezvousSession.h"
#include "DMasterSession.h"
#include "DTun/OpWatch.h"
#include "DTun/SManager.h"

namespace DNode
{
    class RendezvousFastSession : public RendezvousSession
    {
    public:
        RendezvousFastSession(DTun::SManager& localMgr, DTun::SManager& remoteMgr, DTun::UInt32 nodeId, const DTun::ConnId& connId);
        ~RendezvousFastSession();

        bool start(const std::string& serverAddr, int serverPort, const Callback& callback);

        virtual void onMsg(DTun::UInt8 msgId, const void* msg);

        virtual void onEstablished();

    private:
        void onHelloSend(int err);
        void onPingSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff);
        void onRecvPing(int err, int numBytes, DTun::UInt32 ip, DTun::UInt16 port);
        void onPingTimeout();

        DTun::SManager& localMgr_;
        DTun::SManager& remoteMgr_;
        boost::mutex m_;
        bool established_;
        std::vector<char> rcvBuff_;
        Callback callback_;
        DTun::UInt32 destIp_;
        DTun::UInt16 destPort_;
        boost::shared_ptr<DTun::OpWatch> watch_;
        boost::shared_ptr<DMasterSession> masterSession_;
        boost::shared_ptr<DTun::SConnection> pingConn_;
    };
}

#endif

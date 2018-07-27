#ifndef _DMASTERCLIENT_H_
#define _DMASTERCLIENT_H_

#include "DTun/Types.h"
#include "DTun/DProtocol.h"
#include "DTun/SManager.h"
#include "DTun/AppConfig.h"
#include "ProxySession.h"
#include "RendezvousSession.h"
#include <boost/optional.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/weak_ptr.hpp>
#include <set>
#include <list>

namespace DNode
{
    class DMasterClient : boost::noncopyable
    {
    public:
        typedef boost::function<void (int, const boost::shared_ptr<DTun::SHandle>, DTun::UInt32, DTun::UInt16)> RegisterConnectionCallback;

        DMasterClient(DTun::SManager& remoteMgr, DTun::SManager& localMgr,
            const boost::shared_ptr<DTun::AppConfig>& appConfig);
        ~DMasterClient();

        bool start();

        DTun::ConnId registerConnection(DTun::UInt32 remoteIp,
            DTun::UInt16 remotePort,
            const RegisterConnectionCallback& callback);

        void closeConnection(const DTun::ConnId& connId);

        void dump();

        bool getDstNodeId(DTun::UInt32 remoteIp, DTun::UInt32& dstNodeId) const;

    private:
        enum RendezvousMode
        {
            RendezvousModeUnknown = 0,
            RendezvousModeFast,
            RendezvousModeSymmConn,
            RendezvousModeSymmAcc
        };

        enum ConnStatus
        {
            ConnStatusNone = 0,
            ConnStatusPending,
            ConnStatusEstablished
        };

        struct RouteEntry
        {
            RouteEntry()
            : ip(0)
            , mask(0) {}

            DTun::UInt32 ip;
            DTun::UInt32 mask;
            boost::optional<DTun::UInt32> nodeId;
        };

        struct ConnState
        {
            ConnState()
            : remoteIp(0)
            , remotePort(0)
            , dstNodeIp(0)
            , mode(RendezvousModeUnknown)
            , status(ConnStatusNone)
            , triedFastOnly(false) {}

            DTun::ConnId connId;
            DTun::UInt32 remoteIp;
            DTun::UInt16 remotePort;
            DTun::UInt32 dstNodeIp;
            RegisterConnectionCallback callback;
            RendezvousMode mode;
            ConnStatus status;
            bool triedFastOnly;
            boost::shared_ptr<RendezvousSession> rSess;
            HandleKeepalive keepalive;
            boost::shared_ptr<ProxySession> proxySession;
        };

        typedef std::vector<RouteEntry> Routes;
        typedef std::map<DTun::ConnId, ConnState> ConnStateMap;
        typedef std::list<DTun::ConnId> ConnIdList;

        void onProbeConnect(int err);
        void onProbeSend(int err);
        void onProbeRecv(int err, int numBytes);
        void onConnect(int err);
        void onHelloSend(int err);
        void onSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff);
        void onRecvHeader(int err, int numBytes);
        void onRecvMsgConn(int err, int numBytes);
        void onRecvMsgConnStatus(int err, int numBytes);
        void onRecvMsgOther(int err, int numBytes, DTun::UInt8 msgId);
        void onProxyDone(const DTun::ConnId& connId);
        void onRendezvous(const DTun::ConnId& connId, int err, SYSSOCKET s, DTun::UInt32 remoteIp, DTun::UInt16 remotePort);

        void sendMsg(DTun::UInt8 msgCode, const void* msg, int msgSize);

        bool processRendezvous(boost::mutex::scoped_lock& lock);

        DTun::SManager& remoteMgr_;
        DTun::SManager& localMgr_;
        std::string address_;
        int port_;
        std::string probeAddress_;
        int probePort_;
        DTun::UInt32 nodeId_;
        Routes routes_;

        boost::mutex m_;
        bool closing_;
        DTun::UInt32 probedIp_;
        DTun::UInt16 probedPort_;
        DTun::UInt32 nextConnIdx_;
        std::vector<char> buff_;
        ConnIdList rendezvousConnIds_;
        ConnStateMap connStates_;
        boost::shared_ptr<DTun::SConnection> conn_;
        boost::shared_ptr<DTun::SConnector> connector_;
    };

    extern DMasterClient* theMasterClient;
}

#endif

#ifndef _DMASTERCLIENT_H_
#define _DMASTERCLIENT_H_

#include "DTun/Types.h"
#include "DTun/DProtocol.h"
#include "DTun/SManager.h"
#include "DTun/AppConfig.h"
#include "DMasterSession.h"
#include "ProxySession.h"
#include <boost/optional.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/weak_ptr.hpp>
#include <set>

namespace DNode
{
    class DMasterClient : boost::noncopyable
    {
    public:
        typedef boost::function<void (int, DTun::UInt32, DTun::UInt16)> RegisterConnectionCallback;

        DMasterClient(DTun::SManager& remoteMgr, DTun::SManager& localMgr,
            const boost::shared_ptr<DTun::AppConfig>& appConfig);
        ~DMasterClient();

        bool start();

        void changeNumOutConnections(int diff);

        // 's' will be closed even in case of failure!
        DTun::UInt32 registerConnection(SYSSOCKET s,
            DTun::UInt32 remoteIp,
            DTun::UInt16 remotePort,
            const RegisterConnectionCallback& callback);

        void cancelConnection(DTun::UInt32 connId);

        void dump();

        bool getDstNodeId(DTun::UInt32 remoteIp, DTun::UInt32& dstNodeId) const;

    private:
        struct RouteEntry
        {
            RouteEntry()
            : ip(0)
            , mask(0) {}

            DTun::UInt32 ip;
            DTun::UInt32 mask;
            boost::optional<DTun::UInt32> nodeId;
        };

        struct ConnMasterSession
        {
            boost::shared_ptr<DMasterSession> sess;
            RegisterConnectionCallback callback;
        };

        struct SysSocketHolder : boost::noncopyable
        {
            SysSocketHolder();
            explicit SysSocketHolder(SYSSOCKET sock);
            ~SysSocketHolder();

            SYSSOCKET sock;
        };

        typedef std::vector<RouteEntry> Routes;
        typedef std::map<DTun::UInt32, ConnMasterSession> ConnMasterSessionMap;
        typedef std::map<boost::shared_ptr<DMasterSession>, boost::shared_ptr<SysSocketHolder> > AccMasterSessions;
        typedef std::set<boost::shared_ptr<ProxySession> > ProxySessions;

        void onConnect(int err);
        void onSend(int err);
        void onRecvHeader(int err, int numBytes);
        void onRecvMsgConn(int err, int numBytes);
        void onRecvMsgConnOK(int err, int numBytes);
        void onRecvMsgConnErr(int err, int numBytes);
        void onRegisterConnection(int err, DTun::UInt32 connId);
        void onAcceptConnection(int err, const boost::weak_ptr<DMasterSession>& sess,
            DTun::UInt32 localIp,
            DTun::UInt16 localPort,
            DTun::UInt32 remoteIp,
            DTun::UInt16 remotePort);
        void onProxyDone(const boost::weak_ptr<ProxySession>& sess);

        void startAccMasterSession(DTun::UInt32 srcNodeId,
            DTun::UInt32 srcConnId,
            DTun::UInt32 localIp,
            DTun::UInt16 localPort,
            DTun::UInt32 remoteIp,
            DTun::UInt16 remotePort);

        DTun::SManager& remoteMgr_;
        DTun::SManager& localMgr_;
        std::string address_;
        int port_;
        DTun::UInt32 nodeId_;
        Routes routes_;

        boost::mutex m_;
        bool closing_;
        DTun::UInt32 nextConnId_;
        int numOutConnections_;
        std::vector<char> buff_;
        ConnMasterSessionMap connMasterSessions_;
        AccMasterSessions accMasterSessions_;
        ProxySessions proxySessions_;
        boost::shared_ptr<DTun::SConnection> conn_;
        boost::shared_ptr<DTun::SConnector> connector_;
    };

    extern DMasterClient* theMasterClient;
}

#endif

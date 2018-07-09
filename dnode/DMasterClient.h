#ifndef _DMASTERCLIENT_H_
#define _DMASTERCLIENT_H_

#include "DTun/Types.h"
#include "DTun/DProtocol.h"
#include "DTun/UDTReactor.h"
#include "DTun/UDTConnector.h"
#include "DTun/UDTConnection.h"
#include "DTun/TCPReactor.h"
#include "DMasterSession.h"
#include "ProxySession.h"

namespace DNode
{
    class DMasterClient : boost::noncopyable
    {
    public:
        typedef boost::function<void (int, DTun::UInt32, DTun::UInt16)> RegisterConnectionCallback;

        DMasterClient(DTun::UDTReactor& udtReactor, DTun::TCPReactor& tcpReactor, const std::string& address, int port, DTun::UInt32 nodeId);
        ~DMasterClient();

        bool start();

        // 's' will be closed even in case of failure!
        DTun::UInt32 registerConnection(SYSSOCKET s,
            DTun::UInt32 remoteIp,
            DTun::UInt16 remotePort,
            const RegisterConnectionCallback& callback);

        void cancelConnection(DTun::UInt32 connId);

    private:
        struct ConnMasterSession
        {
            boost::shared_ptr<DMasterSession> sess;
            RegisterConnectionCallback callback;
        };

        typedef std::map<DTun::UInt32, ConnMasterSession> ConnMasterSessionMap;
        typedef std::set<boost::shared_ptr<DMasterSession> > AccMasterSessions;
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

        DTun::UDTReactor& udtReactor_;
        DTun::TCPReactor& tcpReactor_;
        std::string address_;
        int port_;
        DTun::UInt32 nodeId_;

        boost::mutex m_;
        DTun::UInt32 nextConnId_;
        std::vector<char> buff_;
        ConnMasterSessionMap connMasterSessions_;
        AccMasterSessions accMasterSessions_;
        ProxySessions proxySessions_;
        boost::shared_ptr<DTun::UDTConnection> conn_;
        boost::shared_ptr<DTun::UDTConnector> connector_;
    };

    extern DMasterClient* theMasterClient;
}

#endif

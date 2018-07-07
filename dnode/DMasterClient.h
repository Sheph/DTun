#ifndef _DMASTERCLIENT_H_
#define _DMASTERCLIENT_H_

#include "DTun/Types.h"
#include "DTun/DProtocol.h"
#include "DTun/UDTReactor.h"
#include "DTun/UDTConnector.h"
#include "DTun/UDTConnection.h"
#include "DMasterSession.h"

namespace DNode
{
    class DMasterClient : boost::noncopyable
    {
    public:
        typedef boost::function<void (int, DTun::UInt32, DTun::UInt16)> RegisterConnectionCallback;

        DMasterClient(DTun::UDTReactor& reactor, const std::string& address, int port, DTun::UInt32 nodeId);
        ~DMasterClient();

        bool start();

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

        typedef std::map<DTun::UInt32, ConnMasterSession> MasterSessionMap;

        void onConnect(int err);
        void onSend(int err);
        void onRecvHeader(int err, int numBytes);
        void onRecvMsg(int err, int numBytes);
        void onRegisterConnection(int err, DTun::UInt32 connId);

        DTun::UDTReactor& reactor_;
        std::string address_;
        int port_;
        DTun::UInt32 nodeId_;

        boost::mutex m_;
        DTun::UInt32 nextConnId_;
        std::vector<char> buff_;
        MasterSessionMap connMasterSessions_;
        boost::shared_ptr<DTun::UDTConnection> conn_;
        boost::shared_ptr<DTun::UDTConnector> connector_;
    };

    extern DMasterClient* theMasterClient;
}

#endif

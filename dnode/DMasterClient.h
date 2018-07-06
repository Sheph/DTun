#ifndef _DMASTERCLIENT_H_
#define _DMASTERCLIENT_H_

#include "DTun/Types.h"
#include "DTun/DProtocol.h"
#include "DTun/UDTReactor.h"
#include "DTun/UDTConnector.h"
#include "DTun/UDTConnection.h"

namespace DNode
{
    class DMasterClient : boost::noncopyable
    {
    public:
        DMasterClient(DTun::UDTReactor& reactor, const std::string& address, int port, DTun::UInt32 nodeId);
        ~DMasterClient();

        bool start();

    private:
        void onConnect(int err);
        void onSend(int err);
        void onRecvHeader(int err, int numBytes);
        void onRecvMsg(int err, int numBytes);

        DTun::UDTReactor& reactor_;
        std::string address_;
        int port_;
        DTun::UInt32 nodeId_;
        std::vector<char> buff_;
        boost::shared_ptr<DTun::UDTConnection> conn_;
        boost::shared_ptr<DTun::UDTConnector> connector_;
    };
}

#endif

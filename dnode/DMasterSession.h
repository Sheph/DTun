#ifndef _DMASTERSESSION_H_
#define _DMASTERSESSION_H_

#include "DTun/DProtocol.h"
#include "DTun/UDTConnection.h"
#include "DTun/UDTConnector.h"
#include <boost/noncopyable.hpp>
#include <map>

namespace DNode
{
    class DMasterSession : boost::noncopyable
    {
    public:
        typedef boost::function<void (int)> Callback;

        DMasterSession(DTun::UDTReactor& reactor, const std::string& address, int port);
        ~DMasterSession();

        bool startConnector(SYSSOCKET s, DTun::UInt32 srcNodeId,
            DTun::UInt32 dstNodeId,
            DTun::UInt32 connId,
            DTun::UInt32 remoteIp,
            DTun::UInt16 remotePort,
            const Callback& callback);
        bool startAcceptor(SYSSOCKET s, DTun::UInt32 srcNodeId,
            DTun::UInt32 connId,
            const Callback& callback);

        inline const boost::shared_ptr<DTun::UDTConnection>& conn() const { return conn_; }

    private:
        bool start(SYSSOCKET s, const Callback& callback);

        void onConnect(int err);
        void onSend(int err);

        DTun::UDTReactor& reactor_;
        std::string address_;
        int port_;
        std::vector<char> buff_;
        Callback callback_;
        boost::shared_ptr<DTun::UDTConnection> conn_;
        boost::shared_ptr<DTun::UDTConnector> connector_;
    };
}

#endif

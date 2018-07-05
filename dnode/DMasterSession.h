#ifndef _DMASTERSESSION_H_
#define _DMASTERSESSION_H_

#include "DTun/DProtocol.h"
#include <boost/noncopyable.hpp>
#include <map>
#include "udt.h"

namespace DNode
{
    class DMasterSession : boost::noncopyable
    {
    public:
        enum Type
        {
            TypeConnector = 0,
            TypeAcceptor
        };

        DMasterSession(SYSSOCKET s, Type type);
        ~DMasterSession();

        inline DTun::UInt32 nodeId() const { return nodeId_; }

        void start();

        void registerConnRequest(DTun::UInt32 connId, DTun::UInt32 dstNodeId);

        void setConnRequestErr(DTun::UInt32 connId,
            DTun::UInt32 errCode);

        void setAllConnRequestsErr(DTun::UInt32 dstNodeId,
            DTun::UInt32 errCode);

        void setConnRequestOk(DTun::UInt32 connId,
            DTun::UInt32 dstNodeIp,
            DTun::UInt16 dstNodePort);

        void sendConnRequest(DTun::UInt32 srcNodeId,
            DTun::UInt32 srcNodeIp,
            DTun::UInt16 srcNodePort,
            DTun::UInt32 connId,
            DTun::UInt32 ip,
            DTun::UInt16 port);

    private:
        typedef std::map<DTun::UInt32, DTun::UInt32> ConnRequestMap;

        void onSend(int err);
        void onRecv(int err, int numBytes);

        void onMsg(const DTun::DProtocolMsgHello& msg);
        void onMsg(const DTun::DProtocolMsgHelloConn& msg);
        void onMsg(const DTun::DProtocolMsgHelloAcc& msg);

        boost::shared_ptr<SessionListener> listener_;

        boost::shared_ptr<DTun::DConnection> conn_;
        Type type_;
        DTun::UInt32 nodeId_;

        ConnRequestMap connRequests_;
    };
}

#endif
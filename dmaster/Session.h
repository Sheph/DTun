#ifndef _SESSION_H_
#define _SESSION_H_

#include "DTun/DProtocol.h"
#include "DTun/UDTConnection.h"
#include <boost/noncopyable.hpp>
#include <map>

namespace DMaster
{
    class Session : boost::noncopyable
    {
    public:
        enum Type
        {
            TypeUnknown = 0,
            TypePersistent,
            TypeConnector,
            TypeAcceptor
        };

        typedef boost::function<void ()> StartPersistentCallback;
        typedef boost::function<void (DTun::UInt32, DTun::UInt32, DTun::UInt32, DTun::UInt16)> StartConnectorCallback;
        typedef boost::function<void (DTun::UInt32)> StartAcceptorCallback;
        typedef boost::function<void (int)> ErrorCallback;

        explicit Session(const boost::shared_ptr<DTun::UDTConnection>& conn);
        ~Session();

        inline void setStartPersistentCallback(const StartPersistentCallback& cb) { startPersistentCallback_ = cb; }
        inline void setStartConnectorCallback(const StartConnectorCallback& cb) { startConnectorCallback_ = cb; }
        inline void setStartAcceptorCallback(const StartAcceptorCallback& cb) { startAcceptorCallback_ = cb; }
        inline void setErrorCallback(const ErrorCallback& cb) { errorCallback_ = cb; }

        inline const boost::shared_ptr<DTun::UDTConnection>& conn() const { return conn_; }

        inline Type type() const { return type_; }

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

        void onSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff);
        void onRecvHeader(int err, int numBytes);
        void onRecvMsgHello(int err, int numBytes);
        void onRecvMsgHelloConn(int err, int numBytes);
        void onRecvMsgHelloAcc(int err, int numBytes);
        void onRecvAny(int err, int numBytes);

        void startRecvAny();

        void sendMsg(DTun::UInt8 msgCode, const void* msg, int msgSize);

        StartPersistentCallback startPersistentCallback_;
        StartConnectorCallback startConnectorCallback_;
        StartAcceptorCallback startAcceptorCallback_;
        ErrorCallback errorCallback_;

        Type type_;
        DTun::UInt32 nodeId_;

        ConnRequestMap connRequests_;
        std::vector<char> buff_;

        boost::shared_ptr<DTun::UDTConnection> conn_;
    };
}

#endif

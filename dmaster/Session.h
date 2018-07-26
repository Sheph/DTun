#ifndef _SESSION_H_
#define _SESSION_H_

#include "DTun/DProtocol.h"
#include "DTun/SConnection.h"
#include <boost/noncopyable.hpp>
#include <boost/optional.hpp>
#include <map>
#include <vector>

namespace DMaster
{
    class Session : boost::noncopyable
    {
    public:
        enum Type
        {
            TypeUnknown = 0,
            TypePersistent,
            TypeProbe,
            TypeFast,
            TypeSymm
        };

        typedef boost::function<void ()> StartPersistentCallback;
        typedef boost::function<void (const DTun::ConnId&)> StartFastCallback;
        typedef boost::function<void (const DTun::ConnId&)> StartSymmCallback;
        typedef boost::function<void (DTun::UInt8, const void*)> MessageCallback;
        typedef boost::function<void (int)> ErrorCallback;

        explicit Session(const boost::shared_ptr<DTun::SConnection>& conn);
        ~Session();

        inline void setStartPersistentCallback(const StartPersistentCallback& cb) { startPersistentCallback_ = cb; }
        inline void setStartFastCallback(const StartFastCallback& cb) { startFastCallback_ = cb; }
        inline void setStartSymmCallback(const StartSymmCallback& cb) { startSymmCallback_ = cb; }
        inline void setMessageCallback(const MessageCallback& cb) { messageCallback_ = cb; }
        inline void setErrorCallback(const ErrorCallback& cb) { errorCallback_ = cb; }

        inline Type type() const { return type_; }

        inline DTun::UInt32 nodeId() const { return nodeId_; }

        inline bool isSymm() const { return symm_; }

        inline DTun::UInt32 peerIp() const { return peerIp_; }
        inline DTun::UInt16 peerPort() const { return peerPort_; }

        void start();

        void sendConnRequest(const DTun::ConnId& connId,
            DTun::UInt32 ip,
            DTun::UInt16 port,
            DTun::UInt8 mode,
            DTun::UInt32 srcIp);

        void sendConnStatus(const DTun::ConnId& connId,
            DTun::UInt8 statusCode,
            DTun::UInt8 mode = DPROTOCOL_RMODE_FAST,
            DTun::UInt32 dstIp = 0);

        void sendFast(const DTun::ConnId& connId,
            DTun::UInt32 nodeIp,
            DTun::UInt16 nodePort);

        void sendSymm(const DTun::ConnId& connId,
            DTun::UInt32 nodeIp,
            DTun::UInt16 nodePort);

        void sendSymmNext(const DTun::ConnId& connId);

    private:
        void onSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff);
        void onRecvHeader(int err, int numBytes);
        void onRecvMsgHello(int err, int numBytes);
        void onRecvMsgHelloProbe();
        void onRecvMsgHelloFast(int err, int numBytes);
        void onRecvMsgHelloSymm(int err, int numBytes);
        void onRecvMsgOther(int err, int numBytes, DTun::UInt8 msgCode);

        void startRecvHeader();

        void sendMsg(DTun::UInt8 msgCode, const void* msg, int msgSize);

        StartPersistentCallback startPersistentCallback_;
        StartFastCallback startFastCallback_;
        StartSymmCallback startSymmCallback_;
        MessageCallback messageCallback_;
        ErrorCallback errorCallback_;

        Type type_;
        DTun::UInt32 nodeId_;
        bool symm_;

        std::vector<char> buff_;

        DTun::UInt32 peerIp_;
        DTun::UInt16 peerPort_;

        boost::shared_ptr<DTun::SConnection> conn_;
    };
}

#endif

#ifndef _SERVER_H_
#define _SERVER_H_

#include "Session.h"
#include "DTun/SManager.h"
#include "DTun/SAcceptor.h"
#include <boost/noncopyable.hpp>
#include <boost/weak_ptr.hpp>
#include <set>

namespace DMaster
{
    class Server : boost::noncopyable
    {
    public:
        explicit Server(DTun::SManager& mgr, int port);
        ~Server();

        bool start();

        void run();

        void stop();

    private:
        struct Conn
        {
            Conn()
            : srcMode(DPROTOCOL_RMODE_FAST)
            , dstMode(DPROTOCOL_RMODE_FAST) {}

            Conn(const boost::shared_ptr<Session>& srcSess,
                const boost::shared_ptr<Session>& dstSess,
                DTun::UInt8 srcMode,
                DTun::UInt8 dstMode)
            : srcSess(srcSess)
            , dstSess(dstSess)
            , srcMode(srcMode)
            , dstMode(dstMode) {}

            boost::shared_ptr<Session> srcSess;
            boost::shared_ptr<Session> dstSess;
            DTun::UInt8 srcMode;
            DTun::UInt8 dstMode;
        };

        typedef std::map<DTun::ConnId, Conn> ConnMap;
        typedef std::set<boost::shared_ptr<Session> > Sessions;

        void onAccept(const boost::shared_ptr<DTun::SHandle>& handle);

        void onSessionStartPersistent(const boost::weak_ptr<Session>& sess);

        void onSessionStartFast(const boost::weak_ptr<Session>& sess, const DTun::ConnId& connId);

        void onSessionStartSymm(const boost::weak_ptr<Session>& sess, const DTun::ConnId& connId);

        void onSessionMessage(const boost::weak_ptr<Session>& sess, DTun::UInt8 msgCode, const void* msg);

        void onSessionError(const boost::weak_ptr<Session>& sess, int errCode);

        void onSessionHelloFast(const boost::shared_ptr<Session>& sess, const DTun::ConnId& connId);

        void onSessionHelloSymm(const boost::shared_ptr<Session>& sess, const DTun::ConnId& connId);

        void onSessionConnCreate(const boost::shared_ptr<Session>& sess, const DTun::ConnId& connId,
            DTun::UInt32 dstNodeId,
            DTun::UInt32 remoteIp,
            DTun::UInt16 remotePort,
            bool bestEffort);

        void onSessionConnClose(const boost::shared_ptr<Session>& sess, const DTun::ConnId& connId, bool established);

        void onSessionReady(const boost::shared_ptr<Session>& sess, const DTun::ConnId& connId);

        void onSessionSymmNext(const boost::shared_ptr<Session>& sess, const DTun::ConnId& connId);

        boost::shared_ptr<Session> findPersistentSession(DTun::UInt32 nodeId) const;

        void removeSession(const boost::shared_ptr<Session>& sess);

        int port_;
        DTun::SManager& mgr_;
        ConnMap conns_;
        Sessions sessions_;
        boost::shared_ptr<DTun::SAcceptor> acceptor_;
    };
}

#endif

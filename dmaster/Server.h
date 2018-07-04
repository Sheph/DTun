#ifndef _SERVER_H_
#define _SERVER_H_

#include "Session.h"
#include <boost/noncopyable.hpp>
#include <boost/weak_ptr.hpp>

namespace DMaster
{
    class Server;

    class ServerSessionListener : public SessionListener
    {
    public:
        ServerSessionListener(Server* server, const boost::shared_ptr<Session>& sess);
        ~ServerSessionListener();

        virtual void onStartPersistent();

        virtual void onStartConnector(DTun::UInt32 dstNodeId,
            DTun::UInt32 connId,
            DTun::UInt32 remoteIp,
            DTun::UInt16 remotePort);

        virtual void onStartAcceptor(DTun::UInt32 connId);

        virtual void onError(int errCode);

    private:
        Server* server_;
        boost::weak_ptr<Session> sess_;
    };

    class Server : boost::noncopyable
    {
    public:
        explicit Server(int port);
        ~Server();

        bool start();

        void run();

        void stop();

    private:
        friend class ServerSessionListener;

        void onSessionStartPersistent(const boost::shared_ptr<Session>& sess);

        void onSessionStartConnector(const boost::shared_ptr<Session>& sess,
            DTun::UInt32 dstNodeId,
            DTun::UInt32 connId,
            DTun::UInt32 remoteIp,
            DTun::UInt16 remotePort);

        void onSessionStartAcceptor(const boost::shared_ptr<Session>& sess, DTun::UInt32 connId);

        void onSessionError(const boost::shared_ptr<Session>& sess, int errCode);

        int port_;
        int eid_;
        bool stopping_;
        UDTSOCKET serverSocket_;
        std::set<boost::shared_ptr<Session> > sessions_;
    };
}

#endif

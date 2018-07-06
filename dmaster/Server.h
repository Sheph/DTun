#ifndef _SERVER_H_
#define _SERVER_H_

#include "Session.h"
#include "DTun/UDTReactor.h"
#include "DTun/UDTAcceptor.h"
#include <boost/noncopyable.hpp>
#include <boost/weak_ptr.hpp>

namespace DMaster
{
    class Server : boost::noncopyable
    {
    public:
        explicit Server(int port);
        ~Server();

        bool start();

        void run();

        void stop();

    private:
        void onAccept(UDTSOCKET sock);

        void onSessionStartPersistent(const boost::weak_ptr<Session>& sess);

        void onSessionStartConnector(const boost::weak_ptr<Session>& sess,
            DTun::UInt32 dstNodeId,
            DTun::UInt32 connId,
            DTun::UInt32 remoteIp,
            DTun::UInt16 remotePort);

        void onSessionStartAcceptor(const boost::weak_ptr<Session>& sess, DTun::UInt32 connId);

        void onSessionError(const boost::weak_ptr<Session>& sess, int errCode);

        int port_;
        DTun::UDTReactor reactor_;
        boost::shared_ptr<DTun::UDTAcceptor> acceptor_;
        std::set<boost::shared_ptr<Session> > sessions_;
    };
}

#endif

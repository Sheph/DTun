#ifndef _SERVER_H_
#define _SERVER_H_

#include "Session.h"
#include "DTun/UDTReactor.h"
#include "DTun/UDTManager.h"
#include "DTun/SAcceptor.h"
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
        typedef std::set<boost::shared_ptr<Session> > Sessions;

        void onAccept(const boost::shared_ptr<DTun::SHandle>& handle);

        void onSessionStartPersistent(const boost::weak_ptr<Session>& sess);

        void onSessionStartConnector(const boost::weak_ptr<Session>& sess,
            DTun::UInt32 dstNodeId,
            DTun::UInt32 connId,
            DTun::UInt32 remoteIp,
            DTun::UInt16 remotePort);

        void onSessionStartAcceptor(const boost::weak_ptr<Session>& sess, DTun::UInt32 srcNodeId, DTun::UInt32 connId);

        void onSessionError(const boost::weak_ptr<Session>& sess, int errCode);

        boost::shared_ptr<Session> findPersistentSession(DTun::UInt32 nodeId) const;

        void removeSession(const boost::shared_ptr<Session>& sess);

        int port_;
        DTun::UDTReactor reactor_;
        DTun::UDTManager mgr_;
        Sessions sessions_;
        boost::shared_ptr<DTun::SAcceptor> acceptor_;
    };
}

#endif

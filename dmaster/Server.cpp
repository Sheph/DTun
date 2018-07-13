#include "Server.h"
#include "Logger.h"
#include "DTun/Utils.h"
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>
#include <sstream>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <netdb.h>

namespace DMaster
{
    Server::Server(DTun::SManager& mgr, int port)
    : port_(port)
    , mgr_(mgr)
    {
    }

    Server::~Server()
    {
    }

    bool Server::start()
    {
        if (!mgr_.reactor().start()) {
            return false;
        }

        addrinfo hints;

        memset(&hints, 0, sizeof(struct addrinfo));

        hints.ai_flags = AI_PASSIVE;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        std::ostringstream os;
        os << port_;

        addrinfo* res;

        if (::getaddrinfo(NULL, os.str().c_str(), &hints, &res) != 0) {
            LOG4CPLUS_ERROR(logger(), "Illegal port number or port is busy");
            return false;
        }

        boost::shared_ptr<DTun::SHandle> serverHandle = mgr_.createStreamSocket();
        if (!serverHandle) {
            freeaddrinfo(res);
            LOG4CPLUS_INFO(logger(), "Server is NOT ready at port " << port_);
            return true;
        }

        if (!serverHandle->bind(res->ai_addr, res->ai_addrlen)) {
            freeaddrinfo(res);
            return false;
        }

        freeaddrinfo(res);

        acceptor_ = serverHandle->createAcceptor();

        if (!acceptor_->listen(10, boost::bind(&Server::onAccept, this, _1))) {
           acceptor_.reset();
           return false;
        }

        LOG4CPLUS_INFO(logger(), "Server is ready at port " << port_);

        return true;
    }

    void Server::run()
    {
        mgr_.reactor().run();
        acceptor_.reset();
        sessions_.clear();
        mgr_.reactor().processUpdates();
    }

    void Server::stop()
    {
        mgr_.reactor().stop();
    }

    void Server::onAccept(const boost::shared_ptr<DTun::SHandle>& handle)
    {
        LOG4CPLUS_TRACE(logger(), "Server::onAccept(" << handle << ")");

        boost::shared_ptr<Session> session = boost::make_shared<Session>(handle->createConnection());

        sessions_.insert(session);

        session->setStartPersistentCallback(boost::bind(&Server::onSessionStartPersistent, this, boost::weak_ptr<Session>(session)));
        session->setStartConnectorCallback(boost::bind(&Server::onSessionStartConnector, this, boost::weak_ptr<Session>(session), _1, _2, _3, _4));
        session->setStartAcceptorCallback(boost::bind(&Server::onSessionStartAcceptor, this, boost::weak_ptr<Session>(session), _1, _2));
        session->setErrorCallback(boost::bind(&Server::onSessionError, this, boost::weak_ptr<Session>(session), _1));

        session->start();
    }

    void Server::onSessionStartPersistent(const boost::weak_ptr<Session>& sess)
    {
        boost::shared_ptr<Session> sess_shared = sess.lock();
        if (!sess_shared) {
            return;
        }

        for (Sessions::const_iterator it = sessions_.begin(); it != sessions_.end(); ++it) {
            boost::shared_ptr<Session> other = *it;
            if ((other->nodeId() == sess_shared->nodeId()) &&
                (other->type() == Session::TypePersistent) &&
                (other != sess_shared)) {
                LOG4CPLUS_WARN(logger(), "new persistent node " << sess_shared->nodeId() << ", overriding");
                removeSession(other);
                break;
            }
        }

        LOG4CPLUS_INFO(logger(), "client " << DTun::ipPortToString(sess_shared->peerIp(), sess_shared->peerPort())
            << ", nodeId = " << sess_shared->nodeId() << " connected");
    }

    void Server::onSessionStartConnector(const boost::weak_ptr<Session>& sess,
        DTun::UInt32 dstNodeId,
        DTun::UInt32 connId,
        DTun::UInt32 remoteIp,
        DTun::UInt16 remotePort)
    {
        boost::shared_ptr<Session> sess_shared = sess.lock();
        if (!sess_shared) {
            return;
        }

        LOG4CPLUS_TRACE(logger(), "Server::onSessionStartConnector(" << sess_shared->nodeId() << ", "
            << dstNodeId << ", " << connId << ", " << DTun::ipPortToString(remoteIp, remotePort) << ")");

        boost::shared_ptr<Session> srcSess = findPersistentSession(sess_shared->nodeId());

        if (!srcSess) {
            LOG4CPLUS_ERROR(logger(), "persistent source session not found for connector srcNodeId = "
                << sess_shared->nodeId() << ", dstNodeId = " << dstNodeId << ", addr = " << DTun::ipPortToString(remoteIp, remotePort));
            return;
        }

        srcSess->registerConnRequest(connId, dstNodeId);

        boost::shared_ptr<Session> dstSess = findPersistentSession(dstNodeId);

        if (!dstSess) {
            LOG4CPLUS_ERROR(logger(), "persistent dest session not found for connector srcNodeId = "
                << sess_shared->nodeId() << ", dstNodeId = " << dstNodeId << ", addr = " << DTun::ipPortToString(remoteIp, remotePort));
            srcSess->setConnRequestErr(connId, DPROTOCOL_ERR_NOTFOUND);
            return;
        }

        if (sess_shared->peerIp() == 0) {
            LOG4CPLUS_ERROR(logger(), "no src peer address, cannot proceed with connector");
            srcSess->setConnRequestErr(connId, DPROTOCOL_ERR_UNKNOWN);
            return;
        }

        dstSess->sendConnRequest(sess_shared->nodeId(), sess_shared->peerIp(), sess_shared->peerPort(),
            connId, remoteIp, remotePort);
    }

    void Server::onSessionStartAcceptor(const boost::weak_ptr<Session>& sess, DTun::UInt32 srcNodeId, DTun::UInt32 connId)
    {
        boost::shared_ptr<Session> sess_shared = sess.lock();
        if (!sess_shared) {
            return;
        }

        LOG4CPLUS_TRACE(logger(), "Server::onSessionStartAcceptor(" << sess_shared->nodeId() << ", " << srcNodeId << ", " << connId << ")");

        boost::shared_ptr<Session> srcSess = findPersistentSession(srcNodeId);

        if (!srcSess) {
            LOG4CPLUS_ERROR(logger(), "persistent source session not found for acceptor srcNodeId = "
                << srcNodeId << ", dstNodeId = " << sess_shared->nodeId());
            return;
        }

        if (sess_shared->peerIp() == 0) {
            LOG4CPLUS_ERROR(logger(), "no dst peer address, cannot proceed with acceptor");
            srcSess->setConnRequestErr(connId, DPROTOCOL_ERR_UNKNOWN);
            return;
        }

        srcSess->setConnRequestOk(connId, sess_shared->peerIp(), sess_shared->peerPort());
    }

    void Server::onSessionError(const boost::weak_ptr<Session>& sess, int errCode)
    {
        boost::shared_ptr<Session> sess_shared = sess.lock();
        if (!sess_shared) {
            return;
        }

        LOG4CPLUS_TRACE(logger(), "Server::onSessionError(" << sess_shared->nodeId() << ", " << errCode << ")");

        if (sess_shared->type() == Session::TypePersistent) {
            LOG4CPLUS_INFO(logger(), "client " << DTun::ipPortToString(sess_shared->peerIp(), sess_shared->peerPort())
                << ", nodeId = " << sess_shared->nodeId() << " disconnected");
        }

        removeSession(sess_shared);
    }

    boost::shared_ptr<Session> Server::findPersistentSession(DTun::UInt32 nodeId) const
    {
        for (Sessions::const_iterator it = sessions_.begin(); it != sessions_.end(); ++it) {
            boost::shared_ptr<Session> other = *it;
            if ((other->nodeId() == nodeId) &&
                (other->type() == Session::TypePersistent)) {
                return other;
            }
        }
        return boost::shared_ptr<Session>();
    }

    void Server::removeSession(const boost::shared_ptr<Session>& sess)
    {
        if (sess->type() == Session::TypePersistent) {
            for (Sessions::const_iterator it = sessions_.begin(); it != sessions_.end(); ++it) {
                boost::shared_ptr<Session> other = *it;
                if (other->type() == Session::TypePersistent) {
                    other->setAllConnRequestsErr(sess->nodeId(), DPROTOCOL_ERR_UNKNOWN);
                }
            }
        }

        sessions_.erase(sess);
    }
}

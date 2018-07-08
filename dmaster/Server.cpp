#include "Server.h"
#include "Logger.h"
#include "DTun/Utils.h"
#include <boost/make_shared.hpp>
#include <sstream>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <netdb.h>

namespace DMaster
{
    Server::Server(int port)
    : port_(port)
    {
    }

    Server::~Server()
    {
    }

    bool Server::start()
    {
        if (!reactor_.start()) {
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

        UDTSOCKET serverSocket = UDT::socket(res->ai_family, res->ai_socktype, res->ai_protocol);

        if (serverSocket == UDT::INVALID_SOCK) {
            LOG4CPLUS_ERROR(logger(), "Cannot create UDT socket: " << UDT::getlasterror().getErrorMessage());
            freeaddrinfo(res);
            return false;
        }

        if (UDT::bind(serverSocket, res->ai_addr, res->ai_addrlen) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot bind UDT socket: " << UDT::getlasterror().getErrorMessage());
            freeaddrinfo(res);
            UDT::close(serverSocket);
            return false;
        }

        freeaddrinfo(res);

        acceptor_ = boost::make_shared<DTun::UDTAcceptor>(boost::ref(reactor_), serverSocket);

        if (!acceptor_->listen(10, boost::bind(&Server::onAccept, this, _1))) {
           UDT::close(serverSocket);
           acceptor_.reset();
           return false;
        }

        LOG4CPLUS_INFO(logger(), "Server is ready at port " << port_);

        return true;
    }

    void Server::run()
    {
        reactor_.run();
        acceptor_.reset();
        sessions_.clear();
        reactor_.processUpdates();
    }

    void Server::stop()
    {
        reactor_.stop();
    }

    void Server::onAccept(UDTSOCKET sock)
    {
        LOG4CPLUS_INFO(logger(), "onAccept(" << sock << ")");

        boost::shared_ptr<Session> session = boost::make_shared<Session>(
            boost::make_shared<DTun::UDTConnection>(boost::ref(reactor_), sock));

        sessions_.insert(session);

        session->setStartPersistentCallback(boost::bind(&Server::onSessionStartPersistent, this, boost::weak_ptr<Session>(session)));
        session->setStartConnectorCallback(boost::bind(&Server::onSessionStartConnector, this, boost::weak_ptr<Session>(session), _1, _2, _3, _4));
        session->setStartAcceptorCallback(boost::bind(&Server::onSessionStartAcceptor, this, boost::weak_ptr<Session>(session), _1));
        session->setErrorCallback(boost::bind(&Server::onSessionError, this, boost::weak_ptr<Session>(session), _1));

        session->start();
    }

    void Server::onSessionStartPersistent(const boost::weak_ptr<Session>& sess)
    {
        boost::shared_ptr<Session> sess_shared = sess.lock();
        if (!sess_shared) {
            return;
        }

        LOG4CPLUS_TRACE(logger(), "onSessionStartPersistent(" << sess_shared->nodeId() << ")");
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

        LOG4CPLUS_TRACE(logger(), "onSessionStartConnector(" << sess_shared->nodeId() << ", "
            << dstNodeId << ", " << connId << ", " << DTun::ipPortToString(remoteIp, remotePort) << ")");

        boost::shared_ptr<Session> srcSess = findPersistentSession(sess_shared->nodeId());

        if (!srcSess) {
            LOG4CPLUS_ERROR(logger(), "persistent source session not found for connector");
            return;
        }

        srcSess->registerConnRequest(connId, dstNodeId);

        boost::shared_ptr<Session> dstSess = findPersistentSession(dstNodeId);

        if (!dstSess) {
            LOG4CPLUS_ERROR(logger(), "persistent dest session not found for connector");
            srcSess->setConnRequestErr(connId, DPROTOCOL_ERR_NOTFOUND);
            return;
        }

        DTun::UInt32 srcIp = 0;
        DTun::UInt16 srcPort = 0;

        if (!sess_shared->conn()->getPeerName(srcIp, srcPort)) {
            srcSess->setConnRequestErr(connId, DPROTOCOL_ERR_UNKNOWN);
            return;
        }

        dstSess->sendConnRequest(sess_shared->nodeId(), srcIp, srcPort, connId, remoteIp, remotePort);
    }

    void Server::onSessionStartAcceptor(const boost::weak_ptr<Session>& sess, DTun::UInt32 connId)
    {
        boost::shared_ptr<Session> sess_shared = sess.lock();
        if (!sess_shared) {
            return;
        }

        LOG4CPLUS_TRACE(logger(), "onSessionStartAcceptor(" << sess_shared->nodeId() << ")");
    }

    void Server::onSessionError(const boost::weak_ptr<Session>& sess, int errCode)
    {
        boost::shared_ptr<Session> sess_shared = sess.lock();
        if (!sess_shared) {
            return;
        }

        LOG4CPLUS_TRACE(logger(), "onSessionError(" << sess_shared->nodeId() << ", " << errCode << ")");

        sessions_.erase(sess_shared);
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
}

#include "Server.h"
#include "Logger.h"
#include <boost/make_shared.hpp>
#include <sstream>
#include <unistd.h>
#include <cstdlib>
#include <cstring>
#include <netdb.h>

namespace DMaster
{
    ServerSessionListener::ServerSessionListener(Server* server, const boost::shared_ptr<Session>& sess)
    : server_(server)
    , sess_(sess)
    {
    }

    ServerSessionListener::~ServerSessionListener()
    {
    }

    void ServerSessionListener::onStartPersistent()
    {
        server_->onSessionStartPersistent(sess_.lock());
    }

    void ServerSessionListener::onStartConnector(DTun::UInt32 dstNodeId,
        DTun::UInt32 connId,
        DTun::UInt32 remoteIp,
        DTun::UInt16 remotePort)
    {
        server_->onSessionStartConnector(sess_.lock(), dstNodeId, connId, remoteIp, remotePort);
    }

    void ServerSessionListener::onStartAcceptor(DTun::UInt32 connId)
    {
        server_->onSessionStartAcceptor(sess_.lock(), connId);
    }

    void ServerSessionListener::onError(int errCode)
    {
        server_->onSessionError(sess_.lock(), errCode);
    }

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
    }

    void Server::stop()
    {
        acceptor_.reset();
        reactor_.stop();
    }

    void Server::onAccept(UDTSOCKET sock)
    {
        LOG4CPLUS_INFO(logger(), "onAccept(" << sock << ")");
        sleep(3);
        UDT::close(sock);
    }

    void Server::onSessionStartPersistent(const boost::shared_ptr<Session>& sess)
    {
    }

    void Server::onSessionStartConnector(const boost::shared_ptr<Session>& sess,
        DTun::UInt32 dstNodeId,
        DTun::UInt32 connId,
        DTun::UInt32 remoteIp,
        DTun::UInt16 remotePort)
    {
    }

    void Server::onSessionStartAcceptor(const boost::shared_ptr<Session>& sess, DTun::UInt32 connId)
    {
    }

    void Server::onSessionError(const boost::shared_ptr<Session>& sess, int errCode)
    {
    }
}

#include "Server.h"
#include "Logger.h"
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
    , eid_(UDT::ERROR)
    , stopping_(false)
    , serverSocket_(UDT::INVALID_SOCK)
    {
    }

    Server::~Server()
    {
        if (serverSocket_ != UDT::INVALID_SOCK) {
            UDT::close(serverSocket_);
            UDT::epoll_release(eid_);
        }
    }

    bool Server::start()
    {
        eid_ = UDT::epoll_create();

        if (eid_ == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot create epoll: " << UDT::getlasterror().getErrorMessage());
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
            UDT::epoll_release(eid_);
            eid_ = UDT::ERROR;
            return false;
        }

        serverSocket_ = UDT::socket(res->ai_family, res->ai_socktype, res->ai_protocol);

        if (serverSocket_ == UDT::INVALID_SOCK) {
            LOG4CPLUS_ERROR(logger(), "Cannot create UDT socket: " << UDT::getlasterror().getErrorMessage());
            UDT::epoll_release(eid_);
            eid_ = UDT::ERROR;
            freeaddrinfo(res);
            return false;
        }

        if (UDT::bind(serverSocket_, res->ai_addr, res->ai_addrlen) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot bind UDT socket: " << UDT::getlasterror().getErrorMessage());
            UDT::epoll_release(eid_);
            eid_ = UDT::ERROR;
            freeaddrinfo(res);
            UDT::close(serverSocket_);
            serverSocket_ = UDT::INVALID_SOCK;
            return false;
        }

        freeaddrinfo(res);

        if (UDT::listen(serverSocket_, 10) == UDT::ERROR) {
           LOG4CPLUS_ERROR(logger(), "Cannot listen UDT socket: " << UDT::getlasterror().getErrorMessage());
           UDT::epoll_release(eid_);
           eid_ = UDT::ERROR;
           UDT::close(serverSocket_);
           serverSocket_ = UDT::INVALID_SOCK;
           return false;
        }

        LOG4CPLUS_INFO(logger(), "Server is ready at port " << port_);

        return true;
    }

    void Server::run()
    {
        if (serverSocket_ == UDT::INVALID_SOCK) {
            LOG4CPLUS_ERROR(logger(), "Server not started");
            return;
        }

        int events = UDT_EPOLL_IN;
        if (UDT::epoll_add_usock(eid_, serverSocket_, &events) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot add server sock to epoll: " << UDT::getlasterror().getErrorMessage());
            return;
        }

        std::set<UDTSOCKET> readfds, writefds;

        while (!stopping_) {
            readfds.clear();
            writefds.clear();
            if (UDT::epoll_wait(eid_, &readfds, &writefds, 1000, NULL, NULL) == UDT::ERROR) {
                if (UDT::getlasterror().getErrorCode() == CUDTException::ETIMEOUT) {
                    LOG4CPLUS_TRACE(logger(), "epoll timeout");
                    continue;
                }
                LOG4CPLUS_ERROR(logger(), "epoll_wait error: " << UDT::getlasterror().getErrorMessage());
                break;
            }
            for (std::set<UDTSOCKET>::const_iterator it = readfds.begin(); it != readfds.end(); ++it) {
                LOG4CPLUS_TRACE(logger(), "epoll rd: " << *it);
            }
            for (std::set<UDTSOCKET>::const_iterator it = writefds.begin(); it != writefds.end(); ++it) {
                LOG4CPLUS_TRACE(logger(), "epoll wr: " << *it);
            }
            if (readfds.empty() && writefds.empty()) {
                LOG4CPLUS_TRACE(logger(), "epoll empty run");
            }
        }
    }

    void Server::stop()
    {
        if (serverSocket_ == UDT::INVALID_SOCK) {
            LOG4CPLUS_ERROR(logger(), "Server not started");
            return;
        }

        stopping_ = true;
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

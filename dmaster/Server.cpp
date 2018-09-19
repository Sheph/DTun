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
            return false;
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
        conns_.clear();
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
        session->setStartFastCallback(boost::bind(&Server::onSessionStartFast, this, boost::weak_ptr<Session>(session), _1));
        session->setStartSymmCallback(boost::bind(&Server::onSessionStartSymm, this, boost::weak_ptr<Session>(session), _1));
        session->setMessageCallback(boost::bind(&Server::onSessionMessage, this, boost::weak_ptr<Session>(session), _1, _2));
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
            << ", nodeId = " << sess_shared->nodeId() << ", symm = " << sess_shared->isSymm() << " connected");
    }

    void Server::onSessionStartFast(const boost::weak_ptr<Session>& sess, const DTun::ConnId& connId)
    {
        boost::shared_ptr<Session> sess_shared = sess.lock();
        if (!sess_shared) {
            return;
        }

        onSessionHelloFast(sess_shared, connId);
    }

    void Server::onSessionStartSymm(const boost::weak_ptr<Session>& sess, const DTun::ConnId& connId)
    {
        boost::shared_ptr<Session> sess_shared = sess.lock();
        if (!sess_shared) {
            return;
        }

        onSessionHelloSymm(sess_shared, connId);
    }

    void Server::onSessionMessage(const boost::weak_ptr<Session>& sess, DTun::UInt8 msgCode, const void* msg)
    {
        boost::shared_ptr<Session> sess_shared = sess.lock();
        if (!sess_shared) {
            return;
        }

        switch (msgCode) {
        case DPROTOCOL_MSG_CONN_CREATE: {
            const DTun::DProtocolMsgConnCreate* msgConnCreate = (const DTun::DProtocolMsgConnCreate*)msg;
            onSessionConnCreate(sess_shared, DTun::fromProtocolConnId(msgConnCreate->connId), msgConnCreate->dstNodeId,
                msgConnCreate->remoteIp, msgConnCreate->remotePort, msgConnCreate->bestEffort);
            break;
        }
        case DPROTOCOL_MSG_CONN_CLOSE: {
            const DTun::DProtocolMsgConnClose* msgConnClose = (const DTun::DProtocolMsgConnClose*)msg;
            onSessionConnClose(sess_shared, DTun::fromProtocolConnId(msgConnClose->connId), msgConnClose->established);
            break;
        }
        case DPROTOCOL_MSG_READY: {
            const DTun::DProtocolMsgReady* msgReady = (const DTun::DProtocolMsgReady*)msg;
            onSessionReady(sess_shared, DTun::fromProtocolConnId(msgReady->connId));
            break;
        }
        case DPROTOCOL_MSG_SYMM_NEXT: {
            const DTun::DProtocolMsgSymmNext* msgSymmNext = (const DTun::DProtocolMsgSymmNext*)msg;
            onSessionSymmNext(sess_shared, DTun::fromProtocolConnId(msgSymmNext->connId));
            break;
        }
        default:
            assert(false);
            break;
        }
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

    void Server::onSessionHelloFast(const boost::shared_ptr<Session>& sess, const DTun::ConnId& connId)
    {
        LOG4CPLUS_TRACE(logger(), "Server::onSessionHelloFast(" << sess->nodeId() << ", "
            << connId << ")");

        if (sess->peerIp() == 0) {
            LOG4CPLUS_ERROR(logger(), "no src peer address, cannot proceed with HelloFast");
            return;
        }

        boost::shared_ptr<Session> pSess = findPersistentSession(sess->nodeId());

        if (!pSess) {
            LOG4CPLUS_ERROR(logger(), "persistent session not found for nodeId = " << sess->nodeId());
            return;
        }

        ConnMap::iterator it = conns_.find(connId);
        if (it == conns_.end()) {
            LOG4CPLUS_TRACE(logger(), "connId = " << connId << " not found");
            return;
        }

        if (pSess == it->second.srcSess) {
            it->second.dstSess->sendFast(connId, sess->peerIp(), sess->peerPort());
        } else if (pSess == it->second.dstSess) {
            it->second.srcSess->sendFast(connId, sess->peerIp(), sess->peerPort());
        } else {
            LOG4CPLUS_ERROR(logger(), "cannot send fast for connId = " << connId << ", not allowed");
        }
    }

    void Server::onSessionHelloSymm(const boost::shared_ptr<Session>& sess, const DTun::ConnId& connId)
    {
        LOG4CPLUS_TRACE(logger(), "Server::onSessionHelloSymm(" << sess->nodeId() << ", "
            << connId << ")");

        if (sess->peerIp() == 0) {
            LOG4CPLUS_ERROR(logger(), "no src peer address, cannot proceed with HelloSymm");
            return;
        }

        boost::shared_ptr<Session> pSess = findPersistentSession(sess->nodeId());

        if (!pSess) {
            LOG4CPLUS_ERROR(logger(), "persistent session not found for nodeId = " << sess->nodeId());
            return;
        }

        ConnMap::iterator it = conns_.find(connId);
        if (it == conns_.end()) {
            LOG4CPLUS_TRACE(logger(), "connId = " << connId << " not found");
            return;
        }

        if (pSess == it->second.srcSess) {
            it->second.dstSess->sendSymm(connId, sess->peerIp(), sess->peerPort());
        } else if (pSess == it->second.dstSess) {
            it->second.srcSess->sendSymm(connId, sess->peerIp(), sess->peerPort());
        } else {
            LOG4CPLUS_ERROR(logger(), "cannot send symm for connId = " << connId << ", not allowed");
        }
    }

    void Server::onSessionConnCreate(const boost::shared_ptr<Session>& sess, const DTun::ConnId& connId,
        DTun::UInt32 dstNodeId,
        DTun::UInt32 remoteIp,
        DTun::UInt16 remotePort,
        bool bestEffort)
    {
        LOG4CPLUS_TRACE(logger(), "Server::onSessionConnCreate(" << sess->nodeId() << ", "
            << connId << ", " << dstNodeId << ", " << DTun::ipPortToString(remoteIp, remotePort) << ")");

        if (sess->nodeId() != connId.nodeId) {
            LOG4CPLUS_ERROR(logger(), "bad nodeId in connId " << connId);
            sess->sendConnStatus(connId, DPROTOCOL_STATUS_ERR_UNKNOWN);
            return;
        }

        if (dstNodeId == connId.nodeId) {
            LOG4CPLUS_ERROR(logger(), "bad dstNodeId, must be different from connId's src node");
            sess->sendConnStatus(connId, DPROTOCOL_STATUS_ERR_UNKNOWN);
            return;
        }

        if (sess->peerIp() == 0) {
            LOG4CPLUS_ERROR(logger(), "no src peer address, cannot proceed with ConnCreate");
            sess->sendConnStatus(connId, DPROTOCOL_STATUS_ERR_UNKNOWN);
            return;
        }

        if (conns_.count(connId) > 0) {
            LOG4CPLUS_ERROR(logger(), "connId " << connId << " already exists");
            sess->sendConnStatus(connId, DPROTOCOL_STATUS_ERR_UNKNOWN);
            return;
        }

        boost::shared_ptr<Session> dstSess = findPersistentSession(dstNodeId);

        if (!dstSess) {
            LOG4CPLUS_ERROR(logger(), "persistent dest session not found for dstNodeId = "
                << dstNodeId << ", addr = " << DTun::ipPortToString(remoteIp, remotePort));
            sess->sendConnStatus(connId, DPROTOCOL_STATUS_ERR_NOTFOUND);
            return;
        }

        if (dstSess->peerIp() == 0) {
            LOG4CPLUS_ERROR(logger(), "no dst peer address, cannot proceed with ConnCreate");
            sess->sendConnStatus(connId, DPROTOCOL_STATUS_ERR_UNKNOWN);
            return;
        }

        if (sess->isSymm() && dstSess->isSymm()) {
            LOG4CPLUS_ERROR(logger(), "both peers behind symmetrical NAT, cannot proceed");
            sess->sendConnStatus(connId, DPROTOCOL_STATUS_ERR_SYMM);
            return;
        }

        DTun::UInt8 srcMode = DPROTOCOL_RMODE_FAST;
        DTun::UInt8 dstMode = DPROTOCOL_RMODE_FAST;

        if (sess->isSymm() || dstSess->isSymm()) {
            if (sess->isSymm()) {
                srcMode = DPROTOCOL_RMODE_SYMM_CONN;
                dstMode = DPROTOCOL_RMODE_SYMM_ACC;
            } else {
                srcMode = DPROTOCOL_RMODE_SYMM_ACC;
                dstMode = DPROTOCOL_RMODE_SYMM_CONN;
            }
        }

        conns_[connId] = Conn(sess, dstSess, srcMode, dstMode);

        sess->sendConnStatus(connId, DPROTOCOL_STATUS_PENDING, srcMode, dstSess->peerIp());

        dstSess->sendConnRequest(connId, remoteIp, remotePort, dstMode, sess->peerIp(), bestEffort);
    }

    void Server::onSessionConnClose(const boost::shared_ptr<Session>& sess, const DTun::ConnId& connId, bool established)
    {
        LOG4CPLUS_TRACE(logger(), "Server::onSessionConnClose(" << sess->nodeId() << ", "
            << connId << ", " << established << ")");

        ConnMap::iterator it = conns_.find(connId);
        if (it == conns_.end()) {
            LOG4CPLUS_TRACE(logger(), "connId = " << connId << " not found");
            return;
        }

        DTun::UInt8 statusCode = established ? DPROTOCOL_STATUS_ESTABLISHED : DPROTOCOL_STATUS_ERR_CANCELED;

        if (sess == it->second.srcSess) {
            it->second.dstSess->sendConnStatus(connId, statusCode, it->second.dstMode);
        } else if (sess == it->second.dstSess) {
            it->second.srcSess->sendConnStatus(connId, statusCode, it->second.srcMode);
        } else {
            LOG4CPLUS_ERROR(logger(), "cannot close connId = " << connId << ", not allowed");
            return;
        }

        conns_.erase(it);
    }

    void Server::onSessionReady(const boost::shared_ptr<Session>& sess, const DTun::ConnId& connId)
    {
        LOG4CPLUS_TRACE(logger(), "Server::onSessionReady(" << sess->nodeId() << ", "
            << connId << ")");

        ConnMap::iterator it = conns_.find(connId);
        if (it == conns_.end()) {
            LOG4CPLUS_TRACE(logger(), "connId = " << connId << " not found");
            return;
        }

        if (sess == it->second.srcSess) {
            it->second.dstSess->sendReady(connId);
        } else if (sess == it->second.dstSess) {
            it->second.srcSess->sendReady(connId);
        } else {
            LOG4CPLUS_ERROR(logger(), "cannot Ready connId = " << connId << ", not allowed");
            return;
        }
    }

    void Server::onSessionSymmNext(const boost::shared_ptr<Session>& sess, const DTun::ConnId& connId)
    {
        LOG4CPLUS_TRACE(logger(), "Server::onSessionSymmNext(" << sess->nodeId() << ", "
            << connId << ")");

        ConnMap::iterator it = conns_.find(connId);
        if (it == conns_.end()) {
            LOG4CPLUS_TRACE(logger(), "connId = " << connId << " not found");
            return;
        }

        if (sess == it->second.srcSess) {
            it->second.dstSess->sendSymmNext(connId);
        } else if (sess == it->second.dstSess) {
            it->second.srcSess->sendSymmNext(connId);
        } else {
            LOG4CPLUS_ERROR(logger(), "cannot SymmNext connId = " << connId << ", not allowed");
            return;
        }
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
            for (ConnMap::iterator it = conns_.begin(); it != conns_.end();) {
                if (it->second.srcSess == sess) {
                    it->second.dstSess->sendConnStatus(it->first,
                        DPROTOCOL_STATUS_ERR_CANCELED, it->second.dstMode);
                    conns_.erase(it++);
                } else if (it->second.dstSess == sess) {
                    it->second.srcSess->sendConnStatus(it->first,
                        DPROTOCOL_STATUS_ERR_CANCELED, it->second.srcMode);
                    conns_.erase(it++);
                } else {
                    ++it;
                }
            }
        }

        sessions_.erase(sess);
    }
}

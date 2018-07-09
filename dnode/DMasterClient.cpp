#include "DMasterClient.h"
#include "Logger.h"
#include "DTun/Utils.h"
#include <boost/make_shared.hpp>

namespace DNode
{
    DMasterClient* theMasterClient = NULL;

    DMasterClient::DMasterClient(DTun::UDTReactor& udtReactor, DTun::TCPReactor& tcpReactor, const std::string& address, int port, DTun::UInt32 nodeId)
    : udtReactor_(udtReactor)
    , tcpReactor_(tcpReactor)
    , address_(address)
    , port_(port)
    , nodeId_(nodeId)
    , nextConnId_(0)
    {
    }

    DMasterClient::~DMasterClient()
    {
    }

    bool DMasterClient::start()
    {
        UDPSOCKET sock = UDT::socket(AF_INET, SOCK_STREAM, 0);
        if (sock == UDT::INVALID_SOCK) {
            LOG4CPLUS_ERROR(logger(), "Cannot create UDT socket: " << UDT::getlasterror().getErrorMessage());
            return false;
        }

        connector_ = boost::make_shared<DTun::UDTConnector>(boost::ref(udtReactor_), sock);

        std::ostringstream os;
        os << port_;

        if (!connector_->connect(address_, os.str(), boost::bind(&DMasterClient::onConnect, this, _1))) {
            return false;
        }

        return true;
    }

    DTun::UInt32 DMasterClient::registerConnection(SYSSOCKET s,
        DTun::UInt32 remoteIp,
        DTun::UInt16 remotePort,
        const RegisterConnectionCallback& callback)
    {
        boost::mutex::scoped_lock lock(m_);

        if (!conn_) {
            SYS_CLOSE_SOCKET(s);
            return 0;
        }

        boost::shared_ptr<DMasterSession> sess =
            boost::make_shared<DMasterSession>(boost::ref(udtReactor_), address_, port_);

        DTun::UInt32 connId = nextConnId_++;
        if (connId == 0) {
            connId = nextConnId_++;
        }

        if (!sess->startConnector(s, nodeId_, nodeId_, connId, remoteIp, remotePort,
            boost::bind(&DMasterClient::onRegisterConnection, this, _1, connId))) {
            return false;
        }

        connMasterSessions_[connId].sess = sess;
        connMasterSessions_[connId].callback = callback;

        return true;
    }

    void DMasterClient::cancelConnection(DTun::UInt32 connId)
    {
        boost::mutex::scoped_lock lock(m_);

        ConnMasterSessionMap::iterator it = connMasterSessions_.find(connId);
        if (it == connMasterSessions_.end()) {
            return;
        }

        ConnMasterSession tmp = it->second;

        connMasterSessions_.erase(it);

        lock.unlock();
    }

    void DMasterClient::onConnect(int err)
    {
        LOG4CPLUS_INFO(logger(), "onConnect(" << err << ")");

        boost::mutex::scoped_lock lock(m_);

        UDTSOCKET sock = connector_->sock();

        connector_->close();

        if (err) {
            UDT::close(sock);
        } else {
            conn_ = boost::make_shared<DTun::UDTConnection>(boost::ref(udtReactor_), sock);

            DTun::DProtocolHeader header;
            DTun::DProtocolMsgHello msg;

            header.msgCode = DPROTOCOL_MSG_HELLO;
            msg.nodeId = nodeId_;

            buff_.resize(sizeof(header) + sizeof(msg));
            memcpy(&buff_[0], &header, sizeof(header));
            memcpy(&buff_[0] + sizeof(header), &msg, sizeof(msg));

            conn_->write(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&DMasterClient::onSend, this, _1));
        }
    }

    void DMasterClient::onSend(int err)
    {
        LOG4CPLUS_INFO(logger(), "onSend(" << err << ")");

        boost::mutex::scoped_lock lock(m_);

        if (err) {
            boost::shared_ptr<DTun::UDTConnection> tmp = conn_;
            conn_.reset();
            lock.unlock();
            return;
        }

        buff_.resize(sizeof(DTun::DProtocolHeader));
        conn_->read(&buff_[0], &buff_[0] + buff_.size(),
            boost::bind(&DMasterClient::onRecvHeader, this, _1, _2),
            true);
    }

    void DMasterClient::onRecvHeader(int err, int numBytes)
    {
        LOG4CPLUS_INFO(logger(), "onRecvHeader(" << err << ", " << numBytes << ")");

        boost::mutex::scoped_lock lock(m_);

        if (err) {
            boost::shared_ptr<DTun::UDTConnection> tmp = conn_;
            conn_.reset();
            lock.unlock();
            return;
        }

        DTun::DProtocolHeader header;
        assert(numBytes == sizeof(header));
        memcpy(&header, &buff_[0], numBytes);

        switch (header.msgCode) {
        case DPROTOCOL_MSG_CONN:
            buff_.resize(sizeof(DTun::DProtocolMsgConn));
            conn_->read(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&DMasterClient::onRecvMsgConn, this, _1, _2),
                true);
            break;
        case DPROTOCOL_MSG_CONN_OK:
            buff_.resize(sizeof(DTun::DProtocolMsgConnOK));
            conn_->read(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&DMasterClient::onRecvMsgConnOK, this, _1, _2),
                true);
            break;
        case DPROTOCOL_MSG_CONN_ERR:
            buff_.resize(sizeof(DTun::DProtocolMsgConnErr));
            conn_->read(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&DMasterClient::onRecvMsgConnErr, this, _1, _2),
                true);
            break;
        default:
            LOG4CPLUS_ERROR(logger(), "bad msg code: " << static_cast<int>(header.msgCode));
            boost::shared_ptr<DTun::UDTConnection> tmp = conn_;
            conn_.reset();
            lock.unlock();
            break;
        }
    }

    void DMasterClient::onRecvMsgConn(int err, int numBytes)
    {
        LOG4CPLUS_INFO(logger(), "onRecvMsgConn(" << err << ", " << numBytes << ")");

        boost::mutex::scoped_lock lock(m_);

        if (err) {
            boost::shared_ptr<DTun::UDTConnection> tmp = conn_;
            conn_.reset();
            lock.unlock();
            return;
        }

        DTun::DProtocolMsgConn msg;
        assert(numBytes == sizeof(msg));
        memcpy(&msg, &buff_[0], numBytes);

        LOG4CPLUS_TRACE(logger(), "Proxy request: src = " << msg.srcNodeId
            << ", src_addr = " << DTun::ipPortToString(msg.srcNodeIp, msg.srcNodePort)
            << ", connId = " << msg.connId << ", remote_addr = " << DTun::ipPortToString(msg.ip, msg.port) << ")");

        startAccMasterSession(msg.srcNodeId, msg.connId, msg.ip, msg.port, msg.srcNodeIp, msg.srcNodePort);

        buff_.resize(sizeof(DTun::DProtocolHeader));
        conn_->read(&buff_[0], &buff_[0] + buff_.size(),
            boost::bind(&DMasterClient::onRecvHeader, this, _1, _2),
            true);
    }

    void DMasterClient::onRecvMsgConnOK(int err, int numBytes)
    {
        LOG4CPLUS_INFO(logger(), "onRecvMsgConnOK(" << err << ", " << numBytes << ")");

        boost::mutex::scoped_lock lock(m_);

        if (err) {
            boost::shared_ptr<DTun::UDTConnection> tmp = conn_;
            conn_.reset();
            lock.unlock();
            return;
        }

        DTun::DProtocolMsgConnOK msg;
        assert(numBytes == sizeof(msg));
        memcpy(&msg, &buff_[0], numBytes);

        ConnMasterSessionMap::iterator it = connMasterSessions_.find(msg.connId);
        if (it == connMasterSessions_.end()) {
            LOG4CPLUS_ERROR(logger(), "connId " << msg.connId << " not found");
            return;
        }

        ConnMasterSession tmp = it->second;

        connMasterSessions_.erase(it);

        buff_.resize(sizeof(DTun::DProtocolHeader));
        conn_->read(&buff_[0], &buff_[0] + buff_.size(),
            boost::bind(&DMasterClient::onRecvHeader, this, _1, _2),
            true);

        lock.unlock();

        tmp.sess.reset();
        tmp.callback(0, msg.dstNodeIp, msg.dstNodePort);
    }

    void DMasterClient::onRecvMsgConnErr(int err, int numBytes)
    {
        LOG4CPLUS_INFO(logger(), "onRecvMsgConnErr(" << err << ", " << numBytes << ")");

        boost::mutex::scoped_lock lock(m_);

        if (err) {
            boost::shared_ptr<DTun::UDTConnection> tmp = conn_;
            conn_.reset();
            lock.unlock();
            return;
        }

        DTun::DProtocolMsgConnErr msg;
        assert(numBytes == sizeof(msg));
        memcpy(&msg, &buff_[0], numBytes);

        ConnMasterSessionMap::iterator it = connMasterSessions_.find(msg.connId);
        if (it == connMasterSessions_.end()) {
            LOG4CPLUS_ERROR(logger(), "connId " << msg.connId << " not found");
            return;
        }

        ConnMasterSession tmp = it->second;

        connMasterSessions_.erase(it);

        buff_.resize(sizeof(DTun::DProtocolHeader));
        conn_->read(&buff_[0], &buff_[0] + buff_.size(),
            boost::bind(&DMasterClient::onRecvHeader, this, _1, _2),
            true);

        lock.unlock();

        tmp.sess.reset();
        tmp.callback(msg.errCode, 0, 0);
    }

    void DMasterClient::onRegisterConnection(int err, DTun::UInt32 connId)
    {
        boost::mutex::scoped_lock lock(m_);

        ConnMasterSessionMap::iterator it = connMasterSessions_.find(connId);
        if (it == connMasterSessions_.end()) {
            return;
        }

        ConnMasterSession tmp = it->second;

        it->second.sess.reset();

        if (err) {
            connMasterSessions_.erase(it);
        }

        lock.unlock();

        if (err) {
            tmp.sess.reset();
            tmp.callback(err, 0, 0);
        }
    }

    void DMasterClient::onAcceptConnection(int err, const boost::weak_ptr<DMasterSession>& sess,
        DTun::UInt32 localIp,
        DTun::UInt16 localPort,
        DTun::UInt32 remoteIp,
        DTun::UInt16 remotePort)
    {
        LOG4CPLUS_INFO(logger(), "onAcceptConnection(" << err << ")");

        boost::shared_ptr<DMasterSession> sess_shared = sess.lock();
        if (!sess_shared) {
            return;
        }

        boost::mutex::scoped_lock lock(m_);

        accMasterSessions_.erase(sess_shared);

        if (!err) {
            boost::shared_ptr<ProxySession> proxySess =
                boost::make_shared<ProxySession>(boost::ref(udtReactor_), boost::ref(tcpReactor_));

            if (proxySess->start(localIp, localPort, remoteIp, remotePort,
                boost::bind(&DMasterClient::onProxyDone, this, boost::weak_ptr<ProxySession>(proxySess)))) {
                proxySessions_.insert(proxySess);
            }
        }

        lock.unlock();
    }

    void DMasterClient::onProxyDone(const boost::weak_ptr<ProxySession>& sess)
    {
        LOG4CPLUS_INFO(logger(), "onProxyDone()");

        boost::shared_ptr<ProxySession> sess_shared = sess.lock();
        if (!sess_shared) {
            return;
        }

        boost::mutex::scoped_lock lock(m_);

        proxySessions_.erase(sess_shared);

        lock.unlock();
    }

    void DMasterClient::startAccMasterSession(DTun::UInt32 srcNodeId,
        DTun::UInt32 srcConnId,
        DTun::UInt32 localIp,
        DTun::UInt16 localPort,
        DTun::UInt32 remoteIp,
        DTun::UInt16 remotePort)
    {
        SYSSOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == SYS_INVALID_SOCKET) {
            LOG4CPLUS_ERROR(logger(), "Cannot create UDP socket");
            return;
        }

        struct sockaddr_in addr;

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        int res = ::bind(s, (const struct sockaddr*)&addr, sizeof(addr));

        if (res == SYS_SOCKET_ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot bind UDP socket");
            SYS_CLOSE_SOCKET(s);
            return;
        }

        boost::shared_ptr<DMasterSession> sess =
            boost::make_shared<DMasterSession>(boost::ref(udtReactor_), address_, port_);

        if (!sess->startAcceptor(s, srcNodeId, nodeId_, srcConnId,
            boost::bind(&DMasterClient::onAcceptConnection, this, _1, boost::weak_ptr<DMasterSession>(sess),
                localIp, localPort, remoteIp, remotePort))) {
            return;
        }

        accMasterSessions_.insert(sess);
    }
}

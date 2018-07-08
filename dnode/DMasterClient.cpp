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
            UDT::close(sock);
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

        MasterSessionMap::iterator it = connMasterSessions_.find(connId);
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
        default:
            LOG4CPLUS_ERROR(logger(), "bad msg code: " << header.msgCode);
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

        buff_.resize(sizeof(DTun::DProtocolHeader));
        conn_->read(&buff_[0], &buff_[0] + buff_.size(),
            boost::bind(&DMasterClient::onRecvHeader, this, _1, _2),
            true);
    }

    void DMasterClient::onRegisterConnection(int err, DTun::UInt32 connId)
    {
        boost::mutex::scoped_lock lock(m_);

        MasterSessionMap::iterator it = connMasterSessions_.find(connId);
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
}

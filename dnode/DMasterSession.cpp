#include "DMasterSession.h"
#include "Logger.h"
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>

namespace DNode
{
    DMasterSession::DMasterSession(DTun::UDTReactor& reactor, const std::string& address, int port)
    : reactor_(reactor)
    , address_(address)
    , port_(port)
    {
    }

    DMasterSession::~DMasterSession()
    {
    }

    bool DMasterSession::startConnector(SYSSOCKET s, DTun::UInt32 srcNodeId,
        DTun::UInt32 dstNodeId,
        DTun::UInt32 connId,
        DTun::UInt32 remoteIp,
        DTun::UInt16 remotePort,
        const Callback& callback)
    {
        DTun::DProtocolHeader header;
        DTun::DProtocolMsgHelloConn msg;

        header.msgCode = DPROTOCOL_MSG_HELLO_CONN;
        msg.srcNodeId = srcNodeId;
        msg.dstNodeId = dstNodeId;
        msg.connId = connId;
        msg.remoteIp = remoteIp;
        msg.remotePort = remotePort;

        buff_.resize(sizeof(header) + sizeof(msg));
        memcpy(&buff_[0], &header, sizeof(header));
        memcpy(&buff_[0] + sizeof(header), &msg, sizeof(msg));

        return start(s, callback);
    }

    bool DMasterSession::startAcceptor(SYSSOCKET s, DTun::UInt32 srcNodeId,
        DTun::UInt32 dstNodeId,
        DTun::UInt32 connId,
        const Callback& callback)
    {
        DTun::DProtocolHeader header;
        DTun::DProtocolMsgHelloAcc msg;

        header.msgCode = DPROTOCOL_MSG_HELLO_ACC;
        msg.srcNodeId = srcNodeId;
        msg.dstNodeId = dstNodeId;
        msg.connId = connId;

        buff_.resize(sizeof(header) + sizeof(msg));
        memcpy(&buff_[0], &header, sizeof(header));
        memcpy(&buff_[0] + sizeof(header), &msg, sizeof(msg));

        return start(s, callback);
    }

    bool DMasterSession::start(SYSSOCKET s, const Callback& callback)
    {
        UDPSOCKET sock = UDT::socket(AF_INET, SOCK_STREAM, 0);
        if (sock == UDT::INVALID_SOCK) {
            LOG4CPLUS_ERROR(logger(), "Cannot create UDT socket: " << UDT::getlasterror().getErrorMessage());
            SYS_CLOSE_SOCKET(s);
            return false;
        }

        if (UDT::bind2(sock, s) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot bind UDT socket: " << UDT::getlasterror().getErrorMessage());
            UDT::close(sock);
            SYS_CLOSE_SOCKET(s);
            return false;
        }

        callback_ = callback;

        connector_ = boost::make_shared<DTun::UDTConnector>(boost::ref(reactor_), sock);

        std::ostringstream os;
        os << port_;

        if (!connector_->connect(address_, os.str(), boost::bind(&DMasterSession::onConnect, this, _1))) {
            return false;
        }

        return true;
    }

    void DMasterSession::onConnect(int err)
    {
        LOG4CPLUS_INFO(logger(), "DMasterSession::onConnect(" << err << ")");

        UDTSOCKET sock = connector_->sock();

        connector_->close();

        if (err) {
            UDT::close(sock);
            callback_(err);
        } else {
            conn_ = boost::make_shared<DTun::UDTConnection>(boost::ref(reactor_), sock);

            conn_->write(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&DMasterSession::onSend, this, _1));
        }
    }

    void DMasterSession::onSend(int err)
    {
        LOG4CPLUS_INFO(logger(), "DMasterSession::onSend(" << err << ")");

        callback_(err);
    }
}

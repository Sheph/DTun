#include "DMasterClient.h"
#include "Logger.h"
#include <boost/make_shared.hpp>

namespace DNode
{
    DMasterClient::DMasterClient(DTun::UDTReactor& reactor, const std::string& address, int port, DTun::UInt32 nodeId)
    : reactor_(reactor)
    , address_(address)
    , port_(port)
    , nodeId_(nodeId)
    {
    }

    DMasterClient::~DMasterClient()
    {
        if (connector_) {
            connector_->close();
        }
        if (conn_) {
            conn_->close();
        }
    }

    bool DMasterClient::start()
    {
        UDPSOCKET sock = UDT::socket(AF_INET, SOCK_STREAM, 0);
        if (sock == UDT::INVALID_SOCK) {
            LOG4CPLUS_ERROR(logger(), "Cannot create UDT socket: " << UDT::getlasterror().getErrorMessage());
            return false;
        }

        connector_ = boost::make_shared<DTun::UDTConnector>(boost::ref(reactor_), sock);

        std::ostringstream os;
        os << port_;

        if (!connector_->connect(address_, os.str(), boost::bind(&DMasterClient::onConnect, this, _1))) {
            UDT::close(sock);
            return false;
        }

        return true;
    }

    void DMasterClient::onConnect(int err)
    {
        LOG4CPLUS_INFO(logger(), "onConnect(" << err << ")");

        UDTSOCKET sock = connector_->sock();

        connector_->close();

        if (err) {
            UDT::close(sock);
        } else {
            conn_ = boost::make_shared<DTun::UDTConnection>(boost::ref(reactor_), sock);

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

        if (err) {
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

        if (err) {
            return;
        }

        buff_.resize(100);
        conn_->read(&buff_[0], &buff_[0] + buff_.size(),
            boost::bind(&DMasterClient::onRecvMsg, this, _1, _2),
            true);
    }

    void DMasterClient::onRecvMsg(int err, int numBytes)
    {
        LOG4CPLUS_INFO(logger(), "onRecvMsg(" << err << ", " << numBytes << ")");

        if (err) {
            return;
        }

        buff_.resize(sizeof(DTun::DProtocolHeader));
        conn_->read(&buff_[0], &buff_[0] + buff_.size(),
            boost::bind(&DMasterClient::onRecvHeader, this, _1, _2),
            true);
    }
}

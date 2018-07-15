#include "DMasterSession.h"
#include "Logger.h"
#include "DTun/Utils.h"
#include "DTun/SConnector.h"
#include "DTun/SConnection.h"
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>

namespace DNode
{
    DMasterSession::DMasterSession(DTun::SManager& mgr, const std::string& address, int port)
    : mgr_(mgr)
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
        boost::shared_ptr<DTun::SHandle> handle = mgr_.createStreamSocket();
        if (!handle) {
            DTun::closeSysSocketChecked(s);
            return false;
        }

        if (!handle->bind(s)) {
            DTun::closeSysSocketChecked(s);
            return false;
        }

        callback_ = callback;

        connector_ = handle->createConnector();

        std::ostringstream os;
        os << port_;

        if (!connector_->connect(address_, os.str(), boost::bind(&DMasterSession::onConnect, this, _1), DTun::SConnector::ModeNormal)) {
            return false;
        }

        return true;
    }

    void DMasterSession::onConnect(int err)
    {
        LOG4CPLUS_TRACE(logger(), "DMasterSession::onConnect(" << err << ")");

        boost::shared_ptr<DTun::SHandle> handle = connector_->handle();

        connector_->close();

        if (err) {
            handle->close();
            callback_(err);
        } else {
            conn_ = handle->createConnection();

            conn_->write(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&DMasterSession::onSend, this, _1));
        }
    }

    void DMasterSession::onSend(int err)
    {
        LOG4CPLUS_TRACE(logger(), "DMasterSession::onSend(" << err << ")");

        callback_(err);
    }
}

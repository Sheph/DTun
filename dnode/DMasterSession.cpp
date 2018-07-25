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

    bool DMasterSession::startFast(SYSSOCKET s, DTun::UInt32 nodeId, const DTun::ConnId& connId,
        const Callback& callback)
    {
        DTun::DProtocolHeader header;
        DTun::DProtocolMsgHelloFast msg;

        header.msgCode = DPROTOCOL_MSG_HELLO_FAST;
        msg.nodeId = nodeId;
        msg.connId = DTun::toProtocolConnId(connId);

        buff_.resize(sizeof(header) + sizeof(msg));
        memcpy(&buff_[0], &header, sizeof(header));
        memcpy(&buff_[0] + sizeof(header), &msg, sizeof(msg));

        return start(s, callback);
    }

    bool DMasterSession::startSymm(SYSSOCKET s, DTun::UInt32 nodeId, const DTun::ConnId& connId,
        const Callback& callback)
    {
        DTun::DProtocolHeader header;
        DTun::DProtocolMsgHelloSymm msg;

        header.msgCode = DPROTOCOL_MSG_HELLO_SYMM;
        msg.nodeId = nodeId;
        msg.connId = DTun::toProtocolConnId(connId);

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

        conn_->close(true);

        callback_(err);
    }
}

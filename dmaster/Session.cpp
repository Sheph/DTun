#include "Session.h"
#include "Logger.h"
#include <boost/bind.hpp>

namespace DMaster
{
    Session::Session(const boost::shared_ptr<DTun::UDTConnection>& conn)
    : type_(TypeUnknown)
    , nodeId_(0)
    , conn_(conn)
    {
    }

    Session::~Session()
    {
    }

    void Session::start()
    {
        buff_.resize(sizeof(DTun::DProtocolHeader));
        conn_->read(&buff_[0], &buff_[0] + buff_.size(),
            boost::bind(&Session::onRecvHeader, this, _1, _2),
            true);
    }

    void Session::registerConnRequest(DTun::UInt32 connId, DTun::UInt32 dstNodeId)
    {
    }

    void Session::setConnRequestErr(DTun::UInt32 connId,
        DTun::UInt32 errCode)
    {
    }

    void Session::setAllConnRequestsErr(DTun::UInt32 dstNodeId,
        DTun::UInt32 errCode)
    {
    }

    void Session::setConnRequestOk(DTun::UInt32 connId,
        DTun::UInt32 dstNodeIp,
        DTun::UInt16 dstNodePort)
    {
    }

    void Session::sendConnRequest(DTun::UInt32 srcNodeId,
        DTun::UInt32 srcNodeIp,
        DTun::UInt16 srcNodePort,
        DTun::UInt32 connId,
        DTun::UInt32 ip,
        DTun::UInt16 port)
    {
    }

    void Session::onSend(int err)
    {
    }

    void Session::onRecvHeader(int err, int numBytes)
    {
        LOG4CPLUS_TRACE(logger(), "onRecvHeader(" << err << ", " << numBytes << ")");

        if (err) {
            if (errorCallback_) {
                errorCallback_(err);
            }
            return;
        }

        DTun::DProtocolHeader header;
        assert(numBytes == sizeof(header));
        memcpy(&header, &buff_[0], numBytes);

        switch (header.msgCode) {
        case DPROTOCOL_MSG_HELLO:
            buff_.resize(sizeof(DTun::DProtocolMsgHello));
            conn_->read(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&Session::onRecvMsgHello, this, _1, _2),
                true);
            break;
        case DPROTOCOL_MSG_HELLO_CON:
            buff_.resize(sizeof(DTun::DProtocolMsgHelloConn));
            conn_->read(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&Session::onRecvMsgHelloConn, this, _1, _2),
                true);
            break;
        case DPROTOCOL_MSG_HELLO_ACC:
            buff_.resize(sizeof(DTun::DProtocolMsgHelloAcc));
            conn_->read(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&Session::onRecvMsgHelloAcc, this, _1, _2),
                true);
            break;
        default:
            LOG4CPLUS_ERROR(logger(), "bad msg code: " << header.msgCode);
            if (errorCallback_) {
                errorCallback_(CUDTException::EUNKNOWN);
            }
            break;
        }
    }

    void Session::onRecvMsgHello(int err, int numBytes)
    {
        LOG4CPLUS_TRACE(logger(), "onRecvMsgHello(" << err << ", " << numBytes << ")");

        if (err) {
            if (errorCallback_) {
                errorCallback_(err);
            }
            return;
        }

        DTun::DProtocolMsgHello msg;
        assert(numBytes == sizeof(msg));
        memcpy(&msg, &buff_[0], numBytes);

        type_ = TypePersistent;
        nodeId_ = msg.nodeId;

        if (startPersistentCallback_) {
            startPersistentCallback_();
        }
    }

    void Session::onRecvMsgHelloConn(int err, int numBytes)
    {
        LOG4CPLUS_TRACE(logger(), "onRecvMsgHelloConn(" << err << ", " << numBytes << ")");

        if (err) {
            if (errorCallback_) {
                errorCallback_(err);
            }
            return;
        }
    }

    void Session::onRecvMsgHelloAcc(int err, int numBytes)
    {
        LOG4CPLUS_TRACE(logger(), "onRecvMsgHelloAcc(" << err << ", " << numBytes << ")");

        if (err) {
            if (errorCallback_) {
                errorCallback_(err);
            }
            return;
        }
    }
}

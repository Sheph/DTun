#include "Session.h"
#include "Logger.h"
#include <boost/bind.hpp>
#include <boost/make_shared.hpp>

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
        if (connRequests_.count(connId) > 0) {
            LOG4CPLUS_ERROR(logger(), "connId " << connId << " already exists");
            return;
        }

        connRequests_[connId] = dstNodeId;
    }

    void Session::setConnRequestErr(DTun::UInt32 connId,
        DTun::UInt32 errCode)
    {
        ConnRequestMap::iterator it = connRequests_.find(connId);
        if (it == connRequests_.end()) {
            LOG4CPLUS_ERROR(logger(), "connId " << connId << " not found");
            return;
        }

        connRequests_.erase(it);

        DTun::DProtocolMsgConnErr msg;

        msg.connId = connId;
        msg.errCode = errCode;

        sendMsg(DPROTOCOL_MSG_CONN_ERR, &msg, sizeof(msg));
    }

    void Session::setAllConnRequestsErr(DTun::UInt32 dstNodeId,
        DTun::UInt32 errCode)
    {
    }

    void Session::setConnRequestOk(DTun::UInt32 connId,
        DTun::UInt32 dstNodeIp,
        DTun::UInt16 dstNodePort)
    {
        ConnRequestMap::iterator it = connRequests_.find(connId);
        if (it == connRequests_.end()) {
            LOG4CPLUS_ERROR(logger(), "connId " << connId << " not found");
            return;
        }

        connRequests_.erase(it);

        DTun::DProtocolMsgConnOK msg;

        msg.connId = connId;
        msg.dstNodeIp = dstNodeIp;
        msg.dstNodePort = dstNodePort;

        sendMsg(DPROTOCOL_MSG_CONN_OK, &msg, sizeof(msg));
    }

    void Session::sendConnRequest(DTun::UInt32 srcNodeId,
        DTun::UInt32 srcNodeIp,
        DTun::UInt16 srcNodePort,
        DTun::UInt32 connId,
        DTun::UInt32 ip,
        DTun::UInt16 port)
    {
        DTun::DProtocolMsgConn msg;

        msg.srcNodeId = srcNodeId;
        msg.srcNodeIp = srcNodeIp;
        msg.srcNodePort= srcNodePort;
        msg.connId = connId;
        msg.ip = ip;
        msg.port = port;

        sendMsg(DPROTOCOL_MSG_CONN, &msg, sizeof(msg));
    }

    void Session::onSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff)
    {
        LOG4CPLUS_TRACE(logger(), "onSend(" << err << ")");

        if (err) {
            if (errorCallback_) {
                errorCallback_(err);
            }
            return;
        }
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
        case DPROTOCOL_MSG_HELLO_CONN:
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

        startRecvAny();

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

        DTun::DProtocolMsgHelloConn msg;
        assert(numBytes == sizeof(msg));
        memcpy(&msg, &buff_[0], numBytes);

        type_ = TypeConnector;
        nodeId_ = msg.srcNodeId;

        startRecvAny();

        if (startConnectorCallback_) {
            startConnectorCallback_(msg.dstNodeId, msg.connId, msg.remoteIp, msg.remotePort);
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

        DTun::DProtocolMsgHelloAcc msg;
        assert(numBytes == sizeof(msg));
        memcpy(&msg, &buff_[0], numBytes);

        type_ = TypeAcceptor;
        nodeId_ = msg.dstNodeId;

        startRecvAny();

        if (startAcceptorCallback_) {
            startAcceptorCallback_(msg.srcNodeId, msg.connId);
        }
    }

    void Session::onRecvAny(int err, int numBytes)
    {
        LOG4CPLUS_TRACE(logger(), "onRecvAny(" << err << ", " << numBytes << ")");

        if (errorCallback_) {
            errorCallback_(err ? err : CUDTException::EUNKNOWN);
        }
    }

    void Session::startRecvAny()
    {
        buff_.resize(1);
        conn_->read(&buff_[0], &buff_[0] + buff_.size(),
            boost::bind(&Session::onRecvAny, this, _1, _2),
            false);
    }

    void Session::sendMsg(DTun::UInt8 msgCode, const void* msg, int msgSize)
    {
        DTun::DProtocolHeader header;

        header.msgCode = msgCode;

        boost::shared_ptr<std::vector<char> > sndBuff =
            boost::make_shared<std::vector<char> >(sizeof(header) + msgSize);

        memcpy(&(*sndBuff)[0], &header, sizeof(header));
        memcpy(&(*sndBuff)[0] + sizeof(header), msg, msgSize);

        conn_->write(&(*sndBuff)[0], &(*sndBuff)[0] + sndBuff->size(),
            boost::bind(&Session::onSend, this, _1, sndBuff));
    }
}

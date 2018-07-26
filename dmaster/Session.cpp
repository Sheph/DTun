#include "Session.h"
#include "Logger.h"
#include <boost/bind.hpp>
#include <boost/make_shared.hpp>

namespace DMaster
{
    Session::Session(const boost::shared_ptr<DTun::SConnection>& conn)
    : type_(TypeUnknown)
    , nodeId_(0)
    , symm_(false)
    , conn_(conn)
    {
        // Another UDT crap, if we do this later after receiving
        // HELLO we might run into situation when UDT library implicitly closes
        // our socket and doesn't allow us to query for peer address...
        if (!conn->handle()->getPeerName(peerIp_, peerPort_)) {
            peerIp_ = 0;
            peerPort_ = 0;
        }
    }

    Session::~Session()
    {
    }

    void Session::start()
    {
        startRecvHeader();
    }

    void Session::sendConnRequest(const DTun::ConnId& connId,
        DTun::UInt32 ip,
        DTun::UInt16 port,
        DTun::UInt8 mode,
        DTun::UInt32 srcIp)
    {
        DTun::DProtocolMsgConn msg;

        msg.connId = DTun::toProtocolConnId(connId);
        msg.ip = ip;
        msg.port = port;
        msg.mode = mode;
        msg.srcIp = srcIp;

        sendMsg(DPROTOCOL_MSG_CONN, &msg, sizeof(msg));
    }

    void Session::sendConnStatus(const DTun::ConnId& connId,
        DTun::UInt8 statusCode,
        DTun::UInt8 mode,
        DTun::UInt32 dstIp)
    {
        DTun::DProtocolMsgConnStatus msg;

        msg.connId = DTun::toProtocolConnId(connId);
        msg.mode = mode;
        msg.statusCode = statusCode;
        msg.dstIp = dstIp;

        sendMsg(DPROTOCOL_MSG_CONN_STATUS, &msg, sizeof(msg));
    }

    void Session::sendFast(const DTun::ConnId& connId,
        DTun::UInt32 nodeIp,
        DTun::UInt16 nodePort)
    {
        DTun::DProtocolMsgFast msg;

        msg.connId = DTun::toProtocolConnId(connId);
        msg.nodeIp = nodeIp;
        msg.nodePort = nodePort;

        sendMsg(DPROTOCOL_MSG_FAST, &msg, sizeof(msg));
    }

    void Session::sendSymm(const DTun::ConnId& connId,
        DTun::UInt32 nodeIp,
        DTun::UInt16 nodePort)
    {
        DTun::DProtocolMsgSymm msg;

        msg.connId = DTun::toProtocolConnId(connId);
        msg.nodeIp = nodeIp;
        msg.nodePort = nodePort;

        sendMsg(DPROTOCOL_MSG_SYMM, &msg, sizeof(msg));
    }

    void Session::sendSymmNext(const DTun::ConnId& connId)
    {
        DTun::DProtocolMsgSymmNext msg;

        msg.connId = DTun::toProtocolConnId(connId);

        sendMsg(DPROTOCOL_MSG_SYMM_NEXT, &msg, sizeof(msg));
    }

    void Session::onSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff)
    {
        LOG4CPLUS_TRACE(logger(), "Session::onSend(" << err << ")");

        if (err) {
            if (errorCallback_) {
                errorCallback_(err);
            }
            return;
        }
    }

    void Session::onRecvHeader(int err, int numBytes)
    {
        LOG4CPLUS_TRACE(logger(), "Session::onRecvHeader(" << err << ", " << numBytes << ")");

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
        case DPROTOCOL_MSG_HELLO_PROBE:
            onRecvMsgHelloProbe();
            break;
        case DPROTOCOL_MSG_HELLO_FAST:
            buff_.resize(sizeof(DTun::DProtocolMsgHelloFast));
            conn_->read(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&Session::onRecvMsgHelloFast, this, _1, _2),
                true);
            break;
        case DPROTOCOL_MSG_HELLO_SYMM:
            buff_.resize(sizeof(DTun::DProtocolMsgHelloSymm));
            conn_->read(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&Session::onRecvMsgHelloSymm, this, _1, _2),
                true);
            break;
        case DPROTOCOL_MSG_CONN_CREATE:
            buff_.resize(sizeof(DTun::DProtocolMsgConnCreate));
            conn_->read(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&Session::onRecvMsgOther, this, _1, _2, header.msgCode),
                true);
            break;
        case DPROTOCOL_MSG_CONN_CLOSE:
            buff_.resize(sizeof(DTun::DProtocolMsgConnClose));
            conn_->read(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&Session::onRecvMsgOther, this, _1, _2, header.msgCode),
                true);
            break;
        case DPROTOCOL_MSG_SYMM_NEXT:
            buff_.resize(sizeof(DTun::DProtocolMsgSymmNext));
            conn_->read(&buff_[0], &buff_[0] + buff_.size(),
                boost::bind(&Session::onRecvMsgOther, this, _1, _2, header.msgCode),
                true);
            break;
        default:
            LOG4CPLUS_ERROR(logger(), "bad msg code: " << header.msgCode);
            if (errorCallback_) {
                errorCallback_(1);
            }
            break;
        }
    }

    void Session::onRecvMsgHello(int err, int numBytes)
    {
        LOG4CPLUS_TRACE(logger(), "Session::onRecvMsgHello(" << err << ", " << numBytes << ")");

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
        symm_ = msg.probePort && (msg.probePort != peerPort_);
        //symm_ = (nodeId_ == 1);

        startRecvHeader();

        if (startPersistentCallback_) {
            startPersistentCallback_();
        }
    }

    void Session::onRecvMsgHelloProbe()
    {
        LOG4CPLUS_TRACE(logger(), "Session::onRecvMsgHelloProbe()");

        type_ = TypeProbe;

        DTun::DProtocolMsgProbe msg;

        msg.srcIp = peerIp_;
        msg.srcPort = peerPort_;

        sendMsg(DPROTOCOL_MSG_PROBE, &msg, sizeof(msg));

        startRecvHeader();
    }

    void Session::onRecvMsgHelloFast(int err, int numBytes)
    {
        LOG4CPLUS_TRACE(logger(), "Session::onRecvMsgHelloFast(" << err << ", " << numBytes << ")");

        if (err) {
            if (errorCallback_) {
                errorCallback_(err);
            }
            return;
        }

        DTun::DProtocolMsgHelloFast msg;
        assert(numBytes == sizeof(msg));
        memcpy(&msg, &buff_[0], numBytes);

        type_ = TypeFast;
        nodeId_ = msg.nodeId;

        startRecvHeader();

        if (startFastCallback_) {
            startFastCallback_(DTun::fromProtocolConnId(msg.connId));
        }
    }

    void Session::onRecvMsgHelloSymm(int err, int numBytes)
    {
        LOG4CPLUS_TRACE(logger(), "Session::onRecvMsgHelloSymm(" << err << ", " << numBytes << ")");

        if (err) {
            if (errorCallback_) {
                errorCallback_(err);
            }
            return;
        }

        DTun::DProtocolMsgHelloSymm msg;
        assert(numBytes == sizeof(msg));
        memcpy(&msg, &buff_[0], numBytes);

        type_ = TypeSymm;
        nodeId_ = msg.nodeId;

        startRecvHeader();

        if (startSymmCallback_) {
            startSymmCallback_(DTun::fromProtocolConnId(msg.connId));
        }
    }

    void Session::onRecvMsgOther(int err, int numBytes, DTun::UInt8 msgCode)
    {
        LOG4CPLUS_TRACE(logger(), "Session::onRecvMsgOther(" << err << ", " << numBytes << ", " << (int)msgCode << ")");

        if (err) {
            if (errorCallback_) {
                errorCallback_(err ? err : 1);
            }
            return;
        }

        if (messageCallback_) {
            messageCallback_(msgCode, &buff_[0]);
        }

        startRecvHeader();
    }

    void Session::startRecvHeader()
    {
        buff_.resize(sizeof(DTun::DProtocolHeader));
        conn_->read(&buff_[0], &buff_[0] + buff_.size(),
            boost::bind(&Session::onRecvHeader, this, _1, _2),
            true);
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

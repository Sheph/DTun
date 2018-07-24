#include "RendezvousFastSession.h"
#include "Logger.h"
#include "DTun/Utils.h"
#include <boost/make_shared.hpp>

namespace DNode
{
    RendezvousFastSession::RendezvousFastSession(DTun::SManager& localMgr, DTun::SManager& remoteMgr, DTun::UInt32 nodeId, const DTun::ConnId& connId)
    : RendezvousSession(nodeId, connId)
    , localMgr_(localMgr)
    , remoteMgr_(remoteMgr)
    , established_(false)
    , destIp_(0)
    , destPort_(0)
    , watch_(boost::make_shared<DTun::OpWatch>(boost::ref(localMgr.reactor())))
    {
    }

    RendezvousFastSession::~RendezvousFastSession()
    {
        watch_->close();
    }

    bool RendezvousFastSession::start(const std::string& serverAddr, int serverPort, const Callback& callback)
    {
        boost::mutex::scoped_lock lock(m_);

        SYSSOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (s == SYS_INVALID_SOCKET) {
            LOG4CPLUS_ERROR(logger(), "Cannot create UDP socket");
            return false;
        }

        SYSSOCKET boundSock = ::dup(s);
        if (boundSock == -1) {
            LOG4CPLUS_ERROR(logger(), "Cannot dup UDP socket");
            DTun::closeSysSocketChecked(s);
            return false;
        }

        struct sockaddr_in addr;

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

        int res = ::bind(s, (const struct sockaddr*)&addr, sizeof(addr));

        if (res == SYS_SOCKET_ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot bind UDP socket");
            DTun::closeSysSocketChecked(s);
            DTun::closeSysSocketChecked(boundSock);
            return false;
        }

        boost::shared_ptr<DTun::SHandle> handle = localMgr_.createDatagramSocket(boundSock);
        if (!handle) {
            DTun::closeSysSocketChecked(s);
            DTun::closeSysSocketChecked(boundSock);
            return false;
        }

        callback_ = callback;
        pingConn_ = handle->createConnection();
        masterSession_ =
            boost::make_shared<DMasterSession>(boost::ref(remoteMgr_), serverAddr, serverPort);
        return masterSession_->startFast(s, nodeId(), connId(),
            boost::bind(&RendezvousFastSession::onHelloSend, this, _1));
    }

    void RendezvousFastSession::onMsg(DTun::UInt8 msgId, const void* msg)
    {
        if (msgId != DPROTOCOL_MSG_FAST) {
            return;
        }

        const DTun::DProtocolMsgFast* msgFast = (const DTun::DProtocolMsgFast*)msg;

        boost::mutex::scoped_lock lock(m_);

        if (!callback_ || (destIp_ != 0)) {
            return;
        }

        destIp_ = msgFast->nodeIp;
        destPort_ = msgFast->nodePort;

        if (established_) {
            Callback cb = callback_;
            callback_ = Callback();
            lock.unlock();
            SYSSOCKET s = pingConn_->handle()->duplicate();
            pingConn_->close();
            cb(0, s, destIp_, destPort_);
            return;
        }

        lock.unlock();

        localMgr_.reactor().post(
            watch_->wrap(boost::bind(&RendezvousFastSession::onPingTimeout, this)));
    }

    void RendezvousFastSession::onEstablished()
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousFastSession::onEstablished()");

        boost::mutex::scoped_lock lock(m_);

        if (!callback_) {
            return;
        }

        if (destIp_ == 0) {
            established_ = true;
            return;
        }

        Callback cb = callback_;
        callback_ = Callback();
        lock.unlock();
        SYSSOCKET s = pingConn_->handle()->duplicate();
        pingConn_->close();
        cb(0, s, destIp_, destPort_);
    }

    void RendezvousFastSession::onHelloSend(int err)
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousFastSession::onHelloSend(" << err << ")");

        boost::mutex::scoped_lock lock(m_);

        if (!callback_) {
            return;
        }

        if (!err) {
            rcvBuff_.resize(4096);
            pingConn_->readFrom(&rcvBuff_[0], &rcvBuff_[0] + rcvBuff_.size(),
                boost::bind(&RendezvousFastSession::onRecvPing, this, _1, _2, _3, _4));
            return;
        }

        Callback cb = callback_;
        callback_ = Callback();
        lock.unlock();
        cb(err, SYS_INVALID_SOCKET, 0, 0);
    }

    void RendezvousFastSession::onPingSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff)
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousFastSession::onPingSend(" << err << ")");

        boost::mutex::scoped_lock lock(m_);

        if (!callback_ || !err) {
            return;
        }

        Callback cb = callback_;
        callback_ = Callback();
        lock.unlock();
        cb(err, SYS_INVALID_SOCKET, 0, 0);
    }

    void RendezvousFastSession::onRecvPing(int err, int numBytes, DTun::UInt32 ip, DTun::UInt16 port)
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousFastSession::onRecvPing(" << err << ", " << numBytes << ", src=" << DTun::ipPortToString(ip, port) << ")");

        boost::mutex::scoped_lock lock(m_);

        if (!callback_ || (destIp_ == 0)) {
            return;
        }

        if (!err) {
            if (numBytes != 4) {
                LOG4CPLUS_WARN(logger(), "RendezvousFastSession::onRecvPing bad ping len: "
                    << numBytes);
                pingConn_->readFrom(&rcvBuff_[0], &rcvBuff_[0] + rcvBuff_.size(),
                    boost::bind(&RendezvousFastSession::onRecvPing, this, _1, _2, _3, _4));
                return;
            } else {
                uint8_t a = rcvBuff_[0];
                uint8_t b = rcvBuff_[1];
                uint8_t c = rcvBuff_[2];
                uint8_t d = rcvBuff_[3];
                if ((a != 0xAA) || (b != 0xBB) || (c != 0xCC) || (d != 0xDD)) {
                    LOG4CPLUS_WARN(logger(), "RendezvousFastSession::onRecvPing bad ping: "
                        << (int)a << "," << (int)b << "," << (int)c << "," << (int)d);
                    pingConn_->readFrom(&rcvBuff_[0], &rcvBuff_[0] + rcvBuff_.size(),
                        boost::bind(&RendezvousFastSession::onRecvPing, this, _1, _2, _3, _4));
                    return;
                }
                if ((ip != destIp_) || (port != destPort_)) {
                    LOG4CPLUS_WARN(logger(), "RendezvousFastSession::onRecvPing bad source ip/port");
                    pingConn_->readFrom(&rcvBuff_[0], &rcvBuff_[0] + rcvBuff_.size(),
                        boost::bind(&RendezvousFastSession::onRecvPing, this, _1, _2, _3, _4));
                    return;
                }
            }
        }

        Callback cb = callback_;
        callback_ = Callback();
        lock.unlock();
        if (err) {
            cb(err, SYS_INVALID_SOCKET, 0, 0);
        } else {
            SYSSOCKET s = pingConn_->handle()->duplicate();
            pingConn_->close();
            cb(0, s, ip, port);
        }
    }

    void RendezvousFastSession::onPingTimeout()
    {
        LOG4CPLUS_TRACE(logger(), "RendezvousFastSession::onPingTimeout()");

        boost::mutex::scoped_lock lock(m_);

        if (!callback_) {
            return;
        }

        assert(destIp_ != 0);
        assert(destPort_ != 0);

        boost::shared_ptr<std::vector<char> > sndBuff =
            boost::make_shared<std::vector<char> >(4);

        (*sndBuff)[0] = 0xAA;
        (*sndBuff)[1] = 0xBB;
        (*sndBuff)[2] = 0xCC;
        (*sndBuff)[3] = 0xDD;

        pingConn_->writeTo(&(*sndBuff)[0], &(*sndBuff)[0] + sndBuff->size(),
            destIp_, destPort_,
            boost::bind(&RendezvousFastSession::onPingSend, this, _1, sndBuff));

        lock.unlock();

        localMgr_.reactor().post(
            watch_->wrap(boost::bind(&RendezvousFastSession::onPingTimeout, this)), 1000);
    }
}

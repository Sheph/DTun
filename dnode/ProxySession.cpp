#include "ProxySession.h"
#include "DTun/Utils.h"
#include "Logger.h"
#include <boost/make_shared.hpp>

namespace DNode
{
    ProxySession::ProxySession(DTun::UDTReactor& udtReactor, DTun::TCPReactor& tcpReactor)
    : udtReactor_(udtReactor)
    , tcpReactor_(tcpReactor)
    , localSndBuff_(65535)
    , localSndBuffBytes_(0)
    , remoteSndBuff_(65535)
    , remoteSndBuffBytes_(0)
    , connected_(false)
    , done_(false)
    , localShutdown_(false)
    {
    }

    ProxySession::~ProxySession()
    {
    }

    bool ProxySession::start(SYSSOCKET s, DTun::UInt32 localIp, DTun::UInt16 localPort,
        DTun::UInt32 remoteIp, DTun::UInt16 remotePort, const DoneCallback& callback)
    {
        boost::mutex::scoped_lock lock(m_);

        UDTSOCKET remoteSock = UDT::socket(AF_INET, SOCK_STREAM, 0);
        if (remoteSock == UDT::INVALID_SOCK) {
            LOG4CPLUS_ERROR(logger(), "Cannot create UDT socket: " << UDT::getlasterror().getErrorMessage());
            DTun::closeSysSocketChecked(s);
            return false;
        }

        bool optval = false;
        if (UDT::setsockopt(remoteSock, 0, UDT_REUSEADDR, &optval, sizeof(optval)) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot set reuseaddr for UDT socket: " << UDT::getlasterror().getErrorMessage());
            DTun::closeUDTSocketChecked(remoteSock);
            DTun::closeSysSocketChecked(s);
            return false;
        }

        if (UDT::bind2(remoteSock, s) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot bind UDT socket: " << UDT::getlasterror().getErrorMessage());
            DTun::closeUDTSocketChecked(remoteSock);
            DTun::closeSysSocketChecked(s);
            return false;
        }

        remoteConnector_ = boost::make_shared<DTun::UDTConnector>(boost::ref(udtReactor_), remoteSock);

        if (!remoteConnector_->connect(DTun::ipToString(remoteIp), DTun::portToString(remotePort),
            boost::bind(&ProxySession::onRemoteConnect, this, _1), true)) {
            lock.unlock();
            remoteConnector_.reset();
            return false;
        }

        SYSSOCKET localSock = ::socket(AF_INET, SOCK_STREAM, 0);
        if (localSock == SYS_INVALID_SOCKET) {
            LOG4CPLUS_ERROR(logger(), "Cannot create TCP socket: " << strerror(errno));
            lock.unlock();
            remoteConnector_.reset();
            return false;
        }

        localConnector_ = boost::make_shared<DTun::TCPConnector>(boost::ref(tcpReactor_), localSock);

        if (!localConnector_->connect(DTun::ipToString(localIp), DTun::portToString(localPort),
            boost::bind(&ProxySession::onLocalConnect, this, _1))) {
            lock.unlock();
            localConnector_.reset();
            remoteConnector_.reset();
            return false;
        }

        callback_ = callback;

        return true;
    }

    void ProxySession::onLocalConnect(int err)
    {
        LOG4CPLUS_TRACE(logger(), "ProxySession::onLocalConnect(" << err << ")");

        SYSSOCKET sock = localConnector_->sock();
        localConnector_->close();

        boost::mutex::scoped_lock lock(m_);

        if (done_ || err) {
            DTun::closeSysSocketChecked(sock);
        }

        if (done_) {
            return;
        }

        if (err) {
            done_ = true;
            lock.unlock();
            callback_();
            return;
        }

        localConn_ = boost::make_shared<DTun::TCPConnection>(boost::ref(tcpReactor_), sock);

        if (remoteConn_ && !connected_) {
            connected_ = true;
            onBothConnected();
        }
    }

    void ProxySession::onLocalSend(int err, int numBytes)
    {
        LOG4CPLUS_TRACE(logger(), "ProxySession::onLocalSend(" << err << ", " << numBytes << ")");

        boost::mutex::scoped_lock lock(m_);
        if (done_) {
            return;
        }

        if (err) {
            done_ = true;
            lock.unlock();
            callback_();
            return;
        }

        bool fireRecv = false;

        if (localSndBuffBytes_ >= (int)localSndBuff_.capacity()) {
            assert(localSndBuffBytes_ == (int)localSndBuff_.capacity());
            fireRecv = true;
        }

        localSndBuffBytes_ -= numBytes;

        if (fireRecv) {
            recvRemote();
        }
    }

    void ProxySession::onLocalRecv(int err, int numBytes)
    {
        LOG4CPLUS_TRACE(logger(), "ProxySession::onLocalRecv(" << err << ", " << numBytes << ")");

        boost::mutex::scoped_lock lock(m_);
        if (done_) {
            return;
        }

        if (err) {
            done_ = true;
            lock.unlock();
            callback_();
            return;
        }

        if (numBytes > 0) {
            sendRemote(numBytes);
            recvLocal();
        } else {
            localShutdown_ = true;
        }
    }

    void ProxySession::onRemoteConnect(int err)
    {
        LOG4CPLUS_TRACE(logger(), "ProxySession::onRemoteConnect(" << err << ")");

        UDTSOCKET sock = remoteConnector_->sock();
        remoteConnector_->close();

        boost::mutex::scoped_lock lock(m_);

        if (done_ || err) {
            DTun::closeUDTSocketChecked(sock);
        }

        if (done_) {
            return;
        }

        if (err) {
            done_ = true;
            lock.unlock();
            callback_();
            return;
        }

        remoteConn_ = boost::make_shared<DTun::UDTConnection>(boost::ref(udtReactor_), sock);

        if (localConn_ && !connected_) {
            connected_ = true;
            onBothConnected();
        }
    }

    void ProxySession::onRemoteSend(int err, int numBytes)
    {
        LOG4CPLUS_TRACE(logger(), "ProxySession::onRemoteSend(" << err << ", " << numBytes << ")");

        boost::mutex::scoped_lock lock(m_);
        if (done_) {
            return;
        }

        if (err) {
            done_ = true;
            lock.unlock();
            callback_();
            return;
        }

        bool fireRecv = false;

        if (remoteSndBuffBytes_ >= (int)remoteSndBuff_.capacity()) {
            assert(remoteSndBuffBytes_ == (int)remoteSndBuff_.capacity());
            fireRecv = true;
        }

        remoteSndBuffBytes_ -= numBytes;

        if ((remoteSndBuffBytes_ <= 0) && localShutdown_) {
            assert(remoteSndBuffBytes_ == 0);
            done_ = true;
            lock.unlock();
            callback_();
            return;
        }

        if (fireRecv && !localShutdown_) {
            recvLocal();
        }
    }

    void ProxySession::onRemoteRecv(int err, int numBytes)
    {
        LOG4CPLUS_TRACE(logger(), "ProxySession::onRemoteRecv(" << err << ", " << numBytes << ")");

        boost::mutex::scoped_lock lock(m_);
        if (done_) {
            return;
        }

        if (err) {
            done_ = true;
            lock.unlock();
            callback_();
            return;
        }

        sendLocal(numBytes);
        recvRemote();
    }

    void ProxySession::onBothConnected()
    {
        LOG4CPLUS_TRACE(logger(), "ProxySession::onBothConnected()");

        recvLocal();
        recvRemote();
    }

    void ProxySession::recvLocal()
    {
        int toRecv = remoteSndBuff_.capacity() - remoteSndBuffBytes_;

        if (toRecv <= 0) {
            return;
        }

        localRcvBuff_.resize(toRecv);
        localConn_->read(&localRcvBuff_[0], &localRcvBuff_[0] + localRcvBuff_.size(),
            boost::bind(&ProxySession::onLocalRecv, this, _1, _2));
    }

    void ProxySession::sendLocal(int numBytes)
    {
        assert(numBytes <= (int)remoteRcvBuff_.size());
        assert(numBytes <= (int)(localSndBuff_.capacity() - localSndBuffBytes_));

        localSndBuff_.insert(localSndBuff_.end(), remoteRcvBuff_.begin(), remoteRcvBuff_.begin() + numBytes);
        localSndBuffBytes_ += numBytes;

        boost::circular_buffer<char>::const_array_range arr = localSndBuff_.array_one();
        localConn_->write(arr.first, arr.first + arr.second,
            boost::bind(&ProxySession::onLocalSend, this, _1, arr.second));
        arr = localSndBuff_.array_two();
        if (arr.second > 0) {
            localConn_->write(arr.first, arr.first + arr.second,
                boost::bind(&ProxySession::onLocalSend, this, _1, arr.second));
        }

        localSndBuff_.erase_begin(numBytes);
    }

    void ProxySession::recvRemote()
    {
        int toRecv = localSndBuff_.capacity() - localSndBuffBytes_;

        if (toRecv <= 0) {
            return;
        }

        remoteRcvBuff_.resize(toRecv);
        remoteConn_->read(&remoteRcvBuff_[0], &remoteRcvBuff_[0] + remoteRcvBuff_.size(),
            boost::bind(&ProxySession::onRemoteRecv, this, _1, _2),
            false);
    }

    void ProxySession::sendRemote(int numBytes)
    {
        assert(numBytes <= (int)localRcvBuff_.size());
        assert(numBytes <= (int)(remoteSndBuff_.capacity() - remoteSndBuffBytes_));

        remoteSndBuff_.insert(remoteSndBuff_.end(), localRcvBuff_.begin(), localRcvBuff_.begin() + numBytes);
        remoteSndBuffBytes_ += numBytes;

        boost::circular_buffer<char>::const_array_range arr = remoteSndBuff_.array_one();
        remoteConn_->write(arr.first, arr.first + arr.second,
            boost::bind(&ProxySession::onRemoteSend, this, _1, arr.second));
        arr = remoteSndBuff_.array_two();
        if (arr.second > 0) {
            remoteConn_->write(arr.first, arr.first + arr.second,
                boost::bind(&ProxySession::onRemoteSend, this, _1, arr.second));
        }

        remoteSndBuff_.erase_begin(numBytes);
    }
}

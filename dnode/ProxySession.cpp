#include "ProxySession.h"
#include "DTun/Utils.h"
#include "DTun/SConnector.h"
#include "DTun/SConnection.h"
#include "Logger.h"
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>

#define DTUN_PROXY_BUFF_SIZE (208 * 1024)
#define DTUN_PROXY_CHUNK_SIZE (4 * 1024)

namespace DNode
{
    ProxySession::ProxySession(DTun::SManager& remoteMgr, DTun::SManager& localMgr)
    : remoteMgr_(remoteMgr)
    , localMgr_(localMgr)
    , localSndBuff_(DTUN_PROXY_BUFF_SIZE)
    , localSndBuffBytes_(0)
    , remoteSndBuff_(DTUN_PROXY_BUFF_SIZE)
    , remoteSndBuffBytes_(0)
    , connected_(false)
    , done_(false)
    , localShutdown_(false)
    {
    }

    ProxySession::~ProxySession()
    {
    }

    bool ProxySession::start(const boost::shared_ptr<DTun::SHandle>& remoteHandle, DTun::UInt32 localIp, DTun::UInt16 localPort,
        DTun::UInt32 remoteIp, DTun::UInt16 remotePort, const DoneCallback& callback)
    {
        DTun::UInt32 ip;
        DTun::UInt16 port;
        remoteHandle->getSockName(ip, port);
        LOG4CPLUS_INFO(logger(), "LOCAL PORT = " << ntohs(port) << ", PEER = " << DTun::ipPortToString(remoteIp, remotePort));

        boost::mutex::scoped_lock lock(m_);

        remoteConnector_ = remoteHandle->createConnector();

        if (!remoteConnector_->connect(DTun::ipToString(remoteIp), DTun::portToString(remotePort),
            boost::bind(&ProxySession::onRemoteConnect, this, _1), DTun::SConnector::ModeRendezvousAcc)) {
            lock.unlock();
            remoteConnector_.reset();
            return false;
        }

        boost::shared_ptr<DTun::SHandle> localHandle = localMgr_.createStreamSocket();
        if (!localHandle) {
            lock.unlock();
            remoteConnector_.reset();
            return false;
        }

        localConnector_ = localHandle->createConnector();

        if (!localConnector_->connect(DTun::ipToString(localIp), DTun::portToString(localPort),
            boost::bind(&ProxySession::onLocalConnect, this, _1), DTun::SConnector::ModeNormal)) {
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

        boost::shared_ptr<DTun::SHandle> handle = localConnector_->handle();
        localConnector_->close();

        boost::mutex::scoped_lock lock(m_);

        if (done_ || err) {
            handle->close();
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

        localConn_ = handle->createConnection();

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

        boost::shared_ptr<DTun::SHandle> handle = remoteConnector_->handle();
        remoteConnector_->close();

        boost::mutex::scoped_lock lock(m_);

        if (done_ || err) {
            handle->close();
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

        remoteConn_ = handle->createConnection();

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

        boost::shared_ptr<std::vector<char> > sndBuff =
            boost::make_shared<std::vector<char> >(1);

        (*sndBuff)[0] = 0xE1;

        remoteConn_->write(&(*sndBuff)[0], &(*sndBuff)[0] + sndBuff->size(),
            boost::bind(&ProxySession::onHandshakeSend, this, _1, sndBuff));

        recvLocal();
        recvRemote();
    }

    void ProxySession::recvLocal()
    {
        int toRecv = std::min((int)(remoteSndBuff_.capacity() - remoteSndBuffBytes_), DTUN_PROXY_CHUNK_SIZE);

        if (toRecv <= 0) {
            return;
        }

        localRcvBuff_.resize(toRecv);
        localConn_->read(&localRcvBuff_[0], &localRcvBuff_[0] + localRcvBuff_.size(),
            boost::bind(&ProxySession::onLocalRecv, this, _1, _2), false);
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
        int toRecv = std::min((int)(localSndBuff_.capacity() - localSndBuffBytes_), DTUN_PROXY_CHUNK_SIZE);

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

    void ProxySession::onHandshakeSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff)
    {
        LOG4CPLUS_TRACE(logger(), "ProxySession::onHandshakeSend(" << err << ")");

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
    }
}

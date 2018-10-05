#include "DTun/UTPHandleImpl.h"
#include "DTun/UTPManager.h"
#include "DTun/Utils.h"
#include "Logger.h"
#include <boost/make_shared.hpp>

namespace DTun
{
    UTPHandleImpl::UTPHandleImpl(UTPManager& mgr)
    : mgr_(mgr)
    , utpSock_(NULL)
    , eof_(false)
    , rcvBuff_(DTUN_RCV_BUFF_SIZE)
    , localPort_(0)
    , waitDummy_(false)
    {
    }

    UTPHandleImpl::UTPHandleImpl(UTPManager& mgr,
        const boost::shared_ptr<SConnection>& conn, utp_socket* utpSock)
    : mgr_(mgr)
    , utpSock_(utpSock)
    , eof_(false)
    , rcvBuff_(DTUN_RCV_BUFF_SIZE)
    , conn_(conn)
    , waitDummy_(true)
    {
        UInt32 ip = 0;
        bool res = conn_->handle()->getSockName(ip, localPort_);
        assert(res);
    }

    UTPHandleImpl::~UTPHandleImpl()
    {
        assert(!utpSock_);
        assert(!conn_);
        assert(localPort_ == 0);
    }

    boost::shared_ptr<SConnection> UTPHandleImpl::kill(bool sameThreadOnly, bool abort, UInt16& localPort)
    {
        assert(!sameThreadOnly || mgr_.reactor().isSameThread());
        boost::shared_ptr<SConnection> res = conn_;
        localPort = localPort_;
        if (res) {
            assert(localPort);
        }
        conn_.reset();
        localPort_ = 0;
        if (utpSock_) {
            mgr_.unbind(utpSock_);
            utp_close(utpSock_);
            utpSock_ = NULL;
        }
        return res;
    }

    bool UTPHandleImpl::bind(SYSSOCKET s)
    {
        assert(!conn_);
        if (conn_) {
            return false;
        }

        conn_ = mgr_.createTransportConnection(s);
        if (conn_) {
            UInt32 ip;
            bool res = conn_->handle()->getSockName(ip, localPort_);
            assert(res);
        }
        return !!conn_;
    }

    bool UTPHandleImpl::bind(const struct sockaddr* name, int namelen)
    {
        assert(!conn_);
        if (conn_) {
            return false;
        }

        conn_ = mgr_.createTransportConnection(name, namelen);
        if (conn_) {
            UInt32 ip;
            bool res = conn_->handle()->getSockName(ip, localPort_);
            assert(res);
        }
        return !!conn_;
    }

    bool UTPHandleImpl::getSockName(UInt32& ip, UInt16& port) const
    {
        if (!conn_) {
            LOG4CPLUS_ERROR(logger(), "socket is not bound");
            return false;
        }

        return conn_->handle()->getSockName(ip, port);
    }

    bool UTPHandleImpl::getPeerName(UInt32& ip, UInt16& port) const
    {
        if (!utpSock_ || connectCallback_ || listenCallback_ || !conn_) {
            LOG4CPLUS_ERROR(logger(), "socket is not connected");
            return false;
        }

        struct sockaddr_in_utp addr;
        socklen_t addrLen = sizeof(addr);

        int res = utp_getpeername(utpSock_, (struct sockaddr*)&addr, &addrLen);
        assert(res == 0);

        ip = addr.sin_addr.s_addr;
        port = mgr_.getMappedPeerPort(localPort_, addr.sin_addr.s_addr, addr.sin_port);

        return true;
    }

    int UTPHandleImpl::getTTL() const
    {
        assert(conn_);
        if (!conn_) {
            return SYS_INVALID_SOCKET;
        }
        return conn_->handle()->getTTL();
    }

    bool UTPHandleImpl::setTTL(int ttl)
    {
        assert(conn_);
        if (!conn_) {
            return SYS_INVALID_SOCKET;
        }
        return conn_->handle()->setTTL(ttl);
    }

    SYSSOCKET UTPHandleImpl::duplicate()
    {
        assert(conn_);
        if (!conn_) {
            return SYS_INVALID_SOCKET;
        }
        return conn_->handle()->duplicate();
    }

    void UTPHandleImpl::listen(int backlog, const SAcceptor::ListenCallback& callback)
    {
        assert(mgr_.reactor().isSameThread());
        assert(conn_);
        assert(!utpSock_);

        UInt32 ip = 0;
        UInt16 port = 0;

        if (!conn_->handle()->getSockName(ip, port)) {
            return;
        }

        utpSock_ = mgr_.bindAcceptor(port, this);
        if (!utpSock_) {
            return;
        }

        listenCallback_ = callback;
    }

    void UTPHandleImpl::connect(const std::string& address, const std::string& port, const SConnector::ConnectCallback& callback)
    {
        assert(mgr_.reactor().isSameThread());
        assert(!utpSock_);

        if (!conn_) {
            struct sockaddr_in addr;

            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_ANY);

            conn_ = mgr_.createTransportConnection((struct sockaddr*)&addr, sizeof(addr));
            if (!conn_) {
                callback(1);
                return;
            }

            UInt32 ip;
            bool res = conn_->handle()->getSockName(ip, localPort_);
            assert(res);
        }

        addrinfo hints;

        memset(&hints, 0, sizeof(struct addrinfo));

        hints.ai_flags = AI_PASSIVE;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* res;

        if (::getaddrinfo(address.c_str(), port.c_str(), &hints, &res) != 0) {
            LOG4CPLUS_ERROR(logger(), "cannot resolve address/port");
            callback(1);
            return;
        }

        utpSock_ = mgr_.bindConnector(localPort_, this, ((const struct sockaddr_in*)res->ai_addr)->sin_addr.s_addr,
            ((const struct sockaddr_in*)res->ai_addr)->sin_port);

        freeaddrinfo(res);

        if (!utpSock_) {
            callback(1);
            return;
        }

        connectCallback_ = callback;
    }

    int UTPHandleImpl::write(const char* first, const char* last, int& numWritten)
    {
        assert(mgr_.reactor().isSameThread());

        numWritten = 0;

        if (!utpSock_) {
            return UTP_ECONNRESET;
        }

        if (waitDummy_) {
            return 0;
        }

        numWritten = utp_write(utpSock_, (void*)first, (last - first));
        assert(numWritten >= 0);

        return 0;
    }

    int UTPHandleImpl::read(char* first, char* last, int& numRead)
    {
        assert(mgr_.reactor().isSameThread());

        numRead = 0;

        if (!rcvBuff_.empty()) {
            int left = last - first;

            boost::circular_buffer<char>::const_array_range arr = rcvBuff_.array_one();

            int tmp = std::min(left, (int)arr.second);
            memcpy(first, arr.first, tmp);

            first += tmp;
            left -= tmp;
            numRead += tmp;

            arr = rcvBuff_.array_two();

            tmp = std::min(left, (int)arr.second);
            memcpy(first, arr.first, tmp);

            numRead += tmp;

            rcvBuff_.erase_begin(numRead);

            if (utpSock_) {
                utp_read_drained(utpSock_);
            }
        }

        int err = 0;

        if (eof_) {
            err = DTUN_ERR_CONN_CLOSED;
        } else if (!utpSock_) {
            err = 1;
        }

        return err;
    }

    void UTPHandleImpl::onError(int errCode)
    {
        if (utpSock_) {
            mgr_.unbind(utpSock_);
            utp_close(utpSock_);
            utpSock_ = NULL;
        }

        if (connectCallback_) {
            LOG4CPLUS_TRACE(logger(), "UTP onError(" << utp_error_code_names[errCode] << ")");
            SConnector::ConnectCallback cb = connectCallback_;
            connectCallback_ = SConnector::ConnectCallback();
            cb(errCode);
        } else {
            if (errCode == UTP_ETIMEDOUT) {
                LOG4CPLUS_ERROR(logger(), "UTP onError(" << utp_error_code_names[errCode] << ")");
            } else {
                LOG4CPLUS_TRACE(logger(), "UTP onError(" << utp_error_code_names[errCode] << ")");
            }

            if (writeCallback_) {
                writeCallback_(errCode, 0);
            }
            if (readCallback_) {
                readCallback_();
            }
        }
    }

    void UTPHandleImpl::onAccept(const boost::shared_ptr<SHandle>& handle)
    {
        LOG4CPLUS_TRACE(logger(), "UTP onAccept()");

        listenCallback_(handle);
    }

    void UTPHandleImpl::onConnect()
    {
        LOG4CPLUS_TRACE(logger(), "UTP onConnect()");

        char dummy = '\0';
        int res = utp_write(utpSock_, &dummy, 1);
        assert(res == 1);
    }

    void UTPHandleImpl::onWriteable()
    {
        LOG4CPLUS_TRACE(logger(), "UTP onWriteable()");

        if (writeCallback_) {
            writeCallback_(0, 0);
        }
    }

    void UTPHandleImpl::onSent(int numBytes)
    {
        LOG4CPLUS_TRACE(logger(), "UTP onSent(" << numBytes << ")");

        if (connectCallback_) {
            SConnector::ConnectCallback cb = connectCallback_;
            connectCallback_ = SConnector::ConnectCallback();
            cb(0);
            return;
        }

        if (writeCallback_) {
            writeCallback_(0, numBytes);
        }
    }

    void UTPHandleImpl::onRead(const char* data, int numBytes)
    {
        LOG4CPLUS_TRACE(logger(), "UTP onRead(" << numBytes << ")");

        if (numBytes > (int)(rcvBuff_.capacity() - rcvBuff_.size())) {
            LOG4CPLUS_FATAL(logger(), "too much data");
            return;
        }

        rcvBuff_.insert(rcvBuff_.end(), data, data + numBytes);

        if (waitDummy_) {
            rcvBuff_.erase_begin(1);
            waitDummy_ = false;
            if (writeCallback_) {
                writeCallback_(0, 0);
            }
        }

        if (readCallback_) {
            readCallback_();
        }
    }

    void UTPHandleImpl::onEOF()
    {
        LOG4CPLUS_TRACE(logger(), "UTP onEOF()");

        eof_ = true;

        if (readCallback_) {
            readCallback_();
        }
    }

    int UTPHandleImpl::getReadBufferSize() const
    {
        return rcvBuff_.size();
    }

    UInt16 UTPHandleImpl::getTransportPort() const
    {
        if (!conn_) {
            return 0;
        }

        assert(localPort_);

        return localPort_;
    }
}

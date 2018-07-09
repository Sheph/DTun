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
            SYS_CLOSE_SOCKET(s);
            return false;
        }

        if (UDT::bind2(remoteSock, s) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot bind UDT socket: " << UDT::getlasterror().getErrorMessage());
            UDT::close(remoteSock);
            SYS_CLOSE_SOCKET(s);
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
        LOG4CPLUS_INFO(logger(), "ProxySession::onLocalConnect(" << err << ")");
    }

    void ProxySession::onLocalSend(int err, int numBytes)
    {
    }

    void ProxySession::onLocalRecv(int err, int numBytes)
    {
    }

    void ProxySession::onRemoteConnect(int err)
    {
        LOG4CPLUS_INFO(logger(), "ProxySession::onRemoteConnect(" << err << ")");
    }

    void ProxySession::onRemoteSend(int err, int numBytes)
    {
    }

    void ProxySession::onRemoteRecv(int err, int numBytes)
    {
    }
}

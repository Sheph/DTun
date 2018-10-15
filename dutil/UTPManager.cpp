#include "DTun/UTPManager.h"
#include "DTun/UTPHandle.h"
#include "DTun/Utils.h"
#include "Logger.h"
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>
#include <boost/format.hpp>
#include <unistd.h>
#include <fcntl.h>

#define UTP_VERBOSE 1

namespace DTun
{
    static int readRandom(void* buffer, uint32_t num_bytes)
    {
        int fd = open("/dev/urandom", O_RDONLY);
        ssize_t total_rd = 0, rd = 0;

        if (fd == -1)
            return 0;

        while (total_rd < (ssize_t)num_bytes) {
            rd = read(fd, (char*)buffer + total_rd, num_bytes - total_rd);

            if (rd <= 0) {
                close(fd);
                return 0;
            }

            total_rd += rd;
        }

        close(fd);

        return 1;
    }

    struct UTPSocketUserData
    {
        UTPSocketUserData(UInt16 localPort, UTPHandleImpl* handle)
        : localPort(localPort)
        , handle(handle) {}

        ~UTPSocketUserData()
        {
            assert(!handle);
        }

        UInt16 localPort;
        UTPHandleImpl* handle;
    };

    UTPManager::ConnectionInfo::~ConnectionInfo()
    {
        assert(numHandles == 0);
        if (utpSocks.empty()) {
            for (PeerMap::const_iterator it = peers.begin(); it != peers.end(); ++it) {
                for (PortMap::const_iterator jt = it->second.portMap.begin(); jt != it->second.portMap.end(); ++jt) {
                    LOG4CPLUS_ERROR(logger(), "STALE ROUTE " << DTun::ipToString(it->first) << (boost::format(":%02x%02x") % (int)jt->first[0] % (int)jt->first[1]));
                }
            }
            assert(peers.empty());
        }
    }

    bool UTPManager::ConnectionInfo::removeUtpSock(utp_socket* utpSock)
    {
        if (utpSocks.erase(utpSock) != 1) {
            return false;
        }

        struct sockaddr_in_utp addr;
        socklen_t addrLen = sizeof(addr);

        int res = utp_getpeername(utpSock, (struct sockaddr*)&addr, &addrLen);
        assert(res == 0);

        PeerMap::iterator it = peers.find(addr.sin_addr.s_addr);
        assert(it != peers.end());

        UTPPort utpPort;
        memcpy(&utpPort[0], &addr.sin_port[0], sizeof(in_port_utp));

        PortMap::iterator pIt = it->second.portMap.find(utpPort);
        assert(pIt != it->second.portMap.end());

        it->second.portMap.erase(pIt);

        if (it->second.portMap.empty()) {
            peers.erase(it);
        }

        return true;
    }

    UTPManager::UTPManager(SManager& mgr)
    : innerMgr_(mgr)
    , ctx_(NULL)
    , numAliveHandles_(0)
    , inRecv_(false)
    {
    }

    UTPManager::~UTPManager()
    {
        if (!watch_) {
            return;
        }

        // stop watch, timers will no longer fire
        watch_->close();

        ConnectionCache connCache;

        {
            boost::mutex::scoped_lock lock(m_);
            connCache = connCache_;
        }

        // close all connections, i/o will no longer take place
        for (ConnectionCache::iterator it = connCache.begin(); it != connCache.end(); ++it) {
            boost::shared_ptr<SConnection> conn = it->second->conn.lock();
            assert(conn);
            conn->close();
        }

        connCache.clear();

        // no i/o, no timers, it's safe to kill handles even from another thread
        onKillHandles(false);

        utp_destroy(ctx_);

        connCache_.clear();
        assert(toKillHandles_.empty());
        assert(numAliveHandles_ == 0);
    }

    SReactor& UTPManager::reactor()
    {
        return innerMgr_.reactor();
    }

    bool UTPManager::start()
    {
        ctx_ = utp_init(2);
        assert(ctx_);

        utp_context_set_userdata(ctx_, this);

        utp_context_set_option(ctx_, UTP_RCVBUF, DTUN_RCV_BUFF_SIZE);
        utp_context_set_option(ctx_, UTP_SNDBUF, DTUN_SND_BUFF_SIZE);

        utp_set_callback(ctx_, UTP_LOG, &utpLogFunc);
        utp_set_callback(ctx_, UTP_SENDTO, &utpSendToFunc);
        utp_set_callback(ctx_, UTP_ON_ERROR, &utpOnErrorFunc);
        utp_set_callback(ctx_, UTP_ON_STATE_CHANGE, &utpOnStateChangeFunc);
        utp_set_callback(ctx_, UTP_ON_READ, &utpOnReadFunc);
        utp_set_callback(ctx_, UTP_ON_SENT, &utpOnSentFunc);
        utp_set_callback(ctx_, UTP_ON_FIREWALL, &utpOnFirewallFunc);
        utp_set_callback(ctx_, UTP_ON_ACCEPT, &utpOnAcceptFunc);
        utp_set_callback(ctx_, UTP_GET_READ_BUFFER_SIZE, &utpGetReadBufferSizeFunc);
#if UTP_VERBOSE
        utp_context_set_option(ctx_, UTP_LOG_NORMAL, 1);
        utp_context_set_option(ctx_, UTP_LOG_MTU, 1);
        utp_context_set_option(ctx_, UTP_LOG_DEBUG, 1);
#endif

        watch_ = boost::make_shared<OpWatch>(boost::ref(innerMgr_.reactor()));

        innerMgr_.reactor().post(
            watch_->wrap(boost::bind(&UTPManager::onUTPTimeout, this)), 500);

        return true;
    }

    boost::shared_ptr<SHandle> UTPManager::createStreamSocket()
    {
        {
            boost::mutex::scoped_lock lock(m_);
            ++numAliveHandles_;
        }
        return boost::make_shared<UTPHandle>(boost::ref(*this));
    }

    boost::shared_ptr<SHandle> UTPManager::createDatagramSocket(SYSSOCKET s)
    {
        return boost::shared_ptr<SHandle>();
    }

    void UTPManager::addToKill(const boost::shared_ptr<UTPHandleImpl>& handle, bool abort)
    {
        boost::mutex::scoped_lock lock(m_);
        --numAliveHandles_;
        bool res = toKillHandles_.insert(std::make_pair(handle, abort)).second;
        assert(res);
        if (!res) {
            LOG4CPLUS_FATAL(logger(), "double kill!");
        }
        lock.unlock();
        innerMgr_.reactor().post(
            watch_->wrap(boost::bind(&UTPManager::onKillHandles, this, true)));
    }

    boost::shared_ptr<SConnection> UTPManager::createTransportConnection(const struct sockaddr* name, int namelen)
    {
        return createTransportConnectionInternal(name, namelen, SYS_INVALID_SOCKET);
    }

    boost::shared_ptr<SConnection> UTPManager::createTransportConnection(SYSSOCKET s)
    {
        struct sockaddr_in addr;
        socklen_t addrLen = sizeof(addr);

        if (::getsockname(s, (struct sockaddr*)&addr, &addrLen) == SYS_SOCKET_ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot get sys sock name: " << strerror(errno));
            return boost::shared_ptr<SConnection>();
        }

        return createTransportConnectionInternal((const struct sockaddr*)&addr, addrLen, s);
    }

    UInt16 UTPManager::getMappedPeerPort(UInt16 localPort, UInt32 peerIp, const in_port_utp peerPort) const
    {
        boost::mutex::scoped_lock lock(m_);

        ConnectionCache::const_iterator it = connCache_.find(localPort);
        assert(it != connCache_.end());

        PeerMap::const_iterator jt = it->second->peers.find(peerIp);
        assert(jt != it->second->peers.end());

        UTPPort utpPort;
        memcpy(&utpPort[0], &peerPort[0], sizeof(in_port_utp));

        PortMap::const_iterator kt = jt->second.portMap.find(utpPort);
        assert(kt != jt->second.portMap.end());

        return kt->second.port;
    }

    bool UTPManager::haveTransportConnection(UInt16 port) const
    {
        boost::mutex::scoped_lock lock(m_);
        ConnectionCache::const_iterator it = connCache_.find(port);
        if (it == connCache_.end()) {
            return false;
        }
        assert(!it->second->conn.expired());
        return true;
    }

    utp_socket* UTPManager::bindAcceptor(UInt16 localPort, UTPHandleImpl* handle)
    {
        boost::mutex::scoped_lock lock(m_);
        ConnectionCache::iterator cIt = connCache_.find(localPort);
        assert(cIt != connCache_.end());
        assert(!cIt->second->acceptorHandle);

        if (cIt->second->acceptorHandle) {
            return NULL;
        }

        utp_socket* utpSock = utp_create_socket(ctx_);

        UTPSocketUserData* ud = new UTPSocketUserData(localPort, handle);

        utp_set_userdata(utpSock, ud);

        cIt->second->acceptorHandle = handle;
        bool inserted = cIt->second->utpSocks.insert(utpSock).second;
        assert(inserted);

        return utpSock;
    }

    utp_socket* UTPManager::bindConnector(UInt16 localPort, UTPHandleImpl* handle, UInt32 ip, UInt16 port)
    {
        struct sockaddr_in_utp addr;

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET_UTP;
        addr.sin_addr.s_addr = ip;

        if (!readRandom(&addr.sin_port[0], sizeof(in_port_utp))) {
            LOG4CPLUS_ERROR(logger(), "Cannot generate port");
            return NULL;
        }

        boost::mutex::scoped_lock lock(m_);
        ConnectionCache::iterator cIt = connCache_.find(localPort);
        assert(cIt != connCache_.end());

        utp_socket* utpSock = utp_create_socket(ctx_);

        UTPSocketUserData* ud = new UTPSocketUserData(localPort, handle);

        utp_set_userdata(utpSock, ud);

        bool inserted = cIt->second->utpSocks.insert(utpSock).second;
        assert(inserted);

        PeerInfo& peerInfo = cIt->second->peers[ip];

        UTPPort utpPort;
        memcpy(&utpPort[0], &addr.sin_port[0], sizeof(in_port_utp));

        peerInfo.portMap[utpPort].port = port;
        peerInfo.portMap[utpPort].active = true;

        lock.unlock();

        int res = utp_connect(utpSock, (const struct sockaddr*)&addr, sizeof(addr));
        assert(res == 0);

        return utpSock;
    }

    void UTPManager::unbind(utp_socket* utpSock)
    {
        UTPSocketUserData* ud = (UTPSocketUserData*)utp_get_userdata(utpSock);

        boost::mutex::scoped_lock lock(m_);
        ConnectionCache::iterator cIt = connCache_.find(ud->localPort);
        assert(cIt != connCache_.end());
        if (cIt->second->acceptorHandle == ud->handle) {
            ud->handle = NULL;
            cIt->second->acceptorHandle = NULL;
            int cnt = cIt->second->utpSocks.erase(utpSock);
            assert(cnt == 1);
            utp_set_userdata(utpSock, NULL);
            delete ud;
        } else {
            ud->handle = NULL;
        }
    }

    uint64 UTPManager::utpLogFunc(utp_callback_arguments* args)
    {
        LOG4CPLUS_DEBUG(logger(), args->buf);
        return 0;
    }

    uint64 UTPManager::utpSendToFunc(utp_callback_arguments* args)
    {
        UTPManager* this_ = (UTPManager*)utp_context_get_userdata(args->context);

        const struct sockaddr_in_utp* addr = (const struct sockaddr_in_utp*)args->address;

        UInt16 localPort = 0;

        if (args->socket) {
            UTPSocketUserData* ud = (UTPSocketUserData*)utp_get_userdata(args->socket);
            localPort = ud->localPort;
        } else {
            UTPPort in_port;
            memcpy(&in_port[0], &addr->sin_port[0],  sizeof(in_port_utp));

            boost::mutex::scoped_lock lock(this_->m_);
            for (ConnectionCache::iterator it = this_->connCache_.begin(); it != this_->connCache_.end(); ++it) {
                PeerMap::iterator pIt = it->second->peers.find(addr->sin_addr.s_addr);
                if (pIt == it->second->peers.end()) {
                    continue;
                }
                if (pIt->second.portMap.count(in_port) > 0) {
                    localPort = it->first;
                    break;
                }
            }

            if (!localPort) {
                LOG4CPLUS_TRACE(logger(), "No transport "
                    << "to=" << DTun::ipToString(addr->sin_addr.s_addr)
                    << (boost::format(":%02x%02x") % (int)addr->sin_port[0] % (int)addr->sin_port[1]));
                return 0;
            }
        }

        /*LOG4CPLUS_TRACE(logger(), "utpSendToFunc(" << args->len
            << ", from=" << ntohs(localPort)
            << ", to=" << DTun::ipToString(addr->sin_addr.s_addr)
            << (boost::format(":%02x%02x") % (int)addr->sin_port[0] % (int)addr->sin_port[1]) << ")");*/

        boost::shared_ptr<ConnectionInfo> connInfo;

        {
            boost::mutex::scoped_lock lock(this_->m_);
            ConnectionCache::const_iterator it = this_->connCache_.find(localPort);
            if (it != this_->connCache_.end()) {
                connInfo = it->second;
            }
        }

        if (!connInfo) {
            LOG4CPLUS_TRACE(logger(), "No transport from=" << ntohs(localPort)
                << ", to=" << DTun::ipToString(addr->sin_addr.s_addr)
                << (boost::format(":%02x%02x") % (int)addr->sin_port[0] % (int)addr->sin_port[1]));
            return 0;
        }

        boost::shared_ptr<SConnection> conn = connInfo->conn.lock();
        assert(conn);

        UInt16 actualPort = 0;

        PeerMap::const_iterator it = connInfo->peers.find(addr->sin_addr.s_addr);
        if (it != connInfo->peers.end()) {
            UTPPort utpPort;
            memcpy(&utpPort[0], &addr->sin_port[0], sizeof(in_port_utp));
            PortMap::const_iterator jt = it->second.portMap.find(utpPort);
            if (jt != it->second.portMap.end()) {
                actualPort = jt->second.port;
            }
        }

        assert(actualPort != 0);

        boost::shared_ptr<std::vector<char> > sndBuff =
            boost::make_shared<std::vector<char> >(args->len);

        memcpy(&(*sndBuff)[0], args->buf, args->len);
        memcpy(&(*sndBuff)[0], &addr->sin_port[0], sizeof(in_port_utp));

        conn->writeTo(&(*sndBuff)[0], &(*sndBuff)[0] + sndBuff->size(),
            addr->sin_addr.s_addr, actualPort,
            boost::bind(&UTPManager::onSend, this_, _1, sndBuff));

        return 0;
    }

    uint64 UTPManager::utpOnErrorFunc(utp_callback_arguments* args)
    {
        UTPSocketUserData* ud = (UTPSocketUserData*)utp_get_userdata(args->socket);

        if (ud->handle) {
            ud->handle->onError(args->error_code);
        }

        return 0;
    }

    uint64 UTPManager::utpOnStateChangeFunc(utp_callback_arguments* args)
    {
        LOG4CPLUS_TRACE(logger(), "utpOnStateChangeFunc(" << args->socket << ", " << utp_state_names[args->state] << ")");

        UTPManager* this_ = (UTPManager*)utp_context_get_userdata(args->context);

        UTPSocketUserData* ud = (UTPSocketUserData*)utp_get_userdata(args->socket);

        switch (args->state) {
        case UTP_STATE_CONNECT:
            if (ud->handle) {
                ud->handle->onConnect();
            }
            break;
        case UTP_STATE_WRITABLE:
            if (ud->handle) {
                ud->handle->onWriteable();
            }
            break;
        case UTP_STATE_EOF:
            if (ud->handle) {
                ud->handle->onEOF();
            }
            break;
        case UTP_STATE_DESTROYING: {
            assert(ud->localPort);

            boost::mutex::scoped_lock lock(this_->m_);
            ConnectionCache::iterator it = this_->connCache_.find(ud->localPort);

            if ((it != this_->connCache_.end()) && it->second->removeUtpSock(args->socket) &&
                (it->second->numHandles == 0) && it->second->utpSocks.empty()) {
                boost::shared_ptr<SConnection> conn = it->second->conn.lock();
                assert(conn);
                this_->connCache_.erase(it);
                lock.unlock();
                conn->close();
            }

            utp_set_userdata(args->socket, NULL);

            delete ud;
            break;
        }
        default:
            assert(0);
            break;
        }

        return 0;
    }

    uint64 UTPManager::utpOnReadFunc(utp_callback_arguments* args)
    {
        UTPSocketUserData* ud = (UTPSocketUserData*)utp_get_userdata(args->socket);

        if (ud->handle) {
            ud->handle->onRead((const char*)args->buf, args->len);
        }

        return 0;
    }

    uint64 UTPManager::utpOnSentFunc(utp_callback_arguments* args)
    {
        UTPSocketUserData* ud = (UTPSocketUserData*)utp_get_userdata(args->socket);

        if (ud->handle) {
            ud->handle->onSent(args->len);
        }

        return 0;
    }

    uint64 UTPManager::utpOnFirewallFunc(utp_callback_arguments* args)
    {
        UTPManager* this_ = (UTPManager*)utp_context_get_userdata(args->context);

        const struct sockaddr_in_utp* addr = (const struct sockaddr_in_utp*)args->address;

        UTPPort in_port;
        memcpy(&in_port[0], &addr->sin_port[0],  sizeof(in_port_utp));

        boost::mutex::scoped_lock lock(this_->m_);
        for (ConnectionCache::iterator it = this_->connCache_.begin(); it != this_->connCache_.end(); ++it) {
            PeerMap::iterator pIt = it->second->peers.find(addr->sin_addr.s_addr);
            if (pIt == it->second->peers.end()) {
                continue;
            }
            PortMap::iterator pmIt = pIt->second.portMap.find(in_port);
            if (pmIt != pIt->second.portMap.end()) {
                if (!it->second->acceptorHandle) {
                    return 1;
                } else {
                    UTPSocketUserData* ud = new UTPSocketUserData(it->first, NULL);
                    utp_set_userdata(args->socket, ud);
                    pmIt->second.active = true;
                    return 0;
                }
            }
        }

        assert(0);

        return 1;
    }

    uint64 UTPManager::utpOnAcceptFunc(utp_callback_arguments* args)
    {
        UTPManager* this_ = (UTPManager*)utp_context_get_userdata(args->context);

        UTPSocketUserData* ud = (UTPSocketUserData*)utp_get_userdata(args->socket);
        assert(!ud->handle);

        boost::shared_ptr<SConnection> conn;
        UTPHandleImpl* acceptorHandle = NULL;

        {
            boost::mutex::scoped_lock lock(this_->m_);
            ConnectionCache::const_iterator it = this_->connCache_.find(ud->localPort);
            assert(it != this_->connCache_.end());
            conn = it->second->conn.lock();
            assert(conn);
            acceptorHandle = it->second->acceptorHandle;
            assert(acceptorHandle);
            ++it->second->numHandles;
            bool inserted = it->second->utpSocks.insert(args->socket).second;
            assert(inserted);
            ++this_->numAliveHandles_;
        }

        boost::shared_ptr<UTPHandle> handle = boost::make_shared<UTPHandle>(boost::ref(*this_), conn, args->socket);

        ud->handle = handle->impl().get();

        acceptorHandle->onAccept(handle);

        return 0;
    }

    uint64 UTPManager::utpGetReadBufferSizeFunc(utp_callback_arguments* args)
    {
        UTPSocketUserData* ud = (UTPSocketUserData*)utp_get_userdata(args->socket);
        if (ud->handle)
            return ud->handle->getReadBufferSize();
        else
            return 0;
    }

    void UTPManager::onRecv(int err, int numBytes, UInt32 srcIp, UInt16 srcPort,
        UInt16 dstPort, const boost::shared_ptr<ConnectionInfo>& connInfo,
        const boost::shared_ptr<std::vector<char> >& rcvBuff)
    {
        boost::shared_ptr<SConnection> conn_shared = connInfo->conn.lock();
        assert(conn_shared);

        if (numBytes < (int)sizeof(in_port_utp)) {
            //LOG4CPLUS_TRACE(logger(), "UTPManager::onRecv(" << err << ", " << numBytes << ", src=" << ipPortToString(srcIp, srcPort) << ", dst=" << portToString(dstPort) << ")");
        }

        if (err) {
            LOG4CPLUS_ERROR(logger(), "UTPManager::onRecv error!");
            return;
        }

        if (numBytes >= (int)sizeof(in_port_utp)) {
            UTPPort in_port;
            memcpy(&in_port[0], &(*rcvBuff)[0], sizeof(in_port_utp));

            //LOG4CPLUS_TRACE(logger(), "UTPManager::onRecv(" << err << ", " << numBytes << ", src=" << ipPortToString(srcIp, srcPort) << (boost::format(":%02x%02x") % (int)in_port[0] % (int)in_port[1]) << ", dst=" << portToString(dstPort) << ")");

            {
                boost::mutex::scoped_lock lock(m_);
                PeerInfo& peerInfo = connInfo->peers[srcIp];
                PortMap::iterator it = peerInfo.portMap.find(in_port);
                if (it == peerInfo.portMap.end()) {
                    peerInfo.portMap.insert(std::make_pair(in_port, PortInfo(srcPort)));
                } else {
                    if (it->second.port != srcPort) {
                        LOG4CPLUS_WARN(logger(), "Port " << ntohs(it->second.port) << " remapped to " << ntohs(srcPort) << " at " << ipToString(srcIp));
                    }
                    it->second.port = srcPort;
                }
            }

            struct sockaddr_in_utp addr;

            addr.sin_family = AF_INET_UTP;
            addr.sin_addr.s_addr = srcIp;
            memcpy(&addr.sin_port[0], &(*rcvBuff)[0], sizeof(in_port_utp));

            inRecv_ = true;
            if (!utp_process_udp(ctx_, (const byte*)(&(*rcvBuff)[0]), numBytes,
                (const struct sockaddr *)&addr, sizeof(addr))) {
                inRecv_ = false;
                LOG4CPLUS_WARN(logger(), "UDP packet not handled by UTP. Ignoring.");
            }
            inRecv_ = false;

            boost::mutex::scoped_lock lock(m_);
            PeerInfo& peerInfo = connInfo->peers[srcIp];
            PortMap::iterator it = peerInfo.portMap.find(in_port);
            if ((it != peerInfo.portMap.end()) && !it->second.active) {
                peerInfo.portMap.erase(it);
                if (peerInfo.portMap.empty()) {
                    connInfo->peers.erase(srcIp);
                }
            }
        } else if (numBytes == 4) {
            uint8_t a = (*rcvBuff)[0];
            uint8_t b = (*rcvBuff)[1];
            uint8_t c = (*rcvBuff)[2];
            uint8_t d = (*rcvBuff)[3];
            if ((a == 0xAA) && (b == 0xBB) && (c == 0xCC) && ((d == 0xDD) || (d == 0xEE))) {
                LOG4CPLUS_TRACE(logger(), "UTPManager::onRecv support ping");
            } else {
                LOG4CPLUS_WARN(logger(), "UTPManager::onRecv bad support ping: " << (int)a << "," << (int)b << "," << (int)c << "," << (int)d);
            }
        } else if (numBytes == 0) {
            // drain
            for (std::set<utp_socket*>::iterator it = connInfo->utpSocks.begin(); it != connInfo->utpSocks.end(); ++it) {
                utp_socket_issue_deferred_acks(*it);
            }
        } else {
            LOG4CPLUS_WARN(logger(), "UTPManager::onRecv too short " << numBytes);
        }

        conn_shared->readFrom(&(*rcvBuff)[0], &(*rcvBuff)[0] + rcvBuff->size(),
            boost::bind(&UTPManager::onRecv, this, _1, _2, _3, _4, dstPort, connInfo, rcvBuff), true);
    }

    void UTPManager::onSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff)
    {
        if (err) {
            LOG4CPLUS_ERROR(logger(), "UTPManager::onSend error!");
        }
    }

    void UTPManager::onUTPTimeout()
    {
        utp_check_timeouts(ctx_);

        innerMgr_.reactor().post(
            watch_->wrap(boost::bind(&UTPManager::onUTPTimeout, this)), 500);
    }

    void UTPManager::onKillHandles(bool sameThreadOnly)
    {
        LOG4CPLUS_TRACE(logger(), "onKillHandles(" << sameThreadOnly << ")");

        HandleMap toKillHandles;

        {
            boost::mutex::scoped_lock lock(m_);
            toKillHandles.swap(toKillHandles_);
        }

        for (HandleMap::iterator it = toKillHandles.begin(); it != toKillHandles.end(); ++it) {
            UInt16 localPort = 0;
            boost::shared_ptr<SConnection> conn = it->first->kill(sameThreadOnly, it->second, localPort);
            if (conn) {
                removeTransportConnectionInternal(localPort);
            }
            if (sameThreadOnly && conn) {
                // Unfortunately we can't use 0 timeout even with abort, transport needs to be alive for some time to send stuff,
                // but we can at least make timeout smaller...
                innerMgr_.reactor().post(
                    watch_->wrap(boost::bind(&UTPManager::onTransportConnectionKill, this, conn, localPort)), (it->second ? 250 : 1000));
            } else if (conn) {
                onTransportConnectionKill(conn, localPort);
            }
        }
    }

    void UTPManager::onTransportConnectionKill(const boost::shared_ptr<SConnection>& conn, UInt16 localPort)
    {
        //LOG4CPLUS_TRACE(logger(), "onTransportConnectionKill(" << conn.get() << ")");

        boost::mutex::scoped_lock lock(m_);
        ConnectionCache::iterator it = connCache_.find(localPort);
        if ((it == connCache_.end()) || (it->second->numHandles > 0)) {
            return;
        }

        boost::shared_ptr<SConnection> conn2 = it->second->conn.lock();
        assert(conn2);

        if ((conn2 == conn) && (conn.use_count() == 2)) {
            connCache_.erase(it);
        }
    }

    boost::shared_ptr<SConnection> UTPManager::createTransportConnectionInternal(const struct sockaddr* name, int namelen, SYSSOCKET s)
    {
        assert(name->sa_family == AF_INET);
        if (name->sa_family != AF_INET) {
            return boost::shared_ptr<SConnection>();
        }

        const struct sockaddr_in* name4 = (const struct sockaddr_in*)name;

        if (name4->sin_addr.s_addr != htonl(INADDR_ANY)) {
            LOG4CPLUS_ERROR(logger(), "Binding to something other than INADDR_ANY is not supported");
            return boost::shared_ptr<SConnection>();
        }

        bool isAny = (name4->sin_port == 0);

        boost::shared_ptr<SConnection> res;

        boost::mutex::scoped_lock lock(m_);
        if (!isAny) {
            ConnectionCache::iterator it = connCache_.find(name4->sin_port);
            if (it != connCache_.end()) {
                ++it->second->numHandles;
                res = it->second->conn.lock();
                assert(res);
            }
        }

        if (!res) {
            boost::shared_ptr<SHandle> handle = innerMgr_.createDatagramSocket(s);
            if (!handle) {
                return res;
            }

            UInt16 port = name4->sin_port;

            if (s == SYS_INVALID_SOCKET) {
                if (!handle->bind(name, namelen)) {
                    return res;
                }

                UInt32 ip = 0;
                if (isAny && !handle->getSockName(ip, port)) {
                    return res;
                }
            }

            res = handle->createConnection();

            boost::shared_ptr<ConnectionInfo> connInfo = boost::make_shared<ConnectionInfo>(res);
            bool inserted = connCache_.insert(std::make_pair(port, connInfo)).second;
            assert(inserted);
            if (!inserted) {
                LOG4CPLUS_FATAL(logger(), "double port, WTF???");
                return boost::shared_ptr<SConnection>();
            }

            boost::shared_ptr<std::vector<char> > rcvBuff =
                boost::make_shared<std::vector<char> >(8192);
            res->readFrom(&(*rcvBuff)[0], &(*rcvBuff)[0] + rcvBuff->size(),
                boost::bind(&UTPManager::onRecv, this, _1, _2, _3, _4, port, connInfo, rcvBuff), true);
        } else {
            if (s != SYS_INVALID_SOCKET) {
                closeSysSocketChecked(s);
            }
        }

        return res;
    }

    void UTPManager::removeTransportConnectionInternal(UInt16 localPort)
    {
        boost::mutex::scoped_lock lock(m_);
        ConnectionCache::const_iterator it = connCache_.find(localPort);
        assert(it != connCache_.end());
        assert(it->second->numHandles > 0);
        --it->second->numHandles;
    }
}

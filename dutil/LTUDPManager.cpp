#include "DTun/LTUDPManager.h"
#include "DTun/LTUDPHandle.h"
#include "DTun/Utils.h"
#include "Logger.h"
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>
#include <lwip/init.h>
#include <lwip/priv/tcp_priv.h>
#include <lwip/ip4_frag.h>
#include <lwip/inet_chksum.h>

namespace DTun
{
    LTUDPManager::LTUDPManager(SManager& mgr)
    : innerMgr_(mgr)
    , numAliveHandles_(0)
    , tcpTimerMod4_(0)
    {
    }

    LTUDPManager::~LTUDPManager()
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
            if (conn) {
                conn->close();
            }
        }

        // no i/o, no timers, it's safe to kill handles even from another thread
        onKillHandles(false);
        connCache.clear();
        connCache_.clear();
        assert(toKillHandles_.empty());
        assert(numAliveHandles_ == 0);

        netif_remove(&netif_);
    }

    SReactor& LTUDPManager::reactor()
    {
        return innerMgr_.reactor();
    }

    bool LTUDPManager::start()
    {
        lwip_init();

        ip4_addr_t addr;
        stringToIp("1.1.1.1", addr.addr);
        ip4_addr_t netmask;
        stringToIp("255.255.255.0", netmask.addr);
        ip4_addr_t gw;
        ip4_addr_set_any(&gw);

        if (!netif_add(&netif_, &addr, &netmask, &gw, this, netifInitFunc, netifInputFunc)) {
            LOG4CPLUS_ERROR(logger(), "Cannot add lwip netif");
            return false;
        }

        netif_set_up(&netif_);

        netif_set_link_up(&netif_);

        netif_set_default(&netif_);

        netif_.flags |= NETIF_FLAG_TCP_NORST;

        watch_ = boost::make_shared<OpWatch>(boost::ref(innerMgr_.reactor()));

        innerMgr_.reactor().post(
            watch_->wrap(boost::bind(&LTUDPManager::onTcpTimeout, this)), TCP_TMR_INTERVAL);

        return true;
    }

    void LTUDPManager::addToKill(const boost::shared_ptr<LTUDPHandleImpl>& handle, bool abort)
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
            watch_->wrap(boost::bind(&LTUDPManager::onKillHandles, this, true)));
    }

    boost::shared_ptr<SConnection> LTUDPManager::createTransportConnection(const struct sockaddr* name, int namelen)
    {
        return createTransportConnectionInternal(name, namelen, SYS_INVALID_SOCKET);
    }

    boost::shared_ptr<SConnection> LTUDPManager::createTransportConnection(SYSSOCKET s)
    {
        struct sockaddr_in addr;
        socklen_t addrLen = sizeof(addr);

        if (::getsockname(s, (struct sockaddr*)&addr, &addrLen) == SYS_SOCKET_ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot get sys sock name: " << strerror(errno));
            return boost::shared_ptr<SConnection>();
        }

        return createTransportConnectionInternal((const struct sockaddr*)&addr, addrLen, s);
    }

    boost::shared_ptr<SHandle> LTUDPManager::createStreamSocket(const boost::shared_ptr<SConnection>& conn,
        struct tcp_pcb* pcb)
    {
        {
            boost::mutex::scoped_lock lock(m_);
            ++numAliveHandles_;

            ip_addr_t tcp_addr;
            uint16_t tcp_port = 0;
            err_t err = tcp_tcp_get_tcp_addrinfo(pcb, 1, &tcp_addr, &tcp_port);
            assert(err == ERR_OK);
            tcp_port = lwip_htons(tcp_port);

            PortMap::const_iterator pmIt = localPortMap_.find(tcp_port);
            assert(pmIt != localPortMap_.end());

            ConnectionCache::iterator cIt = connCache_.find(pmIt->second.port);
            assert(cIt != connCache_.end());

            assert(cIt->second->isAcceptor);
            assert(cIt->second->numHandles > 0);
            ++cIt->second->numHandles;
        }

        return boost::make_shared<LTUDPHandle>(boost::ref(*this), conn, pcb);
    }

    bool LTUDPManager::haveTransportConnection(UInt16 port) const
    {
        boost::mutex::scoped_lock lock(m_);
        ConnectionCache::const_iterator it = connCache_.find(port);
        if (it == connCache_.end()) {
            return false;
        }
        return !it->second->conn.expired();
    }

    UInt16 LTUDPManager::getMappedPeerPort(UInt16 port, UInt32 peerIp, UInt16 peerPort) const
    {
        boost::mutex::scoped_lock lock(m_);
        ConnectionCache::const_iterator it = connCache_.find(port);
        if (it == connCache_.end()) {
            return peerPort;
        }

        PeerMap::const_iterator jt = it->second->peers.find(peerIp);
        if (jt != it->second->peers.end()) {
            PortMap::const_iterator kt = jt->second.portMap.find(peerPort);
            if (kt != jt->second.portMap.end()) {
                return kt->second.port;
            }
        }

        return peerPort;
    }

    uint16_t LTUDPManager::pcbBindAcceptor(struct tcp_pcb* pcb, uint16_t port)
    {
        boost::mutex::scoped_lock lock(m_);
        ConnectionCache::iterator cIt = connCache_.find(port);
        assert(cIt != connCache_.end());
        assert(cIt->second->isAcceptor || (cIt->second->numHandles == 0));

        uint16_t tcpPort = 0;

        if (cIt->second->numHandles > 0) {
            assert(cIt->second->acceptorLocalPort);
            tcpPort = lwip_ntohs(cIt->second->acceptorLocalPort);
        }

        if (tcp_bind(pcb, IP_ANY_TYPE, tcpPort) != ERR_OK) {
            LOG4CPLUS_ERROR(logger(), "tcp_bind failed");
            return 0;
        }

        ip_addr_t tcpAddr;
        err_t err = tcp_tcp_get_tcp_addrinfo(pcb, 1, &tcpAddr, &tcpPort);
        assert(err == ERR_OK);
        tcpPort = lwip_htons(tcpPort);
        assert(tcpPort != 0);

        localPortMap_[tcpPort].port = port;

        if (!cIt->second->isAcceptor)
            cIt->second->peers.clear();

        cIt->second->acceptorLocalPort = tcpPort;
        cIt->second->isAcceptor = true;
        ++cIt->second->numHandles;

        return tcpPort;
    }

    uint16_t LTUDPManager::pcbBindConnector(struct tcp_pcb* pcb, uint16_t port)
    {
        boost::mutex::scoped_lock lock(m_);
        ConnectionCache::iterator cIt = connCache_.find(port);
        assert(cIt != connCache_.end());
        assert(!cIt->second->isAcceptor || (cIt->second->numHandles == 0));

        if (tcp_bind(pcb, IP_ANY_TYPE, 0) != ERR_OK) {
            LOG4CPLUS_ERROR(logger(), "tcp_bind failed");
            return 0;
        }

        ip_addr_t tcpAddr;
        uint16_t tcpPort = 0;
        err_t err = tcp_tcp_get_tcp_addrinfo(pcb, 1, &tcpAddr, &tcpPort);
        assert(err == ERR_OK);
        tcpPort = lwip_htons(tcpPort);
        assert(tcpPort != 0);

        localPortMap_[tcpPort].port = port;

        if (cIt->second->isAcceptor)
            cIt->second->peers.clear();

        cIt->second->acceptorLocalPort = 0;
        cIt->second->isAcceptor = false;
        ++cIt->second->numHandles;

        return tcpPort;
    }

    uint16_t LTUDPManager::pcbConnect(uint16_t pcbLocalPort, uint32_t ip, uint16_t port)
    {
        boost::mutex::scoped_lock lock(m_);
        assert(pcbLocalPort != 0);
        assert(ip != 0);
        assert(port != 0);

        PortMap::const_iterator pIt = localPortMap_.find(pcbLocalPort);
        assert(pIt != localPortMap_.end());

        ConnectionCache::iterator cIt = connCache_.find(pIt->second.port);
        assert(cIt != connCache_.end());

        assert(!cIt->second->isAcceptor);

        PeerInfo& peerInfo = cIt->second->peers[ip];
        PortInfo* portInfo = NULL;

        uint16_t portToUse = htons(0x400);

        for (PortMap::iterator it = peerInfo.portMap.begin(); it != peerInfo.portMap.end(); ++it) {
            if (it->second.port == port) {
                portInfo = &it->second;
                portToUse = it->first;
                break;
            } else if (it->first == portToUse) {
                portToUse = htons(ntohs(portToUse) + 1);
            }
        }

        if (!portInfo) {
            if (portToUse == 0) {
                // portToUse == 0, uint16_t wrapped around, all busy.
                LOG4CPLUS_ERROR(logger(), "no free out port to use!");
                return 0;
            }

            portInfo = &peerInfo.portMap[portToUse];
            portInfo->port = port;
        }

        if (portInfo->numActiveConns++ == 0) {
            for (PortMap::iterator it = peerInfo.revPortMap.begin(); it != peerInfo.revPortMap.end(); ++it) {
                if (it->second.port == portToUse) {
                    peerInfo.revPortMap.erase(it);
                    break;
                }
            }
        }

        return portToUse;
    }

    void LTUDPManager::pcbDisconnect(uint16_t pcbLocalPort, uint32_t ip, uint16_t pcbRemotePort)
    {
        boost::mutex::scoped_lock lock(m_);
        assert(pcbLocalPort != 0);
        assert(ip != 0);
        assert(pcbRemotePort != 0);

        PortMap::const_iterator pIt = localPortMap_.find(pcbLocalPort);
        assert(pIt != localPortMap_.end());

        ConnectionCache::iterator cIt = connCache_.find(pIt->second.port);
        assert(cIt != connCache_.end());

        assert(!cIt->second->isAcceptor);

        PeerMap::iterator peerIt = cIt->second->peers.find(ip);
        assert(peerIt != cIt->second->peers.end());

        PeerInfo& peerInfo = peerIt->second;

        PortMap::iterator it = peerInfo.portMap.find(pcbRemotePort);
        assert(it != peerInfo.portMap.end());

        --it->second.numActiveConns;
        assert(it->second.numActiveConns >= 0);
    }

    void LTUDPManager::pcbUnbind(uint16_t pcbPort)
    {
        boost::mutex::scoped_lock lock(m_);
        assert(pcbPort != 0);

        PortMap::const_iterator pIt = localPortMap_.find(pcbPort);
        assert(pIt != localPortMap_.end());

        ConnectionCache::iterator cIt = connCache_.find(pIt->second.port);
        assert(cIt != connCache_.end());

        --cIt->second->numHandles;
        assert(cIt->second->numHandles >= 0);
    }

    boost::shared_ptr<SHandle> LTUDPManager::createStreamSocket()
    {
        {
            boost::mutex::scoped_lock lock(m_);
            ++numAliveHandles_;
        }
        return boost::make_shared<LTUDPHandle>(boost::ref(*this));
    }

    boost::shared_ptr<SHandle> LTUDPManager::createDatagramSocket(SYSSOCKET s)
    {
        assert(false);
        return boost::shared_ptr<SHandle>();
    }

    err_t LTUDPManager::netifInitFunc(struct netif* netif)
    {
        netif->name[0] = 'l';
        netif->name[1] = 'u';
        netif->output = netifOutputFunc;
        netif->output_ip6 = NULL;
        // MTU = 1500 - tcp_hdr.
        netif->mtu = 1500 - sizeof(struct tcp_hdr);

        return ERR_OK;
    }

    err_t LTUDPManager::netifInputFunc(struct pbuf* p, struct netif* netif)
    {
        uint8_t ip_version = 0;
        if (p->len > 0) {
            ip_version = (((uint8_t *)p->payload)[0] >> 4);
        }

        if (ip_version == 4) {
            return ip_input(p, netif);
        }

        LOG4CPLUS_ERROR(logger(), "IPv6 packet in ltudp!");

        pbuf_free(p);
        return ERR_OK;
    }

    err_t LTUDPManager::netifOutputFunc(struct netif* netif, struct pbuf* p, const ip4_addr_t* ipaddr)
    {
        LTUDPManager* this_ = (LTUDPManager*)netif->state;

        this_->tmpBuff_.resize(p->tot_len);

        pbuf_copy_partial(p, &this_->tmpBuff_[0], p->tot_len, 0);

        const struct ip_hdr* iphdr = (const struct ip_hdr*)&this_->tmpBuff_[0];
        uint16_t iphdrLen = IPH_HL(iphdr) * 4;

        assert((IPH_OFFSET(iphdr) & PP_HTONS(IP_OFFMASK | IP_MF)) == 0); // ensure no fragmentation
        assert(IPH_PROTO(iphdr) == IP_PROTO_TCP);
        assert(iphdrLen == sizeof(*iphdr));
        assert(iphdr->_tos == 0);
        assert(iphdr->_offset == 0);
        assert(iphdr->_ttl == 255);

        const struct tcp_hdr* tcphdr = (const struct tcp_hdr*)(&this_->tmpBuff_[0] + iphdrLen);

        /*LOG4CPLUS_TRACE(logger(), "LTUDPManager netifOutput(" << p->len
            << ", from=" << DTun::ipPortToString(iphdr->src.addr, tcphdr->src)
            << ", to=" << DTun::ipPortToString(iphdr->dest.addr, tcphdr->dest) << ")");*/

        PortMap::const_iterator pmIt = this_->localPortMap_.find(tcphdr->src);
        if (pmIt == this_->localPortMap_.end()) {
            LOG4CPLUS_TRACE(logger(), "No transport"
                << " from=" << DTun::ipPortToString(iphdr->src.addr, tcphdr->src)
                << " to=" << DTun::ipPortToString(iphdr->dest.addr, tcphdr->dest));
            return ERR_OK;
        }

        boost::shared_ptr<ConnectionInfo> connInfo = this_->getConnectionInfo(pmIt->second.port);
        if (!connInfo) {
            LOG4CPLUS_TRACE(logger(), "No transport"
                << " from=" << DTun::ipPortToString(iphdr->src.addr, tcphdr->src)
                << " to=" << DTun::ipPortToString(iphdr->dest.addr, tcphdr->dest));
            return ERR_OK;
        }

        boost::shared_ptr<SConnection> conn = connInfo->conn.lock();
        if (!conn) {
            LOG4CPLUS_TRACE(logger(), "No transport"
                << " from=" << DTun::ipPortToString(iphdr->src.addr, tcphdr->src)
                << " to=" << DTun::ipPortToString(iphdr->dest.addr, tcphdr->dest));
            return ERR_OK;
        }

        UInt16 actualPort = 0;

        PeerMap::const_iterator it = connInfo->peers.find(iphdr->dest.addr);
        if (it != connInfo->peers.end()) {
            PortMap::const_iterator jt = it->second.portMap.find(tcphdr->dest);
            if (jt != it->second.portMap.end()) {
                actualPort = jt->second.port;
            }
        }

        assert(actualPort != 0);

        boost::shared_ptr<std::vector<char> > sndBuff =
            boost::make_shared<std::vector<char> >(p->tot_len - iphdrLen);

        memcpy(&(*sndBuff)[0], (const char*)tcphdr, sndBuff->size());

        conn->writeTo(&(*sndBuff)[0], &(*sndBuff)[0] + sndBuff->size(),
            iphdr->dest.addr, actualPort,
            boost::bind(&LTUDPManager::onSend, this_, _1, sndBuff));

        return ERR_OK;
    }

    void LTUDPManager::onRecv(int err, int numBytes, UInt32 srcIp, UInt16 srcPort,
        UInt16 dstPort, const boost::shared_ptr<ConnectionInfo>& connInfo,
        const boost::shared_ptr<std::vector<char> >& rcvBuff)
    {
        boost::shared_ptr<SConnection> conn_shared = connInfo->conn.lock();
        if (!conn_shared) {
            return;
        }

        //LOG4CPLUS_TRACE(logger(), "LTUDPManager::onRecv(" << err << ", " << numBytes << ", src=" << ipPortToString(srcIp, srcPort) << ", dst=" << portToString(dstPort) << ")");

        if (err) {
            LOG4CPLUS_ERROR(logger(), "LTUDPManager::onRecv error!");
            return;
        }

        if (numBytes >= (int)sizeof(struct tcp_hdr)) {
            struct pbuf* p = pbuf_alloc(PBUF_RAW, numBytes + sizeof(struct ip_hdr), PBUF_POOL);

            if (p) {
                struct ip_hdr* iphdr = (struct ip_hdr*)&(*rcvBuff)[0];
                struct tcp_hdr* tcphdr = (struct tcp_hdr*)(iphdr + 1);

                IPH_VHL_SET(iphdr, 4, sizeof(struct ip_hdr) / 4);
                IPH_TOS_SET(iphdr, 0);
                IPH_LEN_SET(iphdr, lwip_htons(p->tot_len));
                IPH_OFFSET_SET(iphdr, 0);
                IPH_ID_SET(iphdr, 0);
                IPH_TTL_SET(iphdr, 255);
                IPH_PROTO_SET(iphdr, IP_PROTO_TCP);
                iphdr->src.addr = srcIp;
                iphdr->dest.addr = ip_addr_get_ip4_u32(&netif_.ip_addr);
                IPH_CHKSUM_SET(iphdr, 0);
                IPH_CHKSUM_SET(iphdr, inet_chksum(iphdr, sizeof(struct ip_hdr)));

                if (connInfo->isAcceptor) {
                    boost::mutex::scoped_lock lock(m_);
                    PeerInfo& peerInfo = connInfo->peers[srcIp];
                    PortMap::iterator it = peerInfo.portMap.find(tcphdr->src);
                    if (it == peerInfo.portMap.end()) {
                        peerInfo.portMap.insert(std::make_pair((UInt16)tcphdr->src, PortInfo(srcPort)));
                    } else {
                        if (it->second.port != srcPort) {
                            LOG4CPLUS_WARN(logger(), "Port " << ntohs(it->second.port) << " remapped to " << ntohs(srcPort) << " at " << ipToString(srcIp));
                        }
                        it->second.port = srcPort;
                    }
                    tcphdr->dest = connInfo->acceptorLocalPort;
                } else {
                    boost::mutex::scoped_lock lock(m_);
                    PeerMap::iterator peerIt = connInfo->peers.find(srcIp);
                    if (peerIt == connInfo->peers.end()) {
                        LOG4CPLUS_WARN(logger(), "Peer " << ipToString(srcIp) << ", in data without out data");
                        conn_shared->readFrom(&(*rcvBuff)[0] + sizeof(struct ip_hdr), &(*rcvBuff)[0] + rcvBuff->size() - sizeof(struct ip_hdr),
                            boost::bind(&LTUDPManager::onRecv, this, _1, _2, _3, _4, dstPort, connInfo, rcvBuff));
                        pbuf_free(p);
                        return;
                    }
                    PeerInfo& peerInfo = peerIt->second;
                    PortMap::iterator rpIt = peerInfo.revPortMap.find((UInt16)tcphdr->src);
                    if (rpIt == peerInfo.revPortMap.end()) {
                        bool found = false;
                        for (PortMap::iterator it = peerInfo.portMap.begin(); it != peerInfo.portMap.end(); ++it) {
                            if (it->second.port == srcPort) {
                                rpIt = peerInfo.revPortMap.insert(std::make_pair((UInt16)tcphdr->src, PortInfo(it->first))).first;
                                found = true;
                                break;
                            }
                        }
                        if (!found) {
                            LOG4CPLUS_WARN(logger(), "Peer " << ipPortToString(srcIp, srcPort) << ", in data without out data");
                            conn_shared->readFrom(&(*rcvBuff)[0] + sizeof(struct ip_hdr), &(*rcvBuff)[0] + rcvBuff->size() - sizeof(struct ip_hdr),
                                boost::bind(&LTUDPManager::onRecv, this, _1, _2, _3, _4, dstPort, connInfo, rcvBuff));
                            pbuf_free(p);
                            return;
                        }
                    }

                    PortMap::iterator it = peerInfo.portMap.find(rpIt->second.port);
                    assert(it != peerInfo.portMap.end());

                    if (it->second.port != srcPort) {
                        LOG4CPLUS_WARN(logger(), "Port " << ntohs(it->second.port) << " remapped to " << ntohs(srcPort) << " at " << ipToString(srcIp));
                        for (PortMap::iterator tmpIt = peerInfo.portMap.begin(); tmpIt != peerInfo.portMap.end(); ++tmpIt) {
                            if (tmpIt->second.port == srcPort) {
                                UInt16 tmpPort = tmpIt->first;
                                peerInfo.portMap.erase(tmpIt);
                                for (tmpIt = peerInfo.revPortMap.begin(); tmpIt != peerInfo.revPortMap.end(); ++tmpIt) {
                                    if (tmpIt->second.port == tmpPort) {
                                        peerInfo.revPortMap.erase(tmpIt);
                                        break;
                                    }
                                }
                                break;
                            }
                        }
                        it->second.port = srcPort;
                    }

                    tcphdr->src = it->first;
                }

                tcphdr->chksum = 0;

                pbuf_take(p, &(*rcvBuff)[0], numBytes + sizeof(struct ip_hdr));

                ip_addr_t srcAddr, dstAddr;
                ip_addr_set_ip4_u32(&srcAddr, iphdr->src.addr);
                ip_addr_set_ip4_u32(&dstAddr, iphdr->dest.addr);

                pbuf_header(p, -(s16_t)sizeof(struct ip_hdr));
                uint16_t chksum = ip_chksum_pseudo(p, IP_PROTO_TCP, p->tot_len, &srcAddr, &dstAddr);
                tcphdr = (struct tcp_hdr*)p->payload;
                tcphdr->chksum = chksum;
                pbuf_header(p, sizeof(struct ip_hdr));

                if (netif_.input(p, &netif_) != ERR_OK) {
                    LOG4CPLUS_ERROR(logger(), "netif.input failed");
                    pbuf_free(p);
                }
            } else {
                LOG4CPLUS_ERROR(logger(), "pbuf_alloc failed");
            }
        } else if (numBytes == 4) {
            uint8_t a = (*rcvBuff)[sizeof(struct ip_hdr) + 0];
            uint8_t b = (*rcvBuff)[sizeof(struct ip_hdr) + 1];
            uint8_t c = (*rcvBuff)[sizeof(struct ip_hdr) + 2];
            uint8_t d = (*rcvBuff)[sizeof(struct ip_hdr) + 3];
            if ((a == 0xAA) && (b == 0xBB) && (c == 0xCC) && ((d == 0xDD) || (d == 0xEE))) {
                LOG4CPLUS_TRACE(logger(), "LTUDPManager::onRecv support ping");
            } else {
                LOG4CPLUS_WARN(logger(), "LTUDPManager::onRecv bad support ping: " << (int)a << "," << (int)b << "," << (int)c << "," << (int)d);
            }
        } else {
            LOG4CPLUS_WARN(logger(), "LTUDPManager::onRecv too short " << numBytes);
        }

        conn_shared->readFrom(&(*rcvBuff)[0] + sizeof(struct ip_hdr), &(*rcvBuff)[0] + rcvBuff->size() - sizeof(struct ip_hdr),
            boost::bind(&LTUDPManager::onRecv, this, _1, _2, _3, _4, dstPort, connInfo, rcvBuff));
    }

    void LTUDPManager::onSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff)
    {
        if (err) {
            LOG4CPLUS_ERROR(logger(), "LTUDPManager::onSend error!");
        }
    }

    void LTUDPManager::onTcpTimeout()
    {
        //LOG4CPLUS_TRACE(logger(), "onTcpTimeout()");

        innerMgr_.reactor().post(
            watch_->wrap(boost::bind(&LTUDPManager::onTcpTimeout, this)), TCP_TMR_INTERVAL);

        // Call the TCP timer function (every 1/4 second)
        tcp_tmr();

        // Increment tcp_timer_mod4
        tcpTimerMod4_ = (tcpTimerMod4_ + 1) % 4;

        // Every second, call other timer functions
        if (tcpTimerMod4_ == 0) {
#if IP_REASSEMBLY
            assert(IP_TMR_INTERVAL == 4 * TCP_TMR_INTERVAL);
            ip_reass_tmr();
#endif
            // also do other stuff
            reapConnCache();
        }
    }

    void LTUDPManager::onKillHandles(bool sameThreadOnly)
    {
        LOG4CPLUS_TRACE(logger(), "onKillHandles(" << sameThreadOnly << ")");

        HandleMap toKillHandles;

        {
            boost::mutex::scoped_lock lock(m_);
            toKillHandles.swap(toKillHandles_);
        }

        for (HandleMap::iterator it = toKillHandles.begin(); it != toKillHandles.end(); ++it) {
            boost::shared_ptr<SConnection> conn = it->first->kill(sameThreadOnly, it->second);
            if (sameThreadOnly && conn) {
                // Unfortunately we can't use 0 timeout even with abort, transport needs to be alive for some time to send stuff,
                // but we can at least make timeout smaller...
                innerMgr_.reactor().post(
                    watch_->wrap(boost::bind(&LTUDPManager::onTransportConnectionKill, this, conn)), (it->second ? 250 : 1000));
            }
        }
    }

    void LTUDPManager::onTransportConnectionKill(const boost::shared_ptr<SConnection>& conn)
    {
        //LOG4CPLUS_TRACE(logger(), "onTransportConnectionKill(" << conn.get() << ")");
    }

    void LTUDPManager::reapConnCache()
    {
        boost::mutex::scoped_lock lock(m_);
        for (ConnectionCache::iterator it = connCache_.begin(); it != connCache_.end();) {
            if (it->second->conn.lock()) {
                ++it;
            } else {
                connCache_.erase(it++);
            }
        }
    }

    boost::shared_ptr<LTUDPManager::ConnectionInfo> LTUDPManager::getConnectionInfo(UInt16 port)
    {
        boost::mutex::scoped_lock lock(m_);
        ConnectionCache::const_iterator it = connCache_.find(port);
        if (it == connCache_.end()) {
            return boost::shared_ptr<ConnectionInfo>();
        }
        return it->second;
    }

    boost::shared_ptr<SConnection> LTUDPManager::createTransportConnectionInternal(const struct sockaddr* name, int namelen, SYSSOCKET s)
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
                res = it->second->conn.lock();
                if (!res) {
                    connCache_.erase(it);
                }
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
            if (isAny) {
                ConnectionCache::iterator it = connCache_.find(port);
                if (it != connCache_.end()) {
                    assert(!it->second->conn.lock());
                    connCache_.erase(it);
                }
            }
            bool inserted = connCache_.insert(std::make_pair(port, connInfo)).second;
            assert(inserted);
            if (!inserted) {
                LOG4CPLUS_FATAL(logger(), "double port, WTF???");
                return boost::shared_ptr<SConnection>();
            }

            boost::shared_ptr<std::vector<char> > rcvBuff =
                boost::make_shared<std::vector<char> >(8192);
            res->readFrom(&(*rcvBuff)[0] + sizeof(struct ip_hdr), &(*rcvBuff)[0] + rcvBuff->size() - sizeof(struct ip_hdr),
                boost::bind(&LTUDPManager::onRecv, this, _1, _2, _3, _4, port, connInfo, rcvBuff));
        } else {
            if (s != SYS_INVALID_SOCKET) {
                closeSysSocketChecked(s);
            }
        }

        return res;
    }
}

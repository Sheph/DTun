#include "DTun/LTUDPManager.h"
#include "DTun/LTUDPHandle.h"
#include "DTun/Utils.h"
#include "Logger.h"
#include <boost/make_shared.hpp>
#include <boost/bind.hpp>
#include <lwip/init.h>
#include <lwip/priv/tcp_priv.h>
#include <lwip/ip4_frag.h>

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

        watch_->close();
        toKillHandles_.clear();
        reapConnCache();
        assert(connCache_.empty());
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
        stringToIp("255.255.255.255", addr.addr);
        ip4_addr_t netmask;
        stringToIp("255.255.255.255", netmask.addr);
        ip4_addr_t gw;
        ip4_addr_set_any(&gw);

        if (!netif_add(&netif_, &addr, &netmask, &gw, this, netifInitFunc, netifInputFunc)) {
            LOG4CPLUS_ERROR(logger(), "Cannot add lwip netif");
            return false;
        }

        netif_set_up(&netif_);

        netif_set_link_up(&netif_);

        netif_set_pretend_tcp(&netif_, 1);

        watch_ = boost::make_shared<OpWatch>();

        innerMgr_.reactor().post(
            watch_->wrap(boost::bind(&LTUDPManager::onTcpTimeout, this)), TCP_TMR_INTERVAL);

        return true;
    }

    void LTUDPManager::addToKill(const boost::shared_ptr<LTUDPHandleImpl>& handle)
    {
        boost::mutex::scoped_lock lock(m_);
        --numAliveHandles_;
        bool res = toKillHandles_.insert(handle).second;
        assert(res);
        if (!res) {
            LOG4CPLUS_FATAL(logger(), "double kill!");
        }
    }

    boost::shared_ptr<SConnection> LTUDPManager::createTransportConnection(const struct sockaddr* name, int namelen)
    {
        assert(name->sa_family == AF_INET);
        if (name->sa_family != AF_INET) {
            return boost::shared_ptr<SConnection>();
        }

        const struct sockaddr_in* name4 = (const struct sockaddr_in*)name;

        std::pair<UInt32, UInt16> key = std::make_pair(name4->sin_addr.s_addr, name4->sin_port);
        boost::shared_ptr<SConnection> res;

        boost::mutex::scoped_lock lock(m_);
        ConnectionCache::iterator it = connCache_.find(key);
        if (it != connCache_.end()) {
            res = it->second.lock();
            if (!res) {
                connCache_.erase(it);
            }
        }

        if (!res) {
            boost::shared_ptr<SHandle> handle = innerMgr_.createDatagramSocket();
            if (!handle) {
                return res;
            }
            if (!handle->bind(name, namelen)) {
                return res;
            }
            res = handle->createConnection();
            connCache_.insert(std::make_pair(key, res));

            boost::shared_ptr<std::vector<char> > rcvBuff =
                boost::make_shared<std::vector<char> >(8192);
            res->read(&(*rcvBuff)[0], &(*rcvBuff)[0] + rcvBuff->size(),
                boost::bind(&LTUDPManager::onRecv, this, _1, _2, boost::weak_ptr<SConnection>(res), rcvBuff), false);
        }

        return res;
    }

    boost::shared_ptr<SHandle> LTUDPManager::createStreamSocket()
    {
        boost::mutex::scoped_lock lock(m_);
        ++numAliveHandles_;
        return boost::make_shared<LTUDPHandle>(boost::ref(*this));
    }

    boost::shared_ptr<SHandle> LTUDPManager::createDatagramSocket()
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
        LOG4CPLUS_TRACE(logger(), "LTUDPManager netifOutput(" << p->len << ")");

        return ERR_OK;
    }

    void LTUDPManager::onRecv(int err, int numBytes,
        const boost::weak_ptr<SConnection>& conn,
        const boost::shared_ptr<std::vector<char> >& rcvBuff)
    {
        boost::shared_ptr<SConnection> conn_shared = conn.lock();
        if (!conn_shared) {
            return;
        }

        LOG4CPLUS_TRACE(logger(), "LTUDPManager::onRecv(" << err << ", " << numBytes << ")");

        if (err) {
            LOG4CPLUS_ERROR(logger(), "LTUDPManager::onRecv error!");
            return;
        }

        struct pbuf* p = pbuf_alloc(PBUF_RAW, numBytes, PBUF_POOL);

        if (p) {
            pbuf_take(p, &(*rcvBuff)[0], numBytes);

            if (netif_.input(p, &netif_) != ERR_OK) {
                LOG4CPLUS_ERROR(logger(), "netif.input failed");
                pbuf_free(p);
            }
        } else {
            LOG4CPLUS_ERROR(logger(), "pbuf_alloc failed");
        }

        conn_shared->read(&(*rcvBuff)[0], &(*rcvBuff)[0] + rcvBuff->size(),
            boost::bind(&LTUDPManager::onRecv, this, _1, _2, conn, rcvBuff), false);
    }

    void LTUDPManager::onTcpTimeout()
    {
        LOG4CPLUS_TRACE(logger(), "onTcpTimeout()");

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
        }
    }

    void LTUDPManager::reapConnCache()
    {
        boost::mutex::scoped_lock lock(m_);
        for (ConnectionCache::iterator it = connCache_.begin(); it != connCache_.end();) {
            if (it->second.lock()) {
                ++it;
            } else {
                connCache_.erase(it++);
            }
        }
    }
}

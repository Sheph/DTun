#include "DTun/LTUDPHandleImpl.h"
#include "DTun/LTUDPManager.h"
#include "Logger.h"

namespace DTun
{
    LTUDPHandleImpl::LTUDPHandleImpl(LTUDPManager& mgr)
    : mgr_(mgr)
    , pcb_(NULL)
    {
    }

    LTUDPHandleImpl::~LTUDPHandleImpl()
    {
        assert(!pcb_);
        assert(!conn_);
    }

    void LTUDPHandleImpl::kill(bool sameThreadOnly)
    {
        assert(!sameThreadOnly || mgr_.reactor().isSameThread());
        if (conn_) {
            conn_->close();
            conn_.reset();
        }
        if (pcb_) {
            tcp_close(pcb_);
            pcb_ = NULL;
        }
    }

    bool LTUDPHandleImpl::bind(const struct sockaddr* name, int namelen)
    {
        assert(!conn_);
        if (conn_) {
            return false;
        }

        conn_ = mgr_.createTransportConnection(name, namelen);
        return !!conn_;
    }

    void LTUDPHandleImpl::listen(int backlog, const ListenCallback& callback)
    {
        assert(mgr_.reactor().isSameThread());

        struct tcp_pcb* l = tcp_new_ip_type(IPADDR_TYPE_V4);
        if (!l) {
            LOG4CPLUS_ERROR(logger(), "tcp_new_ip_type failed");
            return;
        }

        if (tcp_bind_to_netif(l, "lu0") != ERR_OK) {
            LOG4CPLUS_ERROR(logger(), "tcp_bind_to_netif failed");
            tcp_close(l);
            return;
        }

        // ensure the listener only accepts connections from this netif
        tcp_bind_netif(l, &mgr_.netif());

        if (!(pcb_ = tcp_listen(l))) {
            LOG4CPLUS_ERROR(logger(), "tcp_listen failed");
            tcp_close(l);
            return;
        }

        tcp_arg(pcb_, this);
        tcp_accept(pcb_, &LTUDPHandleImpl::listenerAcceptFunc);

        listenCallback_ = callback;
    }

    void LTUDPHandleImpl::connect(const std::string& address, const std::string& port, const ConnectCallback& callback, bool rendezvous)
    {
        assert(mgr_.reactor().isSameThread());

        connectCallback_ = callback;
    }

    err_t LTUDPHandleImpl::listenerAcceptFunc(void* arg, struct tcp_pcb* newpcb, err_t err)
    {
        LOG4CPLUS_ERROR(logger(), "LTUDP accept");

        return ERR_VAL;
    }
}

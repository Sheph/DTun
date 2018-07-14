#include "DTun/LTUDPHandleImpl.h"
#include "DTun/LTUDPManager.h"
#include "DTun/Utils.h"
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
        conn_.reset();
        if (pcb_) {
            tcp_arg(pcb_, NULL);
            if (!listenCallback_) {
                tcp_err(pcb_, NULL);
            }
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

    void LTUDPHandleImpl::listen(int backlog, const SAcceptor::ListenCallback& callback)
    {
        assert(mgr_.reactor().isSameThread());
        assert(conn_);
        assert(!pcb_);

        UInt32 ip = 0;
        UInt16 port = 0;

        if (!conn_->handle()->getSockName(ip, port)) {
            return;
        }

        port = lwip_ntohs(port);

        struct tcp_pcb* l = tcp_new_ip_type(IPADDR_TYPE_V4);
        if (!l) {
            LOG4CPLUS_ERROR(logger(), "tcp_new_ip_type failed");
            return;
        }

        if (tcp_bind(l, IP_ANY_TYPE, port) != ERR_OK) {
            LOG4CPLUS_ERROR(logger(), "tcp_bind failed");
            tcp_close(l);
            return;
        }

        if (!(pcb_ = tcp_listen_with_backlog(l, backlog))) {
            LOG4CPLUS_ERROR(logger(), "tcp_listen failed");
            tcp_close(l);
            return;
        }

        tcp_arg(pcb_, this);
        tcp_accept(pcb_, &LTUDPHandleImpl::listenerAcceptFunc);

        listenCallback_ = callback;
    }

    void LTUDPHandleImpl::connect(const std::string& address, const std::string& port, const SConnector::ConnectCallback& callback, bool rendezvous)
    {
        assert(mgr_.reactor().isSameThread());
        assert(!pcb_);

        if (!conn_) {
            struct sockaddr_in addr;

            memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            addr.sin_addr.s_addr = htonl(INADDR_ANY);

            conn_ = mgr_.createTransportConnection((struct sockaddr*)&addr, sizeof(addr));
            if (!conn_) {
                callback(ERR_IF);
                return;
            }
        }

        UInt32 localIp = 0;
        UInt16 localPort = 0;

        if (!conn_->handle()->getSockName(localIp, localPort)) {
            callback(ERR_IF);
            return;
        }

        localPort = lwip_ntohs(localPort);

        struct tcp_pcb* pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
        if (!pcb) {
            LOG4CPLUS_ERROR(logger(), "tcp_new_ip_type failed");
            callback(ERR_MEM);
            return;
        }

        err_t err = tcp_bind(pcb, IP_ANY_TYPE, localPort);
        if (err != ERR_OK) {
            LOG4CPLUS_ERROR(logger(), "tcp_bind failed");
            tcp_close(pcb);
            callback(err);
            return;
        }

        addrinfo hints;

        memset(&hints, 0, sizeof(struct addrinfo));

        hints.ai_flags = AI_PASSIVE;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* res;

        if (::getaddrinfo(address.c_str(), port.c_str(), &hints, &res) != 0) {
            LOG4CPLUS_ERROR(logger(), "cannot resolve address/port");
            tcp_close(pcb);
            callback(ERR_IF);
            return;
        }

        ip_addr_t addr;
        ip_addr_set_ip4_u32(&addr, ((const struct sockaddr_in*)res->ai_addr)->sin_addr.s_addr);

        tcp_arg(pcb, this);
        tcp_err(pcb, &LTUDPHandleImpl::errorFunc);
        err = tcp_connect(pcb, &addr, lwip_ntohs(((const struct sockaddr_in*)res->ai_addr)->sin_port), &LTUDPHandleImpl::connectFunc);

        freeaddrinfo(res);

        if (err != ERR_OK) {
            LOG4CPLUS_ERROR(logger(), "tcp_connect failed");
            tcp_close(pcb);
            callback(err);
            return;
        }

        pcb_ = pcb;
        connectCallback_ = callback;
    }

    err_t LTUDPHandleImpl::listenerAcceptFunc(void* arg, struct tcp_pcb* newpcb, err_t err)
    {
        LOG4CPLUS_ERROR(logger(), "LTUDP accept(" << (int)err << ")");

        return ERR_VAL;
    }

    err_t LTUDPHandleImpl::connectFunc(void* arg, struct tcp_pcb* pcb, err_t err)
    {
        LOG4CPLUS_ERROR(logger(), "LTUDP connect(" << (int)err << ")");

        return ERR_OK;
    }

    void LTUDPHandleImpl::errorFunc(void* arg, err_t err)
    {
        LOG4CPLUS_ERROR(logger(), "LTUDP error(" << (int)err << ")");

        LTUDPHandleImpl* this_ = (LTUDPHandleImpl*)arg;

        this_->pcb_ = NULL;

        if (this_->connectCallback_) {
            SConnector::ConnectCallback cb = this_->connectCallback_;
            this_->connectCallback_ = SConnector::ConnectCallback();
            cb(err);
        }
    }
}

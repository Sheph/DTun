#ifndef _DTUN_LTUDPHANDLEIMPL_H_
#define _DTUN_LTUDPHANDLEIMPL_H_

#include "DTun/Types.h"
#include "DTun/SConnection.h"
#include "DTun/SConnector.h"
#include "DTun/SAcceptor.h"
#include <boost/noncopyable.hpp>
#include <lwip/tcp.h>

namespace DTun
{
    class LTUDPManager;

    class DTUN_API LTUDPHandleImpl : boost::noncopyable
    {
    public:
        explicit LTUDPHandleImpl(LTUDPManager& mgr);
        ~LTUDPHandleImpl();

        void kill(bool sameThreadOnly);

        inline LTUDPManager& mgr() { return mgr_; }

        bool bind(const struct sockaddr* name, int namelen);

        void listen(int backlog, const SAcceptor::ListenCallback& callback);

        void connect(const std::string& address, const std::string& port, const SConnector::ConnectCallback& callback, bool rendezvous);

    private:
        static err_t listenerAcceptFunc(void* arg, struct tcp_pcb* newpcb, err_t err);

        static err_t connectFunc(void* arg, struct tcp_pcb* pcb, err_t err);

        static void errorFunc(void* arg, err_t err);

        LTUDPManager& mgr_;
        struct tcp_pcb* pcb_;
        SAcceptor::ListenCallback listenCallback_;
        SConnector::ConnectCallback connectCallback_;
        boost::shared_ptr<SConnection> conn_;
    };
}

#endif

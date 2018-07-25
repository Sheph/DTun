#ifndef _DTUN_LTUDPHANDLEIMPL_H_
#define _DTUN_LTUDPHANDLEIMPL_H_

// See dnode/ProxySession.h
#define BOOST_CB_DISABLE_DEBUG

#include "DTun/Types.h"
#include "DTun/SConnection.h"
#include "DTun/SConnector.h"
#include "DTun/SAcceptor.h"
#include <boost/noncopyable.hpp>
#include <boost/circular_buffer.hpp>
#include <lwip/tcp.h>
#include <vector>

namespace DTun
{
    class LTUDPManager;

    class DTUN_API LTUDPHandleImpl : boost::noncopyable
    {
    public:
        typedef boost::function<void(int, int)> WriteCallback;
        typedef boost::function<void()> ReadCallback;

        explicit LTUDPHandleImpl(LTUDPManager& mgr);
        LTUDPHandleImpl(LTUDPManager& mgr,
            const boost::shared_ptr<SConnection>& conn, struct tcp_pcb* pcb);
        ~LTUDPHandleImpl();

        boost::shared_ptr<SConnection> kill(bool sameThreadOnly, bool abort);

        inline LTUDPManager& mgr() { return mgr_; }

        inline void setWriteCallback(const WriteCallback& cb) { writeCallback_ = cb; }
        inline void setReadCallback(const ReadCallback& cb) { readCallback_ = cb; }

        bool bind(SYSSOCKET s);

        bool bind(const struct sockaddr* name, int namelen);

        bool getSockName(UInt32& ip, UInt16& port) const;

        bool getPeerName(UInt32& ip, UInt16& port) const;

        SYSSOCKET duplicate();

        void listen(int backlog, const SAcceptor::ListenCallback& callback);

        void connect(const std::string& address, const std::string& port, const SConnector::ConnectCallback& callback);

        int write(const char* first, const char* last, int& numWritten);

        int read(char* first, char* last, int& numRead);

        UInt16 getTransportPort() const;

    private:
        static err_t listenerAcceptFunc(void* arg, struct tcp_pcb* newpcb, err_t err);

        static err_t connectFunc(void* arg, struct tcp_pcb* pcb, err_t err);

        static err_t recvFunc(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err);

        static err_t sentFunc(void* arg, struct tcp_pcb* pcb, u16_t len);

        static void errorFunc(void* arg, err_t err);

        void setupPCB(struct tcp_pcb* pcb);

        LTUDPManager& mgr_;
        struct tcp_pcb* pcb_;
        bool eof_;
        boost::circular_buffer<char> rcvBuff_;
        SAcceptor::ListenCallback listenCallback_;
        SConnector::ConnectCallback connectCallback_;
        WriteCallback writeCallback_;
        ReadCallback readCallback_;
        boost::shared_ptr<SConnection> conn_;
    };
}

#undef BOOST_CB_DISABLE_DEBUG

#endif

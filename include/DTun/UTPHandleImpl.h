#ifndef _DTUN_UTPHANDLEIMPL_H_
#define _DTUN_UTPHANDLEIMPL_H_

// See dnode/ProxySession.h
#define BOOST_CB_DISABLE_DEBUG

#include "DTun/Types.h"
#include "DTun/SConnection.h"
#include "DTun/SConnector.h"
#include "DTun/SAcceptor.h"
#include <boost/noncopyable.hpp>
#include <boost/circular_buffer.hpp>
#include "utp.h"

#define DTUN_RCV_BUFF_SIZE (208 * 1024)
#define DTUN_SND_BUFF_SIZE (208 * 1024)

namespace DTun
{
    class UTPManager;

    class DTUN_API UTPHandleImpl : boost::noncopyable
    {
    public:
        typedef boost::function<void(int, int)> WriteCallback;
        typedef boost::function<void()> ReadCallback;

        explicit UTPHandleImpl(UTPManager& mgr);
        UTPHandleImpl(UTPManager& mgr,
            const boost::shared_ptr<SConnection>& conn, utp_socket* utpSock);
        ~UTPHandleImpl();

        boost::shared_ptr<SConnection> kill(bool sameThreadOnly, bool abort);

        inline UTPManager& mgr() { return mgr_; }

        inline void setWriteCallback(const WriteCallback& cb) { writeCallback_ = cb; }
        inline void setReadCallback(const ReadCallback& cb) { readCallback_ = cb; }

        bool bind(SYSSOCKET s);

        bool bind(const struct sockaddr* name, int namelen);

        bool getSockName(UInt32& ip, UInt16& port) const;

        bool getPeerName(UInt32& ip, UInt16& port) const;

        int getTTL() const;

        bool setTTL(int ttl);

        SYSSOCKET duplicate();

        void listen(int backlog, const SAcceptor::ListenCallback& callback);

        void connect(const std::string& address, const std::string& port, const SConnector::ConnectCallback& callback);

        int write(const char* first, const char* last, int& numWritten);

        int read(char* first, char* last, int& numRead);

        void onError(int errCode);

        void onAccept(const boost::shared_ptr<SHandle>& handle);

        void onConnect();

        void onWriteable();

        void onSent(int numBytes);

        void onRead(const char* data, int numBytes);

        void onEOF();

        int getReadBufferSize() const;

        UInt16 getTransportPort() const;

    private:
        UTPManager& mgr_;
        utp_socket* utpSock_;
        bool eof_;
        boost::circular_buffer<char> rcvBuff_;
        SAcceptor::ListenCallback listenCallback_;
        SConnector::ConnectCallback connectCallback_;
        WriteCallback writeCallback_;
        ReadCallback readCallback_;
        boost::shared_ptr<SConnection> conn_;
        UInt16 localPort_;
        bool waitDummy_;
    };
}

#undef BOOST_CB_DISABLE_DEBUG

#endif

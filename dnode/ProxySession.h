#ifndef _PROXYSESSION_H_
#define _PROXYSESSION_H_

#include "DTun/Types.h"
#include "DTun/UDTReactor.h"
#include "DTun/UDTConnector.h"
#include "DTun/UDTConnection.h"
#include "DTun/TCPReactor.h"
#include "DTun/TCPConnector.h"
#include "DTun/TCPConnection.h"
#include <boost/circular_buffer.hpp>

namespace DNode
{
    class ProxySession : boost::noncopyable
    {
    public:
        typedef boost::function<void ()> DoneCallback;

        ProxySession(DTun::UDTReactor& udtReactor, DTun::TCPReactor& tcpReactor);
        ~ProxySession();

        bool start(DTun::UInt32 localIp, DTun::UInt16 localPort,
            DTun::UInt32 remoteIp, DTun::UInt16 remotePort, const DoneCallback& callback);

    private:
        void onLocalConnect(int err);
        void onLocalSend(int err, int numBytes);
        void onLocalRecv(int err, int numBytes);

        void onRemoteConnect(int err);
        void onRemoteSend(int err, int numBytes);
        void onRemoteRecv(int err, int numBytes);

        DTun::UDTReactor& udtReactor_;
        DTun::TCPReactor& tcpReactor_;
        DoneCallback callback_;

        boost::mutex m_;
        boost::circular_buffer<char> localSndBuff_;
        int localSndBuffBytes_;
        boost::circular_buffer<char> remoteSndBuff_;
        int remoteSndBuffBytes_;

        std::vector<char> localRcvBuff_;
        std::vector<char> remoteRcvBuff_;

        bool connected_;
        bool done_;

        boost::shared_ptr<DTun::TCPConnection> localConn_;
        boost::shared_ptr<DTun::UDTConnection> remoteConn_;
        boost::shared_ptr<DTun::TCPConnector> localConnector_;
        boost::shared_ptr<DTun::UDTConnector> remoteConnector_;
    };
}

#endif

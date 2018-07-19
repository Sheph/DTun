#ifndef _DTUN_LTUDPCONNECTOR_H_
#define _DTUN_LTUDPCONNECTOR_H_

#include "DTun/SConnector.h"
#include "DTun/SConnection.h"
#include "DTun/LTUDPHandle.h"
#include "DTun/OpWatch.h"
#include <set>

namespace DTun
{
    class DTUN_API LTUDPConnector : public SConnector
    {
    public:
        explicit LTUDPConnector(const boost::shared_ptr<LTUDPHandle>& handle);
        ~LTUDPConnector();

        virtual boost::shared_ptr<SHandle> handle() const { return handle_; }

        virtual void close();

        virtual bool connect(const std::string& address, const std::string& port, const ConnectCallback& callback, Mode mode);

    private:
        void onStartConnect(const std::string& address, const std::string& port, const ConnectCallback& callback, Mode mode,
            UInt32 destIp, UInt16 destPort);
        void onConnect(int err, const ConnectCallback& callback);
        void onRendezvousAccept(const boost::shared_ptr<SHandle>& handle, const ConnectCallback& callback);
        void onRendezvousTimeout(int count, int timeoutMs, UInt32 destIp, UInt16 destPort, const ConnectCallback& callback);

        void onConnTimeout(int timeoutMs, std::vector<uint16_t> ports, int cnt, UInt32 destIp, UInt16 destPort);

        bool handedOut_;
        boost::shared_ptr<LTUDPHandle> handle_;
        boost::shared_ptr<OpWatch> watch_;
        std::vector<boost::shared_ptr<SConnection> > conns_;
    };
}

#endif

#ifndef _DTUN_LTUDPCONNECTOR_H_
#define _DTUN_LTUDPCONNECTOR_H_

#include "DTun/SConnector.h"
#include "DTun/LTUDPHandle.h"
#include "DTun/OpWatch.h"

namespace DTun
{
    class DTUN_API LTUDPConnector : public SConnector
    {
    public:
        explicit LTUDPConnector(const boost::shared_ptr<LTUDPHandle>& handle);
        ~LTUDPConnector();

        virtual boost::shared_ptr<SHandle> handle() const { return handle_; }

        virtual void close();

        virtual bool connect(const std::string& address, const std::string& port, const ConnectCallback& callback, bool rendezvous);

    private:
        void onStartConnect(const std::string& address, const std::string& port, const ConnectCallback& callback, bool rendezvous);
        void onConnect(int err, const ConnectCallback& callback);

        bool handedOut_;
        boost::shared_ptr<LTUDPHandle> handle_;
        boost::shared_ptr<OpWatch> watch_;
    };
}

#endif
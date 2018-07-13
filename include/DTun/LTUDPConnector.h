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
        //void onConnect(int err);

        boost::shared_ptr<LTUDPHandle> handle_;
        ConnectCallback callback_;
        bool handedOut_;
        //OpWatch watch_;
    };
}

#endif

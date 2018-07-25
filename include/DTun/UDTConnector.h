#ifndef _DTUN_UDTCONNECTOR_H_
#define _DTUN_UDTCONNECTOR_H_

#include "DTun/UDTHandler.h"
#include "DTun/SConnector.h"

namespace DTun
{
    class DTUN_API UDTConnector : public UDTHandler, public SConnector
    {
    public:
        UDTConnector(UDTReactor& reactor, const boost::shared_ptr<UDTHandle>& handle);
        ~UDTConnector();

        virtual bool connect(const std::string& address, const std::string& port, const ConnectCallback& callback, Mode mode);

        virtual void close(bool immediate = false);

        virtual int getPollEvents() const;

        virtual void handleRead();
        virtual void handleWrite();

    private:
        ConnectCallback callback_;
        bool noCloseSock_;
    };
}

#endif

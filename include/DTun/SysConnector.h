#ifndef _DTUN_SYSCONNECTOR_H_
#define _DTUN_SYSCONNECTOR_H_

#include "DTun/SysHandler.h"
#include "DTun/SConnector.h"

namespace DTun
{
    class DTUN_API SysConnector : public SysHandler, public SConnector
    {
    public:
        SysConnector(SysReactor& reactor, const boost::shared_ptr<SysHandle>& handle);
        ~SysConnector();

        virtual bool connect(const std::string& address, const std::string& port, const ConnectCallback& callback, Mode mode);

        virtual void close(bool immediate = false);

        virtual int getPollEvents() const;

        virtual void handleRead();
        virtual void handleWrite();

    private:
        ConnectCallback callback_;
        bool handedOut_;
    };
}

#endif

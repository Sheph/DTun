#ifndef _DTUN_UDTACCEPTOR_H_
#define _DTUN_UDTACCEPTOR_H_

#include "DTun/UDTHandler.h"
#include "DTun/SAcceptor.h"
#include <boost/thread.hpp>

namespace DTun
{
    class UDTAcceptor : public UDTHandler, public SAcceptor
    {
    public:
        UDTAcceptor(UDTReactor& reactor, const boost::shared_ptr<UDTHandle>& handle);
        ~UDTAcceptor();

        virtual bool listen(int backlog, const ListenCallback& callback);

        virtual void close();

        virtual int getPollEvents() const;

        virtual void handleRead();
        virtual void handleWrite();

    private:
        ListenCallback callback_;
    };
}

#endif

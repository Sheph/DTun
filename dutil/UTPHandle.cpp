#include "DTun/UTPHandle.h"
#include "DTun/UTPManager.h"
#include "DTun/UTPAcceptor.h"
#include "DTun/UTPConnector.h"
#include "DTun/UTPConnection.h"
#include <boost/make_shared.hpp>

namespace DTun
{
    UTPHandle::UTPHandle(UTPManager& mgr)
    : mgr_(mgr)
    , impl_(boost::make_shared<UTPHandleImpl>(boost::ref(mgr)))
    , transportPort_(0)
    {
    }

    UTPHandle::UTPHandle(UTPManager& mgr,
        const boost::shared_ptr<SConnection>& conn, utp_socket* utpSock)
    : mgr_(mgr)
    , impl_(boost::make_shared<UTPHandleImpl>(boost::ref(mgr), conn, utpSock))
    , transportPort_(0)
    {
    }

    UTPHandle::~UTPHandle()
    {
        close();
    }

    SReactor& UTPHandle::reactor()
    {
        return mgr_.reactor();
    }

    void UTPHandle::ping(UInt32 ip, UInt16 port)
    {
        assert(0);
    }

    bool UTPHandle::bind(SYSSOCKET s)
    {
        return impl_->bind(s);
    }

    bool UTPHandle::bind(const struct sockaddr* name, int namelen)
    {
        return impl_->bind(name, namelen);
    }

    bool UTPHandle::getSockName(UInt32& ip, UInt16& port) const
    {
        return impl_->getSockName(ip, port);
    }

    bool UTPHandle::getPeerName(UInt32& ip, UInt16& port) const
    {
        return impl_->getPeerName(ip, port);
    }

    SYSSOCKET UTPHandle::duplicate()
    {
        return impl_->duplicate();
    }

    int UTPHandle::getTTL() const
    {
        return impl_->getTTL();
    }

    bool UTPHandle::setTTL(int ttl)
    {
        return impl_->setTTL(ttl);
    }

    void UTPHandle::close(bool immediate)
    {
        if (impl_) {
            transportPort_ = impl_->getTransportPort();
            impl_->mgr().addToKill(impl_, immediate);
            impl_.reset();
        }
    }

    bool UTPHandle::canReuse() const
    {
        return !impl_ && ((transportPort_ == 0) || !mgr_.haveTransportConnection(transportPort_));
    }

    boost::shared_ptr<SConnector> UTPHandle::createConnector()
    {
        return boost::make_shared<UTPConnector>(shared_from_this());
    }

    boost::shared_ptr<SAcceptor> UTPHandle::createAcceptor()
    {
        return boost::make_shared<UTPAcceptor>(shared_from_this());
    }

    boost::shared_ptr<SConnection> UTPHandle::createConnection()
    {
        return boost::make_shared<UTPConnection>(shared_from_this());
    }
}

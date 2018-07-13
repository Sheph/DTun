#include "DTun/LTUDPHandleImpl.h"
#include "DTun/LTUDPManager.h"

namespace DTun
{
    LTUDPHandleImpl::LTUDPHandleImpl(LTUDPManager& mgr)
    : mgr_(mgr)
    , pcb_(NULL)
    {
    }

    LTUDPHandleImpl::~LTUDPHandleImpl()
    {
    }

    bool LTUDPHandleImpl::bind(const struct sockaddr* name, int namelen)
    {
        assert(!conn_);
        if (conn_) {
            return false;
        }

        conn_ = mgr_.createTransportConnection(name, namelen);
        return !!conn_;
    }

    void LTUDPHandleImpl::listen(int backlog, const ListenCallback& callback)
    {
        listenCallback_ = callback;
    }
}

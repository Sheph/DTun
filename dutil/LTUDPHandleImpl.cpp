#include "DTun/LTUDPHandleImpl.h"

namespace DTun
{
    LTUDPHandleImpl::LTUDPHandleImpl(LTUDPManager& mgr, const boost::shared_ptr<SConnection>& conn)
    : mgr_(mgr)
    , conn_(conn)
    , pcb_(NULL)
    {
    }

    LTUDPHandleImpl::~LTUDPHandleImpl()
    {
    }

    void LTUDPHandleImpl::listen(int backlog, const ListenCallback& callback)
    {
    }
}

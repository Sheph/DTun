#include "PortReservation.h"
#include "PortAllocator.h"

namespace DNode
{
    PortReservation::PortReservation(PortAllocator* allocator)
    : allocator_(allocator)
    {
    }

    PortReservation::~PortReservation()
    {
        cancel();
    }

    void PortReservation::use(const boost::shared_ptr<DTun::SHandle>& handle, int i)
    {
        allocator_->useReservation(this, handle, i);
    }

    void PortReservation::keepalive()
    {
        allocator_->keepaliveReservation(this);
    }

    void PortReservation::cancel()
    {
        allocator_->freeReservation(this);
    }
}

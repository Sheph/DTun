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

    void PortReservation::use()
    {
        allocator_->useReservation(this);
    }

    void PortReservation::cancel()
    {
        allocator_->freeReservation(this);
    }
}

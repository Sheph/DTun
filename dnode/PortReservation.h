#ifndef _PORTRESERVATION_H_
#define _PORTRESERVATION_H_

#include "DTun/Types.h"
#include <boost/noncopyable.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/chrono.hpp>
#include <vector>

namespace DNode
{
    class PortAllocator;

    enum PortStatus
    {
        PortStatusFree = 0,
        PortStatusReservedSymm,
        PortStatusReservedFast,
    };

    struct PortState : boost::noncopyable
    {
        explicit PortState(int id)
        : id(id)
        , status(PortStatusFree) {}

        int id;
        PortStatus status;
        boost::chrono::steady_clock::time_point decayTime;
    };

    class PortStateComparer: public std::binary_function<boost::shared_ptr<PortState>, boost::shared_ptr<PortState>, bool>
    {
    public:
        bool operator()(const boost::shared_ptr<PortState>& l, const boost::shared_ptr<PortState>& r) const
        {
            if (l->decayTime < r->decayTime) {
                return true;
            } else if (l->decayTime > r->decayTime) {
                return false;
            }
            return l->id < r->id;
        }
    };

    typedef std::vector<boost::shared_ptr<PortState> > PortStates;

    class PortReservation : boost::noncopyable
    {
    public:
        explicit PortReservation(PortAllocator* allocator);
        ~PortReservation();

        void use();

        void cancel();

        // For internal use only.
        inline const PortStates& ports() const { return ports_; }
        inline void setPorts(const PortStates& value) { ports_ = value; }

    private:
        PortAllocator* allocator_;
        PortStates ports_;
    };
}

#endif

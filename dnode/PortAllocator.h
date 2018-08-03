#ifndef _PORTALLOCATOR_H_
#define _PORTALLOCATOR_H_

#include "PortReservation.h"
#include "DTun/Types.h"
#include "DTun/SReactor.h"
#include "DTun/OpWatch.h"
#include <boost/noncopyable.hpp>
#include <boost/thread/mutex.hpp>
#include <list>
#include <set>

namespace DNode
{
    class PortAllocator : boost::noncopyable
    {
    public:
        typedef boost::function<void ()> ReserveCallback;

        PortAllocator(DTun::SReactor& reactor, int numSymmPorts, int numFastPorts, int decayTimeoutMs);
        ~PortAllocator();

        boost::shared_ptr<PortReservation> reserveSymmPorts(int numPorts);
        boost::shared_ptr<PortReservation> reserveSymmPortsBestEffort(int numPorts, const ReserveCallback& callback);

        boost::shared_ptr<PortReservation> reserveFastPorts(int numPorts);
        boost::shared_ptr<PortReservation> reserveFastPortsBestEffort(int numPorts, const ReserveCallback& callback);

        std::string dump();

        // For internal use.
        void useReservation(PortReservation* reservation);
        void keepaliveReservation(PortReservation* reservation);
        void freeReservation(PortReservation* reservation);

    private:
        struct Request
        {
            Request()
            : numPorts(0)
            , reservation(NULL) {}

            Request(int numPorts,
                PortReservation* reservation,
                const ReserveCallback& callback)
            : numPorts(numPorts)
            , reservation(reservation)
            , callback(callback) {}

            int numPorts;
            PortReservation* reservation;
            ReserveCallback callback;
        };

        typedef std::set<boost::shared_ptr<PortState>, PortStateComparer> PortStateSet;

        void onDecayTimeout();
        void onProcessRequests();

        PortStates reservePorts(int numPorts, bool isSymm);
        void processRequests(boost::mutex::scoped_lock& lock, bool isSymm);

        DTun::SReactor& reactor_;
        int numPorts_[2];
        int decayTimeoutMs_;

        boost::mutex m_;
        PortStateSet ports_;
        std::list<Request> requests_[2];
        int reservedPorts_[2];
        bool decayRunning_;
        boost::shared_ptr<DTun::OpWatch> watch_;
    };
}

#endif

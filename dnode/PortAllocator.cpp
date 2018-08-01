#include "PortAllocator.h"
#include <boost/make_shared.hpp>

namespace DNode
{
    PortAllocator::PortAllocator(DTun::SReactor& reactor, int numSymmPorts, int numFastPorts, int decayTimeoutMs)
    : reactor_(reactor)
    , decayTimeoutMs_(decayTimeoutMs)
    , decayRunning_(false)
    , watch_(boost::make_shared<DTun::OpWatch>(boost::ref(reactor)))
    {
        numPorts_[0] = numSymmPorts;
        numPorts_[1] = numFastPorts;
        reservedPorts_[0] = 0;
        reservedPorts_[1] = 0;
        for (int i = 0; i < numSymmPorts + numFastPorts; ++i) {
            ports_.insert(boost::make_shared<PortState>(i));
        }
    }

    PortAllocator::~PortAllocator()
    {
        watch_->close();

        assert(reservedPorts_[0] == 0);
        assert(reservedPorts_[1] == 0);
        assert(requests_[0].empty());
        assert(requests_[1].empty());
    }

    boost::shared_ptr<PortReservation> PortAllocator::reserveSymmPorts(int numPorts)
    {
        boost::mutex::scoped_lock lock(m_);

        if (numPorts > (numPorts_[0] - reservedPorts_[0])) {
            return boost::shared_ptr<PortReservation>();
        }

        PortStates ports = reservePorts(numPorts, true);

        if (ports.empty()) {
            return boost::shared_ptr<PortReservation>();
        }

        boost::shared_ptr<PortReservation> res =
            boost::make_shared<PortReservation>(this);

        res->setPorts(ports);

        return res;
    }

    boost::shared_ptr<PortReservation> PortAllocator::reserveSymmPortsDelayed(int numPorts, const ReserveCallback& callback)
    {
        boost::shared_ptr<PortReservation> res =
            boost::make_shared<PortReservation>(this);

        boost::mutex::scoped_lock lock(m_);

        requests_[0].push_back(Request(numPorts, res.get(), callback));

        lock.unlock();

        reactor_.post(
            watch_->wrap(boost::bind(&PortAllocator::onProcessRequests, this)));

        return res;
    }

    boost::shared_ptr<PortReservation> PortAllocator::reserveFastPorts(int numPorts)
    {
        boost::mutex::scoped_lock lock(m_);

        if (numPorts > (numPorts_[1] - reservedPorts_[1])) {
            return boost::shared_ptr<PortReservation>();
        }

        PortStates ports = reservePorts(numPorts, false);

        if (ports.empty()) {
            return boost::shared_ptr<PortReservation>();
        }

        boost::shared_ptr<PortReservation> res =
            boost::make_shared<PortReservation>(this);

        res->setPorts(ports);

        return res;
    }

    boost::shared_ptr<PortReservation> PortAllocator::reserveFastPortsDelayed(int numPorts, const ReserveCallback& callback)
    {
        boost::shared_ptr<PortReservation> res =
            boost::make_shared<PortReservation>(this);

        boost::mutex::scoped_lock lock(m_);

        requests_[1].push_back(Request(numPorts, res.get(), callback));

        lock.unlock();

        reactor_.post(
            watch_->wrap(boost::bind(&PortAllocator::onProcessRequests, this)));

        return res;
    }

    void PortAllocator::useReservation(PortReservation* reservation)
    {
        boost::mutex::scoped_lock lock(m_);

        boost::chrono::steady_clock::time_point now =
            boost::chrono::steady_clock::now();

        assert(!reservation->ports().empty());

        for (size_t i = 0; i < reservation->ports().size(); ++i) {
            boost::shared_ptr<PortState> port = reservation->ports()[i];
            int cnt = ports_.erase(port);
            assert(cnt == 1);
            port->decayTime = now + boost::chrono::milliseconds(decayTimeoutMs_);
            bool res = ports_.insert(port).second;
            assert(res);
        }

        if (decayRunning_) {
            return;
        }

        decayRunning_ = true;

        lock.unlock();

        reactor_.post(
            watch_->wrap(boost::bind(&PortAllocator::onDecayTimeout, this)), decayTimeoutMs_ + 1);
    }

    void PortAllocator::keepaliveReservation(PortReservation* reservation)
    {
        boost::mutex::scoped_lock lock(m_);

        assert(!reservation->ports().empty());

        bool isSymm = false;

        for (size_t i = 0; i < reservation->ports().size(); ++i) {
            boost::shared_ptr<PortState> port = reservation->ports()[i];
            assert(port->status != PortStatusFree);
            isSymm = (port->status == PortStatusReservedSymm);
            if (i > 0) {
                port->status = PortStatusFree;
            }
        }

        int idx = isSymm ? 0 : 1;

        reservedPorts_[idx] -= (reservation->ports().size() - 1);
        assert(reservedPorts_[idx] >= 0);

        PortStates newStates;
        newStates.push_back(reservation->ports()[0]);

        reservation->setPorts(newStates);

        boost::shared_ptr<PortState> port = reservation->ports()[0];
        int cnt = ports_.erase(port);
        assert(cnt == 1);
        port->decayTime = (boost::chrono::steady_clock::time_point::max)();
        bool res = ports_.insert(port).second;
        assert(res);

        lock.unlock();

        reactor_.post(
            watch_->wrap(boost::bind(&PortAllocator::onProcessRequests, this)));
    }

    void PortAllocator::freeReservation(PortReservation* reservation)
    {
        boost::mutex::scoped_lock lock(m_);

        if (reservation->ports().empty()) {
            for (std::list<Request>::iterator it = requests_[0].begin();
                it != requests_[0].end(); ++it) {
                if (it->reservation == reservation) {
                    requests_[0].erase(it);
                    break;
                }
            }
            for (std::list<Request>::iterator it = requests_[1].begin();
                it != requests_[1].end(); ++it) {
                if (it->reservation == reservation) {
                    requests_[1].erase(it);
                    break;
                }
            }
            return;
        }

        bool isSymm = false;

        boost::chrono::steady_clock::time_point now =
            boost::chrono::steady_clock::now();

        bool startDecay = false;

        for (size_t i = 0; i < reservation->ports().size(); ++i) {
            boost::shared_ptr<PortState> port = reservation->ports()[i];
            assert(port->status != PortStatusFree);
            isSymm = (port->status == PortStatusReservedSymm);
            port->status = PortStatusFree;
            if (port->decayTime == (boost::chrono::steady_clock::time_point::max)()) {
                int cnt = ports_.erase(port);
                assert(cnt == 1);
                port->decayTime = now + boost::chrono::milliseconds(decayTimeoutMs_);
                bool res = ports_.insert(port).second;
                assert(res);
                startDecay = true;
            }
        }

        int idx = isSymm ? 0 : 1;

        reservedPorts_[idx] -= reservation->ports().size();
        assert(reservedPorts_[idx] >= 0);

        reservation->setPorts(PortStates());

        if (!decayRunning_) {
            if (startDecay) {
                decayRunning_ = true;
            }
        } else {
            startDecay = false;
        }

        lock.unlock();

        reactor_.post(
            watch_->wrap(boost::bind(&PortAllocator::onProcessRequests, this)));

        if (startDecay) {
            reactor_.post(
                watch_->wrap(boost::bind(&PortAllocator::onDecayTimeout, this)), decayTimeoutMs_ + 1);
        }
    }

    void PortAllocator::onDecayTimeout()
    {
        boost::mutex::scoped_lock lock(m_);

        processRequests(lock, true);
        processRequests(lock, false);

        decayRunning_ = false;

        boost::chrono::steady_clock::time_point now =
            boost::chrono::steady_clock::now();

        int timeoutMs = 0;

        for (PortStateSet::iterator it = ports_.begin(); it != ports_.end(); ++it) {
            if (((*it)->decayTime != (boost::chrono::steady_clock::time_point::max)()) &&
                ((*it)->decayTime > now)) {
                decayRunning_ = true;
                timeoutMs = boost::chrono::duration_cast<boost::chrono::milliseconds>(
                    (*it)->decayTime - now).count() + 1;
                break;
            }
        }

        lock.unlock();

        if (timeoutMs > 0) {
            reactor_.post(
                watch_->wrap(boost::bind(&PortAllocator::onDecayTimeout, this)), timeoutMs);
        }
    }

    void PortAllocator::onProcessRequests()
    {
        boost::mutex::scoped_lock lock(m_);
        processRequests(lock, true);
        processRequests(lock, false);
    }

    PortStates PortAllocator::reservePorts(int numPorts, bool isSymm)
    {
        boost::chrono::steady_clock::time_point now =
            boost::chrono::steady_clock::now();

        PortStates states;

        for (PortStateSet::iterator it = ports_.begin(); it != ports_.end(); ++it) {
            if (((*it)->decayTime > now) || ((int)states.size() == numPorts)) {
                break;
            }
            if ((*it)->status == PortStatusFree) {
                (*it)->status = isSymm ? PortStatusReservedSymm : PortStatusReservedFast;
                states.push_back(*it);
            }
        }

        if ((int)states.size() != numPorts) {
            for (size_t i = 0; i < states.size(); ++i) {
                states[i]->status = PortStatusFree;
            }
            states.clear();
        } else {
            reservedPorts_[isSymm ? 0 : 1] += numPorts;
            assert(reservedPorts_[isSymm ? 0 : 1] <= numPorts_[isSymm ? 0 : 1]);
        }

        return states;
    }

    void PortAllocator::processRequests(boost::mutex::scoped_lock& lock, bool isSymm)
    {
        int idx = isSymm ? 0 : 1;

        while (!requests_[idx].empty()) {
            Request& req = requests_[idx].front();

            if (req.numPorts > (numPorts_[idx] - reservedPorts_[idx])) {
                break;
            }

            PortStates ports = reservePorts(req.numPorts, isSymm);

            if (ports.empty()) {
                break;
            }

            req.reservation->setPorts(ports);

            ReserveCallback cb = req.callback;
            requests_[idx].pop_front();
            lock.unlock();
            cb();
            cb = ReserveCallback();
            lock.lock();
        }
    }
}

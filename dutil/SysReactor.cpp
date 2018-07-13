#include "DTun/SysReactor.h"
#include "DTun/Utils.h"
#include "Logger.h"
#include <sys/epoll.h>

namespace DTun
{
    SysReactor::SysReactor()
    : eid_(-1)
    , stopping_(false)
    , nextCookie_(0)
    , signalWrSock_(SYS_INVALID_SOCKET)
    , signalRdSock_(SYS_INVALID_SOCKET)
    , inPoll_(false)
    , pollIteration_(0)
    , currentlyHandling_(NULL)
    {
    }

    SysReactor::~SysReactor()
    {
        reset();
    }

    bool SysReactor::start()
    {
        log4cplus::NDCContextCreator ndc("SysReactor::start");

        eid_ = ::epoll_create(1);
        if (eid_ == -1) {
            LOG4CPLUS_ERROR(logger(), "Cannot create epoll: " << strerror(errno));
            reset();
            return false;
        }

        signalRdSock_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (signalRdSock_ == SYS_INVALID_SOCKET) {
            LOG4CPLUS_ERROR(logger(), "Cannot create UDP socket: " << strerror(errno));
            reset();
            return false;
        }

        signalWrSock_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (signalWrSock_ == SYS_INVALID_SOCKET) {
            LOG4CPLUS_ERROR(logger(), "Cannot create UDP socket: " << strerror(errno));
            reset();
            return false;
        }

        struct sockaddr_in addr, addrRead;
        socklen_t addrLen;

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (::bind(signalRdSock_, (struct sockaddr*)&addr, sizeof(addr)) == SYS_SOCKET_ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot bind UDP socket: " << strerror(errno));
            reset();
            return false;
        }

        addrLen = sizeof(addrRead);

        if (::getsockname(signalRdSock_, (struct sockaddr*)&addrRead, &addrLen) == SYS_SOCKET_ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot get UDP sock name: " << strerror(errno));
            reset();
            return false;
        }

        if (::connect(signalWrSock_, (const struct sockaddr*)&addrRead, sizeof(addrRead)) == SYS_SOCKET_ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot connect UDP socket: " << strerror(errno));
            reset();
            return false;
        }

        epoll_event ev;
        memset(&ev, 0, sizeof(ev));
        ev.events = EPOLLIN;
        ev.data.fd = signalRdSock_;
        if (::epoll_ctl(eid_, EPOLL_CTL_ADD, signalRdSock_, &ev) == -1) {
            LOG4CPLUS_ERROR(logger(), "epoll_ctl(add): " << strerror(errno));
        }

        return true;
    }

    void SysReactor::run()
    {
        runThreadId_ = boost::this_thread::get_id();

        log4cplus::NDCContextCreator ndc("SysReactor");

        processTokens();

        processUpdates();

        std::vector<epoll_event> ev;

        while (!stopping_) {
            ev.resize(pollHandlers_.size() + 1);

            int timeout = -1;

            {
                boost::mutex::scoped_lock lock(m_);
                inPoll_ = true;
                wakeupTime_.reset();
                if (!tokens_.empty()) {
                    wakeupTime_ = tokens_.begin()->scheduledTime;
                    boost::chrono::steady_clock::time_point now =
                        boost::chrono::steady_clock::now();
                    if (*wakeupTime_ > now) {
                        timeout = boost::chrono::duration_cast<boost::chrono::milliseconds>(
                            *wakeupTime_ - now).count() + 1;
                    } else {
                        timeout = 0;
                    }
                }
            }

            int numReady = ::epoll_wait(eid_, &ev[0], ev.size(), timeout);
            int epollErr = errno;

            {
                boost::mutex::scoped_lock lock(m_);
                inPoll_ = false;
                ++pollIteration_;
                c_.notify_all();
            }

            if (numReady < 0) {
                if (epollErr == EINTR) {
                    // In order to satisfy gdb...
                    continue;
                }
                LOG4CPLUS_ERROR(logger(), "epoll_wait error: " << strerror(errno));
                break;
            }

            for (int i = 0; i < numReady; ++ i) {
                SYSSOCKET fd = ev[i].data.fd;
                if ((ev[i].events & (EPOLLIN | EPOLLERR)) != 0) {
                    if (fd == signalRdSock_) {
                        LOG4CPLUS_TRACE(logger(), "epoll rd: signal");
                        signalRd();
                    } else {
                        LOG4CPLUS_TRACE(logger(), "epoll rd: " << fd);
                        PollHandlerMap::iterator psIt = pollHandlers_.find(fd);
                        if ((psIt != pollHandlers_.end()) && ((psIt->second.pollEvents & EPOLLIN) != 0)) {
                            boost::mutex::scoped_lock lock(m_);
                            HandlerMap::iterator sIt = handlers_.find(psIt->second.cookie);
                            if (sIt != handlers_.end()) {
                                currentlyHandling_ = sIt->second.handler;
                                lock.unlock();
                                currentlyHandling_->handleRead();
                                lock.lock();
                                currentlyHandling_ = NULL;
                                c_.notify_all();
                            }
                        }
                    }
                }
                if ((ev[i].events & (EPOLLOUT | EPOLLERR)) != 0) {
                    LOG4CPLUS_TRACE(logger(), "epoll wr: " << fd);
                    PollHandlerMap::iterator psIt = pollHandlers_.find(fd);
                    if ((psIt != pollHandlers_.end()) && ((psIt->second.pollEvents & EPOLLOUT) != 0)) {
                        boost::mutex::scoped_lock lock(m_);
                        HandlerMap::iterator sIt = handlers_.find(psIt->second.cookie);
                        if (sIt != handlers_.end()) {
                            currentlyHandling_ = sIt->second.handler;
                            lock.unlock();
                            currentlyHandling_->handleWrite();
                            lock.lock();
                            currentlyHandling_ = NULL;
                            c_.notify_all();
                        }
                    }
                }
            }

            processTokens();

            processUpdates();

            LOG4CPLUS_TRACE(logger(), "epoll run done");
        }

        processUpdates();

        runThreadId_ = boost::thread::id();
    }

    void SysReactor::stop()
    {
        stopping_ = true;
        signalWr();
    }

    void SysReactor::post(const Callback& callback, UInt32 timeoutMs)
    {
        boost::chrono::steady_clock::time_point scheduledTime =
            boost::chrono::steady_clock::now() + boost::chrono::milliseconds(timeoutMs);

        boost::mutex::scoped_lock lock(m_);

        tokens_.insert(DispatchToken(scheduledTime, callback, nextTokenId_++));

        if (!isSameThread()) {
            if (!wakeupTime_ || (scheduledTime < *wakeupTime_)) {
                signalWr();
            }
        }
    }

    void SysReactor::dispatch(const Callback& callback)
    {
        if (isSameThread()) {
            callback();
        } else {
            post(callback);
        }
    }

    std::string SysReactor::dump()
    {
        return "";
    }

    void SysReactor::add(SysHandler* handler)
    {
        int evts = handler->getPollEvents();

        boost::mutex::scoped_lock lock(m_);

        handler->setCookie(nextCookie_++);
        handlers_[handler->cookie()] = HandlerInfo(handler, evts);

        if (!isSameThread()) {
            signalWr();
        }
    }

    boost::shared_ptr<SysHandle> SysReactor::remove(SysHandler* handler)
    {
        boost::mutex::scoped_lock lock(m_);

        if (handlers_.count(handler->cookie()) == 0) {
            return boost::shared_ptr<SysHandle>();
        }

        handlers_.erase(handler->cookie());

        if (!isSameThread()) {
            signalWr();
            uint64_t pollIteration = pollIteration_;
            while ((inPoll_ && (pollIteration_ <= pollIteration)) ||
                (currentlyHandling_ == handler)) {
                c_.wait(lock);
            }
        }

        boost::shared_ptr<SysHandle> handle = handler->sysHandle();
        assert(handle);

        PollHandlerMap::iterator it = pollHandlers_.find(handle->sock());
        if ((it != pollHandlers_.end()) && (it->second.cookie == handler->cookie())) {
            if (it->second.pollEvents != 0) {
                epoll_event ev;
                if (::epoll_ctl(eid_, EPOLL_CTL_DEL, handle->sock(), &ev) == -1) {
                    LOG4CPLUS_ERROR(logger(), "epoll_ctl(del): " << strerror(errno));
                }
            }
            it->second.notInEpoll = true;
        }

        handler->resetHandle();

        return handle;
    }

    void SysReactor::update(SysHandler* handler)
    {
        boost::mutex::scoped_lock lock(m_);

        int evts = handler->getPollEvents();

        HandlerMap::iterator it = handlers_.find(handler->cookie());
        if (it == handlers_.end()) {
            return;
        }

        if (it->second.pollEvents == evts) {
            return;
        }

        it->second.pollEvents = evts;

        if (!isSameThread()) {
            signalWr();
        }
    }

    void SysReactor::reset()
    {
        tokens_.clear();
        assert(handlers_.empty());
        assert(pollHandlers_.empty());
        if (eid_ != -1) {
            close(eid_);
            eid_ = -1;
        }
        stopping_ = false;
        if (signalWrSock_ != SYS_INVALID_SOCKET) {
            DTun::closeSysSocketChecked(signalWrSock_);
            signalWrSock_ = SYS_INVALID_SOCKET;
        }
        if (signalRdSock_ != SYS_INVALID_SOCKET) {
            DTun::closeSysSocketChecked(signalRdSock_);
            signalRdSock_ = SYS_INVALID_SOCKET;
        }
    }

    bool SysReactor::isSameThread() const
    {
        return (runThreadId_ == boost::thread::id()) ||
            (boost::this_thread::get_id() == runThreadId_);
    }

    void SysReactor::signalWr()
    {
        char c = '\0';
        if (::send(signalWrSock_, &c, 1, 0) == SYS_SOCKET_ERROR) {
            LOG4CPLUS_ERROR(logger(), "cannot write signalWrSock");
        }
    }

    void SysReactor::signalRd()
    {
        char c = '\0';
        if (::recv(signalRdSock_, &c, 1, 0) != 1) {
            LOG4CPLUS_ERROR(logger(), "cannot read signalRdSock");
        }
    }

    void SysReactor::processUpdates()
    {
        boost::mutex::scoped_lock lock(m_);

        for (PollHandlerMap::iterator it = pollHandlers_.begin(); it != pollHandlers_.end();) {
            HandlerMap::iterator sIt = handlers_.find(it->second.cookie);
            if (sIt == handlers_.end()) {
                if ((it->second.pollEvents != 0) && !it->second.notInEpoll) {
                    epoll_event ev;
                    if (::epoll_ctl(eid_, EPOLL_CTL_DEL, it->first, &ev) == -1) {
                        LOG4CPLUS_ERROR(logger(), "epoll_ctl(del): " << strerror(errno));
                    }
                }
                pollHandlers_.erase(it++);
            } else if (it->second.pollEvents != sIt->second.pollEvents) {
                if (it->second.pollEvents != 0) {
                    epoll_event ev;
                    if (::epoll_ctl(eid_, EPOLL_CTL_DEL, it->first, &ev) == -1) {
                        LOG4CPLUS_ERROR(logger(), "epoll_ctl(del): " << strerror(errno));
                    }
                }
                it->second.pollEvents = sIt->second.pollEvents;
                int pollEvents = it->second.pollEvents;
                if (pollEvents != 0) {
                    pollEvents |= EPOLLERR;
                    epoll_event ev;
                    memset(&ev, 0, sizeof(ev));
                    ev.events = pollEvents;
                    ev.data.fd = it->first;
                    if (::epoll_ctl(eid_, EPOLL_CTL_ADD, it->first, &ev) == -1) {
                        LOG4CPLUS_ERROR(logger(), "epoll_ctl(add): " << strerror(errno));
                    }
                }
                ++it;
            } else {
                ++it;
            }
        }

        for (HandlerMap::iterator it = handlers_.begin(); it != handlers_.end(); ++it) {
            PollHandlerMap::iterator psIt = pollHandlers_.find(it->second.handler->sysHandle()->sock());
            if (psIt == pollHandlers_.end()) {
                std::pair<PollHandlerMap::iterator, bool> res =
                    pollHandlers_.insert(std::make_pair(it->second.handler->sysHandle()->sock(),
                        PollHandlerInfo(it->second.handler->cookie(), it->second.pollEvents)));
                assert(res.second);
                if (!res.second) {
                    LOG4CPLUS_FATAL(logger(), "duplicate fd 1");
                }
                int pollEvents = it->second.pollEvents;
                if (pollEvents != 0) {
                    pollEvents |= EPOLLERR;
                    epoll_event ev;
                    memset(&ev, 0, sizeof(ev));
                    ev.events = pollEvents;
                    ev.data.fd = it->second.handler->sysHandle()->sock();
                    if (::epoll_ctl(eid_, EPOLL_CTL_ADD, it->second.handler->sysHandle()->sock(), &ev) == -1) {
                        LOG4CPLUS_ERROR(logger(), "epoll_ctl(add): " << strerror(errno));
                    }
                }
            } else {
                assert(psIt->second.cookie == it->second.handler->cookie());
                if (psIt->second.cookie != it->second.handler->cookie()) {
                    LOG4CPLUS_FATAL(logger(), "duplicate fd 2");
                }
            }
        }
    }

    void SysReactor::processTokens()
    {
        boost::mutex::scoped_lock lock(m_);

        int count = tokens_.size() * 2;

        while (!tokens_.empty() && (count-- > 0)) {
            boost::chrono::steady_clock::time_point now =
                boost::chrono::steady_clock::now();

            const DispatchToken& first = *(tokens_.begin());

            if (first.scheduledTime > now) {
                break;
            }

            Callback cb = first.callback;

            tokens_.erase(tokens_.begin());

            lock.unlock();
            cb();
            lock.lock();
        }
    }
}

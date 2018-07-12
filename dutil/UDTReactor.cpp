#include "DTun/UDTReactor.h"
#include "DTun/Utils.h"
#include "Logger.h"

namespace DTun
{
    UDTReactor::UDTReactor()
    : eid_(UDT::ERROR)
    , stopping_(false)
    , nextCookie_(0)
    , signalWrSock_(UDT::INVALID_SOCK)
    , signalRdSock_(UDT::INVALID_SOCK)
    , inPoll_(false)
    , pollIteration_(0)
    , currentlyHandling_(NULL)
    {
    }

    UDTReactor::~UDTReactor()
    {
        reset();
    }

    bool UDTReactor::start()
    {
        log4cplus::NDCContextCreator ndc("UDTReactor::start");

        eid_ = UDT::epoll_create();
        if (eid_ == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot create epoll: " << UDT::getlasterror().getErrorMessage());
            reset();
            return false;
        }

        signalRdSock_ = UDT::socket(AF_INET, SOCK_STREAM, 0);
        if (signalRdSock_ == UDT::INVALID_SOCK) {
            LOG4CPLUS_ERROR(logger(), "Cannot create UDT socket: " << UDT::getlasterror().getErrorMessage());
            reset();
            return false;
        }

        signalWrSock_ = UDT::socket(AF_INET, SOCK_STREAM, 0);
        if (signalWrSock_ == UDT::INVALID_SOCK) {
            LOG4CPLUS_ERROR(logger(), "Cannot create UDT socket: " << UDT::getlasterror().getErrorMessage());
            reset();
            return false;
        }

        bool optval = false;
        if (UDT::setsockopt(signalWrSock_, 0, UDT_RCVSYN, &optval, sizeof(optval)) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot setsockopt on UDT socket: " << UDT::getlasterror().getErrorMessage());
            reset();
            return false;
        }

        struct sockaddr_in addr, addrRead;
        int addrLen;

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

        if (UDT::bind(signalRdSock_, (struct sockaddr*)&addr, sizeof(addr)) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot bind UDT socket: " << UDT::getlasterror().getErrorMessage());
            reset();
            return false;
        }

        addrLen = sizeof(addrRead);

        if (UDT::getsockname(signalRdSock_, (struct sockaddr*)&addrRead, &addrLen) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot get UDT sock name: " << UDT::getlasterror().getErrorMessage());
            reset();
            return false;
        }

        if (UDT::listen(signalRdSock_, 1) == UDT::ERROR) {
           LOG4CPLUS_ERROR(logger(), "Cannot listen UDT socket: " << UDT::getlasterror().getErrorMessage());
           reset();
           return false;
        }

        int events = UDT_EPOLL_OUT;
        if (UDT::epoll_add_usock(eid_, signalWrSock_, &events) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot add UDT sock to epoll: " << UDT::getlasterror().getErrorMessage());
            reset();
            return false;
        }

        if (UDT::connect(signalWrSock_, (const struct sockaddr*)&addrRead, sizeof(addrRead)) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot connect UDT socket: " << UDT::getlasterror().getErrorMessage());
            reset();
            return false;
        }

        sockaddr_storage clientaddr;
        int addrlen = sizeof(clientaddr);

        UDTSOCKET client;

        if ((client = UDT::accept(signalRdSock_, (sockaddr*)&clientaddr, &addrlen)) == UDT::INVALID_SOCK) {
            LOG4CPLUS_ERROR(logger(), "Cannot accept UDT socket: " << UDT::getlasterror().getErrorMessage());
            reset();
            return false;
        }

        DTun::closeUDTSocketChecked(signalRdSock_);
        signalRdSock_ = client;

        std::set<UDTSOCKET> readfds, writefds;

        if (UDT::epoll_wait(eid_, &readfds, &writefds, -1) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "epoll_wait error: " << UDT::getlasterror().getErrorMessage());
            reset();
            return false;
        }

        if (writefds.empty() || (writefds.count(signalWrSock_) == 0)) {
            LOG4CPLUS_ERROR(logger(), "epoll_wait: sock not connected");
            reset();
            return false;
        }

        if (UDT::epoll_remove_usock(eid_, signalWrSock_) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot remove UDT sock from epoll: " << UDT::getlasterror().getErrorMessage());
            reset();
            return false;
        }

        optval = true;
        if (UDT::setsockopt(signalWrSock_, 0, UDT_RCVSYN, &optval, sizeof(optval)) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot setsockopt on UDT socket: " << UDT::getlasterror().getErrorMessage());
            reset();
            return false;
        }

        events = UDT_EPOLL_IN;
        if (UDT::epoll_add_usock(eid_, signalRdSock_, &events) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "Cannot add UDT sock to epoll: " << UDT::getlasterror().getErrorMessage());
            reset();
            return false;
        }

        return true;
    }

    void UDTReactor::run()
    {
        runThreadId_ = boost::this_thread::get_id();

        log4cplus::NDCContextCreator ndc("UDTReactor");

        processUpdates();

        std::set<UDTSOCKET> readfds, writefds;

        while (!stopping_) {
            readfds.clear();
            writefds.clear();

            {
                boost::mutex::scoped_lock lock(m_);
                inPoll_ = true;
            }

            int err = UDT::epoll_wait(eid_, &readfds, &writefds, 1000);

            {
                boost::mutex::scoped_lock lock(m_);
                inPoll_ = false;
                ++pollIteration_;
                c_.notify_all();
            }

            if (err == UDT::ERROR) {
                if (UDT::getlasterror().getErrorCode() != CUDTException::ETIMEOUT) {
                    LOG4CPLUS_ERROR(logger(), "epoll_wait error: " << UDT::getlasterror().getErrorMessage());
                    break;
                }
            }

            for (std::set<UDTSOCKET>::const_iterator it = readfds.begin(); it != readfds.end(); ++it) {
                if (*it == signalRdSock_) {
                    LOG4CPLUS_TRACE(logger(), "epoll rd: signal");
                    signalRd();
                } else {
                    LOG4CPLUS_TRACE(logger(), "epoll rd: " << *it);
                    PollHandlerMap::iterator psIt = pollHandlers_.find(*it);
                    if ((psIt != pollHandlers_.end()) && ((psIt->second.pollEvents & UDT_EPOLL_IN) != 0)) {
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
            for (std::set<UDTSOCKET>::const_iterator it = writefds.begin(); it != writefds.end(); ++it) {
                LOG4CPLUS_TRACE(logger(), "epoll wr: " << *it);
                PollHandlerMap::iterator psIt = pollHandlers_.find(*it);
                if ((psIt != pollHandlers_.end()) && ((psIt->second.pollEvents & UDT_EPOLL_OUT) != 0)) {
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

            processUpdates();

            for (PollHandlerMap::iterator it = pollHandlers_.begin(); it != pollHandlers_.end(); ++it) {
                boost::mutex::scoped_lock lock(m_);
                HandlerMap::iterator sIt = handlers_.find(it->second.cookie);
                if (sIt == handlers_.end()) {
                    continue;
                }

                int state = UDT::getsockstate(sIt->second.handler->udtHandle()->sock());

                if ((state != BROKEN) && (state != CLOSED) && (state != NONEXIST)) {
                    // FIXME: UDT does a very bad thing, it closes descriptors implicitly
                    // on connection loss and garbage collects them after a timeout of 1 sec. i.e.
                    // if we're not fast enough we could get a non-existent socket here and after.
                    // Even if so, we can handle it, but what if newly created socket gets the same
                    // id as the closed one ? Well, things won't be pretty... One solution is to make socket
                    // ids inside UDT unique.
                    continue;
                }

                currentlyHandling_ = sIt->second.handler;
                if ((it->second.pollEvents & UDT_EPOLL_OUT) != 0) {
                    lock.unlock();
                    currentlyHandling_->handleWrite();
                    lock.lock();
                }
                if (((it->second.pollEvents & UDT_EPOLL_IN) != 0) && (handlers_.count(it->second.cookie) > 0)) {
                    lock.unlock();
                    currentlyHandling_->handleRead();
                    lock.lock();
                }
                currentlyHandling_ = NULL;
                c_.notify_all();
            }

            processUpdates();

            LOG4CPLUS_TRACE(logger(), "epoll run done");
        }

        processUpdates();

        runThreadId_ = boost::thread::id();
    }

    void UDTReactor::stop()
    {
        stopping_ = true;
        signalWr();
    }

    void UDTReactor::dispatch(const Callback& callback, UInt32 timeoutMs)
    {
        assert(false);
    }

    void UDTReactor::add(UDTHandler* handler)
    {
        int evts = handler->getPollEvents();

        boost::mutex::scoped_lock lock(m_);

        handler->setCookie(nextCookie_++);
        handlers_[handler->cookie()] = HandlerInfo(handler, evts);

        if (!isSameThread()) {
            signalWr();
        }
    }

    boost::shared_ptr<UDTHandle> UDTReactor::remove(UDTHandler* handler)
    {
        boost::mutex::scoped_lock lock(m_);

        if (handlers_.count(handler->cookie()) == 0) {
            return boost::shared_ptr<UDTHandle>();
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

        boost::shared_ptr<UDTHandle> handle = handler->udtHandle();
        assert(handle);

        PollHandlerMap::iterator it = pollHandlers_.find(handle->sock());
        if ((it != pollHandlers_.end()) && (it->second.cookie == handler->cookie())) {
            if (it->second.pollEvents != 0) {
                if (UDT::epoll_remove_usock(eid_, handle->sock()) == UDT::ERROR) {
                    LOG4CPLUS_ERROR(logger(), "epoll_remove_usock: " << UDT::getlasterror().getErrorMessage());
                }
            }
            it->second.notInEpoll = true;
        }

        handler->resetHandle();

        return handle;
    }

    void UDTReactor::update(UDTHandler* handler)
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

    void UDTReactor::reset()
    {
        assert(handlers_.empty());
        assert(pollHandlers_.empty());
        if (eid_ != UDT::ERROR) {
            UDT::epoll_release(eid_);
            eid_ = UDT::ERROR;
        }
        stopping_ = false;
        if (signalWrSock_ != UDT::INVALID_SOCK) {
            DTun::closeUDTSocketChecked(signalWrSock_);
            signalWrSock_ = UDT::INVALID_SOCK;
        }
        if (signalRdSock_ != UDT::INVALID_SOCK) {
            DTun::closeUDTSocketChecked(signalRdSock_);
            signalRdSock_ = UDT::INVALID_SOCK;
        }
    }

    bool UDTReactor::isSameThread()
    {
        return (runThreadId_ == boost::thread::id()) ||
            (boost::this_thread::get_id() == runThreadId_);
    }

    void UDTReactor::signalWr()
    {
        char c = '\0';
        if (UDT::send(signalWrSock_, &c, 1, 0) == UDT::ERROR) {
            LOG4CPLUS_ERROR(logger(), "cannot write signalWrSock");
        }
    }

    void UDTReactor::signalRd()
    {
        char c = '\0';
        if (UDT::recv(signalRdSock_, &c, 1, 0) != 1) {
            LOG4CPLUS_ERROR(logger(), "cannot read signalRdSock");
        }
    }

    void UDTReactor::processUpdates()
    {
        boost::mutex::scoped_lock lock(m_);

        for (PollHandlerMap::iterator it = pollHandlers_.begin(); it != pollHandlers_.end();) {
            HandlerMap::iterator sIt = handlers_.find(it->second.cookie);
            if (sIt == handlers_.end()) {
                if ((it->second.pollEvents != 0) && !it->second.notInEpoll) {
                    if (UDT::epoll_remove_usock(eid_, it->first) == UDT::ERROR) {
                        LOG4CPLUS_ERROR(logger(), "epoll_remove_usock: " << UDT::getlasterror().getErrorMessage());
                    }
                }
                pollHandlers_.erase(it++);
            } else if (it->second.pollEvents != sIt->second.pollEvents) {
                if (it->second.pollEvents != 0) {
                    if (UDT::epoll_remove_usock(eid_, it->first) == UDT::ERROR) {
                        LOG4CPLUS_ERROR(logger(), "epoll_remove_usock: " << UDT::getlasterror().getErrorMessage());
                    }
                }
                it->second.pollEvents = sIt->second.pollEvents;
                int pollEvents = it->second.pollEvents;
                if (pollEvents != 0) {
                    pollEvents |= UDT_EPOLL_ERR;
                    if (UDT::epoll_add_usock(eid_, it->first, &pollEvents) == UDT::ERROR) {
                        LOG4CPLUS_ERROR(logger(), "epoll_add_usock: " << UDT::getlasterror().getErrorMessage());
                    }
                }
                ++it;
            } else {
                ++it;
            }
        }

        for (HandlerMap::iterator it = handlers_.begin(); it != handlers_.end(); ++it) {
            PollHandlerMap::iterator psIt = pollHandlers_.find(it->second.handler->udtHandle()->sock());
            if (psIt == pollHandlers_.end()) {
                std::pair<PollHandlerMap::iterator, bool> res =
                    pollHandlers_.insert(std::make_pair(it->second.handler->udtHandle()->sock(),
                        PollHandlerInfo(it->second.handler->cookie(), it->second.pollEvents)));
                assert(res.second);
                if (!res.second) {
                    LOG4CPLUS_FATAL(logger(), "duplicate fd 1");
                }
                int pollEvents = it->second.pollEvents;
                if (pollEvents != 0) {
                    pollEvents |= UDT_EPOLL_ERR;
                    if (UDT::epoll_add_usock(eid_, it->second.handler->udtHandle()->sock(), &pollEvents) == UDT::ERROR) {
                        LOG4CPLUS_ERROR(logger(), "epoll_add_usock: " << UDT::getlasterror().getErrorMessage());
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
}

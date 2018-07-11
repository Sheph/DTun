#include "DTun/TCPReactor.h"
#include "DTun/Utils.h"
#include "Logger.h"
#include <sys/epoll.h>

namespace DTun
{
    TCPReactor::TCPReactor()
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

    TCPReactor::~TCPReactor()
    {
        reset();
    }

    bool TCPReactor::start()
    {
        log4cplus::NDCContextCreator ndc("TCPReactor::start");

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

    void TCPReactor::run()
    {
        runThreadId_ = boost::this_thread::get_id();

        log4cplus::NDCContextCreator ndc("TCPReactor");

        processUpdates();

        std::vector<epoll_event> ev;

        while (!stopping_) {
            ev.resize(pollSockets_.size() + 1);

            {
                boost::mutex::scoped_lock lock(m_);
                inPoll_ = true;
            }

            int numReady = ::epoll_wait(eid_, &ev[0], ev.size(), -1);
            int epollErr = errno;

            {
                boost::mutex::scoped_lock lock(m_);
                inPoll_ = false;
                ++pollIteration_;
                c_.notify_all();
            }

            if (numReady <= 0) {
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
                        PollSocketMap::iterator psIt = pollSockets_.find(fd);
                        if ((psIt != pollSockets_.end()) && ((psIt->second.pollEvents & EPOLLIN) != 0)) {
                            boost::mutex::scoped_lock lock(m_);
                            SocketMap::iterator sIt = sockets_.find(psIt->second.cookie);
                            if (sIt != sockets_.end()) {
                                currentlyHandling_ = sIt->second.socket;
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
                    PollSocketMap::iterator psIt = pollSockets_.find(fd);
                    if ((psIt != pollSockets_.end()) && ((psIt->second.pollEvents & EPOLLOUT) != 0)) {
                        boost::mutex::scoped_lock lock(m_);
                        SocketMap::iterator sIt = sockets_.find(psIt->second.cookie);
                        if (sIt != sockets_.end()) {
                            currentlyHandling_ = sIt->second.socket;
                            lock.unlock();
                            currentlyHandling_->handleWrite();
                            lock.lock();
                            currentlyHandling_ = NULL;
                            c_.notify_all();
                        }
                    }
                }
            }

            processUpdates();

            LOG4CPLUS_TRACE(logger(), "epoll run done");
        }

        processUpdates();

        runThreadId_ = boost::thread::id();
    }

    void TCPReactor::stop()
    {
        stopping_ = true;
        signalWr();
    }

    void TCPReactor::add(TCPSocket* socket)
    {
        int evts = socket->getPollEvents();

        boost::mutex::scoped_lock lock(m_);

        socket->setCookie(nextCookie_++);
        sockets_[socket->cookie()] = SocketInfo(socket, evts);

        if (!isSameThread()) {
            signalWr();
        }
    }

    SYSSOCKET TCPReactor::remove(TCPSocket* socket)
    {
        boost::mutex::scoped_lock lock(m_);

        if (sockets_.count(socket->cookie()) == 0) {
            return SYS_INVALID_SOCKET;
        }

        sockets_.erase(socket->cookie());

        if (!isSameThread()) {
            signalWr();
            uint64_t pollIteration = pollIteration_;
            while ((inPoll_ && (pollIteration_ <= pollIteration)) ||
                (currentlyHandling_ == socket)) {
                c_.wait(lock);
            }
        }

        SYSSOCKET sock = socket->sock();
        assert(sock != SYS_INVALID_SOCKET);

        PollSocketMap::iterator it = pollSockets_.find(sock);
        if ((it != pollSockets_.end()) && (it->second.cookie == socket->cookie())) {
            if (it->second.pollEvents != 0) {
                epoll_event ev;
                if (::epoll_ctl(eid_, EPOLL_CTL_DEL, sock, &ev) == -1) {
                    LOG4CPLUS_ERROR(logger(), "epoll_ctl(del): " << strerror(errno));
                }
            }
            it->second.notInEpoll = true;
        }

        socket->resetSock();

        return sock;
    }

    void TCPReactor::update(TCPSocket* socket)
    {
        boost::mutex::scoped_lock lock(m_);

        int evts = socket->getPollEvents();

        SocketMap::iterator it = sockets_.find(socket->cookie());
        if (it == sockets_.end()) {
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

    void TCPReactor::reset()
    {
        assert(sockets_.empty());
        assert(pollSockets_.empty());
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

    bool TCPReactor::isSameThread()
    {
        return (runThreadId_ == boost::thread::id()) ||
            (boost::this_thread::get_id() == runThreadId_);
    }

    void TCPReactor::signalWr()
    {
        char c = '\0';
        if (::send(signalWrSock_, &c, 1, 0) == SYS_SOCKET_ERROR) {
            LOG4CPLUS_ERROR(logger(), "cannot write signalWrSock");
        }
    }

    void TCPReactor::signalRd()
    {
        char c = '\0';
        if (::recv(signalRdSock_, &c, 1, 0) != 1) {
            LOG4CPLUS_ERROR(logger(), "cannot read signalRdSock");
        }
    }

    void TCPReactor::processUpdates()
    {
        boost::mutex::scoped_lock lock(m_);

        for (PollSocketMap::iterator it = pollSockets_.begin(); it != pollSockets_.end();) {
            SocketMap::iterator sIt = sockets_.find(it->second.cookie);
            if (sIt == sockets_.end()) {
                if ((it->second.pollEvents != 0) && !it->second.notInEpoll) {
                    epoll_event ev;
                    if (::epoll_ctl(eid_, EPOLL_CTL_DEL, it->first, &ev) == -1) {
                        LOG4CPLUS_ERROR(logger(), "epoll_ctl(del): " << strerror(errno));
                    }
                }
                pollSockets_.erase(it++);
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

        for (SocketMap::iterator it = sockets_.begin(); it != sockets_.end(); ++it) {
            PollSocketMap::iterator psIt = pollSockets_.find(it->second.socket->sock());
            if (psIt == pollSockets_.end()) {
                std::pair<PollSocketMap::iterator, bool> res =
                    pollSockets_.insert(std::make_pair(it->second.socket->sock(),
                        PollSocketInfo(it->second.socket->cookie(), it->second.pollEvents)));
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
                    ev.data.fd = it->second.socket->sock();
                    if (::epoll_ctl(eid_, EPOLL_CTL_ADD, it->second.socket->sock(), &ev) == -1) {
                        LOG4CPLUS_ERROR(logger(), "epoll_ctl(add): " << strerror(errno));
                    }
                }
            } else {
                assert(psIt->second.cookie == it->second.socket->cookie());
                if (psIt->second.cookie != it->second.socket->cookie()) {
                    LOG4CPLUS_FATAL(logger(), "duplicate fd 2");
                }
            }
        }
    }
}

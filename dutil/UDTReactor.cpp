#include "DTun/UDTReactor.h"
#include "Logger.h"

namespace DTun
{
    UDTReactor::UDTReactor()
    : eid_(UDT::ERROR)
    , stopping_(false)
    , signalWrSock_(UDT::INVALID_SOCK)
    , signalRdSock_(UDT::INVALID_SOCK)
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

        UDT::close(signalRdSock_);
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
            if (UDT::epoll_wait(eid_, &readfds, &writefds, -1) == UDT::ERROR) {
                LOG4CPLUS_ERROR(logger(), "epoll_wait error: " << UDT::getlasterror().getErrorMessage());
                break;
            }
            bool processUpdatesCalled = false;
            for (std::set<UDTSOCKET>::const_iterator it = readfds.begin(); it != readfds.end(); ++it) {
                if (*it == signalRdSock_) {
                    LOG4CPLUS_TRACE(logger(), "epoll rd: signal");
                    signalRd();
                    processUpdates();
                    processUpdatesCalled = true;
                } else {
                    LOG4CPLUS_TRACE(logger(), "epoll rd: " << *it);
                    boost::mutex::scoped_lock lock(m_);
                    SocketMap::iterator sIt = sockets_.find(*it);
                    if (sIt != sockets_.end()) {
                        UDTSocket* s = sIt->second.socket;
                        lock.unlock();
                        s->handleRead();
                        processUpdates();
                        processUpdatesCalled = true;
                    }
                }
            }
            for (std::set<UDTSOCKET>::const_iterator it = writefds.begin(); it != writefds.end(); ++it) {
                LOG4CPLUS_TRACE(logger(), "epoll wr: " << *it);
                boost::mutex::scoped_lock lock(m_);
                SocketMap::iterator sIt = sockets_.find(*it);
                if (sIt != sockets_.end()) {
                    UDTSocket* s = sIt->second.socket;
                    lock.unlock();
                    s->handleWrite();
                    processUpdates();
                    processUpdatesCalled = true;
                }
            }
            if (!processUpdatesCalled) {
                processUpdates();
            }
            if (readfds.empty() && writefds.empty()) {
                LOG4CPLUS_TRACE(logger(), "epoll empty run");
            } else {
                LOG4CPLUS_TRACE(logger(), "epoll run done");
            }
        }

        processUpdates();

        runThreadId_ = boost::thread::id();
    }

    void UDTReactor::stop()
    {
        stopping_ = true;
        signalWr();
    }

    void UDTReactor::add(UDTSocket* socket)
    {
        int evts = socket->getPollEvents();

        boost::mutex::scoped_lock lock(m_);
        if (sockets_.count(socket->sock()) > 0) {
            LOG4CPLUS_ERROR(logger(), "socket is already in UDTReactor");
            return;
        }

        sockets_[socket->sock()] = SocketInfo(socket, evts);

        if (isSameThread()) {
            if (UDT::epoll_add_usock(eid_, socket->sock(), &evts) == UDT::ERROR) {
                LOG4CPLUS_ERROR(logger(), "epoll_add_usock: " << UDT::getlasterror().getErrorMessage());
            }
        } else {
            toAddSockets_.insert(socket);
            signalWr();
            while (toAddSockets_.count(socket) > 0) {
                c_.wait(lock);
            }
        }
    }

    void UDTReactor::remove(UDTSocket* socket)
    {
        boost::mutex::scoped_lock lock(m_);
        if (sockets_.count(socket->sock()) == 0) {
            LOG4CPLUS_ERROR(logger(), "socket is not in UDTReactor");
            return;
        }

        sockets_.erase(socket->sock());

        if (isSameThread()) {
            if (UDT::epoll_remove_usock(eid_, socket->sock()) == UDT::ERROR) {
                LOG4CPLUS_ERROR(logger(), "epoll_remove_usock: " << UDT::getlasterror().getErrorMessage());
            }
            lock.unlock();
            socket->handleClose();
        } else {
            toRemoveSockets_.insert(socket);
            signalWr();
            while (toRemoveSockets_.count(socket) > 0) {
                c_.wait(lock);
            }
        }
    }

    void UDTReactor::update(UDTSocket* socket)
    {
        int evts = socket->getPollEvents();

        boost::mutex::scoped_lock lock(m_);

        SocketMap::iterator it = sockets_.find(socket->sock());
        if (it == sockets_.end()) {
            LOG4CPLUS_TRACE(logger(), "socket is not in UDTReactor");
            return;
        }

        if (it->second.pollEvents == evts) {
            return;
        }

        it->second.pollEvents = evts;

        if (isSameThread()) {
            if (UDT::epoll_remove_usock(eid_, socket->sock()) == UDT::ERROR) {
                LOG4CPLUS_ERROR(logger(), "epoll_remove_usock: " << UDT::getlasterror().getErrorMessage());
            }
            if (UDT::epoll_add_usock(eid_, socket->sock(), &evts) == UDT::ERROR) {
                LOG4CPLUS_ERROR(logger(), "epoll_add_usock: " << UDT::getlasterror().getErrorMessage());
            }
        } else {
            toUpdateSockets_.insert(socket);
            signalWr();
            while (toUpdateSockets_.count(socket) > 0) {
                c_.wait(lock);
            }
        }
    }

    void UDTReactor::reset()
    {
        assert(sockets_.empty());
        assert(toAddSockets_.empty());
        assert(toRemoveSockets_.empty());
        assert(toUpdateSockets_.empty());
        if (eid_ != UDT::ERROR) {
            UDT::epoll_release(eid_);
            eid_ = UDT::ERROR;
        }
        stopping_ = false;
        if (signalWrSock_ != UDT::INVALID_SOCK) {
            UDT::close(signalWrSock_);
            signalWrSock_ = UDT::INVALID_SOCK;
        }
        if (signalRdSock_ != UDT::INVALID_SOCK) {
            UDT::close(signalRdSock_);
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
        std::set<UDTSocket*> toRemoveSockets;

        boost::mutex::scoped_lock lock(m_);

        bool haveSome = !toUpdateSockets_.empty() || !toAddSockets_.empty() || !toRemoveSockets_.empty();

        for (std::set<UDTSocket*>::iterator it = toUpdateSockets_.begin(); it != toUpdateSockets_.end(); ++it) {
            SocketMap::iterator sIt = sockets_.find((*it)->sock());
            if (sIt == sockets_.end()) {
                continue;
            }
            if (UDT::epoll_remove_usock(eid_, (*it)->sock()) == UDT::ERROR) {
                LOG4CPLUS_ERROR(logger(), "epoll_remove_usock: " << UDT::getlasterror().getErrorMessage());
            }
            if (UDT::epoll_add_usock(eid_, (*it)->sock(), &sIt->second.pollEvents) == UDT::ERROR) {
                LOG4CPLUS_ERROR(logger(), "epoll_add_usock: " << UDT::getlasterror().getErrorMessage());
            }
        }

        for (std::set<UDTSocket*>::iterator it = toAddSockets_.begin(); it != toAddSockets_.end(); ++it) {
            SocketMap::iterator sIt = sockets_.find((*it)->sock());
            if (sIt == sockets_.end()) {
                continue;
            }
            if (UDT::epoll_add_usock(eid_, (*it)->sock(), &sIt->second.pollEvents) == UDT::ERROR) {
                LOG4CPLUS_ERROR(logger(), "epoll_add_usock: " << UDT::getlasterror().getErrorMessage());
            }
        }

        for (std::set<UDTSocket*>::iterator it = toRemoveSockets_.begin(); it != toRemoveSockets_.end(); ++it) {
            if (UDT::epoll_remove_usock(eid_, (*it)->sock()) == UDT::ERROR) {
                LOG4CPLUS_ERROR(logger(), "epoll_remove_usock: " << UDT::getlasterror().getErrorMessage());
            }
        }

        toUpdateSockets_.clear();
        toAddSockets_.clear();
        toRemoveSockets = toRemoveSockets_;

        lock.unlock();

        for (std::set<UDTSocket*>::iterator it = toRemoveSockets.begin(); it != toRemoveSockets.end(); ++it) {
            (*it)->handleClose();
        }

        lock.lock();
        for (std::set<UDTSocket*>::iterator it = toRemoveSockets.begin(); it != toRemoveSockets.end(); ++it) {
            toRemoveSockets_.erase(*it);
        }
        lock.unlock();

        if (haveSome) {
            c_.notify_all();
        }
    }
}

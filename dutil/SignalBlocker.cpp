#include "DTun/SignalBlocker.h"
#include <cassert>

#include <boost/thread/once.hpp>

static sigset_t signals;
static boost::once_flag initSignalsFlag = BOOST_ONCE_INIT;

static void initSignals()
{
    sigemptyset(&signals);
    sigaddset(&signals, SIGHUP);
    sigaddset(&signals, SIGTERM);
    sigaddset(&signals, SIGINT);
    sigaddset(&signals, SIGQUIT);
    sigaddset(&signals, SIGALRM);
    sigaddset(&signals, SIGUSR1);
    sigaddset(&signals, SIGUSR2);
    sigaddset(&signals, SIGPIPE);
}

namespace DTun
{
    SignalBlocker::SignalBlocker()
    {
        boost::call_once(initSignalsFlag, &initSignals);

        int rc = ::pthread_sigmask(SIG_SETMASK, &signals, &oldMask_);
        assert(rc == 0);

        blocked_ = true;
    }

    SignalBlocker::~SignalBlocker()
    {
        unblock();
    }

    void SignalBlocker::unblock() throw ()
    {
        if (blocked_) {
            int rc = ::pthread_sigmask(SIG_SETMASK, &oldMask_, 0);
            assert(rc == 0);

            blocked_ = false;
        }
    }
}

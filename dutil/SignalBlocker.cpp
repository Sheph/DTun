#include "DTun/SignalBlocker.h"
#include <cassert>

namespace DTun
{
    SignalBlocker::SignalBlocker(bool withSigInt)
    {
        sigset_t signals;

        sigemptyset(&signals);
        sigaddset(&signals, SIGHUP);
        sigaddset(&signals, SIGTERM);
        if (withSigInt) {
            sigaddset(&signals, SIGINT);
        }
        sigaddset(&signals, SIGQUIT);
        sigaddset(&signals, SIGALRM);
        sigaddset(&signals, SIGUSR1);
        sigaddset(&signals, SIGUSR2);
        sigaddset(&signals, SIGPIPE);

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

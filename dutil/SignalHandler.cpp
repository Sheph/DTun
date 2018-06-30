#include "DTun/SignalHandler.h"

#include <boost/thread.hpp>
#include <signal.h>

namespace DTun
{
    static boost::mutex m;
    static SignalHandler* handler = 0;
    static SignalHandler::Callback sigCallback;
    static sigset_t signals;
    static pthread_t tid;

    static void* sigwaitThread(void*)
    {
        /*
         * Run until pthread_cancel is called.
         */

        for (;;) {
            int signal = 0;

            int rc = ::sigwait(&signals, &signal);

            /*
             * Some sigwait() implementations incorrectly return EINTR
             * when interrupted by an unblocked caught signal.
             */

            if (rc == EINTR) {
                continue;
            }
            assert(rc == 0);

            rc = ::pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, 0);
            assert(rc == 0);

            if (sigCallback) {
                try {
                    sigCallback(signal);
                } catch (...) {
                    assert(0);
                }
            }

            rc = ::pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, 0);
            assert(rc == 0);
        }

        return 0;
    }

    SignalHandler::SignalHandler(const Callback& callback)
    {
        boost::mutex::scoped_lock lock(m);

        if (handler) {
            assert(0);
            return;
        }

        handler = this;
        sigCallback = callback;

        lock.unlock();

        sigemptyset(&signals);

        sigaddset(&signals, SIGHUP);
        sigaddset(&signals, SIGTERM);
        sigaddset(&signals, SIGINT);

        int rc = ::pthread_sigmask(SIG_BLOCK, &signals, 0);
        assert(rc == 0);

        rc = ::pthread_create(&tid, 0, sigwaitThread, 0);
        assert(rc == 0);
    }

    SignalHandler::~SignalHandler()
    {
        int rc = ::pthread_cancel(tid);
        assert(rc == 0);

        void* status = 0;
        rc = ::pthread_join(tid, &status);
        assert(rc == 0);
        assert(status == PTHREAD_CANCELED);

        boost::mutex::scoped_lock lock(m);

        handler = 0;
        sigCallback = 0;
    }
}

#ifndef _DTUN_UTPACCEPTOR_H_
#define _DTUN_UTPACCEPTOR_H_

#include "DTun/SReactor.h"
#include "DTun/SAcceptor.h"
#include "DTun/UTPHandle.h"
#include "DTun/OpWatch.h"

namespace DTun
{
    class DTUN_API UTPAcceptor : public SAcceptor
    {
    public:
        explicit UTPAcceptor(const boost::shared_ptr<UTPHandle>& handle);
        ~UTPAcceptor();

        virtual boost::shared_ptr<SHandle> handle() const { return handle_; }

        virtual void close(bool immediate = false);

        virtual bool listen(int backlog, const ListenCallback& callback);

    private:
        void onStartListen(int backlog, const ListenCallback& callback);

        boost::shared_ptr<UTPHandle> handle_;
        boost::shared_ptr<OpWatch> watch_;
    };
}

#endif

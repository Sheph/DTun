#ifndef _DTUN_LTUDPACCEPTOR_H_
#define _DTUN_LTUDPACCEPTOR_H_

#include "DTun/SReactor.h"
#include "DTun/SAcceptor.h"
#include "DTun/LTUDPHandle.h"
#include "DTun/OpWatch.h"

namespace DTun
{
    class DTUN_API LTUDPAcceptor : public SAcceptor
    {
    public:
        explicit LTUDPAcceptor(const boost::shared_ptr<LTUDPHandle>& handle);
        ~LTUDPAcceptor();

        virtual boost::shared_ptr<SHandle> handle() const { return handle_; }

        virtual void close();

        virtual bool listen(int backlog, const ListenCallback& callback);

    private:
        void onStartListen(int backlog, const ListenCallback& callback);

        boost::shared_ptr<LTUDPHandle> handle_;
        boost::shared_ptr<OpWatch> watch_;
    };
}

#endif

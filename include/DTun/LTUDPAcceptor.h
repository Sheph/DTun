#ifndef _DTUN_LTUDPACCEPTOR_H_
#define _DTUN_LTUDPACCEPTOR_H_

#include "DTun/SAcceptor.h"
#include "DTun/LTUDPHandle.h"
#include "DTun/OpWatch.h"

namespace DTun
{
    class LTUDPAcceptor : public SAcceptor
    {
    public:
        explicit LTUDPAcceptor(const boost::shared_ptr<LTUDPHandle>& handle);
        ~LTUDPAcceptor();

        virtual boost::shared_ptr<SHandle> handle() const { return handle_; }

        virtual void close();

        virtual bool listen(int backlog, const ListenCallback& callback);

    private:
        //void onListen(...);

        boost::shared_ptr<LTUDPHandle> handle_;
        //OpWatch watch_;
    };
}

#endif

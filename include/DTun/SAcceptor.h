#ifndef _DTUN_SACCEPTOR_H_
#define _DTUN_SACCEPTOR_H_

#include "DTun/SHandler.h"
#include <boost/function.hpp>

namespace DTun
{
    class DTUN_API SAcceptor : public virtual SHandler
    {
    public:
        typedef boost::function<void (const boost::shared_ptr<SHandle>&)> ListenCallback;

        SAcceptor() {}
        virtual ~SAcceptor() {}

        virtual bool listen(int backlog, const ListenCallback& callback) = 0;
    };
}

#endif

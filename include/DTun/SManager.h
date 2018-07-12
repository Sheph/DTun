#ifndef _DTUN_SMANAGER_H_
#define _DTUN_SMANAGER_H_

#include "DTun/SHandle.h"
#include "DTun/SReactor.h"

namespace DTun
{
    class SManager : boost::noncopyable
    {
    public:
        SManager() {}
        virtual ~SManager() {}

        virtual SReactor& reactor() = 0;

        virtual boost::shared_ptr<SHandle> createStreamSocket() = 0;

        virtual boost::shared_ptr<SHandle> createDatagramSocket() = 0;
    };
}

#endif

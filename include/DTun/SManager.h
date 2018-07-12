#ifndef _DTUN_SMANAGER_H_
#define _DTUN_SMANAGER_H_

#include "DTun/SHandle.h"

namespace DTun
{
    class SManager : boost::noncopyable
    {
    public:
        SManager() {}
        virtual ~SManager() {}

        virtual boost::shared_ptr<SHandle> createStreamSocket() = 0;

        virtual boost::shared_ptr<SHandle> createDatagramSocket() = 0;
    };
}

#endif

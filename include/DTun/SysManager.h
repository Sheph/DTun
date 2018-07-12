#ifndef _DTUN_SYSMANAGER_H_
#define _DTUN_SYSMANAGER_H_

#include "DTun/SManager.h"
#include "DTun/SysReactor.h"

namespace DTun
{
    class SysManager : public SManager
    {
    public:
        explicit SysManager(SysReactor& reactor);
        ~SysManager();

        virtual SReactor& reactor();

        virtual boost::shared_ptr<SHandle> createStreamSocket();

        virtual boost::shared_ptr<SHandle> createDatagramSocket();

    private:
        SysReactor& reactor_;
    };
}

#endif

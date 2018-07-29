#ifndef _DTUN_SYSMANAGER_H_
#define _DTUN_SYSMANAGER_H_

#include "DTun/SManager.h"
#include "DTun/SysReactor.h"

namespace DTun
{
    class DTUN_API SysManager : public SManager
    {
    public:
        explicit SysManager(SysReactor& reactor);
        ~SysManager();

        virtual SReactor& reactor();

        virtual boost::shared_ptr<SHandle> createStreamSocket();

        virtual boost::shared_ptr<SHandle> createDatagramSocket(SYSSOCKET s = SYS_INVALID_SOCKET);

        virtual void enablePortRemap(UInt16 dstPort);

    private:
        SysReactor& reactor_;
    };
}

#endif

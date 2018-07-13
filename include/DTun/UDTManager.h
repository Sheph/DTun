#ifndef _DTUN_UDTMANAGER_H_
#define _DTUN_UDTMANAGER_H_

#include "DTun/SManager.h"
#include "DTun/UDTReactor.h"

namespace DTun
{
    class DTUN_API UDTManager : public SManager
    {
    public:
        explicit UDTManager(UDTReactor& reactor);
        ~UDTManager();

        virtual SReactor& reactor();

        virtual boost::shared_ptr<SHandle> createStreamSocket();

        virtual boost::shared_ptr<SHandle> createDatagramSocket();

    private:
        UDTReactor& reactor_;
    };
}

#endif

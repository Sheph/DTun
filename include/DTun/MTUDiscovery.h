#ifndef _DTUN_MTUDISCOVERY_H_
#define _DTUN_MTUDISCOVERY_H_

#include "DTun/SReactor.h"
#include "DTun/SConnection.h"
#include "DTun/OpWatch.h"
#include <boost/shared_ptr.hpp>

namespace DTun
{
    class DTUN_API MTUDiscovery : boost::noncopyable
    {
    public:
        MTUDiscovery(SReactor& reactor, const boost::shared_ptr<SConnection>& conn,
            const std::vector<char>& probeTransportHeader, const std::vector<char>& probeReplyTransportHeader,
            int minMTU, int maxMTU);
        ~MTUDiscovery();

        void setDest(UInt32 ip, UInt16 port);

        void start();

        void onMTUProbe(const char* data, int numBytes);

        bool onMTUProbeReply(const char* data, int numBytes, int& newMTU);

    private:
        void onMTUProbeTimeout();

        void onSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff);

        SReactor& reactor_;
        boost::weak_ptr<SConnection> conn_;
        std::vector<char> probeTransportHeader_;
        std::vector<char> probeReplyTransportHeader_;
        int minMTU_;
        int maxMTU_;
        const int numTries_;

        UInt32 destIp_;
        UInt16 destPort_;
        int curIndex_;
        int curMTU_;
        int curTry_;
        boost::shared_ptr<OpWatch> watch_;
    };
}

#endif

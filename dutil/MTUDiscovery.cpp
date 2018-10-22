#include "DTun/MTUDiscovery.h"
#include "Logger.h"
#include <boost/make_shared.hpp>

namespace DTun
{
    #pragma pack(1)
    struct MTUProbeHeader
    {
        uint32_t index;
    };

    struct MTUProbeReplyHeader
    {
        uint32_t index;
    };
    #pragma pack()

    MTUDiscovery::MTUDiscovery(SReactor& reactor, const boost::shared_ptr<SConnection>& conn,
        const std::vector<char>& probeTransportHeader, const std::vector<char>& probeReplyTransportHeader,
        int minMTU, int maxMTU)
    : reactor_(reactor)
    , conn_(conn)
    , probeTransportHeader_(probeTransportHeader)
    , probeReplyTransportHeader_(probeReplyTransportHeader)
    , minMTU_(minMTU)
    , maxMTU_(maxMTU)
    , numTries_(6)
    , destIp_(0)
    , destPort_(0)
    , curIndex_(0)
    , curMTU_(minMTU_)
    , curTry_(0)
    , watch_(boost::make_shared<OpWatch>(boost::ref(reactor_)))
    {
    }

    MTUDiscovery::~MTUDiscovery()
    {
        watch_->close();
    }

    void MTUDiscovery::setDest(UInt32 ip, UInt16 port)
    {
        destIp_ = ip;
        destPort_ = port;
    }

    void MTUDiscovery::start()
    {
        reactor_.post(watch_->wrap(boost::bind(&MTUDiscovery::onMTUProbeTimeout, this)), 3000);
    }

    void MTUDiscovery::onMTUProbe(const char* data, int numBytes)
    {
        MTUProbeHeader header;

        if (numBytes < (int)sizeof(header)) {
            LOG4CPLUS_WARN(logger(), "onMTUProbe(too short " << numBytes << " bytes)");
            return;
        }

        memcpy(&header, data, sizeof(header));

        MTUProbeReplyHeader replyHeader;

        replyHeader.index = header.index;

        boost::shared_ptr<std::vector<char> > sndBuff =
            boost::make_shared<std::vector<char> >(probeReplyTransportHeader_.size() + sizeof(replyHeader));

        memcpy(&(*sndBuff)[0], &probeReplyTransportHeader_[0], probeReplyTransportHeader_.size());
        memcpy(&(*sndBuff)[0] + probeReplyTransportHeader_.size(), &replyHeader, sizeof(replyHeader));

        boost::shared_ptr<SConnection> conn = conn_.lock();
        assert(conn);

        conn->writeTo(&(*sndBuff)[0], &(*sndBuff)[0] + sndBuff->size(),
            destIp_, destPort_,
            boost::bind(&MTUDiscovery::onSend, this, _1, sndBuff));
    }

    bool MTUDiscovery::onMTUProbeReply(const char* data, int numBytes, int& newMTU)
    {
        MTUProbeReplyHeader header;

        if (numBytes < (int)sizeof(header)) {
            LOG4CPLUS_WARN(logger(), "onMTUProbeReply(too short " << numBytes << " bytes)");
            return false;
        }

        if (numBytes > (int)sizeof(header)) {
            LOG4CPLUS_WARN(logger(), "onMTUProbeReply(too long " << numBytes << " bytes)");
            return false;
        }

        memcpy(&header, data, sizeof(header));

        if ((int)header.index != curIndex_) {
            return false;
        }

        curTry_ = 0;
        ++curIndex_;
        minMTU_ = curMTU_;
        curMTU_ = (minMTU_ + maxMTU_) / 2;

        newMTU = minMTU_;

        if ((maxMTU_ - minMTU_) <= 16) {
            LOG4CPLUS_INFO(logger(), "onMTUProbeReply(DONE, MTU=" << minMTU_ << ")");
        } else {
            LOG4CPLUS_TRACE(logger(), "onMTUProbeReply(MTU=" << minMTU_ << ")");
        }

        return true;
    }

    void MTUDiscovery::onMTUProbeTimeout()
    {
        if (curTry_ >= numTries_) {
            curTry_ = 0;
            ++curIndex_;
            maxMTU_ = curMTU_ - 1;
            curMTU_ = (minMTU_ + maxMTU_) / 2;

            if ((maxMTU_ - minMTU_) <= 16) {
                LOG4CPLUS_INFO(logger(), "onMTUProbeTimeout(DONE, MTU=" << minMTU_ << ")");
                return;
            } else {
                LOG4CPLUS_TRACE(logger(), "onMTUProbeTimeout(minMTU=" << minMTU_ << ", maxMTU=" << maxMTU_ << ", curMTU=" << curMTU_ << ")");
            }
        } else if ((maxMTU_ - minMTU_) <= 16) {
            return;
        }

        ++curTry_;

        boost::shared_ptr<std::vector<char> > sndBuff =
            boost::make_shared<std::vector<char> >(curMTU_);

        memcpy(&(*sndBuff)[0], &probeTransportHeader_[0], probeTransportHeader_.size());

        MTUProbeHeader header;
        header.index = curIndex_;

        memcpy(&(*sndBuff)[0] + probeTransportHeader_.size(), &header, sizeof(header));
        memset(&(*sndBuff)[0] + probeTransportHeader_.size() + sizeof(header), 0xAA, curMTU_ - probeTransportHeader_.size() - sizeof(header));

        boost::shared_ptr<SConnection> conn = conn_.lock();
        assert(conn);

        conn->writeTo(&(*sndBuff)[0], &(*sndBuff)[0] + sndBuff->size(),
            destIp_, destPort_,
            boost::bind(&MTUDiscovery::onSend, this, _1, sndBuff));

        reactor_.post(watch_->wrap(boost::bind(&MTUDiscovery::onMTUProbeTimeout, this)), 500);
    }

    void MTUDiscovery::onSend(int err, const boost::shared_ptr<std::vector<char> >& sndBuff)
    {
        if (err) {
            LOG4CPLUS_ERROR(logger(), "MTUDiscovery::onSend error!");
        }
    }
}

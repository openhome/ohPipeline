#include <OpenHome/Media/Pipeline/DecodedAudioAggregator.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Debug.h>

using namespace OpenHome;
using namespace OpenHome::Media;

// DecodedAudioAggregator

const TUint DecodedAudioAggregator::kSupportedMsgTypes =   eMode
                                                         | eTrack
                                                         | eDrain
                                                         | eDelay
                                                         | eEncodedStream
                                                         | eMetatext
                                                         | eStreamInterrupted
                                                         | eHalt
                                                         | eFlush
                                                         | eWait
                                                         | eDecodedStream
                                                         | eBitRate
                                                         | eAudioPcm
                                                         | eAudioDsd
                                                         | eQuit;

DecodedAudioAggregator::DecodedAudioAggregator(IPipelineElementDownstream& aDownstreamElement)
    : PipelineElement(kSupportedMsgTypes)
    , iDownstreamElement(aDownstreamElement)
    , iDecodedAudio(nullptr)
    , iChannels(0)
    , iSampleRate(0)
    , iBitDepth(0)
    , iSupportsLatency(false)
    , iAggregationDisabled(false)
    , iAggregatedJiffies(0)
{
}

void DecodedAudioAggregator::Push(Msg* aMsg)
{
    ASSERT(aMsg != nullptr);
    Msg* msg = aMsg->Process(*this);
    if (msg != nullptr) {
        iDownstreamElement.Push(msg);
    }
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgMode* aMsg)
{
    OutputAggregatedAudio();
    iSupportsLatency = (aMsg->Info().LatencyMode() != Latency::NotSupported);
    return aMsg;
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgTrack* aMsg)
{
    OutputAggregatedAudio();
    return aMsg;
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgDrain* aMsg)
{
    OutputAggregatedAudio();
    return aMsg;
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgEncodedStream* aMsg)
{
    OutputAggregatedAudio();
    const auto wasAggregationDisabled = iAggregationDisabled;
    iAggregationDisabled = (iSupportsLatency && aMsg->StreamFormat() != MsgEncodedStream::Format::Encoded);
    if (wasAggregationDisabled != iAggregationDisabled) {
        LOG(kMedia, "DecodedAudioAggregator::ProcessMsg(MsgEncodedStream* ): iAggregationDisabled=%u\n",
                    iAggregationDisabled);
    }
    return aMsg;
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgStreamInterrupted* aMsg)
{
    OutputAggregatedAudio();
    return aMsg;
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgHalt* aMsg)
{
    OutputAggregatedAudio();
    return aMsg;
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgFlush* aMsg)
{
    OutputAggregatedAudio();
    return aMsg;
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgWait* aMsg)
{
    OutputAggregatedAudio();
    return aMsg;
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgDecodedStream* aMsg)
{
    OutputAggregatedAudio();
    ASSERT(iDecodedAudio == nullptr);
    const DecodedStreamInfo& info = aMsg->StreamInfo();
    iChannels = info.NumChannels();
    iSampleRate = info.SampleRate();
    iBitDepth = info.BitDepth();
    return aMsg;
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgAudioPcm* aMsg)
{
    return TryAggregate(aMsg, kPcmPaddingBytes);
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgAudioDsd* aMsg)
{
    return TryAggregate(aMsg, aMsg->JiffiesNonPlayable());
}

Msg* DecodedAudioAggregator::ProcessMsg(MsgQuit* aMsg)
{
    OutputAggregatedAudio();
    return aMsg;
}

TBool DecodedAudioAggregator::AggregatorFull(TUint aBytes, TUint aJiffies)
{
    return (aBytes == DecodedAudio::kMaxBytes || aJiffies >= kMaxJiffies);
}

MsgAudioDecoded* DecodedAudioAggregator::TryAggregate(MsgAudioDecoded* aMsg, TUint aJiffiesNonPlayable)
{
    if (iAggregationDisabled) {
        return aMsg;
    }

    TUint msgJiffies = aMsg->Jiffies() + aJiffiesNonPlayable; // addition of non playable jiffies prevents TryAggregate() from trying to write to buffer without enough free memory
    const TUint jiffiesPerSample = Jiffies::PerSample(iSampleRate);
    const TUint msgBytes = Jiffies::ToBytes(msgJiffies, jiffiesPerSample, iChannels, iBitDepth); // jiffies might be modified here
    ASSERT(msgJiffies == (aMsg->Jiffies() + aJiffiesNonPlayable)); // refuse to handle msgs not terminating on sample boundaries

    if (iDecodedAudio == nullptr) {
        if (AggregatorFull(msgBytes, msgJiffies)) {
            return aMsg;
        }
        else {
            iDecodedAudio = aMsg;
            iAggregatedJiffies = msgJiffies;
            return nullptr;
        }
    }

    TUint aggregatedBytes = Jiffies::ToBytes(iAggregatedJiffies, jiffiesPerSample, iChannels, iBitDepth);

    if (aggregatedBytes + msgBytes <= kMaxBytes) {
        // Have byte capacity to add new data.
        iDecodedAudio->Aggregate(aMsg);

        iAggregatedJiffies += msgJiffies;
        aggregatedBytes = Jiffies::ToBytes(iAggregatedJiffies, jiffiesPerSample, iChannels, iBitDepth);

        if (AggregatorFull(aggregatedBytes, iAggregatedJiffies)) {
            auto msg = iDecodedAudio;
            iDecodedAudio = nullptr;
            iAggregatedJiffies = 0;
            return msg;
        }
    }
    else {
        // Lazy approach here - if new aMsg can't be appended, just return
        // iDecodedAudio and set iDecodedAudio = aMsg.
        // Could add a method to MsgAudioPcm that chops audio when aggregating
        // to make even more efficient use of decoded audio msgs.
        auto msg = iDecodedAudio;
        iDecodedAudio = aMsg;
        iAggregatedJiffies = msgJiffies;
        return msg;
    }

    return nullptr;
}

void DecodedAudioAggregator::OutputAggregatedAudio()
{
    if (iDecodedAudio != nullptr) {
        iDownstreamElement.Push(iDecodedAudio);
        iDecodedAudio = nullptr;
    }
}

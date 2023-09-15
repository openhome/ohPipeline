#include <OpenHome/Media/Pipeline/Ramper.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Pipeline/Msg.h>

using namespace OpenHome;
using namespace OpenHome::Media;

const TUint Ramper::kSupportedMsgTypes =   eMode
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
                                         | eSilence
                                         | eQuit;

Ramper::Ramper(IPipelineElementUpstream& aUpstreamElement,
              TUint aRampJiffiesLong,
              TUint aRampJiffiesShort)
    : PipelineElement(kSupportedMsgTypes)
    , iUpstreamElement(aUpstreamElement)
    , iStreamId(IPipelineIdProvider::kStreamIdInvalid)
    , iFormat(AudioFormat::Pcm)
    , iRamping(false)
    , iRampJiffiesLong(aRampJiffiesLong)
    , iRampJiffiesShort(aRampJiffiesShort)
    , iRampJiffies(aRampJiffiesLong)
    , iRemainingRampSize(0)
    , iCurrentRampValue(Ramp::kMin)
{
}

Ramper::~Ramper()
{
}

Msg* Ramper::Pull()
{
    Msg* msg;
    if (!iQueue.IsEmpty()) {
        msg = iQueue.Dequeue();
    }
    else {
        msg = iUpstreamElement.Pull();
    }
    msg = msg->Process(*this);
    ASSERT(msg != nullptr);
    return msg;
}

Msg* Ramper::ProcessMsg(MsgMode* aMsg)
{
    iRampJiffies = aMsg->Info().RampPauseResumeLong()?
                        iRampJiffiesLong : iRampJiffiesShort;
    return aMsg;
}

Msg* Ramper::ProcessMsg(MsgHalt* aMsg)
{
    iRamping = false;
    return aMsg;
}

Msg* Ramper::ProcessMsg(MsgDecodedStream* aMsg)
{
    const DecodedStreamInfo& info = aMsg->StreamInfo();

    if (IsRampApplicable(info)) {
        iRamping = true;
        iCurrentRampValue = Ramp::kMin;
        iRemainingRampSize = iRampJiffies;
    }
    else {
        iRamping = false;
        iCurrentRampValue = Ramp::kMax;
        iRemainingRampSize = 0;
    }

    iStreamId = info.StreamId();
    iFormat = info.Format();

    return aMsg;
}

Msg* Ramper::ProcessMsg(MsgAudioPcm* aMsg)
{
    return ProcessAudio(aMsg);
}

Msg* Ramper::ProcessMsg(MsgAudioDsd* aMsg)
{
    return ProcessAudio(aMsg);
}

Msg* Ramper::ProcessMsg(MsgSilence* aMsg)
{
    iRamping = false;
    iCurrentRampValue = Ramp::kMax;
    iRemainingRampSize = 0;
    return aMsg;
}

Msg* Ramper::ProcessAudio(MsgAudioDecoded* aMsg)
{
    if (iRamping) {
        MsgAudio* split;
        if (aMsg->Jiffies() > iRemainingRampSize) {
            split = aMsg->Split(iRemainingRampSize);
            if (split != nullptr) {
                iQueue.Enqueue(split);
            }
        }
        split = nullptr;
        iCurrentRampValue = aMsg->SetRamp(iCurrentRampValue, iRemainingRampSize, Ramp::EUp, split);
        if (split != nullptr) {
            iQueue.EnqueueAtHead(split);
        }
        if (iRemainingRampSize == 0 || iCurrentRampValue == Ramp::kMax) {
            iRamping = false;
        }
    }
    return aMsg;
}

TBool Ramper::IsRampApplicable(const DecodedStreamInfo& aInfo)
{
    if (aInfo.Live()) {
        return true;
    }

    const TBool newStream = (aInfo.StreamId() != iStreamId);
    if (newStream && aInfo.SampleStart() > 0) {
        return true;
    }

    if (aInfo.Format() == AudioFormat::Dsd) {
        return true;
    }

    return false;
}

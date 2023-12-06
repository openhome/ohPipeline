#include <OpenHome/Media/Pipeline/StreamValidator.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Pipeline/Msg.h>

using namespace OpenHome;
using namespace OpenHome::Media;


const TUint StreamValidator::kSupportedMsgTypes =   eMode
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
                                                  | eAudioPcm
                                                  | eAudioDsd
                                                  | eSilence
                                                  | eQuit;

StreamValidator::StreamValidator(MsgFactory& aMsgFactory, IPipelineElementDownstream& aDownstreamElement)
    : PipelineElement(kSupportedMsgTypes)
    , iMsgFactory(aMsgFactory)
    , iDownstream(aDownstreamElement)
    , iAnimator(nullptr)
    , iTargetFlushId(MsgFlush::kIdInvalid)
    , iFlushing(false)
{
}

void StreamValidator::SetAnimator(IPipelineAnimator& aPipelineAnimator)
{
    iAnimator = &aPipelineAnimator;
}

void StreamValidator::Push(Msg* aMsg)
{
    Msg* msg = aMsg->Process(*this);
    if (msg != nullptr) {
        iDownstream.Push(msg);
    }
}

Msg* StreamValidator::ProcessMsg(MsgMode* aMsg)
{
    iFlushing = false;
    return aMsg;
}

Msg* StreamValidator::ProcessMsg(MsgTrack* aMsg)
{
    iFlushing = false;
    return aMsg;
}

Msg* StreamValidator::ProcessMsg(MsgMetaText* aMsg)
{
    return ProcessFlushable(aMsg);
}

Msg* StreamValidator::ProcessMsg(MsgFlush* aMsg)
{
    if (iTargetFlushId != MsgFlush::kIdInvalid && iTargetFlushId == aMsg->Id()) {
        iTargetFlushId = MsgFlush::kIdInvalid;
        aMsg->RemoveRef();
        return nullptr;
    }
    return aMsg;
}

Msg* StreamValidator::ProcessMsg(MsgDecodedStream* aMsg)
{
    const DecodedStreamInfo& streamInfo = aMsg->StreamInfo();
    iFlushing = true;
    try {
        ASSERT(iAnimator != nullptr);
        (void)iAnimator->PipelineAnimatorDelayJiffies(streamInfo.Format(),
                                                      streamInfo.SampleRate(),
                                                      streamInfo.BitDepth(),
                                                      streamInfo.NumChannels());
        iFlushing = false;
    }
    catch (FormatUnsupported&) {}
    catch (SampleRateUnsupported&) {}
    catch (BitDepthUnsupported&) {}
    if (iFlushing) {
        IStreamHandler* streamHandler = streamInfo.StreamHandler();
        const TUint streamId = streamInfo.StreamId();
        if (streamHandler != nullptr) {
            (void)streamHandler->OkToPlay(streamId);
            iTargetFlushId = streamHandler->TryStop(streamId);
        }
    }
    return ProcessFlushable(aMsg);
}

Msg* StreamValidator::ProcessMsg(MsgAudioPcm* aMsg)
{
    return ProcessFlushable(aMsg);
}

Msg* StreamValidator::ProcessMsg(MsgAudioDsd* aMsg)
{
    return ProcessFlushable(aMsg);
}

Msg* StreamValidator::ProcessMsg(MsgSilence* aMsg)
{
    return ProcessFlushable(aMsg);
}

Msg* StreamValidator::ProcessFlushable(Msg* aMsg)
{
    if (iFlushing) {
        aMsg->RemoveRef();
        return nullptr;
    }
    return aMsg;
}

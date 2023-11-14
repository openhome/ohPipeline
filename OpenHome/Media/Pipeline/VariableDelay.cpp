#include <OpenHome/Media/Pipeline/VariableDelay.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Private/Standard.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Media;

// AudioDiscarder

class AudioDiscarder : public IMsgProcessor, private INonCopyable
{
public:
    static TUint Run(MsgQueueLite& aQueue, TUint aMaxJiffies, TUint64& aTrackOffset);
private:
    AudioDiscarder(MsgQueueLite& aQueue, TUint aMaxJiffies, TUint64& aTrackOffset);
    TUint Process();
    MsgAudio* ProcessAudio(MsgAudio* aMsg);
    Msg* ProcessAudioDecoded(MsgAudioDecoded* aMsg);
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgTrack* aMsg) override;
    Msg* ProcessMsg(MsgDrain* aMsg) override;
    Msg* ProcessMsg(MsgDelay* aMsg) override;
    Msg* ProcessMsg(MsgEncodedStream* aMsg) override;
    Msg* ProcessMsg(MsgStreamSegment* aMsg) override;
    Msg* ProcessMsg(MsgAudioEncoded* aMsg) override;
    Msg* ProcessMsg(MsgMetaText* aMsg) override;
    Msg* ProcessMsg(MsgStreamInterrupted* aMsg) override;
    Msg* ProcessMsg(MsgHalt* aMsg) override;
    Msg* ProcessMsg(MsgFlush* aMsg) override;
    Msg* ProcessMsg(MsgWait* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsg(MsgBitRate* aMsg) override;
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgAudioDsd* aMsg) override;
    Msg* ProcessMsg(MsgSilence* aMsg) override;
    Msg* ProcessMsg(MsgPlayable* aMsg) override;
    Msg* ProcessMsg(MsgQuit* aMsg) override;
private:
    MsgQueueLite& iQueue;
    const TUint iMaxJiffies;
    TUint64& iTrackOffset;
    TUint iJiffies;
    TBool iComplete;
};

TUint AudioDiscarder::Run(MsgQueueLite& aQueue, TUint aMaxJiffies, TUint64& aTrackOffset)
{
    AudioDiscarder self(aQueue, aMaxJiffies, aTrackOffset);
    self.Process();
    return self.iJiffies;
}
AudioDiscarder::AudioDiscarder(MsgQueueLite& aQueue, TUint aMaxJiffies, TUint64& aTrackOffset)
    : iQueue(aQueue)
    , iMaxJiffies(aMaxJiffies)
    , iTrackOffset(aTrackOffset)
    , iJiffies(0)
    , iComplete(false)
{}
TUint AudioDiscarder::Process()
{
    while (!iComplete && !iQueue.IsEmpty()) {
        Msg* msg = iQueue.Dequeue();
        msg = msg->Process(*this);
        msg->RemoveRef();
    }
    return iJiffies;
}
MsgAudio* AudioDiscarder::ProcessAudio(MsgAudio* aMsg)
{
    const TUint msgJiffies = aMsg->Jiffies();
    if (iJiffies + msgJiffies > iMaxJiffies) {
        const TUint splitPos = iMaxJiffies - iJiffies;
        Msg* split = aMsg->Split(splitPos);
        iQueue.EnqueueAtHead(split);
    }
    iJiffies += aMsg->Jiffies();
    if (iJiffies == iMaxJiffies) {
        iComplete = true;
    }
    return aMsg;
}
Msg* AudioDiscarder::ProcessAudioDecoded(MsgAudioDecoded* aMsg)
{
    auto audio = static_cast<MsgAudioDecoded*>(ProcessAudio(aMsg));
    iTrackOffset = audio->TrackOffset() + audio->Jiffies();
    return audio;
}
Msg* AudioDiscarder::ProcessMsg(MsgMode* aMsg)                { ASSERTS(); return aMsg; }
Msg* AudioDiscarder::ProcessMsg(MsgTrack* aMsg)               { ASSERTS(); return aMsg; }
Msg* AudioDiscarder::ProcessMsg(MsgDrain* aMsg)               { ASSERTS(); return aMsg; }
Msg* AudioDiscarder::ProcessMsg(MsgDelay* aMsg)               { ASSERTS(); return aMsg; }
Msg* AudioDiscarder::ProcessMsg(MsgEncodedStream* aMsg)       { ASSERTS(); return aMsg; }
Msg* AudioDiscarder::ProcessMsg(MsgStreamSegment* aMsg)       { ASSERTS(); return aMsg; }
Msg* AudioDiscarder::ProcessMsg(MsgAudioEncoded* aMsg)        { ASSERTS(); return aMsg; }
Msg* AudioDiscarder::ProcessMsg(MsgMetaText* aMsg)            { ASSERTS(); return aMsg; }
Msg* AudioDiscarder::ProcessMsg(MsgStreamInterrupted* aMsg)   { ASSERTS(); return aMsg; }
Msg* AudioDiscarder::ProcessMsg(MsgHalt* aMsg)                { ASSERTS(); return aMsg; }
Msg* AudioDiscarder::ProcessMsg(MsgFlush* aMsg)               { ASSERTS(); return aMsg; }
Msg* AudioDiscarder::ProcessMsg(MsgWait* aMsg)                { ASSERTS(); return aMsg; }
Msg* AudioDiscarder::ProcessMsg(MsgDecodedStream* aMsg)       { ASSERTS(); return aMsg; }
Msg* AudioDiscarder::ProcessMsg(MsgBitRate* aMsg)             { ASSERTS(); return aMsg; }
Msg* AudioDiscarder::ProcessMsg(MsgAudioPcm* aMsg)
{
    return ProcessAudioDecoded(aMsg);
}
Msg* AudioDiscarder::ProcessMsg(MsgAudioDsd* aMsg)
{
    return ProcessAudioDecoded(aMsg);
}
Msg* AudioDiscarder::ProcessMsg(MsgSilence* aMsg)
{
    return ProcessAudio(aMsg);
}
Msg* AudioDiscarder::ProcessMsg(MsgPlayable* aMsg)            { ASSERTS(); return aMsg; }
Msg* AudioDiscarder::ProcessMsg(MsgQuit* aMsg)                { ASSERTS(); return aMsg; }


// VariableDelayBase

const TUint VariableDelayBase::kSupportedMsgTypes =   eMode
                                                    | eTrack
                                                    | eDrain
                                                    | eDelay
                                                    | eEncodedStream
                                                    | eAudioEncoded
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

static const TChar* kStatus[] = { "Starting"
                                 ,"Running"
                                 ,"RampingDown"
                                 ,"RampedDown"
                                 ,"RampingUp" };

VariableDelayBase::VariableDelayBase(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstreamElement, TUint aRampDuration, const TChar* aId)
    : PipelineElement(kSupportedMsgTypes)
    , iMsgFactory(aMsgFactory)
    , iLock("VDEL")
    , iClockPuller(nullptr)
    , iAnimator(nullptr)
    , iDelayJiffies(0)
    , iDelayAdjustment(0)
    , iDecodedStream(nullptr)
    , iUpstreamElement(aUpstreamElement)
    , iRampDuration(aRampDuration)
    , iId(aId)
    , iWaitForAudioBeforeGeneratingSilence(false)
    , iPendingStream(nullptr)
    , iTargetFlushId(MsgFlush::kIdInvalid)
    , iDsdBlockSize(0)
{
    ResetStatusAndRamp();
}

VariableDelayBase::~VariableDelayBase()
{
    if (iDecodedStream != nullptr) {
        iDecodedStream->RemoveRef();
    }
}

void VariableDelayBase::SetAnimator(IPipelineAnimator& aAnimator)
{
    iAnimator = &aAnimator;
    try {
        iDsdBlockSize = aAnimator.PipelineAnimatorDsdBlockSizeWords();
    }
    catch (FormatUnsupported&) {}
}

Msg* VariableDelayBase::Pull()
{
    Msg* msg;
    do {
        msg = DoPull();
    } while (msg == nullptr);
    return msg;
}

Msg* VariableDelayBase::DoPull()
{
    Msg* msg = nullptr;
    if (iWaitForAudioBeforeGeneratingSilence) {
        do {
            msg = NextMsg();
            if (msg != nullptr) {
                if (iWaitForAudioBeforeGeneratingSilence) {
                    return msg;
                }
                else {
                    iQueue.Enqueue(msg);
                }
            }
        } while (msg == nullptr && iWaitForAudioBeforeGeneratingSilence);
        msg = nullptr; // Enqueue() above passed ownership of msg back to reservoir
    }

    // msg(s) pulled above may have altered iDelayAdjustment (e.g. MsgMode sets it to zero)
    if ((iStatus == EStarting || iStatus == ERampedDown) && iDelayAdjustment > 0) {
        TUint size = ((TUint)iDelayAdjustment > kMaxMsgSilenceDuration? kMaxMsgSilenceDuration : (TUint)iDelayAdjustment);
        auto stream = iDecodedStream->StreamInfo();
        MsgSilence* silence;
        if (stream.Format() == AudioFormat::Pcm) {
            silence = iMsgFactory.CreateMsgSilence(size, stream.SampleRate(), stream.BitDepth(), stream.NumChannels());
        }
        else {
            silence = iMsgFactory.CreateMsgSilenceDsd(size, stream.SampleRate(), stream.NumChannels(), iDsdBlockSize, 2); // FIXME - get DSD pad bytes programatically
        }
        if (iClockPuller != nullptr) {
            silence->SetObserver(*iClockPuller);
        }
        msg = silence;
        if (size > (TUint)iDelayAdjustment) { // sizes less than one sample will be rounded up
            iDelayAdjustment = 0;
        }
        else {
            iDelayAdjustment -= size;
        }
        if (iDelayAdjustment == 0) {
            LocalDelayApplied();
            if (iStatus == ERampedDown) {
                iStatus = ERampingUp;
                iRampDirection = Ramp::EUp;
                iCurrentRampValue = Ramp::kMin;
                iRemainingRampSize = iRampDuration;
            }
            else {
                iStatus = ERunning;
                iRampDirection = Ramp::ENone;
                iCurrentRampValue = Ramp::kMax;
                iRemainingRampSize = 0;
            }
        }
    }
    else if (msg == nullptr) {
        msg = NextMsg();
    }

    return msg;
}

Msg* VariableDelayBase::NextMsg()
{
    Msg* msg;
    if (iPendingStream != nullptr) {
        msg = iPendingStream;
        iPendingStream = nullptr;
        return msg; // avoid calling ProcessMsg and resetting iStatus
    }
    else if (!iQueue.IsEmpty()) {
        msg = iQueue.Dequeue();
    }
    else {
        msg = iUpstreamElement.Pull();
    }
    msg = msg->Process(*this);
    return msg;
}

void VariableDelayBase::RampMsg(MsgAudio* aMsg)
{
    if (aMsg->Jiffies() > iRemainingRampSize) {
        MsgAudio* remaining = aMsg->Split(iRemainingRampSize);
        iQueue.EnqueueAtHead(remaining);
    }
    MsgAudio* split;
    iCurrentRampValue = aMsg->SetRamp(iCurrentRampValue, iRemainingRampSize, iRampDirection, split);
    if (split != nullptr) {
        iQueue.EnqueueAtHead(split);
    }
}

void VariableDelayBase::ResetStatusAndRamp()
{
    iStatus = EStarting;
    iRampDirection = Ramp::ENone;
    iCurrentRampValue = Ramp::kMax;
    iRemainingRampSize = iRampDuration;
}

void VariableDelayBase::SetupRamp()
{
    iWaitForAudioBeforeGeneratingSilence = (iDelayAdjustment > 0);
    LOG(kMedia, "VariableDelay(%s), delay=%u, adjustment=%d\n",
                iId, iDelayJiffies/Jiffies::kPerMs, iDelayAdjustment/(TInt)Jiffies::kPerMs);
    switch (iStatus)
    {
    case EStarting:
        iRampDirection = Ramp::ENone;
        iRemainingRampSize = iRampDuration;
        break;
    case ERunning:
        if (iDelayAdjustment != 0) {
            iStatus = ERampingDown;
            iRampDirection = Ramp::EDown;
            iRemainingRampSize = iRampDuration;
        }
        break;
    case ERampingDown:
        if (iDelayAdjustment == 0) {
            if (iRampDuration == iRemainingRampSize) {
                iStatus = ERunning;
                iRampDirection = Ramp::ENone;
                iRemainingRampSize = 0;
            }
            else {
                iStatus = ERampingUp;
                iRampDirection = Ramp::EUp;
                // retain current value of iCurrentRampValue
                iRemainingRampSize = iRampDuration - iRemainingRampSize;
            }
        }
        break;
    case ERampedDown:
        if (iDelayAdjustment == 0) {
            iStatus = ERampingUp;
            iRampDirection = Ramp::EUp;
            // retain current value of iCurrentRampValue
            iRemainingRampSize = iRampDuration - iRemainingRampSize;
        }
        break;
    case ERampingUp:
        iStatus = ERampingDown;
        iRampDirection = Ramp::EDown;
        // retain current value of iCurrentRampValue
        iRemainingRampSize = iRampDuration - iRemainingRampSize;
        if (iRemainingRampSize == 0) {
            iStatus = ERampedDown;
        }
        break;
    }
}

MsgDecodedStream* VariableDelayBase::UpdateDecodedStream(TUint64 aTrackOffset)
{
    auto s = iDecodedStream->StreamInfo();
    const auto sampleStart = Jiffies::ToSamples(aTrackOffset, s.SampleRate());
    auto stream = iMsgFactory.CreateMsgDecodedStream(s.StreamId(), s.BitRate(), s.BitDepth(), s.SampleRate(),
                                                     s.NumChannels(), s.CodecName(), s.TrackLength(),
                                                     sampleStart, s.Lossless(), s.Seekable(), s.Live(),
                                                     s.AnalogBypass(), s.Format(), s.Multiroom(),
                                                     s.Profile(), s.StreamHandler(), s.Ramp());
    iDecodedStream->RemoveRef();
    iDecodedStream = stream;
    iDecodedStream->AddRef();
    return stream;
}

void VariableDelayBase::HandleDelayChange(TUint aNewDelay)
{
    if (aNewDelay == iDelayJiffies) {
        return;
    }

    iDelayAdjustment += (TInt)(aNewDelay - iDelayJiffies);
    iDelayJiffies = aNewDelay;
    SetupRamp();
    if (iDelayAdjustment != 0 && iClockPuller != nullptr) {
        iClockPuller->Stop();
    }
}

TBool VariableDelayBase::StreamInfoHasChanged(const DecodedStreamInfo& aInfo) const
{
    if (iDecodedStream == nullptr) {
        return true;
    }

    auto info = iDecodedStream->StreamInfo();
    if (info.Format() != aInfo.Format()) {
        return true;
    }
    if (info.SampleRate() != aInfo.SampleRate()) {
        return true;
    }
    if (info.BitDepth() != aInfo.BitDepth()) {
        return true;
    }
    if (info.NumChannels() != aInfo.NumChannels()) {
        return true;
    }
    return false;
}

inline const TChar* VariableDelayBase::Status() const
{
    return kStatus[iStatus];
}

Msg* VariableDelayBase::ProcessMsg(MsgMode* aMsg)
{
    iMode.Replace(aMsg->Mode());
    iLock.Wait();
    if (iClockPuller != nullptr) {
        iClockPuller->Stop();
    }
    iClockPuller = aMsg->ClockPuller().Ptr();
    iDelayJiffies = 0;
    iLock.Signal();
    iDelayAdjustment = 0;
    iWaitForAudioBeforeGeneratingSilence = true;
    ResetStatusAndRamp();
    return aMsg;
}

Msg* VariableDelayBase::ProcessMsg(MsgDrain* aMsg)
{
    if (iClockPuller != nullptr) {
        iClockPuller->Stop();
    }
    iDelayAdjustment = iDelayJiffies;
    if (iDelayAdjustment == 0) {
        iWaitForAudioBeforeGeneratingSilence = false;
        ResetStatusAndRamp();
    }
    else {
        iWaitForAudioBeforeGeneratingSilence = true;
        iRampDirection = Ramp::EDown;
        iCurrentRampValue = Ramp::kMin;
        iRemainingRampSize = 0;
        iStatus = ERampedDown;

    }
    return aMsg;
}

Msg* VariableDelayBase::ProcessMsg(MsgFlush* aMsg)
{
    if (iTargetFlushId != MsgFlush::kIdInvalid
     && aMsg->Id() == iTargetFlushId
     && iStatus == ERampedDown) { // stream or further delay changes since we requested a flush may cause change in status
        LocalDelayApplied();
        iStatus = ERampingUp;
        iRampDirection = Ramp::EUp;
        iCurrentRampValue = Ramp::kMin;
        iRemainingRampSize = iRampDuration;
    }
    return aMsg;
}

Msg* VariableDelayBase::ProcessMsg(MsgDecodedStream* aMsg)
{
    TBool streamInfoChanged = StreamInfoHasChanged(aMsg->StreamInfo());
    if (iDecodedStream != nullptr) {
        iDecodedStream->RemoveRef();
    }
    iDecodedStream = aMsg;
    iDecodedStream->AddRef();
    if (streamInfoChanged) {
        ResetStatusAndRamp();
    }
    return aMsg;
}

Msg* VariableDelayBase::ProcessMsg(MsgAudioPcm* aMsg)
{
    return ProcessAudioDecoded(aMsg);
}

Msg* VariableDelayBase::ProcessMsg(MsgAudioDsd* aMsg)
{
    return ProcessAudioDecoded(aMsg);
}

Msg* VariableDelayBase::ProcessAudioDecoded(MsgAudioDecoded* aMsg)
{
    if (iWaitForAudioBeforeGeneratingSilence) {
        iWaitForAudioBeforeGeneratingSilence = false;
        iQueue.EnqueueAtHead(aMsg);
        return nullptr;
    }

    if (iStatus == EStarting && iDelayAdjustment < 0) {
        iStatus = ERampedDown;
    }

    auto msg = aMsg;
    switch (iStatus)
    {
    case EStarting:
        iStatus = ERunning;
        // fallthrough
    case ERunning:
        // nothing to do, allow the message to be passed out unchanged
        break;
    case ERampingDown:
    {
        RampMsg(msg);
        if (iRemainingRampSize == 0) {
            if (iDelayAdjustment != 0) {
                iStatus = ERampedDown;
                if (iDelayAdjustment < 0) {
                    TUint64 trackOffset;
                    iDelayAdjustment += AudioDiscarder::Run(iQueue, -iDelayAdjustment, trackOffset);
                    const TUint discard = -iDelayAdjustment;
                    if (discard == 0) {
                        iDelayAdjustment = 0;
                        LocalDelayApplied();
                        iStatus = ERampingUp;
                        iRampDirection = Ramp::EUp;
                        iCurrentRampValue = Ramp::kMin;
                        iRemainingRampSize = iRampDuration;
                        auto stream = UpdateDecodedStream(trackOffset);
                        ASSERT(iPendingStream == nullptr);
                        iPendingStream = stream;
                    }
                    else {
                        iTargetFlushId = iDecodedStream->StreamInfo().StreamHandler()->TryDiscard(discard);
                        if (iTargetFlushId != MsgFlush::kIdInvalid) {
                            iDelayAdjustment += discard;
                        }
                    }
                }
            }
            else {
                iStatus = ERampingUp;
                iRampDirection = Ramp::EUp;
                iRemainingRampSize = iRampDuration;
            }
        }
    }
        break;
    case ERampedDown:
    {
        ASSERT_VA(iDelayAdjustment <= 0, "iDelayAdjustment=%d\n", iDelayAdjustment);
        if (iDelayAdjustment < 0) {
            TUint jiffies = msg->Jiffies();
            if (jiffies > (TUint)(-iDelayAdjustment)) {
                MsgAudio* remaining = msg->Split((TUint)(-iDelayAdjustment));
                jiffies = msg->Jiffies();
                iQueue.EnqueueAtHead(remaining);
            }
            iDelayAdjustment += jiffies;
        }
        iDelayAdjustment = std::min(iDelayAdjustment, (TInt)0); // Split() may round up positions that are less than one sample
        if (iDelayAdjustment == 0) {
            LocalDelayApplied();
            iStatus = ERampingUp;
            iRampDirection = Ramp::EUp;
            iRemainingRampSize = iRampDuration;
            iCurrentRampValue = Ramp::kMin;
            const TUint64 trackOffset = msg->TrackOffset() + msg->Jiffies();
            msg->RemoveRef();
            auto stream = UpdateDecodedStream(trackOffset);
            return stream;
        }
        msg->RemoveRef();
        msg = nullptr;
    }
        break;
    case ERampingUp:
    {
        RampMsg(msg);
        if (iRemainingRampSize == 0) {
            iStatus = ERunning;
        }
    }
        break;
    default:
        ASSERTS();
    }

    return msg;
}

Msg* VariableDelayBase::ProcessMsg(MsgSilence* aMsg)
{
    if (iStatus == ERampingUp) {
        iRemainingRampSize = 0;
        iCurrentRampValue = Ramp::kMax;
        iStatus = ERunning;
    }
    else if (iStatus == ERampingDown) {
        iRemainingRampSize = 0;
        iCurrentRampValue = Ramp::kMin;
        if (iDelayAdjustment != 0) {
            iStatus = ERampedDown;
        }
        else {
            iStatus = ERampingUp;
            iRampDirection = Ramp::EUp;
            iRemainingRampSize = iRampDuration;
        }
    }

    return aMsg;
}


// VariableDelayLeft

VariableDelayLeft::VariableDelayLeft(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstreamElement,
                                     TUint aRampDuration, TUint aDownstreamDelay)
    : VariableDelayBase(aMsgFactory, aUpstreamElement, aRampDuration, "left")
    , iDownstreamDelay(aDownstreamDelay)
    , iObserver(nullptr)
{
}

void VariableDelayLeft::SetObserver(IVariableDelayObserver& aObserver)
{
    iObserver = &aObserver;
}

Msg* VariableDelayLeft::ProcessMsg(MsgDelay* aMsg)
{
    const TUint msgDelayJiffies = aMsg->TotalJiffies();
    aMsg->RemoveRef();
    auto msg = iMsgFactory.CreateMsgDelay(std::min(iDownstreamDelay, msgDelayJiffies), msgDelayJiffies);
    TUint delayJiffies = (iDownstreamDelay >= msgDelayJiffies? 0 : msgDelayJiffies - iDownstreamDelay);
    LOG(kMedia, "VariableDelayLeft::ProcessMsg(MsgDelay(%u): delay=%u(%u), prev=%u(%u), iStatus=%s\n",
                msgDelayJiffies,
                delayJiffies, Jiffies::ToMs(delayJiffies),
                iDelayJiffies, Jiffies::ToMs(iDelayJiffies),
                Status());

    HandleDelayChange(delayJiffies);

    return msg;
}

void VariableDelayLeft::LocalDelayApplied()
{
    ASSERT(iObserver != nullptr);
    iObserver->NotifyDelayApplied(iDelayJiffies);
}


// VariableDelayRight

VariableDelayRight::VariableDelayRight(MsgFactory& aMsgFactory,
                                       IPipelineElementUpstream& aUpstreamElement,
                                       TUint aRampDuration, TUint aMinDelay)
    : VariableDelayBase(aMsgFactory, aUpstreamElement, aRampDuration, "right")
    , iMinDelay(aMinDelay)
    , iDelayJiffiesTotal(0)
    , iAnimatorLatency(0)
    , iSampleRate(0)
    , iBitDepth(0)
    , iNumChannels(0)
{
}

Msg* VariableDelayRight::ProcessMsg(MsgMode* aMsg)
{
    iDelayJiffiesTotal = 0;
    return VariableDelayBase::ProcessMsg(aMsg);
}

Msg* VariableDelayRight::ProcessMsg(MsgDelay* aMsg)
{
    const TUint msgDelayJiffies = aMsg->RemainingJiffies();
    const TUint msgDelayTotalJiffies = aMsg->TotalJiffies();
    TUint delayJiffies = std::max(msgDelayJiffies, iMinDelay);
    iDelayJiffiesTotal = delayJiffies;
    aMsg->RemoveRef();
    delayJiffies = (iAnimatorLatency >= delayJiffies? 0 : delayJiffies - iAnimatorLatency);
    delayJiffies = std::max(delayJiffies, iMinDelay);
    LOG(kMedia, "VariableDelayRight::ProcessMsg(MsgDelay(%u): delay=%u(%u), downstream=%u(%u), prev=%u(%u), iStatus=%s\n",
                msgDelayJiffies,
                delayJiffies, Jiffies::ToMs(delayJiffies),
                iAnimatorLatency, Jiffies::ToMs(iAnimatorLatency),
                iDelayJiffies, Jiffies::ToMs(iDelayJiffies),
                Status());

    HandleDelayChange(delayJiffies);

    return iMsgFactory.CreateMsgDelay(delayJiffies, std::max(msgDelayTotalJiffies, iMinDelay));
}

Msg* VariableDelayRight::ProcessMsg(MsgDecodedStream* aMsg)
{
    auto stream = aMsg->StreamInfo();
    TBool streamInfoChanged = StreamInfoHasChanged(stream);
    Msg* msg = VariableDelayBase::ProcessMsg(aMsg);

    if (streamInfoChanged) {
        iSampleRate = stream.SampleRate();
        iBitDepth = stream.BitDepth();
        iNumChannels = stream.NumChannels();

        AdjustDelayForAnimatorLatency();
    }
    return msg;
}

void VariableDelayRight::LocalDelayApplied()
{
    StartClockPuller();
}

void VariableDelayRight::NotifyDelayApplied(TUint /*aJiffies*/)
{
    AutoMutex _(iLock);
    if (iDelayAdjustment == 0) {
        StartClockPuller();
    }
}

void VariableDelayRight::AdjustDelayForAnimatorLatency()
{
    try {
        if (iSampleRate != 0) {
            auto streamInfo = iDecodedStream->StreamInfo();
            ASSERT(iAnimator != nullptr);
            iAnimatorLatency = iAnimator->PipelineAnimatorDelayJiffies(streamInfo.Format(), iSampleRate, iBitDepth, iNumChannels);
            TUint delayJiffies = (iAnimatorLatency >= iDelayJiffiesTotal? 0 : iDelayJiffiesTotal - iAnimatorLatency);
            delayJiffies = std::max(delayJiffies, iMinDelay);
            HandleDelayChange(delayJiffies);
        }
    }
    catch (FormatUnsupported&) {}
    catch (SampleRateUnsupported&) {}
    catch (BitDepthUnsupported&) {}
}

void VariableDelayRight::StartClockPuller()
{
    if (iClockPuller != nullptr) {
        iClockPuller->Start();
    }
}

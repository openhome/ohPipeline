#include <OpenHome/Media/Pipeline/Seeker.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Media/Debug.h>

using namespace OpenHome;
using namespace OpenHome::Media;

Seeker::Seeker(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstreamElement, ISeeker& aSeeker, ISeekRestreamer& aRestreamer, TUint aRampDuration)
    : iFlusher(aUpstreamElement, "Seeker")
    , iMsgFactory(aMsgFactory)
    , iUpstreamElement(aUpstreamElement)
    , iSeeker(aSeeker)
    , iRestreamer(aRestreamer)
    , iLock("SEEK")
    , iState(ERunning)
    , iRampDuration(aRampDuration)
    , iRemainingRampSize(0)
    , iCurrentRampValue(Ramp::kMax)
    , iSeekSeconds(UINT_MAX)
    , iTargetFlushId(MsgFlush::kIdInvalid)
    , iTrackId(Track::kIdNone)
    , iStreamId(IPipelineIdProvider::kStreamIdInvalid)
    , iStreamIsSeekable(false)
    , iSeekConsecutiveFailureCount(0)
    , iMsgStream(nullptr)
    , iSeekInNextStream(false)
    , iDecodeDiscardUntilSeekPoint(false)
{
}

Seeker::~Seeker()
{
    if (iMsgStream != nullptr) {
        iMsgStream->RemoveRef();
    }
}

void Seeker::Seek(TUint aStreamId, TUint aSecondsAbsolute, TBool aRampDown)
{
    LOG(kPipeline, "> Seeker::Seek(%u, %u, %u)\n", aStreamId, aSecondsAbsolute, aRampDown);
    AutoMutex a(iLock);
    if (iState != ERunning) {
        LOG(kPipeline, "Seek request rejected - iState = %u\n", iState);
        THROW(SeekAlreadyInProgress);
    }
    if (iStreamId != aStreamId) {
        LOG(kPipeline, "Seek request rejected - iStreamId=%u\n", iStreamId);
        THROW(SeekStreamInvalid);
    }
    if (!iStreamIsSeekable) {
        LOG(kPipeline, "Seek request rejected - stream is not seekable\n");
        THROW(SeekStreamNotSeekable);
    }
    if (iTrackLengthSeconds != 0 && aSecondsAbsolute > iTrackLengthSeconds) {
        LOG(kPipeline, "Seek request rejected - seekPos (%u) > trackLength (%u)\n", aSecondsAbsolute, iTrackLengthSeconds);
        THROW(SeekPosInvalid);
    }

    iSeekSeconds = aSecondsAbsolute;
    iFlushEndJiffies = 0;

    if (iState == ERampingUp) {
        iState = ERampingDown;
        iRemainingRampSize = iRampDuration - iRemainingRampSize;
        // leave iCurrentRampValue unchanged
    }
    else if (!aRampDown || iState == EFlushing) {
        DoSeek();
    }
    else {
        LOG(kPipeline, "Seeker state -> RampingDown\n");
        iState = ERampingDown;
        iRemainingRampSize = iRampDuration;
        iCurrentRampValue = Ramp::kMax;
    }
}

Msg* Seeker::Pull()
{
    Msg* msg;
    do {
        msg = ((iStreamIsSeekable && !iQueue.IsEmpty())? iQueue.Dequeue() : iFlusher.Pull());
        iLock.Wait();
        msg = msg->Process(*this);
        iLock.Signal();
    } while (msg == nullptr);
    return msg;
}

Msg* Seeker::ProcessMsg(MsgMode* aMsg)
{
    iMode.Replace(aMsg->Mode());
    return aMsg;
}

Msg* Seeker::ProcessMsg(MsgTrack* aMsg)
{
    iTrackId = aMsg->Track().Id();
    return aMsg;
}

Msg* Seeker::ProcessMsg(MsgDrain* aMsg)
{
    return aMsg;
}

Msg* Seeker::ProcessMsg(MsgDelay* aMsg)
{
    return aMsg;
}

Msg* Seeker::ProcessMsg(MsgEncodedStream* aMsg)
{
    iRemainingRampSize = 0;
    iCurrentRampValue = Ramp::kMax;
    if (iState != EFlushing) {
        // don't move out of Flushing
        // ...we'll use iState to start a RampUp when processing the DecodedStream that'll follow
        iState = ERunning;
    }
    iSeekHandle = ISeeker::kHandleError;
    iTrackLengthSeconds = 0;
    iStreamPosJiffies = 0;
    iFlushEndJiffies = 0;
    iStreamId = aMsg->StreamId();
    iStreamIsSeekable = aMsg->Seekable();
    return aMsg;
}

Msg* Seeker::ProcessMsg(MsgStreamSegment* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* Seeker::ProcessMsg(MsgAudioEncoded* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* Seeker::ProcessMsg(MsgMetaText* aMsg)
{
    return ProcessFlushable(aMsg);
}

Msg* Seeker::ProcessMsg(MsgStreamInterrupted* aMsg)
{
    return aMsg;
}

Msg* Seeker::ProcessMsg(MsgHalt* aMsg)
{
    return aMsg;
}

Msg* Seeker::ProcessMsg(MsgFlush* aMsg)
{
    if (iTargetFlushId != MsgFlush::kIdInvalid && iTargetFlushId == aMsg->Id()) {
        ASSERT(iState == EFlushing);
        aMsg->RemoveRef();
        iTargetFlushId = MsgFlush::kIdInvalid;
        // leave iState as Flushing.  Processing of Encoded and Decoded streams relies on this
        iRemainingRampSize = iRampDuration;
        iCurrentRampValue = Ramp::kMin;
        return nullptr;
    }
    return aMsg;
}

Msg* Seeker::ProcessMsg(MsgWait* aMsg)
{
    return aMsg;
}

Msg* Seeker::ProcessMsg(MsgDecodedStream* aMsg)
{
    if (iMsgStream != nullptr) {
        iMsgStream->RemoveRef();
    }
    iMsgStream = aMsg;
    iMsgStream->AddRef();
    const DecodedStreamInfo& streamInfo = aMsg->StreamInfo();
    iTrackLengthSeconds = static_cast<TUint>(streamInfo.TrackLength() / Jiffies::kPerSecond);
    iStreamPosJiffies = Jiffies::PerSample(streamInfo.SampleRate()) * streamInfo.SampleStart();
    iDecodeDiscardUntilSeekPoint = false;
    iFlushEndJiffies = 0;
    if (iSeekInNextStream) {
        iSeekInNextStream = false;
        DoSeek();
    }
    else if (iState == EFlushing) { // we've just completed a seek
        if (iStreamPosJiffies == 0) {
            iState = ERunning;
        }
        else {
            iState = ERampingUp;
            iRemainingRampSize = iRampDuration;
            iCurrentRampValue = Ramp::kMin;
        }
    }
    return aMsg;
}

Msg* Seeker::ProcessMsg(MsgAudioPcm* aMsg)
{
    return ProcessAudio(aMsg);
}

Msg* Seeker::ProcessMsg(MsgAudioDsd* aMsg)
{
    return ProcessAudio(aMsg);
}

Msg* Seeker::ProcessAudio(MsgAudioDecoded* aMsg)
{
    if (iDecodeDiscardUntilSeekPoint && iFlushEndJiffies == iStreamPosJiffies) {
        ASSERT(iState == EFlushing);
        iState = ERampingUp;
        iRemainingRampSize = iRampDuration;
        iCurrentRampValue = Ramp::kMin;
        iFlushEndJiffies = 0;

        iQueue.EnqueueAtHead(aMsg);
        const DecodedStreamInfo& info = iMsgStream->StreamInfo();
        const TUint64 numSamples = iStreamPosJiffies / Jiffies::PerSample(info.SampleRate());
        return iMsgFactory.CreateMsgDecodedStream(info.StreamId(), info.BitRate(), info.BitDepth(),
                                                  info.SampleRate(), info.NumChannels(), info.CodecName(),
                                                  info.TrackLength(), numSamples, info.Lossless(),
                                                  info.Seekable(), info.Live(), info.AnalogBypass(),
                                                  info.Format(), info.Multiroom(), info.Profile(),
                                                  info.StreamHandler(), info.Ramp());
    }

    iStreamPosJiffies = aMsg->TrackOffset() + aMsg->Jiffies();
    if (iFlushEndJiffies != 0 && iFlushEndJiffies < iStreamPosJiffies) {
        ASSERT(iState == EFlushing);
        const TUint splitJiffies = (TUint)(iStreamPosJiffies - iFlushEndJiffies);
        MsgAudio* split = aMsg->Split(aMsg->Jiffies() - splitJiffies);
        if (split != nullptr) {
            iQueue.EnqueueAtHead(split);
        }
        iStreamPosJiffies = iFlushEndJiffies;
    }
    else if (iState == ERampingDown || iState == ERampingUp) {
        MsgAudio* split;
        if (aMsg->Jiffies() > iRemainingRampSize) {
            split = aMsg->Split(iRemainingRampSize);
            if (split != nullptr) {
                if (iState == ERampingDown) {
                    split->SetMuted();
                }
                iQueue.EnqueueAtHead(split);
                iStreamPosJiffies -= split->Jiffies();
            }
        }
        split = nullptr;
        const Ramp::EDirection direction = (iState == ERampingDown? Ramp::EDown : Ramp::EUp);
        iCurrentRampValue = aMsg->SetRamp(iCurrentRampValue, iRemainingRampSize, direction, split);
        if (split != nullptr) {
            if (iState == ERampingDown) {
                split->SetMuted();
            }
            iQueue.EnqueueAtHead(split);
            iStreamPosJiffies -= split->Jiffies();
        }
        if (iRemainingRampSize == 0) {
            if (iState == ERampingDown) {
                DoSeek();
            }
            else { // iState == ERampingUp
                iState = ERunning;
            }
        }
        return aMsg;
    }

    return ProcessFlushable(aMsg);
}

Msg* Seeker::ProcessMsg(MsgSilence* /*aMsg*/)
{
    ASSERTS(); // don't expect to see MsgSilence this far up the pipeline
               // ...and wouldn't immediately know how to handle it, e.g. if iFlushEndJiffies was set
    return nullptr;
}

Msg* Seeker::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* Seeker::ProcessMsg(MsgQuit* aMsg)
{
    return aMsg;
}

void Seeker::NotifySeekComplete(TUint aHandle, TUint aFlushId)
{
    LOG(kPipeline, "> Seeker::NotifySeekComplete(%u, %u))\n", aHandle, aFlushId);
    AutoMutex a(iLock);
    if (aHandle != iSeekHandle) {
        LOG(kPipeline, "> Seeker::NotifySeekComplete - ignoring (wrong handle)\n");
        return;
    }
    iTargetFlushId = aFlushId;
    if (aFlushId == MsgFlush::kIdInvalid) {
        HandleSeekFail();
    }
    else {
        iSeekConsecutiveFailureCount = 0;
        iFlusher.DiscardUntilFlush(iTargetFlushId);
        iState = EFlushing;
    }
}

void Seeker::DoSeek()
{
    LOG(kPipeline, "> Seeker::DoSeek()\n");
    iState = EFlushing; /* set this before calling StartSeek as its possible NotifySeekComplete
                           could be called from another thread before StartSeek returns. */
    iSeeker.StartSeek(iStreamId, iSeekSeconds, *this, iSeekHandle);
    if (iSeekHandle == ISeeker::kHandleError) {
        HandleSeekFail();
    }
    else {
        iQueue.Clear();
        iQueue.Enqueue(iMsgFactory.CreateMsgHalt()); /* inform downstream parties (StarvationMonitor)
                                                        that any subsequent break in audio is expected */
    }
}

Msg* Seeker::ProcessFlushable(Msg* aMsg)
{
    if (iState == EFlushing || iTargetFlushId != MsgFlush::kIdInvalid) {
        aMsg->RemoveRef();
        return nullptr;
    }
    return aMsg;
}

void Seeker::HandleSeekFail()
{
    TUint64 seekJiffies = ((TUint64)iSeekSeconds) * Jiffies::kPerSecond;
    if (seekJiffies > iStreamPosJiffies) {
        LOG(kPipeline, "Seeker::HandleSeekFail() flush until seek point\n");
        iFlushEndJiffies = seekJiffies;
        iState = EFlushing;
        iDecodeDiscardUntilSeekPoint = true;
        iSeekConsecutiveFailureCount = 0;
    }
    else if (seekJiffies == iStreamPosJiffies) {
        LOG(kPipeline, "Seeker::HandleSeekFail() (implausible but) already at seek point\n");
        iState = ERampingUp;
        iRemainingRampSize = iRampDuration;
        iCurrentRampValue = Ramp::kMin;
        iSeekConsecutiveFailureCount = 0;
    }
    else {
        if (++iSeekConsecutiveFailureCount == 1) {
            iTargetFlushId = iRestreamer.SeekRestream(iMode, iTrackId);
            iFlusher.DiscardUntilFlush(iTargetFlushId);
            iState = EFlushing;
            iSeekInNextStream = true;
            LOG(kPipeline, "Seeker::HandleSeekFail() SeekRestream returned %u\n", iTargetFlushId);
        }
        else {
            LOG_ERROR(kPipeline, "Seeker::HandleSeekFail() give up, already failed to seek twice\n");
            iTargetFlushId = MsgFlush::kIdInvalid;
            iSeekConsecutiveFailureCount = 0;
            iState = ERampingUp;
            iRemainingRampSize = iRampDuration;
            iCurrentRampValue = Ramp::kMin;
        }
    }
}

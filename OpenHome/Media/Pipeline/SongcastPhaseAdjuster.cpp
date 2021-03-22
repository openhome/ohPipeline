#include <OpenHome/Media/Pipeline/SongcastPhaseAdjuster.h>
#include <OpenHome/Types.h>
#include <OpenHome/Av/SourceFactory.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Private/Standard.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Media;


// SongcastPhaseAdjuster

const TUint SongcastPhaseAdjuster::kSupportedMsgTypes =   eMode
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

const TUint SongcastPhaseAdjuster::kDropLimitDelayOffsetJiffies;
const Brn SongcastPhaseAdjuster::kModeSongcast("Receiver"); // Av::SourceFactory::kSourceTypeReceiver

SongcastPhaseAdjuster::SongcastPhaseAdjuster(
    MsgFactory& aMsgFactory,
    IPipelineElementUpstream& aUpstreamElement,
    TUint aRampJiffiesLong,
    TUint aRampJiffiesShort,
    TBool aEnabled
)
    : PipelineElement(kSupportedMsgTypes)
    , iMsgFactory(aMsgFactory)
    , iUpstreamElement(aUpstreamElement)
    , iEnabled(aEnabled)
    , iModeSongcast(false)
    , iState(State::Running)
    , iLock("SPAL")
    , iUpdateCount(0)
    , iTrackedJiffies(0)
    , iAudioIn(0)
    , iAudioOut(0)
    , iDecodedStream(nullptr)
    , iMsgSilenceJiffies(0)
    , iMsgAudioJiffies(0)
    , iDelayJiffies(0)
    , iDropLimitJiffies(0)
    , iDroppedJiffies(0)
    , iRampJiffiesLong(aRampJiffiesLong)
    , iRampJiffiesShort(aRampJiffiesShort)
    , iRampJiffies(iRampJiffiesLong)
    , iRemainingRampSize(0)
    , iCurrentRampValue(Ramp::kMin)
{
}

SongcastPhaseAdjuster::~SongcastPhaseAdjuster()
{
    ClearDecodedStream();
}

Msg* SongcastPhaseAdjuster::Pull()
{
    Msg* msg = nullptr;
    do {
        if (!iQueue.IsEmpty()) {
            msg = iQueue.Dequeue();
        }
        else {
            msg = iUpstreamElement.Pull();
        }
        msg = msg->Process(*this);
    } while (msg == nullptr);
    return msg;
}

Msg* SongcastPhaseAdjuster::ProcessMsg(MsgMode* aMsg)
{
    if (aMsg->Mode() == kModeSongcast) {
        iModeSongcast = true;
        iRampJiffies = aMsg->Info().RampPauseResumeLong()?
                        iRampJiffiesLong : iRampJiffiesShort;
        ResetPhaseDelay();
    }
    else {
        iModeSongcast = false;
        iState = State::Running;
    }
    return aMsg;
}

Msg* SongcastPhaseAdjuster::ProcessMsg(MsgDrain* aMsg)
{
    if (iModeSongcast) {
        ResetPhaseDelay();
    }

    return aMsg;
}

Msg* SongcastPhaseAdjuster::ProcessMsg(MsgDelay* aMsg)
{
    if (iModeSongcast) {
        iDelayJiffies = aMsg->DelayJiffies();
        if (iDelayJiffies > kDropLimitDelayOffsetJiffies) {
            iDropLimitJiffies = iDelayJiffies - kDropLimitDelayOffsetJiffies;
        }
    }
    aMsg->RemoveRef();
    return nullptr;
}

Msg* SongcastPhaseAdjuster::ProcessMsg(MsgFlush* aMsg)
{
    return aMsg;
}

Msg* SongcastPhaseAdjuster::ProcessMsg(MsgDecodedStream* aMsg)
{
    ClearDecodedStream();
    if (iModeSongcast) {
        aMsg->AddRef();
        iDecodedStream = aMsg;
    }
    return aMsg;
}

Msg* SongcastPhaseAdjuster::ProcessMsg(MsgAudioPcm* aMsg)
{
    if (iEnabled && iModeSongcast) {
        iMsgAudioJiffies += aMsg->Jiffies();
        PrintStats(Brn("audio"), iMsgAudioJiffies);
        return AdjustAudio(Brn("audio"), aMsg);
    }
    return aMsg;
}

Msg* SongcastPhaseAdjuster::ProcessMsg(MsgSilence* aMsg)
{
    if (iEnabled && iModeSongcast) {
        // Delay will increase and/or gain accuracy the more silence is allowed to pass through the pipeline.
        // Therefore, easiest to allow all MsgSilence to pass to get a snapshot of delay when first MsgAudio seen, and only drop from the start of MsgAudio.
        // Otherwise, if start dropping too early in MsgSilence, can end up dropping so many MsgSilence that we don't get a reasonable estimate of accumulated error and quickly bring the error close to 0 and stop dropping early on in MsgAudio.
        iMsgSilenceJiffies += aMsg->Jiffies();
        PrintStats(Brn("silence"), iMsgSilenceJiffies);
        // return AdjustAudio(Brn("silence"), aMsg);
    }
    return aMsg;
}

void SongcastPhaseAdjuster::Update(TInt aDelta)
{
    iTrackedJiffies += aDelta;
    iUpdateCount++;

    if (aDelta < 0) {
        iAudioOut -= aDelta;
    }
    else {
        iAudioIn += aDelta;
    }
}

void SongcastPhaseAdjuster::Start()
{
}

void SongcastPhaseAdjuster::Stop()
{
}

MsgAudio* SongcastPhaseAdjuster::AdjustAudio(const Brx& aMsgType, MsgAudio* aMsg)
{
    if (iState == State::Adjusting) {
        if (iDelayJiffies == 0) {
            // No MsgDelay (with value > 0) was seen. Switch to running state.
            iState = State::Running;
            return aMsg;
        }
        TInt error = iTrackedJiffies - iDelayJiffies;
        if (error > 0) {
            // Drop audio.
            if (iDroppedJiffies + error > iDropLimitJiffies) {
                error = iDropLimitJiffies - iDroppedJiffies;
            }
            TUint dropped = 0;
            MsgAudio* msg = aMsg;
            if (error > 0) { // Error may have become 0 in drop limit calc above.
                msg = DropAudio(aMsg, error, dropped);
            }
            iDroppedJiffies += dropped;
            error -= dropped;
            if (iDroppedJiffies >= iDropLimitJiffies || error == 0) {
                // Have dropped audio so must now ramp up.
                return StartRampUp(msg);
            }
            return msg;
        }
        else if (error < 0) {
            // Error is 0 or receiver is in front of sender. Highly unlikely receiver would get in front of sender. Any error would likely be minimal. Do nothing.
            // If error < 0, could inject MsgSilence to pull the error in towards 0.
            return aMsg;
        }
        else {
            // error == 0.
            if (iDroppedJiffies > 0) {
                return StartRampUp(aMsg);
            }
            else {
                iState = State::Running;
            }
            return aMsg;
        }
    }
    else if (iState == State::RampingUp) {
        return RampUp(aMsg);
    }
    else {
        return aMsg;
    }
}

MsgAudio* SongcastPhaseAdjuster::DropAudio(MsgAudio* aMsg, TUint aJiffies, TUint& aDroppedJiffies)
{
    ASSERT(aMsg != nullptr);
    if (aJiffies >= aMsg->Jiffies()) {
        aDroppedJiffies = aMsg->Jiffies();
        aMsg->RemoveRef();
        return nullptr;
    }
    else if (aJiffies < aMsg->Jiffies()) {
        auto* remaining = aMsg->Split(aJiffies);
        aDroppedJiffies = aJiffies;
        aMsg->RemoveRef();
        return remaining;
    }
    // aJiffies == 0.
    aDroppedJiffies = 0;
    return aMsg;
}

MsgSilence* SongcastPhaseAdjuster::InjectSilence(TUint aJiffies)
{
    ASSERT(iDecodedStream != nullptr);
    const auto& stream = iDecodedStream->StreamInfo();
    TUint jiffies = aJiffies;
    auto* msg = iMsgFactory.CreateMsgSilence(jiffies, stream.SampleRate(), stream.BitDepth(), stream.NumChannels());
    iInjectedJiffies += jiffies;
    return msg;
}

MsgAudio* SongcastPhaseAdjuster::RampUp(MsgAudio* aMsg)
{
    ASSERT(aMsg != nullptr);
    MsgAudio* split;
    if (aMsg->Jiffies() > iRemainingRampSize && iRemainingRampSize > 0) {
        split = aMsg->Split(iRemainingRampSize);
        if (split != nullptr) {
            iQueue.EnqueueAtHead(split);
        }
    }
    split = nullptr;
    if (iRemainingRampSize > 0) {
        iCurrentRampValue = aMsg->SetRamp(iCurrentRampValue, iRemainingRampSize, Ramp::EUp, split);
    }
    if (split != nullptr) {
        iQueue.EnqueueAtHead(split);
    }
    if (iRemainingRampSize == 0) {
        iState = State::Running;
    }
    return aMsg;
}

MsgAudio* SongcastPhaseAdjuster::StartRampUp(MsgAudio* aMsg)
{
    iState = State::RampingUp;
    iRemainingRampSize = iRampJiffies;
    if (aMsg != nullptr) {
        return RampUp(aMsg);
    }
    return aMsg;
}

void SongcastPhaseAdjuster::ResetPhaseDelay()
{
    iState = State::Adjusting;

    iMsgSilenceJiffies = 0;
    iMsgAudioJiffies = 0;

    iDelayJiffies = 0;
    iDropLimitJiffies = 0;
    iDroppedJiffies = 0;
    iInjectedJiffies = 0;

    iRemainingRampSize = iRampJiffies;
    iCurrentRampValue = Ramp::kMin;
}

void SongcastPhaseAdjuster::ClearDecodedStream()
{
    if (iDecodedStream != nullptr) {
        iDecodedStream->RemoveRef();
        iDecodedStream = nullptr;
    }
}

void SongcastPhaseAdjuster::PrintStats(const Brx& /*aMsgType*/, TUint /*aJiffies*/)
{
    // static const TUint kInitialJiffiesTrackingLimit = 50 * Jiffies::kPerMs;
    // static const TUint kJiffiesStatsInterval = 50 * Jiffies::kPerMs;
    // static const TUint kJiffiesStatsLimit = 500 * Jiffies::kPerMs;
    // if ((aJiffies < kInitialJiffiesTrackingLimit || aJiffies % kJiffiesStatsInterval == 0) && aJiffies <= kJiffiesStatsLimit) {
    //     const TInt tj = iTrackedJiffies;
    //     const TInt err = tj - iDelayJiffies;
    //     const TUint in = iAudioIn;
    //     const TUint out = iAudioOut;
    //
    //     Log::Print("SongcastPhaseAdjuster::PrintStats aMsgType: %.*s, aJiffies: %u (%u ms), tracked jiffies: %d (%u ms), err: %d (%u ms), in: %u (%u ms), out: %u (%u ms)\n", PBUF(aMsgType), aJiffies, Jiffies::ToMs(aJiffies), tj, Jiffies::ToMs((TUint)tj), err, Jiffies::ToMs((TUint)err), in, Jiffies::ToMs(in), out, Jiffies::ToMs(out));
    // }
}

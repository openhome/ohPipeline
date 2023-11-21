#include <OpenHome/Media/Pipeline/StarterTimed.h>
#include <OpenHome/Types.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Env.h>
#include <OpenHome/OsWrapper.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Media/Debug.h>

#include <algorithm>
#include <stdint.h>

using namespace OpenHome;
using namespace OpenHome::Media;

const TUint StarterTimed::kSupportedMsgTypes =   eMode
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
                                               | eAudioPcm
                                               | eAudioDsd
                                               | eSilence
                                               | eQuit;

const TUint StarterTimed::kMaxSilenceJiffies = Jiffies::kPerMs * 5;

StarterTimed::StarterTimed(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstream, IAudioTime& aAudioTime)
    : PipelineElement(kSupportedMsgTypes)
    , iMsgFactory(aMsgFactory)
    , iUpstream(aUpstream)
    , iAudioTime(aAudioTime)
    , iAnimator(nullptr)
    , iLock("STim")
    , iStartTicks(0)
    , iSampleRate(0)
    , iBitDepth(0)
    , iNumChannels(0)
    , iAnimatorDelayJiffies(0)
    , iPending(nullptr)
    , iJiffiesRemaining(0)
{
}

StarterTimed::~StarterTimed()
{
    if (iPending != nullptr) {
        iPending->RemoveRef();
    }
}

void StarterTimed::SetAnimator(IPipelineAnimator& aAnimator)
{
    iAnimator = &aAnimator;
}

void StarterTimed::StartAt(TUint64 aTime)
{
    AutoMutex _(iLock);
    iStartTicks = aTime;
    LOG(kMedia, "StarterTimed::StartAt(%llu)\n", iStartTicks);
}

Msg* StarterTimed::Pull()
{
    Msg* msg = nullptr;
    do {
        if (iJiffiesRemaining != 0) {
            TUint jiffies = std::min(iJiffiesRemaining, kMaxSilenceJiffies);
            if (iFormat == AudioFormat::Pcm) {
                msg = iMsgFactory.CreateMsgSilence(jiffies, iSampleRate, iBitDepth, iNumChannels);
            }
            else {
                msg = iMsgFactory.CreateMsgSilenceDsd(jiffies, iSampleRate, iNumChannels, 6, 2); // DSD sample block words
            }
            if (iJiffiesRemaining < kMaxSilenceJiffies) {
                iJiffiesRemaining = 0; // CreateMsgSilence rounds to nearest sample so jiffies>iJiffiesRemaining is possible in the final call
            }
            else {
                iJiffiesRemaining -= jiffies;
            }
        }
        else if (iPending != nullptr) {
            msg = iPending;
            iPending = nullptr;
        }
        else {
            msg = iUpstream.Pull();
            msg = msg->Process(*this);
        }
    } while (msg == nullptr);
    return msg;
}

Msg* StarterTimed::ProcessMsg(MsgDecodedStream* aMsg)
{
    const auto& info = aMsg->StreamInfo();
    iSampleRate = info.SampleRate();
    iBitDepth = info.BitDepth();
    iNumChannels = info.NumChannels();
    iFormat = info.Format();

    ASSERT(iAnimator != nullptr);
    iAnimatorDelayJiffies = iAnimator->PipelineAnimatorDelayJiffies(iFormat, iSampleRate, iBitDepth, iNumChannels);
    return aMsg;
}

Msg* StarterTimed::ProcessMsg(MsgAudioPcm* aMsg)
{
    return HandleAudioReceived(aMsg);
}

Msg* StarterTimed::ProcessMsg(MsgAudioDsd* aMsg)
{
    return HandleAudioReceived(aMsg);
}

TUint StarterTimed::CalculateDelayJiffies(TUint64 aStartTicks)
{
    TUint64 ticksNow;
    TUint freq;
    iAudioTime.GetTickCount(iSampleRate, ticksNow, freq);

    if (aStartTicks <= ticksNow) {
        TUint64 lateTicks = (ticksNow - aStartTicks);
        TUint lateMs = (TUint)((lateTicks * 1000) / freq);
        LOG(kMedia, "StarterTimed: start time in past (%ums late) - (%llu / %llu)\n", lateMs, aStartTicks, ticksNow);
        return 0;
    }

    const TUint kMaxTicks = 5 * freq; // 5 seconds in ticks
    const TUint64 kDelayTicks = aStartTicks - ticksNow;
    if (kDelayTicks > kMaxTicks) {
        const TUint64 secs = kDelayTicks / freq;
        LOG(kMedia, "StarterTimed: start suspiciously far in the future (> %llu seconds) - (%llu / %llu)\n", secs, aStartTicks, ticksNow);
        return 0;
    }

    ASSERT((UINT64_MAX / kMaxTicks) > Jiffies::kPerSecond); // Ensure enough precision
    TUint64 delayJiffies = (kDelayTicks * Jiffies::kPerSecond) / freq;

    if (delayJiffies <= iAnimatorDelayJiffies) {
        LOG(kMedia, "StarterTimed: Animator delay (%ums) exceeds requested start time (%ums)\n", Jiffies::ToMs(iAnimatorDelayJiffies), Jiffies::ToMs(delayJiffies));
        return 0;
    }
    delayJiffies -= iAnimatorDelayJiffies;

    LOG(kMedia, "StarterTimed: delay jiffies=%llu (%ums)\n", delayJiffies, Jiffies::ToMs(delayJiffies));
    return (TUint)delayJiffies;
}

Msg* StarterTimed::HandleAudioReceived(MsgAudioDecoded* aMsg)
{
    AutoMutex _(iLock);
    if (iStartTicks == 0) {
        iJiffiesRemaining = 0;
        return aMsg;
    }

    iJiffiesRemaining = CalculateDelayJiffies(iStartTicks);
    iStartTicks = 0;
    iPending = aMsg;
    return nullptr;
}


// AudioTimeCpu

AudioTimeCpu::AudioTimeCpu(Environment& aEnv)
    : iOsCtx(aEnv.OsCtx())
    , iTicksAdjustment(0)
{
}

void AudioTimeCpu::GetTickCount(TUint /*aSampleRate*/, TUint64& aTicks, TUint& aFrequency) const
{
    static const TUint kUsTicksPerSecond = 1000000;
    aFrequency = kUsTicksPerSecond;
    aTicks = Os::TimeInUs(iOsCtx);
    aTicks += iTicksAdjustment;
}

void AudioTimeCpu::SetTickCount(TUint64 aTicks)
{
    iTicksAdjustment = aTicks - Os::TimeInUs(iOsCtx);
}

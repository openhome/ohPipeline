#include <OpenHome/Media/Utils/AnimatorBasic.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/OsWrapper.h>
#include <OpenHome/Private/Env.h>
#include <OpenHome/Media/Debug.h>

using namespace OpenHome;
using namespace OpenHome::Media;

PriorityArbitratorAnimator::PriorityArbitratorAnimator(TUint aOpenHomeMax)
    : iOpenHomeMax(aOpenHomeMax)
{
}

TUint PriorityArbitratorAnimator::Priority(const TChar* /*aId*/, TUint aRequested, TUint aHostMax)
{
    ASSERT(aRequested == iOpenHomeMax);
    return aHostMax;
}

TUint PriorityArbitratorAnimator::OpenHomeMin() const
{
    return iOpenHomeMax;
}

TUint PriorityArbitratorAnimator::OpenHomeMax() const
{
    return iOpenHomeMax;
}

TUint PriorityArbitratorAnimator::HostRange() const
{
    return 1;
}

const TUint AnimatorBasic::kSupportedMsgTypes =   eMode
                                                | eDrain
                                                | eHalt
                                                | eDecodedStream
                                                | ePlayable
                                                | eQuit;

AnimatorBasic::AnimatorBasic(
    Environment& aEnv,
    IPipeline& aPipeline,
    TBool aPullable,
    TUint aDsdMaxSampleRate,
    TUint aDsdSampleBlockWords,
    TUint aDsdPadBytesPerWord)
    : PipelineElement(kSupportedMsgTypes)
    , iPipeline(aPipeline)
    , iSem("DRVB", 0)
    , iOsCtx(aEnv.OsCtx())
    , iPullable(aPullable)
    , iDsdMaxSampleRate(aDsdMaxSampleRate)
    , iDsdSampleBlockWords(aDsdSampleBlockWords)
    , iDsdBlockWordsNoPad((aDsdSampleBlockWords * 4) / (kDsdPlayableBytesPerChunk + aDsdPadBytesPerWord))
    , iSampleRate(0)
    , iPlayable(nullptr)
    , iPullValue(IPullableClock::kNominalFreq)
    , iRamping(false)
    , iQuit(false)
{
    iPipeline.SetAnimator(*this);
    iThread = new ThreadFunctor("PipelineAnimator", MakeFunctor(*this, &AnimatorBasic::DriverThread), kPrioritySystemHighest);
    iThread->Start();
}

AnimatorBasic::~AnimatorBasic()
{
    delete iThread;
}

void AnimatorBasic::DriverThread()
{
    // pull the first (assumed non-audio) msg here so that any delays populating the pipeline don't affect timing calculations below.
    {
        Msg* msg = iPipeline.Pull();
        ASSERT(msg != nullptr);
        (void)msg->Process(*this);
    }

    TUint64 now = OsTimeInUs(iOsCtx);
    iLastTimeUs = now;
    iNextTimerDuration = kTimerFrequencyMs;
    iPendingJiffies = kTimerFrequencyMs * Jiffies::kPerMs;
    try {
        for (;;) {
            while (iPendingJiffies > 0) {
                if (iPlayable != nullptr) {
                    ProcessAudio(iPlayable);
                }
                else {
                    Msg* msg = iPipeline.Pull();
                    msg = msg->Process(*this);
                    ASSERT(msg == nullptr);
                }
            }
            if (iQuit) {
                break;
            }
            iLastTimeUs = now;
            if (iNextTimerDuration != 0) {
                try {
                    iSem.Wait(iNextTimerDuration);
                }
                catch (Timeout&) {}
            }
            iNextTimerDuration = kTimerFrequencyMs;
            now = OsTimeInUs(iOsCtx);
            const TUint diffMs = ((TUint)(now - iLastTimeUs + 500)) / 1000;
            if (diffMs > 100) { // assume delay caused by drop-out.  process regular amount of audio
                iPendingJiffies = kTimerFrequencyMs * Jiffies::kPerMs;
            }
            else {
                iPendingJiffies = diffMs * Jiffies::kPerMs;
                if (iPullValue != IPullableClock::kNominalFreq) {
                    TInt64 pending64 = iPullValue * iPendingJiffies;
                    pending64 /= IPullableClock::kNominalFreq;
                    //Log::Print("iPendingJiffies=%08x, pull=%08x\n", iPendingJiffies, pending64); // FIXME
                    //TInt pending = (TInt)iPendingJiffies + (TInt)pending64;
                    //Log::Print("Pulled clock, now want %u jiffies (%ums, %d%%) extra\n", (TUint)pending, Jiffies::ToMs(pending), (pending-(TInt)iPendingJiffies)/iPendingJiffies); // FIXME
                    iPendingJiffies = (TUint)pending64;
                }
            }
        }
    }
    catch (ThreadKill&) {}

    // pull until the pipeline is emptied
    while (!iQuit) {
        Msg* msg = iPipeline.Pull();
        msg = msg->Process(*this);
        ASSERT(msg == nullptr);
        if (iPlayable != nullptr) {
            iPlayable->RemoveRef();
        }
    }
}

TUint AnimatorBasic::JiffiesTotalToJiffiesPlayableDsd(TUint& aTotalJiffies)
{
    const TUint totalSampleBlockJiffies = (iDsdSampleBlockWords * 4) * 8 * iJiffiesPerSample;
    aTotalJiffies -= (aTotalJiffies % totalSampleBlockJiffies);
    TUint playableJiffies = aTotalJiffies / iDsdSampleBlockWords;
    playableJiffies *= iDsdBlockWordsNoPad;
    return playableJiffies;
}

TUint AnimatorBasic::JiffiesPlayableToJiffiesTotalDsd(TUint& aPlayableJiffies)
{
    const TUint playableSampleBlockJiffies = (iDsdBlockWordsNoPad * 4) * 8 * iJiffiesPerSample;
    aPlayableJiffies -= (aPlayableJiffies % playableSampleBlockJiffies);
    TUint totalJiffies = aPlayableJiffies / iDsdBlockWordsNoPad;
    totalJiffies *= iDsdSampleBlockWords;
    return totalJiffies;
}

void AnimatorBasic::ProcessAudio(MsgPlayable* aMsg)
{
    iPlayable = nullptr;
    const TUint numSamples = (aMsg->Bytes() * 8) / (iBitDepth * iNumChannels);
    TUint jiffies = numSamples * iJiffiesPerSample;
    if (iFormat == AudioFormat::Dsd) {
        jiffies = JiffiesTotalToJiffiesPlayableDsd(jiffies);
    }
    if (jiffies > iPendingJiffies) {
        jiffies = iPendingJiffies;
        TUint bytes = 0;
        if (iFormat == AudioFormat::Pcm) {
            bytes = Jiffies::ToBytes(jiffies, iJiffiesPerSample, iNumChannels, iBitDepth);
        }
        else if (iFormat == AudioFormat::Dsd) {
            const TUint totalSamplesPerBlock = ((iDsdSampleBlockWords * 4) * 8) / iNumChannels;
            TUint msgJiffies = JiffiesPlayableToJiffiesTotalDsd(jiffies);
            bytes = Jiffies::ToBytesSampleBlock(msgJiffies, iJiffiesPerSample, iNumChannels, iBitDepth, totalSamplesPerBlock);
        }
        if (bytes == 0) {
            iPendingJiffies = 0;
            iPlayable = aMsg;
            return;
        }
        iPlayable = aMsg->Split(bytes);
    }
    iPendingJiffies -= jiffies;
    aMsg->RemoveRef();
}

Msg* AnimatorBasic::ProcessMsg(MsgMode* aMsg)
{
    iPullValue = IPullableClock::kNominalFreq;
    aMsg->RemoveRef();
    return nullptr;
}

Msg* AnimatorBasic::ProcessMsg(MsgDrain* aMsg)
{
    if (iPlayable != nullptr) {
        iPlayable->RemoveRef();
        iPlayable = nullptr;
    }
    if (iSampleRate != 0) {
        PullClock(IPullableClock::kNominalFreq);
    }
    aMsg->ReportDrained();
    aMsg->RemoveRef();
    return nullptr;
}

Msg* AnimatorBasic::ProcessMsg(MsgHalt* aMsg)
{
    Log::Print("AnimatorBasic - MsgHalt\n");
    iPendingJiffies = 0;
    iNextTimerDuration = 0;
    aMsg->ReportHalted();
    aMsg->RemoveRef();
    return nullptr;
}

Msg* AnimatorBasic::ProcessMsg(MsgDecodedStream* aMsg)
{
    const DecodedStreamInfo& stream = aMsg->StreamInfo();
    iFormat = stream.Format();
    iSampleRate = stream.SampleRate();
    iNumChannels = stream.NumChannels();
    iBitDepth = stream.BitDepth();
    Log::Print("AnimatorBasic - MsgDecodedStream - %u/%u/%u\n", iSampleRate, iBitDepth, iNumChannels);
    iJiffiesPerSample = Jiffies::PerSample(iSampleRate);
    iRamping = false;
    aMsg->RemoveRef();
    return nullptr;
}

Msg* AnimatorBasic::ProcessMsg(MsgPlayable* aMsg)
{
    const TBool ramping = aMsg->Ramp().IsEnabled();
    if (ramping && !iRamping) {
        Log::Print("AnimatorBasic - ramping\n");
    }
    iRamping = ramping;
    ProcessAudio(aMsg);
    return nullptr;
}

Msg* AnimatorBasic::ProcessMsg(MsgQuit* aMsg)
{
    iQuit = true;
    iPendingJiffies = 0;
    iNextTimerDuration = 0;
    aMsg->RemoveRef();
    return nullptr;
}

void AnimatorBasic::PullClock(TUint aMultiplier)
{
    if (!iPullable || iPullValue == aMultiplier) {
        return;
    }
    iPullValue = aMultiplier;
    LOG(kPipeline, "AnimatorBasic::PullClock now at %u%%\n", (TUint)((iPullValue * 100) / IPullableClock::kNominalFreq));
}

TUint AnimatorBasic::MaxPull() const
{
    static const TUint kMaxPull = (kNominalFreq / 100) * 4; // 4% of nominal
    return kMaxPull;
}

TUint AnimatorBasic::PipelineAnimatorBufferJiffies() const
{
    return 0;
}

TUint AnimatorBasic::PipelineAnimatorDelayJiffies(AudioFormat /*aFormat*/, TUint /*aSampleRate*/,
                                                  TUint /*aBitDepth*/, TUint /*aNumChannels*/) const
{
    return 0;
}

void AnimatorBasic::PipelineAnimatorDsdBlockConfiguration(TUint& aSampleBlockWords, TUint& aPadBytesPerChunk) const
{
    aSampleBlockWords = 1;
    aPadBytesPerChunk = 0;
}

TUint AnimatorBasic::PipelineAnimatorMaxBitDepth() const
{
    return 32;
}

void AnimatorBasic::PipelineAnimatorGetMaxSampleRates(TUint& aPcm, TUint& aDsd) const
{
    aPcm = 384000;
    aDsd = iDsdMaxSampleRate;
}

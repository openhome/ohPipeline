#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/ClockPuller.h>
#include <OpenHome/Media/Utils/ProcessorAudioUtils.h>
#include <OpenHome/Net/Core/DvDevice.h>
#include <OpenHome/Media/Pipeline/Pipeline.h>

namespace OpenHome {
    class Environment;
namespace Media {

class PriorityArbitratorAnimator : public IPriorityArbitrator, private INonCopyable
{
public:
    PriorityArbitratorAnimator(TUint aOpenHomeMax);
private: // from IPriorityArbitrator
    TUint Priority(const TChar* aId, TUint aRequested, TUint aHostMax) override;
    TUint OpenHomeMin() const override;
    TUint OpenHomeMax() const override;
    TUint HostRange() const override;
private:
    const TUint iOpenHomeMax;
};

class AnimatorBasic : public PipelineElement, public IPullableClock, public IPipelineAnimator
{
    static const TUint kTimerFrequencyMs = 5;
    static const TUint kSupportedMsgTypes;
    static const TUint kDsdPlayableBytesPerChunk = 4;
public:
    AnimatorBasic(
        Environment& aEnv,
        IPipeline& aPipeline,
        TBool aPullable,
        TUint aDsdMaxSampleRate,
        TUint aDsdSampleBlockWords,
        TUint aDsdPadBytesPerWord);
    ~AnimatorBasic();
private:
    void DriverThread();
    void ProcessAudio(MsgPlayable* aMsg);
    TUint JiffiesTotalToJiffiesPlayableDsd(TUint& aTotalJiffies);
    TUint JiffiesPlayableToJiffiesTotalDsd(TUint& aPlayableJiffies);
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgDrain* aMsg) override;
    Msg* ProcessMsg(MsgHalt* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsg(MsgPlayable* aMsg) override;
    Msg* ProcessMsg(MsgQuit* aMsg) override;
private: // from IPullableClock
    void PullClock(TUint aMultiplier) override;
    TUint MaxPull() const override;
private: // from IPipelineAnimator
    TUint PipelineAnimatorBufferJiffies() const override;
    TUint PipelineAnimatorDelayJiffies(AudioFormat aFormat, TUint aSampleRate, TUint aBitDepth, TUint aNumChannels) const override;
    TUint PipelineAnimatorDsdBlockSizeWords() const override;
    TUint PipelineAnimatorMaxBitDepth() const override;
    void PipelineAnimatorGetMaxSampleRates(TUint& aPcm, TUint& aDsd) const override;
private:
    IPipeline& iPipeline;
    Semaphore iSem;
    OsContext* iOsCtx;
    ThreadFunctor *iThread;
    const TBool iPullable;
    const TUint iDsdMaxSampleRate;
    const TUint iDsdSampleBlockWords;
    const TUint iDsdBlockWordsNoPad;
    AudioFormat iFormat;
    TUint iSampleRate;
    TUint iJiffiesPerSample;
    TUint iNumChannels;
    TUint iBitDepth;
    TUint iPendingJiffies;
    TUint64 iLastTimeUs;
    TUint iNextTimerDuration;
    MsgPlayable* iPlayable;
    TUint64 iPullValue;
    TBool iRamping;
    TBool iQuit;
};

} // namespace Media
} // namespace OpenHome


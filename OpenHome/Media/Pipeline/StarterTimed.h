#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Env.h>
#include <OpenHome/Private/Thread.h>

EXCEPTION(AudioTimeNotSupported)

namespace OpenHome {
namespace Media {

class IAudioTime
{
public:
    virtual ~IAudioTime() {}
    virtual void GetTickCount(TUint aSampleRate, TUint64& aTicks, TUint& aFrequency) const = 0;
    virtual void SetTickCount(TUint64 aTicks) = 0;
};

class IStarterTimed
{
public:
    virtual ~IStarterTimed() {};
    virtual void StartAt(TUint64 aTime) = 0; // aTime units are the same as returned by IAudioTime::GetTickCount
};

class StarterTimed : public PipelineElement, public IPipelineElementUpstream, public IStarterTimed
{
    static const TUint kSupportedMsgTypes;
    static const TUint kMaxSilenceJiffies;
public:
    StarterTimed(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstream, IAudioTime& aAudioTime);
    ~StarterTimed();
public:
    void SetAnimator(IPipelineAnimator& aAnimator);
public: // from IStarterTimed
    void StartAt(TUint64 aTime) override;
private: // from IPipelineElementUpstream
    Msg* Pull() override;
private: // from PipelineElement
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgAudioDsd* aMsg) override;
private:
    TUint CalculateDelayJiffies(TUint64 aStartTicks);
    Msg* HandleAudioReceived(MsgAudioDecoded* aMsg);
private:
    MsgFactory& iMsgFactory;
    IPipelineElementUpstream& iUpstream;
    IAudioTime& iAudioTime;
    IPipelineAnimator* iAnimator;
    Mutex iLock;
    TUint64 iStartTicks; // 0 => disabled
    TUint iSampleRate;
    TUint iBitDepth;
    TUint iNumChannels;
    TUint iAnimatorDelayJiffies;
    AudioFormat iFormat;
    Msg* iPending;
    TUint iJiffiesRemaining;
};

class AudioTimeCpu : public IAudioTime
{
public:
    AudioTimeCpu(Environment& aEnv);
private: // from IAudioTime
    void GetTickCount(TUint aSampleRate, TUint64& aTicks, TUint& aFrequency) const override;
    void SetTickCount(TUint64 aTicks) override;
private:
    OsContext* iOsCtx;
    TInt64 iTicksAdjustment;
};

} // namespace Media
} // namespace OpenHome

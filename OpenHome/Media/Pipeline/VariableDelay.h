#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/ClockPuller.h>

#include <atomic>

namespace OpenHome {
namespace Media {

/*
Element which introduces a delay (likely for lip syncing)
If the delay is increased, silence is introduced.
If the delay is decreased, audio (pulled from upstream) is discarded.
Before any change in delay is actioned, audio spends RampDuration ramping down.
After a delay is actioned, audio spends RampDuration ramping up.
FIXME - no handling of pause-resumes
*/

class IVariableDelayObserver
{
public:
    virtual ~IVariableDelayObserver() {}
    virtual void NotifyDelayApplied(TUint aJiffies) = 0;
};

class VariableDelayBase : public PipelineElement, public IPipelineElementUpstream
{
    static const TUint kMaxMsgSilenceDuration = Jiffies::kPerMs * 2;
    friend class SuiteVariableDelay;
    friend class SuiteVariableDelayLeft;
    friend class SuiteVariableDelayRight;
    static const TUint kSupportedMsgTypes;
public:
    virtual ~VariableDelayBase();
    void SetAnimator(IPipelineAnimator& aAnimator);
protected:
    VariableDelayBase(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstreamElement, TUint aRampDuration, const TChar* aId);
public: // from IPipelineElementUpstream
    Msg* Pull() override;
protected:
    void HandleDelayChange(TUint aNewDelay);
    TBool StreamInfoHasChanged(const DecodedStreamInfo& aInfo) const;
    inline const TChar* Status() const;
private:
    Msg* DoPull();
    Msg* NextMsg();
    void ApplyAnimatorOverride();
    void RampMsg(MsgAudio* aMsg);
    void ResetStatusAndRamp();
    void SetupRamp();
    MsgDecodedStream* UpdateDecodedStream(TUint64 aTrackOffset);
    Msg* ProcessAudioDecoded(MsgAudioDecoded* aMsg);
protected: // from PipelineElement (IMsgProcessor)
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgDrain* aMsg) override;
    Msg* ProcessMsg(MsgFlush* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgAudioDsd* aMsg) override;
    Msg* ProcessMsg(MsgSilence* aMsg) override;
private:
    virtual void LocalDelayApplied() = 0;
private:
    enum EStatus
    {
        EStarting
       ,ERunning
       ,ERampingDown
       ,ERampedDown
       ,ERampingUp
    };
protected:
    MsgFactory& iMsgFactory;
    Mutex iLock;
    IClockPuller* iClockPuller;
    IPipelineAnimator* iAnimator;
    TUint iDelayJiffies;
    TInt iDelayAdjustment;
    MsgDecodedStream* iDecodedStream;
private:
    IPipelineElementUpstream& iUpstreamElement;
    const TUint iRampDuration;
    const TChar* iId;
    MsgQueueLite iQueue;
    EStatus iStatus;
    Ramp::EDirection iRampDirection;
    TBool iWaitForAudioBeforeGeneratingSilence;
    TUint iCurrentRampValue;
    TUint iRemainingRampSize;
    BwsMode iMode;
    MsgDecodedStream* iPendingStream;
    TUint iTargetFlushId;
    TUint iDsdSampleBlockWords;
    TUint iDsdPadBytesPerChunk;
};

class VariableDelayLeft : public VariableDelayBase
{
public:
    VariableDelayLeft(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstreamElement,
                      TUint aRampDuration, TUint aDownstreamDelay);
    void SetObserver(IVariableDelayObserver& aObserver);
private: // from PipelineElement (IMsgProcessor)
    using VariableDelayBase::ProcessMsg;
    Msg* ProcessMsg(MsgDelay* aMsg) override;
private: // from VariableDelayBase
    void LocalDelayApplied() override;
private:
    const TUint iDownstreamDelay; // leave this much delay for downstream
    IVariableDelayObserver* iObserver;
};

class VariableDelayRight : public VariableDelayBase
                         , public IVariableDelayObserver
{
public:
    VariableDelayRight(MsgFactory& aMsgFactory,
                       IPipelineElementUpstream& aUpstreamElement,
                       TUint aRampDuration, TUint aMinDelay);
private: // from PipelineElement (IMsgProcessor)
    using VariableDelayBase::ProcessMsg;
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgDelay* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
private: // from VariableDelayBase
    void LocalDelayApplied() override;
private: // from IVariableDelayObserver
    void NotifyDelayApplied(TUint aJiffies) override;
private:
    void AdjustDelayForAnimatorLatency();
    void StartClockPuller();
private:
    const TUint iMinDelay;
    TUint iDelayJiffiesTotal;
    TUint iAnimatorLatency;
    TUint iSampleRate;
    TUint iBitDepth;
    TUint iNumChannels;
};

} // namespace Media
} // namespace OpenHome

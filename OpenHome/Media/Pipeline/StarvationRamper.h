#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Private/Thread.h>

#include <cstdint>
#include <atomic>

namespace OpenHome {
namespace Media {

class IStarvationRamperObserver
{
public:
    virtual void NotifyStarvationRamperBuffering(TBool aBuffering) = 0;
};

class IPipelineDrainer
{
public:
    virtual void DrainAllAudio() = 0;
    virtual ~IPipelineDrainer() {}
};

class IStarvationRamper
{
public:
    virtual ~IStarvationRamper() {}
    virtual void WaitForOccupancy(TUint aJiffies) = 0;
};

class FlywheelInput : public IPcmProcessor
{
    static const TUint kMaxSampleRate = 192000;
    static const TUint kMaxChannels = 10;
    static const TUint kSubsampleBytes = 4;
public:
    FlywheelInput(TUint aMaxJiffies);
    ~FlywheelInput();
    const Brx& Prepare(MsgQueueLite& aQueue, TUint aJiffies, TUint aSampleRate, TUint aBitDepth, TUint aNumChannels);
private:
    inline static void AppendSubsample8(TByte*& aDest, const TByte*& aSrc);
    inline static void AppendSubsample16(TByte*& aDest, const TByte*& aSrc);
    inline static void AppendSubsample24(TByte*& aDest, const TByte*& aSrc);
    inline static void AppendSubsample32(TByte*& aDest, const TByte*& aSrc);
    void DoProcessFragment(const Brx& aData, TUint aNumChannels, TUint aSubsampleBytes);
private: // from IPcmProcessor
    void BeginBlock() override;
    void ProcessFragment(const Brx& aData, TUint aNumChannels, TUint aSubsampleBytes) override;
    void ProcessSilence(const Brx& aData, TUint aNumChannels, TUint aSubsampleBytes) override;
    void EndBlock() override;
    void Flush() override;
private:
    TByte* iPtr;
    Brn iBuf;
    TByte* iChannelPtr[kMaxChannels];
};

class FlywheelRamperManager;

class RampGenerator : public IPcmProcessor
{
    static const TUint kMaxSampleRate = 192000; // FIXME - duplicated in FlywheelInput
    static const TUint kMaxChannels = 8;
    static const TUint kSubsampleBytes = 4;
public:
    RampGenerator(MsgFactory& iMsgFactory, TUint aInputJiffies, TUint aRampJiffies, TUint aThreadPriority);
    ~RampGenerator();
    void Start(const Brx& aRecentAudio, TUint aSampleRate, TUint aNumChannels, TUint aBitDepth, TUint aCurrentRampValue);
    TBool TryGetAudio(Msg*& aMsg); // returns false / nullptr when all msgs generated & returned
private:
    void FlywheelRamperThread();
private: // from IPcmProcessor
    void BeginBlock() override;
    void ProcessFragment(const Brx& aData, TUint aNumChannels, TUint aSubsampleBytes) override;
    void ProcessSilence(const Brx& aData, TUint aNumChannels, TUint aSubsampleBytes) override;
    void EndBlock() override;
    void Flush() override;
private:
    MsgFactory& iMsgFactory;
    const TUint iRampJiffies;
    Semaphore iSem;
    FlywheelRamperManager* iFlywheelRamper;
    Thread* iThread;
    Bwh* iFlywheelAudio;
    MsgQueue iQueue;
    const Brx* iRecentAudio;
    TUint iSampleRate;
    TUint iNumChannels;
    TUint iBitDepth;
    TUint iCurrentRampValue;
    TUint iRemainingRampSize;
    std::atomic<bool> iActive;
};

class IStarvationMonitorObserver;
class IPipelineElementObserverThread;

class StarvationRamper : public MsgReservoir
                       , public IPipelineElementUpstream
                       , public IPipelineDrainer
                       , public IStarvationRamper
{
    friend class SuiteStarvationRamper;
    static const TUint kTrainingJiffies;
    static const TUint kRampDownJiffies;
    static const TUint kMaxAudioOutJiffies;
public:
    StarvationRamper(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstream,
                     IStarvationRamperObserver& aObserver,
                     IPipelineElementObserverThread& aObserverThread, TUint aSizeJiffies,
                     TUint aThreadPriority, TUint aRampUpSize, TUint aMaxStreamCount);
    ~StarvationRamper();
    void Flush(TUint aId); // ramps down quickly then discards everything up to a flush with the given id
    void DiscardAllAudio(); // discards any buffered audio, forcing a starvation ramp.  Flushes all audio until the next MsgDrain.
    TUint SizeInJiffies() const;
    TUint ThreadPriorityFlywheelRamper() const;
    TUint ThreadPriorityStarvationRamper() const;
public: // from IPipelineDrainer
    void DrainAllAudio() override;
private:
    inline TBool IsFull() const;
    void PullerThread();
    void StartFlywheelRamp();
    void NewStream();
    void ProcessAudioOut(MsgAudio* aMsg);
    void ApplyRamp(MsgAudioDecoded* aMsg);
    void SetBuffering(TBool aBuffering);
    void EventCallback();
private: // from IPipelineElementUpstream
    Msg* Pull() override;
private: // from MsgReservoir
    void ProcessMsgIn(MsgTrack* aMsg) override;
    void ProcessMsgIn(MsgDrain* aMsg) override;
    void ProcessMsgIn(MsgDelay* aMsg) override;
    void ProcessMsgIn(MsgHalt* aMsg) override;
    void ProcessMsgIn(MsgDecodedStream* aMsg) override;
    void ProcessMsgIn(MsgQuit* aMsg) override;
    Msg* ProcessMsgOut(MsgMode* aMsg) override;
    Msg* ProcessMsgOut(MsgTrack* aMsg) override;
    Msg* ProcessMsgOut(MsgDrain* aMsg) override;
    Msg* ProcessMsgOut(MsgMetaText* aMsg) override;
    Msg* ProcessMsgOut(MsgHalt* aMsg) override;
    Msg* ProcessMsgOut(MsgFlush* aMsg) override;
    Msg* ProcessMsgOut(MsgWait* aMsg) override;
    Msg* ProcessMsgOut(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsgOut(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsgOut(MsgAudioDsd* aMsg) override;
    Msg* ProcessMsgOut(MsgSilence* aMsg) override;
private: // from IStarvationRamper
    void WaitForOccupancy(TUint aJiffies) override;
private:
    enum class State {
        Starting,
        Running,
        Halted,
        RampingUp,
        FlywheelRamping,
        RampingDown,
        Flushing
    };
private:
    MsgFactory& iMsgFactory;
    IPipelineElementUpstream& iUpstream;
    IStarvationRamperObserver& iObserver;
    IPipelineElementObserverThread& iObserverThread;
    TUint iMaxJiffies;
    const TUint iThreadPriorityFlywheelRamper;
    const TUint iThreadPriorityStarvationRamper;
    const TUint iRampUpJiffies;
    const TUint iMaxStreamCount;
    Mutex iLock;
    Semaphore iSem;
    FlywheelInput iFlywheelInput;
    RampGenerator* iRampGenerator;
    ThreadFunctor* iPullerThread;
    MsgQueueLite iRecentAudio;
    TUint iRecentAudioJiffies;
    IStreamHandler* iStreamHandler;
    State iState;
    TBool iStarving;
    TBool iExit;
    std::atomic<TBool> iStartDrain;
    std::atomic<TBool> iDraining;
    std::atomic<TBool> iStreamStarted;
    BwsMode iMode;
    TUint iStreamId;
    TUint iSampleRate;
    TUint iBitDepth;
    TUint iNumChannels;
    AudioFormat iFormat;
    TUint iCurrentRampValue;
    TUint iRemainingRampSize;
    TUint iTargetFlushId;
    TUint iLastPulledAudioRampValue;
    TUint iEventId;
    std::atomic<TUint> iTrackStreamCount;
    std::atomic<TUint> iDrainCount;
    std::atomic<TUint> iHaltCount;
    std::atomic<TUint> iStartOccupancyJiffies; // Pull will block once until this level is reached
    Semaphore iSemStartOccupancy;
    std::atomic<bool> iEventBuffering;
    TBool iLastEventBuffering;
    TUint iAudioOutSinceLastStartOccupancy;
};

} //namespace Media
} // namespace OpenHome

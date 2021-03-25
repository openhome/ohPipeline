#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/ClockPuller.h>

#include <atomic>
#include <deque>
#include <queue>

namespace OpenHome {
namespace Media {

/*
Element which minimises initial phase delay in Songcast streams.
Aims to minimise variances in initial phase delay between senders and receivers which could be caused by differences in hardware, audio pipeline, logging and network differences, among other things.
If receiver audio is lagging behind sender at start of stream, this class will drop audio packets, replacing them with silence, until phase delay is minimised.
If receiver audio is ahead of sender at start of stream, this class will delay outputting receiver audio, replacing with silence, until phase delay is minimised.
*/
class SongcastPhaseAdjuster : public PipelineElement, public IPipelineElementUpstream, public IClockPuller
{
private:
    static const TUint kSupportedMsgTypes;
    static const TUint kDropLimitDelayOffsetJiffies = 56448 * 10; // 10 ms. Allow dropping up to "initial_delay - kDropLimitDelayOffsetJiffies" jiffies, or 0, whichever is greater.
    static const Brn kModeSongcast;
    enum class State
    {
        Starting,
        Running,
        Adjusting,
        RampingUp
    };
public:
    SongcastPhaseAdjuster(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstreamElement, TUint aRampJiffiesLong, TUint aRampJiffiesShort, TBool aEnabled);
    ~SongcastPhaseAdjuster();
public: // from IPipelineElementUpstream
    Msg* Pull() override;
private: // from PipelineElement (IMsgProcessor)
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgDrain* aMsg) override;
    Msg* ProcessMsg(MsgDelay* aMsg) override;
    Msg* ProcessMsg(MsgFlush* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgSilence* aMsg) override;
public: // from IClockPuller
    void Update(TInt aDelta) override;
    void Start() override;
    void Stop() override;
private:
    MsgAudio* AdjustAudio(const Brx& aMsgType, MsgAudio* aMsg);
    static MsgAudio* DropAudio(MsgAudio* aMsg, TUint aJiffies, TUint& aDroppedJiffies);
    MsgSilence* InjectSilence(TUint aJiffies);
    MsgAudio* RampUp(MsgAudio* aMsg);
    MsgAudio* StartRampUp(MsgAudio* aMsg);
    void ResetPhaseDelay();
    void ClearDecodedStream();
    void PrintStats(const Brx& aMsgType, TUint aJiffies);
private:
    MsgFactory& iMsgFactory;
    IPipelineElementUpstream& iUpstreamElement;
    const TBool iEnabled;
    TBool iModeSongcast;
    TBool iMsgIsFromUpstream;
    State iState;
    Mutex iLock;
    TUint iUpdateCount;
    std::atomic<TInt> iTrackedJiffies;
    std::atomic<TUint> iAudioIn;
    std::atomic<TUint> iAudioOut;
    MsgDecodedStream* iDecodedStream;
    TUint iMsgSilenceJiffies;
    TUint iMsgAudioJiffies;
    TUint iDelayJiffies;
    TUint iDropLimitJiffies;
    TUint iDroppedJiffies;
    TUint iInjectedJiffies;
    const TUint iRampJiffiesLong;
    const TUint iRampJiffiesShort;
    TUint iRampJiffies;
    TUint iRemainingRampSize;
    TUint iCurrentRampValue;
    MsgQueueLite iQueue; // Empty unless we have to split a msg during a ramp.
};

} // namespace Media
} // namespace OpenHome

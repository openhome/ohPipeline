#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Protocol/Protocol.h>

#include <limits.h>
#include <vector>

EXCEPTION(FillerInvalidMode);
EXCEPTION(FillerInvalidCommand);
EXCEPTION(UriProviderInvalidId);
EXCEPTION(UriProviderNotSupported);

namespace OpenHome {
namespace Media {

class IClockPuller;

class UriProvider
{
public:
    virtual ~UriProvider();
    const Brx& Mode() const;
    const Media::ModeInfo& ModeInfo() const;
    const Media::ModeTransportControls& ModeTransportControls() const;

    void SetTransportPlay(Functor aPlay);
    void SetTransportPause(Functor aPause);
    void SetTransportStop(Functor aStop);
    void SetTransportNext(Functor aNext);
    void SetTransportPrev(Functor aPrev);
    void SetTransportSeek(FunctorGeneric<TUint> aSeek); // Absolute seek position in seconds.

    virtual ModeClockPullers ClockPullers();
    virtual TBool IsValid(TUint aTrackId) const;
    virtual void Begin(TUint aTrackId) = 0;
    virtual void BeginLater(TUint aTrackId) = 0; // Queue a track but return ePlayLater when OkToPlay() is called
    virtual EStreamPlay GetNext(Track*& aTrack) = 0;
    virtual TUint CurrentTrackId() const = 0; // Id of last delivered track.  Or of pending track requested via Begin or Move[After|Before]
    virtual void MoveNext() = 0;
    virtual void MovePrevious() = 0;
    virtual void MoveTo(const Brx& aCommand);
    virtual void Interrupt(TBool aInterrupt);
protected:
    enum class Latency          { Supported, NotSupported };
    enum class Pause            { Supported, NotSupported };
    enum class Next             { Supported, NotSupported };
    enum class Prev             { Supported, NotSupported };
    enum class Repeat           { Supported, NotSupported };
    enum class Random           { Supported, NotSupported };
    enum class RampPauseResume  { Long, Short };
    enum class RampSkip         { Long, Short };
protected:
    UriProvider(const TChar* aMode, Latency aLatency,
                Pause aPauseSupported, // UriProvider that supports pause may still find this over-ridden by individual streams
                Next aNextSupported, Prev aPrevSupported,
                Repeat aRepeatSupported, Random aRandomSupported,
                RampPauseResume aRampPauseResume, RampSkip aRampSkip);
private:
    BwsMode iMode;
    Media::ModeInfo iModeInfo;
    Media::ModeTransportControls iTransportControls;
};

class Filler : private Thread, public IPipelineElementDownstream, private IMsgProcessor
{
    static const TUint kPrefetchTrackIdInvalid = UINT_MAX;
public:
    Filler(IPipelineElementDownstream& aPipeline, IPipelineIdTracker& aPipelineIdTracker,
           IPipelineIdManager& aPipelineIdManager, IFlushIdProvider& aFlushIdProvider,
           MsgFactory& aMsgFactory, TrackFactory& aTrackFactory, IStreamPlayObserver& aStreamPlayObserver,
           IPipelineIdProvider& aIdProvider, TUint aThreadPriority, TUint aDefaultDelay);
    ~Filler();
    void Add(UriProvider& aUriProvider);
    void Start(IUriStreamer& aUriStreamer);
    void Quit();
    void Play(const Brx& aMode, TUint aTrackId);
    void PlayLater(const Brx& aMode, TUint aTrackId);
    void Play(const Brx& aMode, const Brx& aCommand);
    void Play(const Brx& aMode);
    void Prepare(const Brx& aMode, const Brx& aCommand);
    TUint Stop(); // Stops filler and encourages protocols to stop.  Returns haltId iff filler was active
    TUint Flush(); // Stops filler, encourages protocols to stop.  Returns flushId.  MsgFlush will be delivered once protocol is stopped.
    void Next(const Brx& aMode);
    void Prev(const Brx& aMode);
    TBool IsStopped() const;
    TUint NullTrackId() const;
private:
    void UpdateActiveUriProvider(const Brx& aMode);
    TUint StopLocked();
private: // from Thread
    void Run() override;
private: // from IPipelineElementDownstream
    void Push(Msg* aMsg) override;
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgTrack* aMsg) override;
    Msg* ProcessMsg(MsgDrain* aMsg) override;
    Msg* ProcessMsg(MsgDelay* aMsg) override;
    Msg* ProcessMsg(MsgEncodedStream* aMsg) override;
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
    class NullTrackStreamHandler : public IStreamHandler, private INonCopyable
    {
    public:
        static const TUint kNullTrackStreamId = 0;
    public:
        NullTrackStreamHandler(IPipelineIdProvider& aIdProvider);
    private: // from IStreamHandler
        EStreamPlay OkToPlay(TUint aStreamId) override;
        TUint TrySeek(TUint aStreamId, TUint64 aOffset) override;
        TUint TryDiscard(TUint aJiffies) override;
        TUint TryStop(TUint aStreamId) override;
        void NotifyStarving(const Brx& aMode, TUint aStreamId, TBool aStarving) override;
    private:
        IPipelineIdProvider& iIdProvider;
    };
private:
    mutable Mutex iLock;
    IPipelineElementDownstream& iPipeline;
    IPipelineIdTracker& iPipelineIdTracker;
    IPipelineIdManager& iPipelineIdManager;
    IFlushIdProvider& iFlushIdProvider;
    MsgFactory& iMsgFactory;
    std::vector<UriProvider*> iUriProviders;
    Mutex iLockUriProvider;
    UriProvider* iActiveUriProvider;
    IUriStreamer* iUriStreamer;
    Track* iTrack;
    TBool iStopped;
    TBool iQuit;
    TBool iChangedMode;
    TBool iWaitingForAudio;
    TBool iNoAudioBeforeNextTrack;
    EStreamPlay iTrackPlayStatus;
    TUint iNextHaltId;
    TUint iPendingHaltId;
    TUint iNextFlushId;
    Track* iNullTrack; // delivered when uri provider cannot return a Track
    NullTrackStreamHandler iNullTrackStreamHandler;
    IStreamPlayObserver& iStreamPlayObserver;
    const TUint iDefaultDelay;
    TUint iPrefetchTrackId;
};

} // namespace Media
} // namespace OpenHome


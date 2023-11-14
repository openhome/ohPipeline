#pragma once

#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/PipelineObserver.h>
#include <OpenHome/Media/Pipeline/AsyncTrackObserver.h>
#include <OpenHome/Media/Pipeline/AirplayReporter.h>
#include <OpenHome/Media/Pipeline/SpotifyReporter.h>
#include <OpenHome/Media/Pipeline/StarterTimed.h>
#include <OpenHome/Media/Pipeline/StarvationRamper.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/MuteManager.h>
#include <OpenHome/Media/Pipeline/Attenuator.h>

#include <vector>

namespace OpenHome {
    class IInfoAggregator;
    class IThreadPool;
namespace Media {
    namespace Codec {
        class ContainerBase;
        class CodecBase;
    }
class Pipeline;
class PipelineInitParams;
class IPipelineAnimator;
class ProtocolManager;
class ITrackObserver;
class Filler;
class IdManager;
class IMimeTypeList;
class Protocol;
class ContentProcessor;
class UriProvider;
class IVolumeRamper;
class IVolumeMuterStepped;
class IDRMProvider;
class IAudioTime;

class PriorityArbitratorPipeline : public IPriorityArbitrator, private INonCopyable
{
    static const TUint kNumThreads = 4; // Filler, CodecController, Gorger, StarvationMonitor
public:
    PriorityArbitratorPipeline(TUint aOpenHomeMax);
private: // from IPriorityArbitrator
    TUint Priority(const TChar* aId, TUint aRequested, TUint aHostMax) override;
    TUint OpenHomeMin() const override;
    TUint OpenHomeMax() const override;
    TUint HostRange() const override;
private:
    const TUint iOpenHomeMax;
};

class IModeObserver
{
public:
    virtual void NotifyModeAdded(const Brx& aMode) = 0;
    virtual ~IModeObserver() {}
};

/**
 * External interface to the pipeline.
 */
class PipelineManager : public IPipeline
                      , public IPipelineIdManager
                      , public IMute
                      , public IPipelineObservable
                      , public IPostPipelineLatencyObserver
                      , public IAttenuator
                      , public IPipelineDrainer
                      , public IStarterTimed
                      , private IPipelineObserver
                      , private ISeekRestreamer
                      , private IUrlBlockWriter
{
public:
    PipelineManager(
        PipelineInitParams* aInitParams,
        IInfoAggregator& aInfoAggregator,
        TrackFactory& aTrackFactory,
        Optional<IAudioTime> aAudioTime);
    ~PipelineManager();
    /**
     * Signal that the pipeline should quit.
     *
     * Normal shutdown order is
     *    Call Quit()
     *    Wait until Pull() returns a MsgQuit
     *    delete PipelineManager
     */
    void Quit();
    /**
    * Add a container to the pipeline.
    *
    * There should only be a single instance of each container added.
    * Must be called before Start().
    *
    * @param[in] aContainer       Ownership transfers to PipelineManager.
    */
    void Add(Codec::ContainerBase* aContainer);
    /**
     * Add a codec to the pipeline.
     *
     * There should only be a single instance of each codec added.
     * Must be called before Start().
     *
     * @param[in] aCodec           Ownership transfers to PipelineManager.
     */
    void Add(Codec::CodecBase* aCodec);
    /**
     * Add a protocol to the pipeline.
     *
     * Multiple instances of a protocol may be added.
     * Must be called before Start().
     *
     * @param[in] aProtocol        Ownership transfers to PipelineManager.
     */
    void Add(Protocol* aProtocol);
    /**
     * Add a content processor to the pipeline.
     *
     * Typically only used by the Radio source (so may be added by it)
     * Must be called before Start().
     *
     * @param[in] aContentProcessor   Ownership transfers to PipelineManager.
     */
    void Add(ContentProcessor* aContentProcessor);
    /**
     * Add a DRM provider to the pipeline to handle protected content
     *
     * Must be called before Start().
     * @param[in] aProvider  Ownership transfers to PipelineManager
     */
    void Add(IDRMProvider* aProvider);
    /**
     * Add a uri provider to the pipeline.
     *
     * Must be called before Start().
     * Will typically be called during construction of a source so need not be called
     * directly by application code.
     *
     * @param[in] aUriProvider     Ownership transfers to PipelineManager.
     */
    void Add(UriProvider* aUriProvider);
    /**
     * Signal that all plug-ins have been Add()ed and the pipeline is ready to receive audio.
     *
     * Begin() can only be called after Start() returns.
     */
    void Start(IVolumeRamper& aVolumeRamper, IVolumeMuterStepped& aVolumeMuter);
    void AddObserver(ITrackObserver& aObserver);
    void AddObserver(IModeObserver& aObserver);
    /**
     * Retrieve the AsyncTrackObserver.
     *
     * @return  IAsyncTrackObserver that allows clients to report out-of-band track
     *          and metadata information to the pipeline.
     */
    IAsyncTrackObserver& AsyncTrackObserver() const;
    /**
     * Retrieve a sample reporter.
     *
     * @return  IAirplayReporter that reports the number of samples that have
     *          passed by it since the last MsgMode.
     */
    IAirplayReporter& AirplayReporter() const;
    /**
     * Retrieve a track change observer.
     *
     * @return  IAirplayTrackObserver that can be notified out-of-band that the
     *          current track has changed, allowing IPipelinePropertyObservers
     *          to be updated without requiring a MsgTrack to be passed down
     *          the pipeline.
     */
    IAirplayTrackObserver& AirplayTrackObserver() const;
    /**
     * Retrieve a sample reporter.
     *
     * @return  ISpotifyReporter that reports the number of samples that have
     *          passed by it since the last MsgMode.
     */
    ISpotifyReporter& SpotifyReporter() const;
    /**
     * Retrieve a track change observer.
     *
     * @return  ISpotifyTrackObserver that can be notified out-of-band that the
     *          current track has changed, allowing IPipelinePropertyObservers
     *          to be updated without requiring a MsgTrack to be passed down
     *          the pipeline.
     */
    ISpotifyTrackObserver& SpotifyTrackObserver() const;
    /**
     * Retrieve singleton msg factory.
     *
     * @return  MsgFactory that can be used to create pipeline messages.
     */
    MsgFactory& Factory();
    /**
     * Retrieve Songcast phase adjuster.
     *
     * @return  IClockPuller that can be notified of pipeline occupancy to allow
     *          it to adjust the initial phase delay of streams that require lip syncing.
     */
    IClockPuller& PhaseAdjuster();
    /**
     * Instruct the pipeline what should be streamed next.
     *
     * Several other tracks may already exist in the pipeline.  Call Stop() or
     * RemoveAll() before this to control what is played next.
     *
     * @param[in] aMode            Identifier for the UriProvider
     * @param[in] aTrackId         Identifier of track to be played (Id() from Track class,
     *                             not pipelineTrackId as reported by pipeline).
     */
    void Begin(const Brx& aMode, TUint aTrackId);
    /**
     * Play the pipeline.
     *
     * Pipeline state will move to either EPipelinePlaying or EPipelineBuffering.
     * Begin() must have been called more recently than Stop() or RemoveAll() for audio
     * to be played.
     */
    void Play();
    /**
     * Halt the pipeline, instruct it what to play next then restart.
     *
     * @param[in] aMode            Identifier for the UriProvider
     * @param[in] aCommand         Mode-specific string, telling the UriProvider
     *                             what to play.
     */
    void PlayAs(const Brx& aMode, const Brx& aCommand);
    /**
     * Pause the pipeline.
     *
     * If the pipeline is playing, the current track will ramp down.  Calling Play()
     * will restart playback at the same point.
     * Pipeline state will then change to EPipelinePaused.
     */
    void Pause();
    /**
     * Warn of a (planned) pending discontinuity in audio.
     *
     * Tell the pipeline to ramp down then discard audio until it pulls a MsgFlush with
     * identifier aFlushId.  Pipeline state will then move into EPipelineWaiting.
     */
    void Wait(TUint aFlushId);
    /**
     * Flush pipeline as quickly as possible.
     *
     * Be careful how you use this.  It may cause a flywheel ramp (which itself
     * may cause a small glitch) if used when pipeline latency is very low.
     */
    void FlushQuick(TUint aFlushId);
    /**
     * Stop the pipeline.
     *
     * Stop playing any current track.  Don't play any pending tracks already in the
     * pipeline.  Don't add any new tracks to the pipeline.  Pipeline state will move
     * to EPipelineStopped.
     */
    void Stop();
    /**
     * Remove all current pipeline content, fetch but don't play a new track.
     *
     * Stops any current track (ramping down if necessary) then invalidates any pending
     * tracks.  Begins to fetch a new track.  Metadata describing the track will be
     * reported to observers but the track will not start playing.
     * This allows UIs to show what will be played next and can be useful e.g. when switching sources.
     *
     * @param[in] aMode            Identifier for the UriProvider
     * @param[in] aTrackId         Identifier of track to be played (Id() from Track class,
     *                             not pipelineTrackId as reported by pipeline).
     */
    void StopPrefetch(const Brx& aMode, TUint aTrackId);
    /**
     * Remove all pipeline content.  Prevent new content from being added.
     *
     * Use Begin() to select what should be played next.
     */
    void RemoveAll();
    /**
     * Seek to a specified point inside the current track.
     *
     * @param[in] aStreamId        Stream identifier.
     * @param[in] aSecondsAbsolute Number of seconds into the track to seek to.
     */
    void Seek(TUint aStreamId, TUint aSecondsAbsolute);
    /**
     * Move immediately to the next track from the current UriProvider (or Source).
     *
     * Ramps down, removes the rest of the current track then fetches the track that
     * logically follows it.  The caller is responsible for calling Play() to start
     * playing this new track.
     */
    void Next();
    /**
     * Move immediately to the previous track from the current UriProvider (or Source).
     *
     * Ramps down, removes the rest of the current track then fetches the track that
     * logically precedes it.  The caller is responsible for calling Play() to start
     * playing this new track.
     */
    void Prev();
    IPipelineElementUpstream& InsertElements(IPipelineElementUpstream& aTail);
    TUint SenderMinLatencyMs() const;
    void GetThreadPriorityRange(TUint& aMin, TUint& aMax) const;
    void GetThreadPriorities(TUint& aFiller, TUint& aFlywheelRamper, TUint& aStarvationRamper, TUint& aCodec, TUint& aEvent);
    void GetMaxSupportedSampleRates(TUint& aPcm, TUint& aDsd) const;
public: // from IPipelineObservable
    void AddObserver(IPipelineObserver& aObserver) override;
    void RemoveObserver(IPipelineObserver& aObserver) override;
private:
    void RemoveAllLocked();
private: // from IPipeline
    Msg* Pull() override;
    void SetAnimator(IPipelineAnimator& aAnimator) override;
private: // from IPipelineIdManager
    void InvalidateAt(TUint aId) override;
    void InvalidateAfter(TUint aId) override;
    void InvalidatePending() override;
    void InvalidateAll() override;
private: // from IMute
    void Mute() override;   // Synchronous; i.e., pipeline will be muted when this call returns.
    void Unmute() override;
private: // from IPostPipelineLatencyObserver
    void PostPipelineLatencyChanged() override;
private: // from IAttenuator
    void SetAttenuation(TUint aAttenuation) override;
private: // from IPipelineDrainer
    void DrainAllAudio() override;
private: // from IStarterTimed
    void StartAt(TUint64 aTime) override;
private: // from IPipelineObserver
    void NotifyPipelineState(EPipelineState aState) override;
    void NotifyMode(const Brx& aMode, const ModeInfo& aInfo,
                    const ModeTransportControls& aTransportControls) override;
    void NotifyTrack(Track& aTrack, TBool aStartOfStream) override;
    void NotifyMetaText(const Brx& aText) override;
    void NotifyTime(TUint aSeconds) override;
    void NotifyStreamInfo(const DecodedStreamInfo& aStreamInfo) override;
private: // from ISeekRestreamer
    TUint SeekRestream(const Brx& aMode, TUint aTrackId) override;
private: // from IUrlBlockWriter
    TBool TryGet(IWriter& aWriter, const Brx& aUrl, TUint64 aOffset, TUint aBytes) override;
private:
    class PrefetchObserver : public IStreamPlayObserver
    {
    public:
        PrefetchObserver();
        ~PrefetchObserver();
        void SetTrack(TUint aTrackId);
        void Wait(TUint aTimeoutMs);
    private: // from IStreamPlayObserver
        void NotifyTrackFailed(TUint aTrackId);
        void NotifyStreamPlayStatus(TUint aTrackId, TUint aStreamId, EStreamPlay aStatus);
    private:
        void CheckTrack(TUint aTrackId);
    private:
        Mutex iLock;
        Semaphore iSem;
        TUint iTrackId;
    };
private:
    Mutex iLock;
    Mutex iPublicLock;
    Pipeline* iPipeline;
    ProtocolManager* iProtocolManager;
    TUint iFillerPriority;
    Filler* iFiller;
    IdManager* iIdManager;
    std::vector<UriProvider*> iUriProviders;
    Mutex iLockObservers;
    std::vector<IPipelineObserver*> iObservers;
    IModeObserver* iModeObserver;
    EPipelineState iPipelineState;
    Semaphore iPipelineStoppedSem;
    BwsMode iMode;
    TUint iTrackId;
    PrefetchObserver* iPrefetchObserver;
};

} // namespace Media
} // namespace OpenHome

#include <OpenHome/Av/QobuzConnect/SourceQobuzConnect.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Media/UriProviderRepeater.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Av/Source.h>
#include <OpenHome/Av/SourceFactory.h>
#include <OpenHome/Av/QobuzConnect/App.h>
#include <OpenHome/Av/QobuzConnect/ProtocolQobuzConnect.h>
#include <OpenHome/Debug-ohMediaPlayer.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;


// SourceFactory

const TChar* SourceFactory::kSourceTypeQobuzConnect = "QOBUZCONNECT";
const Brn SourceFactory::kSourceNameQobuzConnect("Qobuz Connect");

ISource* SourceFactory::NewQobuzConnect(
    IMediaPlayer& aMediaPlayer,
    Net::IMdnsProvider& aMdnsProvider,
    const Brx& aAppId,
    const Brx& aAppSecret,
    const Brx& aManufacturer,
    const Brx& aModel,
    const Brx& aSerialNumber)
{ // static
    return new SourceQobuzConnect(
        aMediaPlayer, aMdnsProvider, aAppId, aAppSecret, aManufacturer, aModel, aSerialNumber);
}


// UriProviderQobuzConnect

UriProviderQobuzConnect::UriProviderQobuzConnect(const TChar* aMode, TrackFactory& aTrackFactory)
    : UriProviderRepeater(
        aMode,
        Latency::External,
        aTrackFactory,
        Pause::Supported,
        Next::NotSupported,
        Prev::NotSupported,
        Repeat::NotSupported,
        Random::NotSupported,
        RampPauseResume::Short)
{
}

Optional<IClockPuller> UriProviderQobuzConnect::ClockPuller()
{
    return &iClockPuller;
}


// SourceQobuzConnect

namespace {
    // A Linn DSM is always a "streamer" from Qobuz Connect's point of view, and we always want
    // it to negotiate the highest quality it can - these are Linn/product decisions, not
    // per-instantiation configuration, so (mirroring how RAAT/other SDK-specific enum types never
    // appear in SourceFactory.h's public signature) they live here rather than being threaded
    // through as factory parameters.
    const QbzDeviceType kDeviceType = QBZ_DEVICE_TYPE_STREAMER;
    const QbzAudioQuality kMaxAudioQuality = QBZ_AUDIO_QUALITY_HI_RES_LEVEL_3;

    // One-shot target for RegisterFriendlyNameObserver's callback (see ctor below) - a plain
    // MakeFunctorGeneric(Object&, method) bind rather than a lambda-wrapping helper, since only
    // FunctorGeneric<const Brx&>'s object+member-function overload is confirmed available here.
    class FriendlyNameCapture
    {
    public:
        FriendlyNameCapture(Bwx& aOut) : iOut(aOut) {}
        void Set(const Brx& aName) { iOut.Replace(aName); }
    private:
        Bwx& iOut;
    };
}

SourceQobuzConnect::SourceQobuzConnect(
    IMediaPlayer& aMediaPlayer,
    Net::IMdnsProvider& aMdnsProvider,
    const Brx& aAppId,
    const Brx& aAppSecret,
    const Brx& aManufacturer,
    const Brx& aModel,
    const Brx& aSerialNumber)

    : Source(
        SourceFactory::kSourceNameQobuzConnect,
        SourceFactory::kSourceTypeQobuzConnect,
        aMediaPlayer.Pipeline(),
        false) // not visible by default, mirrors SourceRaat
    , iMetadataHandler(nullptr)
    , iVolumeManager(aMediaPlayer.VolumeManager())
    , iConfigLimit(aMediaPlayer.ConfigManager().GetNum(VolumeConfig::kKeyLimit))
    , iSubscriberIdLimit(0)
    , iVolumeUser(0)
    , iVolumeLimit(0)
    , iMuted(false)
    , iTrack(nullptr)
    , iBeginPending(false)
    , iPipelineOwned(false)
{
    // Grab the device's current friendly/room name once, up front (RegisterFriendlyNameObserver
    // calls back synchronously with the current value - see FriendlyNameManager::
    // RegisterFriendlyNameObserver). v1 simplification: unlike RAAT (which re-reads Product's
    // name/model details fresh each time its own thread starts), this doesn't track later
    // friendly-name changes - Qobuz Connect does support renaming post-creation
    // (qbz_connect_set_device_name) but that's not wired up here.
    Bws<IFriendlyNameObservable::kMaxFriendlyNameBytes> deviceName;
    {
        auto& observable = aMediaPlayer.FriendlyNameObservable();
        FriendlyNameCapture capture(deviceName);
        const TUint id = observable.RegisterFriendlyNameObserver(
            MakeFunctorGeneric<const Brx&>(capture, &FriendlyNameCapture::Set));
        observable.DeregisterFriendlyNameObserver(id);
    }

    const Brx& uniqueDeviceId = aMediaPlayer.Device().Udn();

    iMetadataHandler = new QobuzConnectMetadataHandler(aMediaPlayer.Pipeline().AsyncTrackObserver());

    iApp = new QobuzConnectApp(
        aMediaPlayer,
        aMdnsProvider,
        *this,
        aAppId,
        aAppSecret,
        deviceName,
        aManufacturer,
        aModel,
        aSerialNumber,
        uniqueDeviceId,
        kDeviceType,
        kMaxAudioQuality);

    iProtocol = new ProtocolQobuzConnect(
        aMediaPlayer.Env(),
        iApp->Reader(),
        aMediaPlayer.TrackFactory());
    aMediaPlayer.Add(iProtocol); // passes ownership

    iUriProvider = new UriProviderQobuzConnect(SourceFactory::kSourceTypeQobuzConnect, aMediaPlayer.TrackFactory());
    iUriProvider->SetTransportPlay(MakeFunctor(iApp->MediaControl(), &QobuzConnectMediaControl::TryPlay));
    iUriProvider->SetTransportPause(MakeFunctor(iApp->MediaControl(), &QobuzConnectMediaControl::TryPause));
    iUriProvider->SetTransportStop(MakeFunctor(iApp->MediaControl(), &QobuzConnectMediaControl::TryStop));
    iPipeline.Add(iUriProvider); // transfers ownership

    // Each fires synchronously, right here, with the current value - too early to reach the SDK
    // (no core yet), but keeps iVolumeUser/iVolumeLimit/iMuted correct from construction onwards
    // for QobuzNotifyActiveStateChanged() to push later.
    iVolumeManager.AddVolumeObserver(*this);
    iVolumeManager.AddMuteObserver(*this);
    iSubscriberIdLimit = iConfigLimit.Subscribe(MakeFunctorConfigNum(*this, &SourceQobuzConnect::LimitChanged));

    iDefaultMetadata.Replace("<DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\">");
    iDefaultMetadata.Append("<item id=\"\" parentID=\"\" restricted=\"True\">");
    iDefaultMetadata.Append("<dc:title>");
    iDefaultMetadata.Append("Qobuz Connect");
    iDefaultMetadata.Append("</dc:title>");
    iDefaultMetadata.Append("<upnp:class>object.item.audioItem</upnp:class>");
    iDefaultMetadata.Append("</item>");
    iDefaultMetadata.Append("</DIDL-Lite>");

    iTimer = new Timer(aMediaPlayer.Env(), MakeFunctor(*this, &SourceQobuzConnect::Start), "SourceQobuzConnect");
    iTimer->FireIn(kStartupDelayMs); // mirrors SourceRaat's startup delay (fired from IProductObserver::Started() there; simplified to a fixed delay here rather than adding another observer)
}

SourceQobuzConnect::~SourceQobuzConnect()
{
    iConfigLimit.Unsubscribe(iSubscriberIdLimit);
    delete iTimer;
    delete iApp;
    delete iMetadataHandler; // IAsyncTrackObserver has no RemoveClient() - RAAT's equivalent handler is never unregistered either, since both live as long as the Pipeline itself
    if (iTrack != nullptr) {
        iTrack->RemoveRef();
    }
}

void SourceQobuzConnect::Activate(TBool aAutoPlay, TBool aPrefetchAllowed)
{
    SourceBase::Activate(aAutoPlay, aPrefetchAllowed);
    InitialiseSourceQobuzConnect();
}

void SourceQobuzConnect::Deactivate()
{
    // DS switched to a different source while Qobuz Connect was playing - ask the SDK to stop,
    // so the Controller (phone app) reflects reality rather than continuing to show playback in
    // progress.
    //
    // This has to be a genuine stop, not a pause: DS's Pipeline unconditionally interrupts
    // whichever protocol is currently streaming as part of any source switch (confirmed on
    // hardware - ProtocolQobuzConnect::Stream() actually exits, via DoInterrupt()/TryStop()),
    // regardless of what we separately tell the SDK. Telling the SDK "paused" left it believing
    // the old stream was still alive underneath; pressing Play in the Controller then only
    // triggered resume_playback_callback, whose Stream() re-entry immediately found nothing
    // valid to read and gave up silently - no audio, and only actually recovered once the SDK's
    // own inactivity handling separately stopped and restarted the stream from scratch (the same
    // path a manual track skip takes). Calling TryStop() here instead makes the SDK's own model
    // match DS's reality, so the Controller's Play button goes through the normal fresh-start
    // path (QobuzNotifyStreamReady()/QobuzNotifyPlaybackInitiated()) that already works.
    LOG(kQobuzConnect, "SourceQobuzConnect::Deactivate()\n");
    iApp->MediaControl().TryStop();
    // The DS Pipeline is no longer ours - some other source is about to occupy it, and it's about
    // to actually be interrupted regardless (see above). Clear eagerly rather than waiting for
    // QobuzNotifyPlaybackStopped() to do the same asynchronously, so a reactivation racing in
    // before that callback arrives still goes through InitialiseSourceQobuzConnect()'s full
    // Pipeline::Begin() path rather than being incorrectly skipped - see iPipelineOwned's comment.
    iPipelineOwned = false;
    iBeginPending = false;
    SourceBase::Deactivate();
}

void SourceQobuzConnect::PipelineStopped()
{
}

TBool SourceQobuzConnect::TryActivateNoPrefetch(const Brx& aMode)
{
    if (aMode != iUriProvider->Mode()) {
        return false;
    }
    EnsureActiveNoPrefetch();
    return true;
}

void SourceQobuzConnect::StandbyEnabled()
{
}

void SourceQobuzConnect::QobuzNotifyStreamReady()
{
    LOG(kQobuzConnect, "SourceQobuzConnect::QobuzNotifyStreamReady()\n");
    EnsureActiveNoPrefetch();
    InitialiseSourceQobuzConnect();
    iProtocol->NotifySetup();
}

void SourceQobuzConnect::QobuzNotifyPlaybackInitiated(TBool aStartPaused)
{
    LOG(kQobuzConnect, "SourceQobuzConnect::QobuzNotifyPlaybackInitiated(%u)\n", aStartPaused);
    iProtocol->NotifyStart();
    if (aStartPaused) {
        iPipeline.Pause();
    }
    else {
        iPipeline.Play();
    }
    const auto& format = iApp->Reader().StreamFormat();
    iApp->MediaControl().NotifyPlaybackInitiated(format.SampleRate(), format.BitDepth(), format.NumChannels(), aStartPaused);
}

void SourceQobuzConnect::QobuzNotifyPlaybackPaused()
{
    // iPipeline is only ours to touch while we're the active DS source, but the SDK still needs
    // acknowledging regardless of iActive - this callback is the asynchronous confirmation of
    // some earlier pause request (e.g. TryPause() via DS's own UI pause control), and iActive may
    // have changed in the meantime. Skipping the ack in that case would leave the Controller
    // showing stale playback state.
    if (iActive) {
        iPipeline.Pause();
    }
    iApp->MediaControl().NotifyPlaybackPaused();
}

void SourceQobuzConnect::QobuzNotifyPlaybackResumed()
{
    // Unlike QobuzNotifyPlaybackPaused(), a resume request should bring DS back onto this source
    // if something else deactivated it - mirrors QobuzNotifyStreamReady()'s
    // EnsureActiveNoPrefetch() call, which resume doesn't otherwise get paired with (it's a
    // standalone callback, not one that fires alongside stream-ready the way playback-initiated
    // does). Harmless/no-op when already active. A DS-side source switch itself now goes through
    // TryStop() (see Deactivate()) rather than resume, since DS's Pipeline tears the stream down
    // regardless of what we tell the SDK - but the SDK can still legitimately request a resume in
    // other circumstances (e.g. pause via DS's own UI while remaining the active source).
    EnsureActiveNoPrefetch();
    iPipeline.Play();
    iApp->MediaControl().NotifyPlaybackResumed();
}

void SourceQobuzConnect::QobuzNotifyPlaybackStopped()
{
    iBeginPending = false; // session has genuinely ended - a later QobuzNotifyStreamReady() must be allowed to push a fresh Begin() again
    if (iActive) {
        const TUint flushId = iProtocol->FlushAsync();
        if (flushId != Media::MsgFlush::kIdInvalid) {
            iPipeline.Wait(flushId);
            iPipeline.Pause();
        }
    }
    iApp->MediaControl().NotifyPlaybackStopped();
}

void SourceQobuzConnect::QobuzNotifySeekInProgress()
{
    // QobuzConnectMediaControl::HandleSeekInProgress() already flushed AudioStream's own
    // upstream buffer (QobuzConnectAudioStream::FlushForSeek) - but that alone isn't enough:
    // ProtocolQobuzConnect::Stream()'s inner Read loop doesn't itself exit for a mid-stream seek
    // (same stream id, no dispose/replace), so whatever audio was already handed downstream to
    // the Pipeline (iSupply->OutputData(), before the seek was requested) stays queued there and
    // plays out to completion before any post-seek audio does.
    //
    // FlushAsync() registers a flush id before interrupting the reader, so that once the
    // interrupted Read() throws and Stream()'s Flush() section runs, it emits a proper
    // OutputFlush() - a message-based signal that discards that queued downstream audio far
    // faster than waiting for it to drain in real time. iPipeline.Wait(flushId) blocks until
    // that's confirmed (mirroring QobuzNotifyPlaybackStopped()'s existing pattern) before
    // NotifySetup()/NotifyStart() wake Stream()'s outer loop back into a fresh Read loop for the
    // post-seek data. Skipping the flush/wait step and only interrupting+resyncing left
    // Stream()'s subsequent OutputDrain() call with nothing to synchronise against, so it simply
    // blocked for the full ISupply::kMaxDrainMs (5000ms - the exact delay observed on hardware)
    // before timing out and proceeding anyway.
    if (iActive) {
        const TUint flushId = iProtocol->FlushAsync();
        iApp->Reader().Interrupt();
        if (flushId != Media::MsgFlush::kIdInvalid) {
            iPipeline.Wait(flushId);
        }
        iProtocol->NotifySetup();
        iProtocol->NotifyStart();
    }
}

void SourceQobuzConnect::QobuzNotifyActiveStateChanged(TBool aActive)
{
    LOG(kQobuzConnect, "SourceQobuzConnect::QobuzNotifyActiveStateChanged(%u)\n", aActive);
    if (aActive) {
        EnsureActiveNoPrefetch();
        // The SDK core is guaranteed to exist by the time any callback (including this one) can
        // fire, unlike the AddVolumeObserver()/AddMuteObserver()/ConfigNum::Subscribe() calls in
        // the constructor, which ran far too early to reach it - push the current values now so
        // the Controller's volume slider/mute button start out correct rather than stale/default.
        PushVolume();
        iApp->MediaControl().SyncMute(iMuted.load());
    }
    // Renderer becoming inactive (aActive == false) doesn't necessarily mean playback stopped -
    // the Controller may just be switching which renderer is selected, and Qobuz Connect will
    // separately call stop_playback_callback if it actually wants playback to stop - so there's
    // deliberately nothing to do here in that case.
}

void SourceQobuzConnect::QobuzNotifyMetadataChanged(const Brx& aTitle, const Brx& aArtist, const Brx& aAlbum, const Brx& aArtworkUri)
{
    iMetadataHandler->MetadataChanged(aTitle, aArtist, aAlbum, aArtworkUri);
}

void SourceQobuzConnect::QobuzNotifyStreamSeeked(uint64_t aPositionMs)
{
    iApp->MediaControl().NotifySeeked(aPositionMs);
}

void SourceQobuzConnect::QobuzNotifyVolumeChanged(TUint aVolumePercent)
{
    // Controller requested a new absolute volume, in the SDK's fixed 0-100 scale - map onto DS's
    // currently configured volume limit (not VolumeMax() - see iConfigLimit's comment), so the
    // full width of the Controller's slider actually reaches the top of the usable range.
    const TUint limit = iVolumeLimit.load();
    const TUint dsVolume = (aVolumePercent * limit + 50) / 100;
    LOG(kQobuzConnect, "SourceQobuzConnect::QobuzNotifyVolumeChanged(%u%%) -> %u/%u\n", aVolumePercent, dsVolume, limit);
    try {
        iVolumeManager.SetVolume(dsVolume);
    }
    catch (VolumeNotSupported&) {}
    catch (VolumeOutOfRange&) {}
}

void SourceQobuzConnect::QobuzNotifyMuteStateChanged(TBool aMuted)
{
    LOG(kQobuzConnect, "SourceQobuzConnect::QobuzNotifyMuteStateChanged(%u)\n", aMuted);
    if (aMuted) {
        iVolumeManager.Mute();
    }
    else {
        iVolumeManager.Unmute();
    }
}

void SourceQobuzConnect::VolumeChanged(const IVolumeValue& aVolume)
{
    // DS's volume changed for some reason (Controller request, IR remote, front panel, Linn
    // app...) - cache it and push the SDK's-scale equivalent, so the Controller's own displayed
    // volume doesn't go stale. Cached regardless of whether it can reach the SDK yet - see
    // QobuzNotifyActiveStateChanged().
    iVolumeUser.store(aVolume.VolumeUser());
    PushVolume();
}

void SourceQobuzConnect::MuteChanged(TBool aValue)
{
    iMuted.store(aValue);
    iApp->MediaControl().SyncMute(aValue);
}

void SourceQobuzConnect::LimitChanged(Configuration::ConfigNum::KvpNum& aKvp)
{
    // The user reconfigured the volume limit - the SDK's fixed 0-100 scale now maps onto a
    // different DS volume range, so re-derive and re-push the Controller-facing equivalent of
    // whatever DS's current (unchanged) volume already is.
    iVolumeLimit.store(aKvp.Value());
    PushVolume();
}

void SourceQobuzConnect::PushVolume()
{
    const TUint limit = iVolumeLimit.load();
    const TUint volUser = iVolumeUser.load();
    // volUser can exceed limit transiently (e.g. the limit was just lowered below the current
    // volume - DS clamps the actual output down but VolumeChanged() may not have fired yet).
    const TUint qobuzVolume = (limit == 0) ? 0 : std::min<TUint>(100, (volUser * 100 + limit / 2) / limit);
    iApp->MediaControl().SyncVolume(qobuzVolume);
}

void SourceQobuzConnect::InitialiseSourceQobuzConnect()
{
    /* iProtocol->IsStreaming() only flips once the Pipeline's own thread actually dispatches
     * to ProtocolQobuzConnect::Stream() - iPipeline.Begin() below just enqueues that. Activate()
     * (from a source switch) and QobuzNotifyStreamReady() (from HandleInitiatePlayback) can each
     * reach this method independently within that dispatch-latency window - e.g. a source switch
     * away from another source can take long enough that a HandleInitiatePlayback() arriving
     * shortly after still sees IsStreaming()==false, so both push a fresh Begin(). The second
     * Begin()/RemoveAll() then interrupts the first Stream() call before it can ever deliver
     * audio, and since ProtocolQobuzConnect::Stream() is meant to stay resident for the whole
     * session (subsequent tracks arrive via NotifySetup/NotifyStart, not fresh Begin() calls),
     * once it's interrupted like this nothing ever re-enters it - Qobuz Connect goes silent for
     * the rest of the session. iBeginPending closes that window: it's set synchronously here,
     * unlike IsStreaming(), and only cleared once QobuzNotifyPlaybackStopped() confirms the
     * session has genuinely ended. */
    if ((iProtocol->IsStreaming() && iPipelineOwned) || iBeginPending) {
        return;
    }
    iBeginPending = true;

    /* Push the default track into the pipeline, mirroring SourceRaat::InitialiseSourceRaat -
     * this ensures we've entered ProtocolQobuzConnect::Stream and are ready to receive
     * NotifySetup/NotifyStart. */
    if (iTrack != nullptr) {
        iTrack->RemoveRef();
    }
    iTrack = iUriProvider->SetTrack(ProtocolQobuzConnect::kUri, iDefaultMetadata);
    iPipeline.RemoveAll();
    iPipeline.Begin(iUriProvider->Mode(), iTrack->Id());
    iPipeline.Play();
    iPipelineOwned = true;
}

void SourceQobuzConnect::Start()
{
    iApp->Start();
}

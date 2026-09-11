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
    , iTrack(nullptr)
    , iBeginPending(false)
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
    delete iTimer;
    delete iApp;
    if (iTrack != nullptr) {
        iTrack->RemoveRef();
    }
}

void SourceQobuzConnect::Activate(TBool aAutoPlay, TBool aPrefetchAllowed)
{
    SourceBase::Activate(aAutoPlay, aPrefetchAllowed);
    InitialiseSourceQobuzConnect();
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
    if (!iActive) {
        return;
    }
    iPipeline.Pause();
    iApp->MediaControl().NotifyPlaybackPaused();
}

void SourceQobuzConnect::QobuzNotifyPlaybackResumed()
{
    if (!iActive) {
        return;
    }
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
    // QobuzConnectMediaControl::HandleSeekInProgress() already flushed the audio buffer
    // (QobuzConnectAudioStream::FlushForSeek) before calling this - nothing further to do here
    // until fresh post-seek data starts flowing and drives its own OutputStream/OutputData calls
    // the normal way.
}

void SourceQobuzConnect::QobuzNotifyActiveStateChanged(TBool aActive)
{
    LOG(kQobuzConnect, "SourceQobuzConnect::QobuzNotifyActiveStateChanged(%u)\n", aActive);
    if (aActive) {
        EnsureActiveNoPrefetch();
    }
    // Renderer becoming inactive (aActive == false) doesn't necessarily mean playback stopped -
    // the Controller may just be switching which renderer is selected, and Qobuz Connect will
    // separately call stop_playback_callback if it actually wants playback to stop - so there's
    // deliberately nothing to do here in that case.
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
    if (iProtocol->IsStreaming() || iBeginPending) {
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
}

void SourceQobuzConnect::Start()
{
    iApp->Start();
}

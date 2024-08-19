#include <OpenHome/Av/Raat/SourceRaat.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Media/UriProviderRepeater.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Av/Source.h>
#include <OpenHome/Av/SourceFactory.h>
#include <OpenHome/Av/Raat/App.h>
#include <OpenHome/Av/Raat/ProtocolRaat.h>
#include <OpenHome/Av/Raat/Transport.h>
#include <OpenHome/Configuration/ConfigManager.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;


// SourceFactory

const Brn RoonProtocolSelector::kKeyProtocol("Roon.Protocol");
const TUint RoonProtocolSelector::kValRaat = 0;
const TUint RoonProtocolSelector::kValScd = 1;

RoonProtocolSelector::RoonProtocolSelector(Configuration::IConfigInitialiser& aConfigInitialiser)
    : iProtocol(RoonProtocol::Raat)
    , iSubscriberId(Configuration::IConfigManager::kSubscriptionIdInvalid)
{
    const int arr[] = { kValRaat, kValScd };
    std::vector<TUint> protocols(arr, arr + sizeof(arr) / sizeof(arr[0]));
    iConfigProtocol = new Configuration::ConfigChoice(aConfigInitialiser, kKeyProtocol, protocols, kValRaat);
    iSubscriberId = iConfigProtocol->Subscribe(Configuration::MakeFunctorConfigChoice(*this, &RoonProtocolSelector::ProtocolChanged));
}

RoonProtocolSelector::~RoonProtocolSelector()
{
    ASSERT(iConfigProtocol == nullptr);
}

RoonProtocol RoonProtocolSelector::Protocol() const
{
    return iProtocol;
}

Configuration::ConfigChoice* RoonProtocolSelector::Transfer()
{
    ASSERT(iConfigProtocol != nullptr);
    iConfigProtocol->Unsubscribe(iSubscriberId);
    auto config = iConfigProtocol;
    iConfigProtocol = nullptr;
    return config;
}

void RoonProtocolSelector::ProtocolChanged(Configuration::KeyValuePair<TUint>& aKvp)
{
    if (aKvp.Value() == kValRaat) {
        iProtocol = RoonProtocol::Raat;
    }
    else {
        iProtocol = RoonProtocol::Scd;
    }
}


ISource* SourceFactory::NewRoon(
    IMediaPlayer& aMediaPlayer,
    Media::IAudioTime& aAudioTime,
    Media::IPullableClock& aPullableClock,
    IRaatSignalPathObservable& aSignalPathObservable,
    const Brx& aSerialNumber,
    const Brx& aSoftwareVersion,
    const Brx& aConfigUrl)
{ // static
    RoonProtocolSelector selector(aMediaPlayer.ConfigInitialiser());
    auto configVal = selector.Transfer();
    auto protocol = selector.Protocol();
    if (protocol == RoonProtocol::Raat) {
        return new SourceRaat(
            aMediaPlayer,
            aAudioTime,
            aPullableClock,
            aSignalPathObservable,
            configVal,
            aSerialNumber,
            aSoftwareVersion,
            aConfigUrl);
    }
    else {
        return SourceFactory::NewScd(aMediaPlayer, configVal);
    }
}

ISource* SourceFactory::NewRaat(
    IMediaPlayer& aMediaPlayer,
    Media::IAudioTime& aAudioTime,
    Media::IPullableClock& aPullableClock,
    IRaatSignalPathObservable& aSignalPathObservable,
    const Brx& aSerialNumber,
    const Brx& aSoftwareVersion,
    const Brx& aConfigUrl)
{ // static
    return new SourceRaat(
        aMediaPlayer,
        aAudioTime,
        aPullableClock,
        aSignalPathObservable,
        nullptr,
        aSerialNumber,
        aSoftwareVersion,
        aConfigUrl);
}


// UriProviderRaat 

UriProviderRaat::UriProviderRaat(
    const TChar* aMode,
    TrackFactory& aTrackFactory)
    : UriProviderRepeater(
        aMode,
        Latency::External,
        aTrackFactory,
        Pause::Supported,
        Next::Supported,
        Prev::Supported,
        Repeat::Supported,
        Random::Supported,
        RampPauseResume::Short)
{
}

Optional<IClockPuller> UriProviderRaat::ClockPuller()
{
    return &iClockPuller;
}


const TChar* SourceFactory::kSourceTypeRaat = "RAAT";
const Brn SourceFactory::kSourceNameRaat("Roon Ready");

// SourceRaat

SourceRaat::SourceRaat(
    IMediaPlayer& aMediaPlayer,
    Media::IAudioTime& aAudioTime,
    Media::IPullableClock& aPullableClock,
    IRaatSignalPathObservable& aSignalPathObservable,
    Optional<Configuration::ConfigChoice> aProtocolSelector,
    const Brx& aSerialNumber,
    const Brx& aSoftwareVersion,
    const Brx& aConfigUrl)

    : Source(
        SourceFactory::kSourceNameRaat,
        SourceFactory::kSourceTypeRaat,
        aMediaPlayer.Pipeline(),
        false) // not visible by default
    , iProtocolSelector(aProtocolSelector.Ptr())
    , iTrack(nullptr)
{
    iApp = new RaatApp(
        aMediaPlayer.Env(),
        aMediaPlayer,
        *this,
        aAudioTime,
        aPullableClock,
        aSignalPathObservable,
        aSerialNumber,
        aSoftwareVersion,
        aConfigUrl);

    iProtocol = new ProtocolRaat(
        aMediaPlayer.Env(),
        iApp->Reader(),
        aMediaPlayer.TrackFactory());
    aMediaPlayer.Add(iProtocol); // passes ownership

    iUriProvider = new UriProviderRaat(SourceFactory::kSourceTypeRaat, aMediaPlayer.TrackFactory());
    iUriProvider->SetTransportPlay(MakeFunctor(iApp->Transport(), &IRaatTransport::Play));
    iUriProvider->SetTransportPause(MakeFunctor(iApp->Transport(), &IRaatTransport::TryPause));
    iUriProvider->SetTransportStop(MakeFunctor(iApp->Transport(), &IRaatTransport::Stop));
    iUriProvider->SetTransportNext(MakeFunctor(iApp->Transport(), &IRaatTransport::TryMoveNext));
    iUriProvider->SetTransportPrev(MakeFunctor(iApp->Transport(), &IRaatTransport::TryMovePrev));
    iPipeline.Add(iUriProvider); // transfers ownership

    iDefaultMetadata.Replace("<DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\">");
    iDefaultMetadata.Append("<item id=\"\" parentID=\"\" restricted=\"True\">");
    iDefaultMetadata.Append("<dc:title>");
    iDefaultMetadata.Append("Roon");
    iDefaultMetadata.Append("</dc:title>");
    iDefaultMetadata.Append("<upnp:class>object.item.audioItem</upnp:class>");
    iDefaultMetadata.Append("</item>");
    iDefaultMetadata.Append("</DIDL-Lite>");

    aMediaPlayer.Product().AddObserver(*this);

    iTimer = new Timer(aMediaPlayer.Env(), MakeFunctor(*this, &SourceRaat::Start), "SourceRaat");
}

SourceRaat::~SourceRaat()
{
    delete iTimer;
    delete iProtocolSelector;
    delete iApp;
    if (iTrack != nullptr) {
        iTrack->RemoveRef();
    }
}

void SourceRaat::Activate(TBool aAutoPlay, TBool aPrefetchAllowed)
{
    SourceBase::Activate(aAutoPlay, aPrefetchAllowed); // FIXME - remove this if we find no work to do here
    Initialise();
}

void SourceRaat::PipelineStopped()
{
}

TBool SourceRaat::TryActivateNoPrefetch(const Brx& aMode)
{
    if (aMode != iUriProvider->Mode()) {
        return false;
    }

    EnsureActiveNoPrefetch();
    return true;
}

void SourceRaat::StandbyEnabled()
{
}

void SourceRaat::NotifySetup()
{
    EnsureActiveNoPrefetch();
    Initialise();
    iProtocol->NotifySetup();
    iPipeline.Play();
}

void SourceRaat::NotifyStart()
{
    iProtocol->NotifyStart();
    iPipeline.Play();
}

void SourceRaat::NotifyStop()
{
    if (!iActive) {
        return;
    }
    TUint flushId = iProtocol->FlushAsync();
    if (flushId != MsgFlush::kIdInvalid) {
        iPipeline.Wait(flushId);
        iPipeline.Pause();
    }
}

void SourceRaat::Started()
{
    iTimer->FireIn(kStartupDelayMs);
}

void SourceRaat::SourceIndexChanged()
{
    // deliberately blank - we implement IProductObserver for Started() only
}

void SourceRaat::SourceXmlChanged()
{
    // deliberately blank - we implement IProductObserver for Started() only
}

void SourceRaat::ProductUrisChanged()
{
    // deliberately blank - we implement IProductObserver for Started() only
}

void SourceRaat::Initialise()
{
    if (iProtocol->IsStreaming()) {
        return;
    }

    /* Push the default track into the pipeline
     * This ensures that we've entered ProtocolRaat::Stream and are ready to
     * receive notifications to configure or begin streaming audio
     */
    if (iTrack != nullptr) {
        iTrack->RemoveRef();
    }
    iTrack = iUriProvider->SetTrack(ProtocolRaat::kUri, iDefaultMetadata);
    iPipeline.RemoveAll();
    iPipeline.Begin(iUriProvider->Mode(), iTrack->Id());
    iPipeline.Play();
}

void SourceRaat::Start()
{
    iApp->Start();
}

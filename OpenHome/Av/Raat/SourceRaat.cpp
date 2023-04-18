#include <OpenHome/Av/Raat/SourceRaat.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Media/UriProviderSingleTrack.h>
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
    IRaatSignalPathObservable* aSignalPathObservable,
    const Brx& aSerialNumber,
    const Brx& aSoftwareVersion,
    TUint aDsdSampleBlockWords,
    TUint aDsdPadBytesPerChunk)
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
            aSoftwareVersion);
    }
    else {
        return SourceFactory::NewScd(aMediaPlayer, configVal, aDsdSampleBlockWords, aDsdPadBytesPerChunk);
    }
}

ISource* SourceFactory::NewRaat(
    IMediaPlayer& aMediaPlayer,
    Media::IAudioTime& aAudioTime,
    Media::IPullableClock& aPullableClock,
    IRaatSignalPathObservable* aSignalPathObservable,
    const Brx& aSerialNumber,
    const Brx& aSoftwareVersion)
{ // static
    return new SourceRaat(
        aMediaPlayer,
        aAudioTime,
        aPullableClock,
        aSignalPathObservable,
        nullptr,
        aSerialNumber,
        aSoftwareVersion);
}


// UriProviderRaat 

UriProviderRaat::UriProviderRaat(
    const TChar* aMode,
    TrackFactory& aTrackFactory)
    : UriProviderSingleTrack(
        aMode,
        false /* supportsLatency*/,
        false /* supportsPause */,
        aTrackFactory)
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
    IRaatSignalPathObservable* aSignalPathObservable,
    Optional<Configuration::ConfigChoice> aProtocolSelector,
    const Brx& aSerialNumber,
    const Brx& aSoftwareVersion)
    : Source(
        SourceFactory::kSourceNameRaat,
        SourceFactory::kSourceTypeRaat,
        aMediaPlayer.Pipeline(),
        false /* not visible by default */)
    , iLock("SRat")
    , iMediaPlayer(aMediaPlayer)
    , iAudioTime(aAudioTime)
    , iPullableClock(aPullableClock)
    , iSignalPathObservable(aSignalPathObservable)
    , iProtocolSelector(aProtocolSelector.Ptr())
    , iApp(nullptr)
    , iTrack(nullptr)
    , iSerialNumber(aSerialNumber)
    , iSoftwareVersion(aSoftwareVersion)
{
    iUriProvider = new UriProviderRaat(SourceFactory::kSourceTypeRaat, aMediaPlayer.TrackFactory());
    iUriProvider->SetTransportPlay(MakeFunctor(*this, &SourceRaat::Play));
    iUriProvider->SetTransportPause(MakeFunctor(*this, &SourceRaat::Pause));
    iUriProvider->SetTransportStop(MakeFunctor(*this, &SourceRaat::Stop));
    iUriProvider->SetTransportNext(MakeFunctor(*this, &SourceRaat::Next));
    iUriProvider->SetTransportPrev(MakeFunctor(*this, &SourceRaat::Prev));
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
}

SourceRaat::~SourceRaat()
{
    delete iProtocolSelector;
    delete iSignalPathObservable;
    delete iApp;
    if (iTrack != nullptr) {
        iTrack->RemoveRef();
    }
}

void SourceRaat::Activate(TBool aAutoPlay, TBool aPrefetchAllowed)
{
    SourceBase::Activate(aAutoPlay, aPrefetchAllowed); // FIXME - remove this if we find no work to do here
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

void SourceRaat::Play(const Brx& aUri)
{
    EnsureActiveNoPrefetch();
    AutoMutex _(iLock);
    if (iTrack != nullptr) {
        iTrack->RemoveRef();
    }
    iTrack = iUriProvider->SetTrack(aUri, iDefaultMetadata);
    iPipeline.RemoveAll();
    iPipeline.Begin(iUriProvider->Mode(), iTrack->Id());
    iPipeline.Play();
}

void SourceRaat::Started()
{
    iApp = new RaatApp(
        iMediaPlayer.Env(),
        iMediaPlayer,
        *this,
        iAudioTime,
        iPullableClock,
        *iSignalPathObservable,
        iSerialNumber,
        iSoftwareVersion);
    auto protocol = new ProtocolRaat(
        iMediaPlayer.Env(),
        iApp->Reader(),
        iMediaPlayer.TrackFactory());
    iMediaPlayer.Add(protocol);
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

void SourceRaat::Play()
{
    iApp->Transport().Play();
    iPipeline.Play();
}

void SourceRaat::Pause()
{
    if (iApp->Transport().CanPause()) {
        iPipeline.Pause();
    }
    else {
        iApp->Transport().Stop();
        iPipeline.Stop();
    }
}

void SourceRaat::Stop()
{
    iApp->Transport().Stop();
    iPipeline.Stop();
}

void SourceRaat::Next()
{
    if (iApp->Transport().CanMoveNext()) {
        iPipeline.Next();
    }
}

void SourceRaat::Prev()
{
    if (iApp->Transport().CanMovePrev()) {
        iPipeline.Prev();
    }
}

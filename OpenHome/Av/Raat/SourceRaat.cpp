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
#include <OpenHome/Av/Raat/Time.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;


// SourceFactory

ISource* SourceFactory::NewRaat(
    IMediaPlayer& aMediaPlayer,
    IRaatTime* aRaatTime,
    IRaatSignalPathObservable* aSignalPathObservable)
{ // static
    return new SourceRaat(aMediaPlayer, aRaatTime, aSignalPathObservable);
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
    return nullptr;
}


const TChar* SourceFactory::kSourceTypeRaat = "RAAT";
const Brn SourceFactory::kSourceNameRaat("RAAT");

// SourceRaat

SourceRaat::SourceRaat(
    IMediaPlayer& aMediaPlayer,
    IRaatTime* aRaatTime,
    IRaatSignalPathObservable* aSignalPathObservable)
    : Source(
        SourceFactory::kSourceNameRaat,
        SourceFactory::kSourceTypeRaat,
        aMediaPlayer.Pipeline(),
        false /* not visible by default */)
    , iLock("SRat")
    , iMediaPlayer(aMediaPlayer)
    , iRaatTime(aRaatTime)
    , iSignalPathObservable(aSignalPathObservable)
    , iApp(nullptr)
    , iTrack(nullptr)
{
    iUriProvider = new UriProviderRaat(SourceFactory::kSourceTypeRaat, aMediaPlayer.TrackFactory());
    iUriProvider->SetTransportPlay(MakeFunctor(*this, &SourceRaat::Play));
    iUriProvider->SetTransportStop(MakeFunctor(*this, &SourceRaat::Stop));
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
    delete iSignalPathObservable;
    delete iApp;
    delete iRaatTime;
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
        *iRaatTime,
        *iSignalPathObservable,
        Brn("12345"),
        Brn("0.0.0"));
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
    // deliberately blank - this source does not support any Transport commands, even the ones that are mandatory elsewhere
}

void SourceRaat::Stop()
{
    // deliberately blank - this source does not support any Transport commands, even the ones that are mandatory elsewhere
}

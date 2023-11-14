#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/NetworkAdapterList.h>
#include <OpenHome/Av/Debug.h>
#include <OpenHome/Av/Product.h>
#include <OpenHome/Av/Source.h>
#include <OpenHome/Av/OhMetadata.h>
#include <OpenHome/Av/Raop/SourceRaop.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Media/UriProviderRepeater.h>
#include <OpenHome/Av/Raop/ProtocolRaop.h>
#include <OpenHome/Av/Raop/Raop.h>
#include <OpenHome/Av/SourceFactory.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Av/VolumeManager.h>
#include <OpenHome/Av/Raop/CodecRaopApple.h>
#include <OpenHome/Optional.h>
#include <OpenHome/Private/Converter.h>

#include <limits.h>
#include <memory>

namespace OpenHome {
namespace Av {

class UriProviderRaop : public Media::UriProviderRepeater
{
public:
    UriProviderRaop(IMediaPlayer& aMediaPlayer, Optional<Media::IClockPuller> aClockPuller);
private: // from UriProvider
    Optional<Media::IClockPuller> ClockPuller() override;
private:
    Optional<Media::IClockPuller> iClockPuller;
};

} // namespace Av
} // namespace OpenHome


using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Configuration;
using namespace OpenHome::Media;
using namespace OpenHome::Net;


// SourceFactory

ISource* SourceFactory::NewRaop(IMediaPlayer& aMediaPlayer, Optional<Media::IClockPuller> aClockPuller, const Brx& aMacAddr, TUint aServerThreadPriority, IMdnsProvider& aMdnsProvider)
{ // static
    UriProviderRaop* raopUriProvider = new UriProviderRaop(aMediaPlayer, aClockPuller);
    aMediaPlayer.Add(raopUriProvider);
    return new SourceRaop(aMediaPlayer, *raopUriProvider, aMacAddr, aServerThreadPriority, aMdnsProvider);
}

const TChar* SourceFactory::kSourceTypeRaop = "NetAux";
const Brn SourceFactory::kSourceNameRaop("Net Aux");


// UriProviderRaop

UriProviderRaop::UriProviderRaop(IMediaPlayer& aMediaPlayer, Optional<Media::IClockPuller> aClockPuller)
    : UriProviderRepeater("RAOP", Latency::Internal, aMediaPlayer.TrackFactory())
    , iClockPuller(aClockPuller)
{
}

Optional<IClockPuller> UriProviderRaop::ClockPuller()
{
    return iClockPuller;
}


// SourceRaop

const Brn SourceRaop::kRaopPrefix("raop://");

SourceRaop::SourceRaop(IMediaPlayer& aMediaPlayer, UriProviderRaop& aUriProvider, const Brx& aMacAddr, TUint aServerThreadPriority, IMdnsProvider& aMdnsProvider)
    : Source(SourceFactory::kSourceNameRaop, SourceFactory::kSourceTypeRaop, aMediaPlayer.Pipeline(), false)
    , iEnv(aMediaPlayer.Env())
    , iLock("SRAO")
    , iUriProvider(aUriProvider)
    , iServerManager(aMediaPlayer.Env(), kMaxUdpSize, kMaxUdpPackets, aServerThreadPriority)
    , iSessionActive(false)
    , iTrack(nullptr)
    , iTrackPosSeconds(0)
    , iStreamId(UINT_MAX)
    , iTransportState(Media::EPipelineStopped)
    , iSemSessionStart("SRDS", 0)
    , iQuit(false)
{
    iRaopDiscovery = new RaopDiscovery(aMediaPlayer.Env(), aMediaPlayer.PowerManager(), aMediaPlayer.FriendlyNameObservable(), aMacAddr, aMediaPlayer.Pipeline(), aMdnsProvider);
    iRaopDiscovery->AddObserver(*this);

    iAudioId = iServerManager.CreateServer();
    iControlId = iServerManager.CreateServer();
    iTimingId = iServerManager.CreateServer();

    TimerFactory timerFactory(aMediaPlayer.Env());
    iProtocol = new ProtocolRaop(aMediaPlayer.Env(), aMediaPlayer.TrackFactory(), *iRaopDiscovery, iServerManager, iAudioId, iControlId, aServerThreadPriority, aServerThreadPriority, timerFactory); // Creating directly, rather than through ProtocolFactory.
    iPipeline.Add(iProtocol);   // takes ownership
    iPipeline.AddObserver(*this);

    const TUint serverAudioPort = ServerPort(iAudioId);
    const TUint serverControlPort = ServerPort(iControlId);
    const TUint serverTimingPort = ServerPort(iTimingId);
    iRaopDiscovery->SetListeningPorts(serverAudioPort, serverControlPort, serverTimingPort);

    NetworkAdapterList& adapterList = iEnv.NetworkAdapterList();
    Functor functor = MakeFunctor(*this, &SourceRaop::HandleInterfaceChange);
    iCurrentAdapterChangeListenerId = adapterList.AddCurrentChangeListener(functor, "SourceRaop-current");
    iSubnetListChangeListenerId = adapterList.AddSubnetListChangeListener(functor, "SourceRaop-subnet");

    // Give session start thread same priority as UDP server threads to ensure source switch isn't delayed by UDP server threads receiving packets.
    iThreadSessionStart = new ThreadFunctor("RaopSessionStart", MakeFunctor(*this, &SourceRaop::SessionStartThread), aServerThreadPriority);
    iThreadSessionStart->Start();
}

SourceRaop::~SourceRaop()
{
    iQuit = true;
    iSemSessionStart.Signal();
    delete iThreadSessionStart;

    iEnv.NetworkAdapterList().RemoveCurrentChangeListener(iCurrentAdapterChangeListenerId);
    iEnv.NetworkAdapterList().RemoveSubnetListChangeListener(iSubnetListChangeListenerId);
    delete iRaopDiscovery;

    iLock.Wait();
    if (iTrack != nullptr) {
        iTrack->RemoveRef();
    }
    if (iSessionActive) {
        iSessionActive = false;
    }
    iLock.Signal();
}

IRaopDiscovery& SourceRaop::Discovery()
{
    return *iRaopDiscovery;
}

void SourceRaop::Activate(TBool aAutoPlay, TBool aPrefetchAllowed)
{
    SourceBase::Activate(aAutoPlay, aPrefetchAllowed);
    iLock.Wait();
    iTrackPosSeconds = 0;

    if (iSessionActive) {
        StartNewTrack();
        iLock.Signal();
        if (aPrefetchAllowed) {
            iPipeline.Play();
        }
    }
    else {
        if (iTrack != nullptr) {
            iTrack->RemoveRef();
            iTrack = nullptr;
        }
        GenerateMetadata(); // Updates iDidlLite.
        iTrack = iUriProvider.SetTrack(iNextTrackUri, iDidlLite);
        const TUint trackId = (iTrack==nullptr? Track::kIdNone : iTrack->Id());
        iLock.Signal();
        if (aPrefetchAllowed) {
            iPipeline.StopPrefetch(iUriProvider.Mode(), trackId);
        }
    }
}

void SourceRaop::Deactivate()
{
    iLock.Wait();
    iTransportState = Media::EPipelineStopped;
    iSessionActive = false; // If switching away from Net Aux, don't want to allow session to be re-initialised without user explicitly re-selecting device from a control point.
    iLock.Signal();
    Source::Deactivate();
}

TBool SourceRaop::TryActivateNoPrefetch(const Brx& aMode)
{
    if (iUriProvider.Mode() != aMode) {
        return false;
    }
    EnsureActiveNoPrefetch();
    return true;
}

void SourceRaop::StandbyEnabled()
{
    iPipeline.Stop();
    {
        AutoMutex _(iLock);
        iTransportState = Media::EPipelineStopped;
        iSessionActive = false; // If switching away from Net Aux, don't want to allow session to be re-initialised without user explicitly re-selecting device from a control point.
    }
}

void SourceRaop::PipelineStopped()
{
    // FIXME - could nullptr iPipeline (if we also changed it to be a pointer)
}

void SourceRaop::GenerateMetadata()
/**
 * Helper method - should only be called during construction.
 */
{
    // Get current system name.
    iDidlLite.SetBytes(0);
    WriterBuffer w(iDidlLite);
    WriterDIDLLite writer(Brn(""), DIDLLite::kItemTypeAudioItem, w);

    // Append name.
    Bws<kMaxSourceNameBytes> name;
    Name(name);
    writer.WriteTitle(name);

    writer.WriteEnd();
}

void SourceRaop::StartNewTrack()
{
    iPipeline.RemoveAll();
    if (iTrack != nullptr) {
        iTrack->RemoveRef();
        iTrack = nullptr;
    }

    GenerateMetadata(); // Updates iDidlLite.
    iTrack = iUriProvider.SetTrack(iNextTrackUri, iDidlLite);
    const TUint trackId = (iTrack==nullptr? Track::kIdNone : iTrack->Id());
    iPipeline.Begin(iUriProvider.Mode(), trackId);

    iTransportState = Media::EPipelinePlaying;
}

void SourceRaop::NotifySessionStart(TUint aControlPort, TUint aTimingPort)
{
    LOG(kMedia, ">SourceRaop::NotifySessionStart aControlPort: %u, aTimingPort: %u\n", aControlPort, aTimingPort);

    /*
     * If the media player is in standby, synchronously calling
     * DoActivate()/DoPlay() here will take the media player out of standby,
     * which could take a long time (on the order of seconds), depending on
     * the length of time that subsystems take to come out of standby.
     *
     * To avoid the possibility of the RAOP session timing out (either because
     * the sender gave up during that time, or because the RAOP timeout freed
     * up the server thread), return from this method immediately so that
     * further RAOP setup comms can take place and perform any potentially
     * length standby-disable handling on a separate thread.
     */

    // Initialise members so that thread can access appropriate iNextTrackUri.
    AutoMutex a(iLock);
    iSessionActive = true;

    iNextTrackUri.Replace(kRaopPrefix);
    Ascii::AppendDec(iNextTrackUri, aControlPort);
    iNextTrackUri.Append('.');
    Ascii::AppendDec(iNextTrackUri, aTimingPort);

    // Do any potentially lengthy tasks on separate thread.
    SessionStartAsynchronous();
    LOG(kMedia, "<SourceRaop::NotifySessionStart\n");
}

void SourceRaop::NotifySessionEnd()
{
    LOG(kMedia, ">SourceRaop::NotifySessionEnd\n");
    iLock.Wait();
    iNextTrackUri.SetBytes(0);

    TBool shouldStop = (IsActive() && iSessionActive) ? true : false;
    if (shouldStop) {
        iPipeline.RemoveAll();
        if (iTrack != nullptr) {
            iTrack->RemoveRef();
            iTrack = nullptr;
        }
    }

    iSessionActive = false;
    iTransportState = Media::EPipelineStopped;
    iLock.Signal();

    if (shouldStop) {
        iPipeline.Stop();
    }
    LOG(kMedia, "<SourceRaop::NotifySessionEnd\n");
}

void SourceRaop::NotifySessionWait(TUint aSeq, TUint aTime)
{
    LOG(kMedia, ">SourceRaop::NotifySessionWait aSeq: %u, aTime: %u\n", aSeq, aTime);
    // This call will only come in while session should be active.
    // However, it's possible that the pipeline may not have recognised the
    // stream and asked ProtocolRaop to TryStop(), which will have caused it to
    // exit its Stream() method, so SendFlush() will return
    // MsgFlush::kIdInvalid (as it can no longer send a flush).

    AutoMutex a(iLock);
    if (IsActive() && iSessionActive) {
        // Synchronous callback ensures ::FlushCallback() is able to notify pipeline of flush ID to wait for BEFORE protocol module sends that flush ID into pipeline.
        iProtocol->SendFlush(aSeq, aTime, MakeFunctorGeneric<TUint>(*this, &SourceRaop::FlushCallback));
    }

    LOG(kMedia, "<SourceRaop::NotifySessionWait\n");
}

void SourceRaop::NotifyPipelineState(Media::EPipelineState aState)
{
    iLock.Wait();
    iTransportState = aState;
    iLock.Signal();
}

void SourceRaop::NotifyMode(const Brx& /*aMode*/,
                            const ModeInfo& /*aInfo*/,
                            const ModeTransportControls& /*aTransportControls*/)
{
}

void SourceRaop::NotifyTrack(Media::Track& aTrack, TBool /*aStartOfStream*/)
{
    iLock.Wait();
    if (iTrack != nullptr) {
        iTrack->RemoveRef();
    }
    iTrack = &aTrack;
    iTrack->AddRef();
    iLock.Signal();
}

void SourceRaop::NotifyMetaText(const Brx& /*aText*/)
{
}

void SourceRaop::NotifyTime(TUint aSeconds)
{
    iLock.Wait();
    iTrackPosSeconds = aSeconds;
    iLock.Signal();
}

void SourceRaop::NotifyStreamInfo(const Media::DecodedStreamInfo& aStreamInfo)
{
    iLock.Wait();
    iStreamId = aStreamInfo.StreamId();
    iLock.Signal();
}

TUint SourceRaop::ServerPort(TUint aId)
{
    auto server = iServerManager.Find(aId);
    const TUint port = server->Port();
    server->RemoveRef();
    return port;
}

void SourceRaop::FlushCallback(TUint aFlushId)
{
    // Called synchronously during SendFlush() call in ::NotifySessionWait();
    if (aFlushId != MsgFlush::kIdInvalid) {
        iTransportState = Media::EPipelineWaiting;
        iPipeline.Wait(aFlushId);
    }
}
 
void SourceRaop::HandleInterfaceChange()
{
    //iRaopDiscovery->Disable();
    //iRaopDiscovery->Enable();
    const TUint serverAudioPort = ServerPort(iAudioId);
    const TUint serverControlPort = ServerPort(iControlId);
    const TUint serverTimingPort = ServerPort(iTimingId);
    iRaopDiscovery->SetListeningPorts(serverAudioPort, serverControlPort, serverTimingPort);
}

void SourceRaop::SessionStartAsynchronous()
{
    iSemSessionStart.Signal();
}

void SourceRaop::SessionStartThread()
{
    for (;;) {
        iSemSessionStart.Wait();

        if (iQuit) {
            return;
        }

        LOG(kMedia, ">SourceRaop::SessionStartThread\n");
        // Setup pipeline (taking media player out of standby if required).
        ActivateIfNotActive();
        {
            AutoMutex a(iLock);
            StartNewTrack();
        }
        DoPlay();
        LOG(kMedia, "<SourceRaop::SessionStartThread\n");

    }
}

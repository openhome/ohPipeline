#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Optional.h>
#include <OpenHome/Av/SourceFactory.h>
#include <OpenHome/Av/Source.h>
#include <OpenHome/Av/Songcast/ProviderReceiver.h>
#include <OpenHome/Av/Songcast/ZoneHandler.h>
#include <OpenHome/Media/ClockPuller.h>
#include <OpenHome/Media/PipelineObserver.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Media/UriProviderSingleTrack.h>
#include <OpenHome/Media/Codec/CodecFactory.h>
#include <OpenHome/Av/Songcast/ProtocolOhu.h>
#include <OpenHome/Av/Songcast/ProtocolOhm.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Pipeline/Logger.h>
#include <OpenHome/Av/Songcast/OhmMsg.h>
#include <OpenHome/Av/Songcast/Splitter.h>
#include <OpenHome/Av/Songcast/SenderThread.h>
#include <OpenHome/Av/Songcast/Sender.h>
#include <OpenHome/Av/Product.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Av/Debug.h>
#include <OpenHome/PowerManager.h>
#include <OpenHome/Private/Env.h>
#include <OpenHome/Private/NetworkAdapterList.h>
#include <OpenHome/Private/TIpAddressUtils.h>

namespace OpenHome {
namespace Media {
    class PipelineManager;
    class Sender;
}
namespace Av {

class UriProviderSongcast : public Media::UriProviderSingleTrack
{
public:
    UriProviderSongcast(IMediaPlayer& aMediaPlayer, Optional<Media::IClockPuller> aClockPuller);
private: // from UriProvider
    Optional<Media::IClockPuller> ClockPuller() override;
private:
    Media::IClockPuller* iClockPuller;
};

class SongcastSender;

class SourceReceiver : public Source, private ISourceReceiver, private IZoneListener, private Media::IPipelineObserver
{
    static const TChar* kProtocolInfo;
public:
    SourceReceiver(IMediaPlayer& aMediaPlayer,
                   Optional<Media::IClockPuller> aClockPuller,
                   Optional<IOhmTimestamper> aTxTimestamper,
                   Optional<IOhmTimestamper> aRxTimestamper,
                   Optional<IOhmMsgProcessor> aOhmMsgObserver);
    ~SourceReceiver();
private: // from ISource
    void Activate(TBool aAutoPlay, TBool aPrefetchAllowed) override;
    void Deactivate() override;
    TBool TryActivateNoPrefetch(const Brx& aMode) override;
    void StandbyEnabled() override;
    void PipelineStopped() override;
private: // from ISourceReceiver
    void Play() override;
    void Stop() override;
    void SetSender(const Brx& aUri, const Brx& aMetadata) override;
    void SenderInfo(Bwx& aUri, Bwx& aMetadata) override;
private: // from IZoneListener
    void ZoneUriChanged(const Brx& aZone, const Brx& aUri) override;
    void NotifyPresetInfo(TUint aPreset, const Brx& aMetadata) override;
private: // from Media::IPipelineObserver
    void NotifyPipelineState(Media::EPipelineState aState) override;
    void NotifyMode(const Brx& aMode, const Media::ModeInfo& aInfo,
                    const Media::ModeTransportControls& aTransportControls) override;
    void NotifyTrack(Media::Track& aTrack, TBool aStartOfStream) override;
    void NotifyMetaText(const Brx& aText) override;
    void NotifyTime(TUint aSeconds) override;
    void NotifyStreamInfo(const Media::DecodedStreamInfo& aStreamInfo) override;
private:
    void UriChanged();
    void ZoneChangeThread();
    void CurrentAdapterChanged();
private:
    Mutex iLock;
    Mutex iActivationLock;
    Mutex iUriLock;
    Environment& iEnv;
    ThreadFunctor* iZoneChangeThread;
    ZoneHandler* iZoneHandler;
    ProviderReceiver* iProviderReceiver;
    UriProviderSongcast* iUriProvider;
    OhmMsgFactory* iOhmMsgFactory;
    Uri iUri; // allocated here as stack requirements are too high for an automatic variable
    Bws<ZoneHandler::kMaxZoneBytes> iZone;
    Media::BwsTrackUri iTrackUri;
    Media::BwsTrackMetaData iTrackMetadata;
    TUint iTrackId;
    TBool iPlaying;
    TBool iQuit;
    Media::BwsTrackUri iPendingTrackUri;
    SongcastSender* iSender;
    StoreText* iStoreZone;
    StoreText* iStoreUri;
    StoreText* iStoreMetadata;
    TUint iNacnId;
};

class SongcastSender : private Media::IPipelineObserver
                     , private IProductObserver
{
public:
    SongcastSender(IMediaPlayer& aMediaPlayer, ZoneHandler& aZoneHandler,
                   Optional<IOhmTimestamper> aTxTimestamper, const Brx& aMode,
                   IUnicastOverrideObserver& aUnicastOverrideObserver);
    ~SongcastSender();
private: // from Media::IPipelineObserver
    void NotifyPipelineState(Media::EPipelineState aState) override;
    void NotifyMode(const Brx& aMode, const Media::ModeInfo& aInfo,
                    const Media::ModeTransportControls& aTransportControls) override;
    void NotifyTrack(Media::Track& aTrack, TBool aStartOfStream) override;
    void NotifyMetaText(const Brx& aText) override;
    void NotifyTime(TUint aSeconds) override;
    void NotifyStreamInfo(const Media::DecodedStreamInfo& aStreamInfo) override;
private: // from IProductObserver
    void Started() override;
    void SourceIndexChanged() override;
    void SourceXmlChanged() override;
    void ProductUrisChanged() override;
private:
    void FriendlyNameChanged(const Brx& aName);
private:
    Mutex iLock;
    SenderThread* iSenderThread;
    Sender* iSender;
    Product& iProduct;
    IFriendlyNameObservable& iFriendlyNameObservable;
    Media::Logger* iLoggerSender;
    Splitter* iSplitter;
    Media::Logger* iLoggerSplitter;
    TUint iFriendlyNameId;
};

} // namespace Av
} // namespace OpenHome


using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Net;
using namespace OpenHome::Media;
using namespace OpenHome::Configuration;

// SourceFactory

ISource* SourceFactory::NewReceiver(IMediaPlayer& aMediaPlayer,
                                    Optional<IClockPuller> aClockPuller,
                                    Optional<IOhmTimestamper> aTxTimestamper,
                                    Optional<IOhmTimestamper> aRxTimestamper,
                                    Optional<IOhmMsgProcessor> aOhmMsgObserver)
{ // static
    return new SourceReceiver(aMediaPlayer, aClockPuller, aTxTimestamper, aRxTimestamper, aOhmMsgObserver);
}

const TChar* SourceFactory::kSourceTypeReceiver = "Receiver";
const Brn SourceFactory::kSourceNameReceiver("Songcast");


// UriProviderSongcast

UriProviderSongcast::UriProviderSongcast(IMediaPlayer& aMediaPlayer, Optional<Media::IClockPuller> aClockPuller)
    : UriProviderSingleTrack(SourceFactory::kSourceTypeReceiver,
                             Latency::Internal,
                             false, /* supports pause */
                             aMediaPlayer.TrackFactory())
    , iClockPuller(aClockPuller.Ptr())
{
}

Optional<IClockPuller> UriProviderSongcast::ClockPuller()
{
    return iClockPuller;
}


// SourceReceiver

const TChar* SourceReceiver::kProtocolInfo = "ohz:*:*:*,ohm:*:*:*,ohu:*.*.*";

SourceReceiver::SourceReceiver(IMediaPlayer& aMediaPlayer,
                               Optional<Media::IClockPuller> aClockPuller,
                               Optional<IOhmTimestamper> aTxTimestamper,
                               Optional<IOhmTimestamper> aRxTimestamper,
                               Optional<IOhmMsgProcessor> aOhmMsgObserver)
    : Source(SourceFactory::kSourceNameReceiver, SourceFactory::kSourceTypeReceiver, aMediaPlayer.Pipeline())
    , iLock("SRX1")
    , iActivationLock("SRX2")
    , iUriLock("SRX3")
    , iEnv(aMediaPlayer.Env())
    , iTrackId(Track::kIdNone)
    , iPlaying(false)
    , iQuit(false)
{
    Environment& env = aMediaPlayer.Env();
    DvDeviceStandard& device = aMediaPlayer.Device();
    iZoneChangeThread = new ThreadFunctor("ZoneChangeHandler", MakeFunctor(*this, &SourceReceiver::ZoneChangeThread));
    iZoneChangeThread->Start();
    iZoneHandler = new ZoneHandler(env, device.Udn());

    // Receiver
    iStoreUri = new StoreText(aMediaPlayer.ReadWriteStore(), aMediaPlayer.PowerManager(), kPowerPriorityNormal,
                               Brn("Receiver.Uri"), Brx::Empty(), iTrackUri.MaxBytes());
    iStoreMetadata = new StoreText(aMediaPlayer.ReadWriteStore(), aMediaPlayer.PowerManager(), kPowerPriorityNormal,
                               Brn("Receiver.Metadata"), Brx::Empty(), iTrackMetadata.MaxBytes());
    iProviderReceiver = new ProviderReceiver(device, *this, kProtocolInfo);
    iUriProvider = new UriProviderSongcast(aMediaPlayer, aClockPuller);
    iUriProvider->SetTransportPlay(MakeFunctor(*this, &SourceReceiver::Play));
    iUriProvider->SetTransportStop(MakeFunctor(*this, &SourceReceiver::Stop));
    iPipeline.Add(iUriProvider);
    iOhmMsgFactory = new OhmMsgFactory(210, 10, 10);
    TrackFactory& trackFactory = aMediaPlayer.TrackFactory();
    auto protocolOhm = new ProtocolOhm(env, *iOhmMsgFactory, trackFactory, aRxTimestamper, iUriProvider->Mode(), aOhmMsgObserver);
    iPipeline.Add(protocolOhm);
    iPipeline.Add(new ProtocolOhu(env, *iOhmMsgFactory, trackFactory, aRxTimestamper, iUriProvider->Mode(), aOhmMsgObserver));
    iStoreZone = new StoreText(aMediaPlayer.ReadWriteStore(), aMediaPlayer.PowerManager(), kPowerPriorityNormal,
                               Brn("Receiver.Zone"), Brx::Empty(), iZone.MaxBytes());
    iStoreZone->Get(iZone);
    iZoneHandler->AddListener(*this);
    iPipeline.AddObserver(*this);
    iNacnId = iEnv.NetworkAdapterList().AddCurrentChangeListener(MakeFunctor(*this, &SourceReceiver::CurrentAdapterChanged), "SourceReceiver", false);

    // Sender
    iSender = new SongcastSender(aMediaPlayer, *iZoneHandler, aTxTimestamper, iUriProvider->Mode(), *protocolOhm);
}

SourceReceiver::~SourceReceiver()
{
    delete iSender;
    iEnv.NetworkAdapterList().RemoveCurrentChangeListener(iNacnId);
    delete iStoreZone;
    delete iStoreUri;
    delete iStoreMetadata;
    delete iOhmMsgFactory;
    iZoneHandler->RemoveListener(*this);
    delete iZoneChangeThread;
    delete iProviderReceiver;
    delete iZoneHandler;
}

void SourceReceiver::Activate(TBool aAutoPlay, TBool aPrefetchAllowed)
{
    LOG(kSongcast, "SourceReceiver::Activate()\n");
    SourceBase::Activate(aAutoPlay, aPrefetchAllowed);
    if (aPrefetchAllowed) {
        iPipeline.StopPrefetch(iUriProvider->Mode(), Track::kIdNone);
        if (aAutoPlay) {
            iPlaying = true;
        }
        if (iZone.Bytes() > 0) {
            iZoneHandler->StartMonitoring(iZone);
        }
    }
    else {
        iPipeline.RemoveAll();
    }
}

void SourceReceiver::Deactivate()
{
    LOG(kSongcast, "SourceReceiver::Deactivate()\n");
    iProviderReceiver->NotifyPipelineState(EPipelineStopped);
    iZoneHandler->ClearCurrentSenderUri();
    iZoneHandler->StopMonitoring();
    iPlaying = false;
    iTrackUri.Replace(Brx::Empty());
    iStoreZone->Write();
    iStoreUri->Write();
    iStoreMetadata->Write();
    Source::Deactivate();
}

TBool SourceReceiver::TryActivateNoPrefetch(const Brx& aMode)
{
    if (iUriProvider->Mode() != aMode) {
        return false;
    }
    EnsureActiveNoPrefetch();
    return true;
}

void SourceReceiver::StandbyEnabled()
{
    Stop();
    iTrackUri.Replace(Brx::Empty());
}

void SourceReceiver::PipelineStopped()
{
    iLock.Wait();
    iQuit = true;
    iLock.Signal();
}

void SourceReceiver::Play()
{
    LOG(kSongcast, "SourceReceiver::Play()\n");
    EnsureActiveNoPrefetch();
    TBool doPlay = false;
    {
        AutoMutex a(iLock);
        iPlaying = true;
        if (iZone.Bytes() > 0) {
            iZoneHandler->StartMonitoring(iZone);
        }
        if (iTrackUri.Bytes() > 0) {
            iZoneHandler->SetCurrentSenderUri(iTrackUri);
            iPipeline.Begin(iUriProvider->Mode(), iTrackId);
            doPlay = true;
        }
    }
    if (doPlay) {
        DoPlay();
    }
}

void SourceReceiver::Stop()
{
    LOG(kSongcast, "SourceReceiver::Stop()\n");
    iLock.Wait();
    iPlaying = false;
    iPipeline.Stop();
    iZoneHandler->ClearCurrentSenderUri();
    iZoneHandler->StopMonitoring();
    iLock.Signal();
}

void SourceReceiver::SetSender(const Brx& aUri, const Brx& aMetadata)
{
    LOG(kSongcast, "SourceReceiver::SetSender(%.*s)\n", PBUF(aUri));
    if (aUri.Bytes() > 0) {
        EnsureActiveNoPrefetch();
    }
    AutoMutex a(iLock);
    if (aUri.Bytes() > 0) {
        iUri.Replace(aUri);
    }
    else {
        iUri.Clear(); /* special case treatment for an empty uri.  iUri.Replace() would throw
                         if passed a 0-byte buffer.  Passing a 0-byte buffer is the only way
                         the provider has of clearing a sender though... */
    }
    // FIXME - may later want to handle a 'preset' scheme to allow presets to be selected from UI code
    if (iUri.Scheme() == ZoneHandler::kProtocolZone) {
        Endpoint ep;
        try {
            ep.SetPort(iUri.Port());
            ep.SetAddress(iUri.Host());
        }
        catch (NetworkError&) {
            THROW(UriError);
        }
        const Endpoint& tgt = iZoneHandler->MulticastEndpoint();
        if (!TIpAddressUtils::Equals(ep.Address(), tgt.Address()) || ep.Port() != tgt.Port()) {
            THROW(UriError);
        }
        const Brx& path = iUri.Path();
        if (path.Bytes() < 2 || path[0] != '/') {
            THROW(UriError);
        }
        iTrackUri.Replace(Brx::Empty());
        iTrackMetadata.Replace(Brx::Empty());
        iZone.Replace(path.Split(1)); // remove leading /
        iStoreZone->Set(iZone);
        if (iPlaying) {
            iZoneHandler->StartMonitoring(iZone);
        }
        else {
            iTrackId = Track::kIdNone;
            iPipeline.StopPrefetch(iUriProvider->Mode(), iTrackId);
        }
    }
    else {
        iZone.Replace(Brx::Empty());
        iStoreZone->Set(iZone);
        iZoneHandler->ClearCurrentSenderUri();
        iZoneHandler->StopMonitoring();
        iTrackUri.Replace(aUri);
        iTrackMetadata.Replace(aMetadata);
        if (IsActive()) {
            UriChanged();
        }
    }
    iStoreUri->Set(aUri);
    iStoreMetadata->Set(aMetadata);
}

void SourceReceiver::SenderInfo(Bwx& aUri, Bwx& aMetadata)
{
    iStoreUri->Get(aUri);
    iStoreMetadata->Get(aMetadata);
}

void SourceReceiver::ZoneUriChanged(const Brx& aZone, const Brx& aUri)
{
    LOG(kSongcast, "SourceReceiver::ZoneUriChanged(%.*s, %.*s)\n",
                   PBUF(aZone), PBUF(aUri));
    // FIXME - use of iZone/iTrackUri not threadsafe
    if (aZone == iZone && aUri != iTrackUri) {

        iZoneHandler->SetCurrentSenderUri(aUri);
        iUriLock.Wait();
        iPendingTrackUri.Replace(aUri);
        iUriLock.Signal();
        iZoneChangeThread->Signal();
    }
}

void SourceReceiver::NotifyPresetInfo(TUint /*aPreset*/, const Brx& /*aMetadata*/)
{
    // FIXME - will need to implement this once we support preset selection via UI
}

void SourceReceiver::NotifyPipelineState(EPipelineState aState)
{
    if (IsActive()) {
        iProviderReceiver->NotifyPipelineState(aState);
    }
}

void SourceReceiver::NotifyMode(const Brx& /*aMode*/,
                                const ModeInfo& /*aInfo*/,
                                const ModeTransportControls& /*aTransportControls*/)
{
}

void SourceReceiver::NotifyTrack(Track& /*aTrack*/, TBool /*aStartOfStream*/)
{
}

void SourceReceiver::NotifyMetaText(const Brx& /*aText*/)
{
}

void SourceReceiver::NotifyTime(TUint /*aSeconds*/)
{
}

void SourceReceiver::NotifyStreamInfo(const DecodedStreamInfo& /*aStreamInfo*/)
{
}

void SourceReceiver::UriChanged()
{
    LOG(kSongcast, "SourceReceiver::UriChanged().  IsActive=%u, Playing=%u, url=%.*s\n",
                   IsActive(), iPlaying, PBUF(iTrackUri));
    Track* track = iUriProvider->SetTrack(iTrackUri, iTrackMetadata);
    if (track == nullptr) {
        iTrackId = Track::kIdNone;
        iPipeline.StopPrefetch(iUriProvider->Mode(), iTrackId);
    }
    else {
        iTrackId = track->Id();
        track->RemoveRef();
        if (IsActive() && iPlaying) {
            iPipeline.RemoveAll();
            iPipeline.Begin(iUriProvider->Mode(), iTrackId);
            iLock.Signal();
            /* DoPlay calls PowerManager::StandbyDisable, which calls standby handlers with a PowerManager lock held
               Another thread may be running Enabled standby handlers with the same PowerManager lock held.  One of
               these callbacks can call SourceReceiver::Stop, which waits on iLock. */
            DoPlay();
            iLock.Wait();
        }
    }
}

void SourceReceiver::ZoneChangeThread()
{
    for (;;) {
        iZoneChangeThread->Wait();
        AutoMutex a(iLock);
        if (iQuit) {
            break;
        }
        iUriLock.Wait();
        iTrackUri.Replace(iPendingTrackUri);
        iUriLock.Signal();
        UriChanged();
    }
}

void SourceReceiver::CurrentAdapterChanged()
{
    if (IsActive() && iZone.Bytes() > 0) {
        iZoneHandler->StartMonitoring(iZone);
    }
}


// SongcastSender

SongcastSender::SongcastSender(IMediaPlayer& aMediaPlayer, ZoneHandler& aZoneHandler,
                               Optional<IOhmTimestamper> aTxTimestamper, const Brx& aMode,
                               IUnicastOverrideObserver& aUnicastOverrideObserver)
    : iLock("STX1")
    , iProduct(aMediaPlayer.Product())
    , iFriendlyNameObservable(aMediaPlayer.FriendlyNameObservable())
{
    Media::PipelineManager& pipeline = aMediaPlayer.Pipeline();
    TUint priorityFiller = 0;
    TUint priorityFlywheelRamper = 0;
    TUint priorityStarvationRamper = 0;
    TUint priorityCodec = 0;
    TUint priorityEvent = 0;
    pipeline.GetThreadPriorities(priorityFiller, priorityFlywheelRamper, priorityStarvationRamper, priorityCodec, priorityEvent);
    const TUint senderThreadPriority = priorityFiller;
    iSender = new Sender(aMediaPlayer.Env(), aMediaPlayer.Device(), aZoneHandler,
                         aTxTimestamper, aMediaPlayer.ConfigInitialiser(), senderThreadPriority,
                         Brx::Empty(), pipeline.SenderMinLatencyMs(), aMode,
                         aUnicastOverrideObserver);
    iLoggerSender = new Logger("Sender", *iSender);
    //iLoggerSender->SetEnabled(true);
    //iLoggerSender->SetFilter(Logger::EMsgAll);
    iSenderThread = new SenderThread(*iLoggerSender, pipeline.Factory(), priorityStarvationRamper-1);
    iSplitter = new Splitter(*iSenderThread, aMode);
    iLoggerSplitter = new Logger(*iSplitter, "Splitter");
    iSplitter->SetUpstream(pipeline.InsertElements(*iLoggerSplitter));
    //iLoggerSplitter->SetEnabled(true);
    //iLoggerSplitter->SetFilter(Logger::EMsgAll);
    aMediaPlayer.AddAttribute("Sender");
    pipeline.AddObserver(*this);
    iProduct.AddObserver(*this);
    iFriendlyNameId = iFriendlyNameObservable.RegisterFriendlyNameObserver(MakeFunctorGeneric<const Brx&>(*this, &SongcastSender::FriendlyNameChanged));
}

SongcastSender::~SongcastSender()
{
    iFriendlyNameObservable.DeregisterFriendlyNameObserver(iFriendlyNameId);
    delete iLoggerSplitter;
    delete iSplitter;
    delete iSenderThread;
    delete iLoggerSender;
    delete iSender;
}

void SongcastSender::NotifyPipelineState(EPipelineState aState)
{
    iSender->NotifyPipelineState(aState);
}

void SongcastSender::NotifyMode(const Brx& /*aMode*/,
                                const ModeInfo& /*aInfo*/,
                                const ModeTransportControls& /*aTransportControls*/)
{
}

void SongcastSender::NotifyTrack(Track& /*aTrack*/, TBool /*aStartOfStream*/)
{
}

void SongcastSender::NotifyMetaText(const Brx& /*aText*/)
{
}

void SongcastSender::NotifyTime(TUint /*aSeconds*/)
{
}

void SongcastSender::NotifyStreamInfo(const DecodedStreamInfo& /*aStreamInfo*/)
{
}

void SongcastSender::Started()
{
}

void SongcastSender::SourceIndexChanged()
{
}

void SongcastSender::SourceXmlChanged()
{
}

void SongcastSender::ProductUrisChanged()
{
    Bws<Product::kMaxRoomBytes> room;
    Bws<Product::kMaxNameBytes> name;
    Brn info;
    Bws<Product::kMaxUriBytes> imageUri;
    Bws<Product::kMaxUriBytes> imageHiresUri;
    iProduct.GetProductDetails(room, name, info, imageUri, imageHiresUri);
    iSender->SetImageUri(imageUri);
}

void SongcastSender::FriendlyNameChanged(const Brx& aName)
{
    iSender->SetName(aName);
}

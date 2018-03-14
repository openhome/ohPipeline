#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Types.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Net/Core/DvDevice.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Av/VolumeManager.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/UriProviderSingleTrack.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Av/KvpStore.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Av/Product.h>
#include <OpenHome/Av/ProviderTime.h>
#include <OpenHome/Av/ProviderInfo.h>
#include <OpenHome/Av/ProviderTransport.h>
#include <OpenHome/Av/ProviderFactory.h>
#include <OpenHome/Av/Songcast/ZoneHandler.h>
#include <OpenHome/Configuration/IStore.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Configuration/ProviderConfig.h>
#include <OpenHome/Configuration/ProviderConfigApp.h>
#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Media/MimeTypeList.h>
#include <OpenHome/Av/Logger.h>
#include <OpenHome/UnixTimestamp.h>
#include <OpenHome/Av/TransportPins.h>
#include <OpenHome/Av/PodcastPins.h>

#include <memory>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Configuration;
using namespace OpenHome::Media;
using namespace OpenHome::Net;

// MediaPlayerInitParams

MediaPlayerInitParams* MediaPlayerInitParams::New(const Brx& aDefaultRoom, const Brx& aDefaultName)
{ // static
    return new MediaPlayerInitParams(aDefaultRoom, aDefaultName);
}

MediaPlayerInitParams::MediaPlayerInitParams(const Brx& aDefaultRoom, const Brx& aDefaultName)
    : iDefaultRoom(aDefaultRoom)
    , iDefaultName(aDefaultName)
    , iThreadPoolHigh(1)
    , iThreadPoolMedium(1)
    , iThreadPoolLow(1)
    , iConfigAppEnable(false)
{
}

void MediaPlayerInitParams::EnableConfigApp()
{
    iConfigAppEnable = true;
}

void MediaPlayerInitParams::SetThreadPoolSize(TUint aCountHigh, TUint aCountMedium, TUint aCountLow)
{
    iThreadPoolHigh = aCountHigh;
    iThreadPoolMedium = aCountMedium;
    iThreadPoolLow = aCountLow;
}

const Brx& MediaPlayerInitParams::DefaultRoom() const
{
    return iDefaultRoom;
}

const Brx& MediaPlayerInitParams::DefaultName() const
{
    return iDefaultName;
}

TBool MediaPlayerInitParams::ConfigAppEnabled() const
{
    return iConfigAppEnable;
}

TUint MediaPlayerInitParams::ThreadPoolCountHigh() const
{
    return iThreadPoolHigh;
}

TUint MediaPlayerInitParams::ThreadPoolCountMedium() const
{
    return iThreadPoolMedium;
}

TUint MediaPlayerInitParams::ThreadPoolCountLow() const
{
    return iThreadPoolLow;
}


// MediaPlayer

MediaPlayer::MediaPlayer(Net::DvStack& aDvStack, Net::CpStack& aCpStack, Net::DvDeviceStandard& aDevice,
                         IStaticDataSource& aStaticDataSource,
                         IStoreReadWrite& aReadWriteStore,
                         PipelineInitParams* aPipelineInitParams,
                         VolumeConsumer& aVolumeConsumer, IVolumeProfile& aVolumeProfile,
                         IInfoAggregator& aInfoAggregator,
                         const Brx& aEntropy,
                         MediaPlayerInitParams* aInitParams)
    : iDvStack(aDvStack)
    , iCpStack(aCpStack)
    , iDevice(aDevice)
    , iReadWriteStore(aReadWriteStore)
    , iConfigProductRoom(nullptr)
    , iConfigProductName(nullptr)
    , iConfigAutoPlay(nullptr)
    , iConfigStartupSource(nullptr)
    , iProviderTransport(nullptr)
    , iProviderConfigApp(nullptr)
    , iLoggerBuffered(nullptr)
    , iDebugManager(nullptr)
    , iTransportPins(nullptr)
    , iPodcastPins(nullptr)
{
    iUnixTimestamp = new OpenHome::UnixTimestamp(iDvStack.Env());
    iKvpStore = new KvpStore(aStaticDataSource);
    iTrackFactory = new Media::TrackFactory(aInfoAggregator, kTrackCount);
    iConfigManager = new Configuration::ConfigManager(iReadWriteStore);
    if (aInitParams->ConfigAppEnabled()) {
        iProviderConfigApp = new ProviderConfigApp(aDevice,
                                                   *iConfigManager, *iConfigManager,
                                                   iReadWriteStore); // must be created before any config values
    }
    iPowerManager = new OpenHome::PowerManager(*iConfigManager);
    iThreadPool = new OpenHome::ThreadPool(aInitParams->ThreadPoolCountHigh(),
                                           aInitParams->ThreadPoolCountMedium(),
                                           aInitParams->ThreadPoolCountLow());
    iConfigProductRoom = new ConfigText(*iConfigManager, Product::kConfigIdRoomBase, Product::kMinRoomBytes, Product::kMaxRoomBytes, aInitParams->DefaultRoom());
    iConfigProductName = new ConfigText(*iConfigManager, Product::kConfigIdNameBase, Product::kMinNameBytes, Product::kMaxNameBytes, aInitParams->DefaultName());
    std::vector<TUint> choices;
    choices.push_back(Product::kAutoPlayDisable);
    choices.push_back(Product::kAutoPlayEnable);
    iConfigAutoPlay = new ConfigChoice(*iConfigManager, Product::kConfigIdAutoPlay, choices, Product::kAutoPlayDisable);
    iProduct = new Av::Product(aDvStack.Env(), aDevice, *iKvpStore, iReadWriteStore, *iConfigManager, *iConfigManager, *iPowerManager);
    iFriendlyNameManager = new Av::FriendlyNameManager(*iProduct);
    iPipeline = new PipelineManager(aPipelineInitParams, aInfoAggregator, *iTrackFactory);
    iVolumeConfig = new VolumeConfig(*iConfigManager, aVolumeProfile);
    iVolumeManager = new Av::VolumeManager(aVolumeConsumer, iPipeline, *iVolumeConfig, aDevice, *iProduct, *iConfigManager, *iPowerManager);
    iCredentials = new Credentials(aDvStack.Env(), aDevice, aReadWriteStore, aEntropy, *iConfigManager, *iPowerManager);
    iProduct->AddAttribute("Credentials");
    iProviderTime = new ProviderTime(aDevice, *iPipeline);
    iProduct->AddAttribute("Time");
    iProviderInfo = new ProviderInfo(aDevice, *iPipeline);
    iProduct->AddAttribute("Info");
    iProviderConfig = new ProviderConfig(aDevice, *iConfigManager);
    iProviderTransport = new ProviderTransport(iDevice, *iPipeline, *iPowerManager, *iProduct, iTransportRepeatRandom);
    iProduct->AddAttribute("Transport");
    if (iProviderConfigApp != nullptr) {
        iProduct->AddAttribute("ConfigApp"); // iProviderConfigApp is instantiated before iProduct
                                             // so this attribute can't be added in the obvious location
    }
    iDebugManager = new DebugManager();

    if (false) {
        iTransportPins = new TransportPins(aDevice, aCpStack);
        iPodcastPins = new PodcastPins(aDevice, *iTrackFactory, aCpStack, iReadWriteStore);
        iDebugManager->Add(*iTransportPins);
        iDebugManager->Add(*iPodcastPins);
    }
}

MediaPlayer::~MediaPlayer()
{
    ASSERT(!iDevice.Enabled());
    delete iPipeline;
    delete iCredentials;
    /**
     * Circular dependency between ConfigStartupSource and Product on certain ConfigValues.
     * Force ConfigStartupSource to de-register its source name listeners.
     * Safe to do as WebAppFramework must already have been stopped.
     */
    delete iProduct;
    delete iFriendlyNameManager;    // All observers should have deregistered.
    delete iConfigStartupSource;
    delete iVolumeManager;
    delete iVolumeConfig;
    delete iProviderTransport;
    delete iProviderConfig;
    delete iProviderTime;
    delete iProviderInfo;
    delete iConfigAutoPlay;
    delete iConfigProductRoom;
    delete iConfigProductName;
    delete iThreadPool;
    delete iPowerManager;
    delete iProviderConfigApp;
    delete iConfigManager;
    delete iTrackFactory;
    delete iKvpStore;
    delete iLoggerBuffered;
    delete iUnixTimestamp;
    delete iDebugManager;
    delete iTransportPins;
    delete iPodcastPins;
}

void MediaPlayer::Quit()
{
    iProduct->Stop();
    iPipeline->Quit();
}

void MediaPlayer::Add(Codec::ContainerBase* aContainer)
{
    iPipeline->Add(aContainer);
}

void MediaPlayer::Add(Codec::CodecBase* aCodec)
{
    iPipeline->Add(aCodec);
}

void MediaPlayer::Add(Protocol* aProtocol)
{
    iPipeline->Add(aProtocol);
}

void MediaPlayer::Add(ISource* aSource)
{
    iProduct->AddSource(aSource);
}

void MediaPlayer::AddAttribute(const TChar* aAttribute)
{
    iProduct->AddAttribute(aAttribute);
}

ILoggerSerial& MediaPlayer::BufferLogOutput(TUint aBytes, IShell& aShell, Optional<ILogPoster> aLogPoster)
{
    iLoggerBuffered = new LoggerBuffered(aBytes, iDevice, *iProduct, aShell, aLogPoster, *iDebugManager);
    return iLoggerBuffered->LoggerSerial();
}

void MediaPlayer::Start(IRebootHandler& aRebootHandler)
{
    // All sources must have been added to Product by time this is called.
    // So, can now initialise startup source ConfigVal.
    iConfigStartupSource = new ConfigStartupSource(*iConfigManager);

    iConfigManager->Open();
    iPipeline->Start(*iVolumeManager, *iVolumeManager);
    iProviderTransport->Start();
    if (iProviderConfigApp != nullptr) {
        iProviderConfigApp->Attach(aRebootHandler);
    }
    iCredentials->Start();
    iMimeTypes.Start();
    iProduct->Start();
    iPowerManager->Start();
}

Environment& MediaPlayer::Env()
{
    return iDvStack.Env();
}

Net::DvStack& MediaPlayer::DvStack()
{
    return iDvStack;
}

Net::CpStack& MediaPlayer::CpStack()
{
    return iCpStack;
}

Net::DvDeviceStandard& MediaPlayer::Device()
{
    return iDevice;
}

Media::PipelineManager& MediaPlayer::Pipeline()
{
    return *iPipeline;
}

Media::TrackFactory& MediaPlayer::TrackFactory()
{
    return *iTrackFactory;
}

IReadStore& MediaPlayer::ReadStore()
{
    return *iKvpStore;
}

IStoreReadWrite& MediaPlayer::ReadWriteStore()
{
    return iReadWriteStore;
}

IConfigManager& MediaPlayer::ConfigManager()
{
    return *iConfigManager;
}

IConfigInitialiser& MediaPlayer::ConfigInitialiser()
{
    return *iConfigManager;
}

IPowerManager& MediaPlayer::PowerManager()
{
    return *iPowerManager;
}

IThreadPool& MediaPlayer::ThreadPool()
{
    return *iThreadPool;
}

Product& MediaPlayer::Product()
{
    return *iProduct;
}

IFriendlyNameObservable& MediaPlayer::FriendlyNameObservable()
{
    return *iFriendlyNameManager;
}

IVolumeManager& MediaPlayer::VolumeManager()
{
    ASSERT(iVolumeManager != nullptr);
    return *iVolumeManager;
}

Media::IMute& MediaPlayer::SystemMute()
{
    return *iPipeline;
}

Credentials& MediaPlayer::CredentialsManager()
{
    ASSERT(iCredentials != nullptr);
    return *iCredentials;
}

MimeTypeList& MediaPlayer::MimeTypes()
{
    return iMimeTypes;
}

void MediaPlayer::Add(UriProvider* aUriProvider)
{
    iPipeline->Add(aUriProvider);
}

RingBufferLogger* MediaPlayer::LogBuffer()
{
    return (iLoggerBuffered ? &iLoggerBuffered->LogBuffer() : nullptr);
}

IUnixTimestamp& MediaPlayer::UnixTimestamp()
{
    return *iUnixTimestamp;
}

ITransportRepeatRandom& MediaPlayer::TransportRepeatRandom()
{
    return iTransportRepeatRandom;
}

DebugManager& MediaPlayer::GetDebugManager()
{
    return *iDebugManager; 
}

TransportPins& MediaPlayer::GetTransportPins()
{
    return *iTransportPins; 
}

PodcastPins& MediaPlayer::GetPodcastPins()
{
    return *iPodcastPins; 
}

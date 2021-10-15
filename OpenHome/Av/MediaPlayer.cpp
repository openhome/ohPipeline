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
#include <OpenHome/Av/ProviderOAuth.h>
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
#include <OpenHome/Av/Pins/Pins.h>
#include <OpenHome/Av/Pins/ProviderPins.h>
#include <OpenHome/Av/Pins/TransportPins.h>
#include <OpenHome/SocketSsl.h>
#include <OpenHome/Av/DeviceAnnouncerMdns.h>

#include <memory>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Configuration;
using namespace OpenHome::Media;
using namespace OpenHome::Net;

// MediaPlayerInitParams

MediaPlayerInitParams* MediaPlayerInitParams::New(const Brx& aDefaultRoom, const Brx& aDefaultName, const Brx& aFriendlyNamePrefix)
{ // static
    return new MediaPlayerInitParams(aDefaultRoom, aDefaultName, aFriendlyNamePrefix);
}

MediaPlayerInitParams::MediaPlayerInitParams(const Brx& aDefaultRoom, const Brx& aDefaultName, const Brx& aFriendlyNamePrefix)
    : iFriendlyNamePrefix(aFriendlyNamePrefix)
    , iDefaultRoom(aDefaultRoom)
    , iDefaultName(aDefaultName)
    , iThreadPoolHigh(1)
    , iThreadPoolMedium(1)
    , iThreadPoolLow(1)
    , iConfigAppEnable(false)
    , iPinsEnable(false)
    , iMaxDevicePins(0)
    , iSsl(nullptr)
    , iConfigStartupMode(true)
    , iConfigAutoPlay(true)
{
}

void MediaPlayerInitParams::EnableConfigApp()
{
    iConfigAppEnable = true;
}

void MediaPlayerInitParams::EnablePins(TUint aMaxDevice)
{
    iPinsEnable = true;
    iMaxDevicePins = aMaxDevice;
}

void MediaPlayerInitParams::SetThreadPoolSize(TUint aCountHigh, TUint aCountMedium, TUint aCountLow)
{
    iThreadPoolHigh = aCountHigh;
    iThreadPoolMedium = aCountMedium;
    iThreadPoolLow = aCountLow;
}

void MediaPlayerInitParams::SetSsl(SslContext& aSsl)
{
    iSsl = &aSsl;
}

void MediaPlayerInitParams::EnableConfigStartupMode(TBool aEnable)
{
    iConfigStartupMode = aEnable;
}

void MediaPlayerInitParams::EnableConfigAutoPlay(TBool aEnable)
{
    iConfigAutoPlay = aEnable;
}

const Brx& MediaPlayerInitParams::FriendlyNamePrefix() const
{
    return iFriendlyNamePrefix;
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

TBool MediaPlayerInitParams::PinsEnabled(TUint& aMaxDevice) const
{
    aMaxDevice = iMaxDevicePins;
    return iPinsEnable;
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

SslContext* MediaPlayerInitParams::Ssl()
{
    return iSsl;
}

TBool MediaPlayerInitParams::ConfigStartupMode() const
{
    return iConfigStartupMode;
}

TBool MediaPlayerInitParams::ConfigAutoPlay() const
{
    return iConfigAutoPlay;
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
    , iPinsManager(nullptr)
    , iProviderPins(nullptr)
    , iTransportPins(nullptr)
    , iDeviceAnnouncerMdns(nullptr)
    , iRadioPresets(nullptr)
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
    Optional<IConfigInitialiser> configInit(aInitParams->ConfigStartupMode() ? iConfigManager : nullptr);
    iPowerManager = new OpenHome::PowerManager(configInit);
    iThreadPool = new OpenHome::ThreadPool(aInitParams->ThreadPoolCountHigh(),
                                           aInitParams->ThreadPoolCountMedium(),
                                           aInitParams->ThreadPoolCountLow());
    auto ssl = aInitParams->Ssl();
    if (ssl == nullptr) {
        iSsl = new SslContext();
        iOwnsSsl = true;
    }
    else {
        iSsl = ssl;
        iOwnsSsl = false;
    }
    iConfigProductRoom = new ConfigText(*iConfigManager, Product::kConfigIdRoomBase, Product::kMinRoomBytes, Product::kMaxRoomBytes, aInitParams->DefaultRoom());
    iConfigProductName = new ConfigText(*iConfigManager, Product::kConfigIdNameBase, Product::kMinNameBytes, Product::kMaxNameBytes, aInitParams->DefaultName());
    if (aInitParams->ConfigAutoPlay()) {
        std::vector<TUint> choices;
        choices.push_back(Product::kAutoPlayDisable);
        choices.push_back(Product::kAutoPlayEnable);
        iConfigAutoPlay = new ConfigChoice(*iConfigManager, Product::kConfigIdAutoPlay, choices, Product::kAutoPlayDisable);
    }
    iProduct = new Av::Product(aDvStack.Env(), aDevice, *iKvpStore, iReadWriteStore, *iConfigManager, *iConfigManager, *iPowerManager);
    iFriendlyNameManager = new Av::FriendlyNameManager(aInitParams->FriendlyNamePrefix(), *iProduct, *iThreadPool);
    iPipeline = new PipelineManager(aPipelineInitParams, aInfoAggregator, *iTrackFactory, *iThreadPool);
    iVolumeConfig = new VolumeConfig(aReadWriteStore, *iConfigManager, *iPowerManager, aVolumeProfile);
    iVolumeManager = new Av::VolumeManager(aVolumeConsumer, iPipeline, *iVolumeConfig, aDevice, *iProduct, *iConfigManager, *iPowerManager, aDvStack.Env());
    iCredentials = new Credentials(aDvStack.Env(), aDevice, aReadWriteStore, aEntropy, *iConfigManager, *iPowerManager);
    iProduct->AddAttribute("Credentials");
    iProviderOAuth = new ProviderOAuth(aDevice, aDvStack.Env(), *iThreadPool, *iCredentials, *iConfigManager, aReadWriteStore);
    iProduct->AddAttribute("OAuth");
    iProviderTime = new ProviderTime(aDevice, *iPipeline);
    iProduct->AddAttribute("Time");
    iProviderInfo = new ProviderInfo(aDevice, *iPipeline);
    iProduct->AddAttribute("Info");
    iProviderConfig = new ProviderConfig(aDevice, *iConfigManager);
    iProviderTransport = new ProviderTransport(aDvStack.Env(), iDevice, *iPipeline, *iPowerManager, *iProduct, iTransportRepeatRandom);
    iProduct->AddAttribute("Transport");
    if (iProviderConfigApp != nullptr) {
        iProduct->AddAttribute("ConfigApp"); // iProviderConfigApp is instantiated before iProduct
                                             // so this attribute can't be added in the obvious location
    }

    TUint maxDevicePins;
    if (aInitParams->PinsEnabled(maxDevicePins)) {
        iPinsManager = new PinsManager(aReadWriteStore, maxDevicePins);
        iProviderPins = new ProviderPins(aDevice, aDvStack.Env(), *iPinsManager);
        iProduct->AddAttribute("Pins");

        iTransportPins = new TransportPins(aDevice, aCpStack);
        iPinsManager->Add(iTransportPins);
    }

    if (aDvStack.Env().MdnsProvider() != nullptr) {
        iDeviceAnnouncerMdns = new DeviceAnnouncerMdns(aDvStack, aDevice, *iFriendlyNameManager);
    }
}

MediaPlayer::~MediaPlayer()
{
    ASSERT(!iDevice.Enabled());
    delete iPipeline;

    /* ProviderOAuth will observe changes in service's enabled state from
     * credentials service. Need to unsubscribe first before freeing credentials */
    delete iProviderOAuth;

    delete iCredentials;

    if (iDeviceAnnouncerMdns != nullptr) {
        delete iDeviceAnnouncerMdns;
    }
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
    delete iProviderPins;
    delete iPinsManager;
    if (iOwnsSsl) {
        delete iSsl;
    }
    delete iThreadPool;
    delete iPowerManager;
    delete iProviderConfigApp;
    delete iConfigManager;
    delete iTrackFactory;
    delete iKvpStore;
    delete iLoggerBuffered;
    delete iUnixTimestamp;
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

    // Only need startup source config value if we have a choice of sources
    if (iConfigStartupSource == nullptr && iProduct->SourceCount() > 1) {
        iConfigStartupSource = new ConfigStartupSource(*iConfigManager);
    }
}

void MediaPlayer::AddAttribute(const TChar* aAttribute)
{
    iProduct->AddAttribute(aAttribute);
}

ILoggerSerial& MediaPlayer::BufferLogOutput(TUint aBytes, IShell& aShell, Optional<ILogPoster> aLogPoster)
{
    iLoggerBuffered = new LoggerBuffered(aBytes, iDevice, *iProduct, aShell, aLogPoster);
    return iLoggerBuffered->LoggerSerial();
}

void MediaPlayer::Start(IRebootHandler& aRebootHandler)
{
    iConfigManager->Open();
    TUint pcm, dsd;
    iPipeline->GetMaxSupportedSampleRates(pcm, dsd);
    Bws<32> sampleRatesAttr;
    if (pcm > 0) {
        sampleRatesAttr.AppendPrintf("PcmMax=%u", pcm);
        iProduct->AddAttribute(sampleRatesAttr);
    }
    if (dsd > 0) {
        sampleRatesAttr.Replace(Brx::Empty());
        sampleRatesAttr.AppendPrintf("DsdMax=%u", dsd);
        iProduct->AddAttribute(sampleRatesAttr);
    }
    iPipeline->Start(*iVolumeManager, *iVolumeManager);
    iProviderTransport->Start();
    if (iProviderConfigApp != nullptr) {
        iProviderConfigApp->Attach(aRebootHandler);
    }
    if (iProviderPins != nullptr) {
        iProviderPins->Start();
    }
    iCredentials->Start();
    iMimeTypes.Start();
    iProduct->Start();
    iPowerManager->Start();
    iDvStack.Start();
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

ProviderOAuth& MediaPlayer::OAuthManager()
{
    return *iProviderOAuth;
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

SslContext& MediaPlayer::Ssl()
{
    return *iSsl;
}

void MediaPlayer::Add(UriProvider* aUriProvider)
{
    iPipeline->Add(aUriProvider);
}

IUnixTimestamp& MediaPlayer::UnixTimestamp()
{
    return *iUnixTimestamp;
}

ITransportRepeatRandom& MediaPlayer::TransportRepeatRandom()
{
    return iTransportRepeatRandom;
}

Optional<IPinsAccountStore> MediaPlayer::PinsAccountStore()
{
    return Optional<IPinsAccountStore>(iPinsManager);
}

Optional<IPinsInvocable> MediaPlayer::PinsInvocable()
{
    return Optional<IPinsInvocable>(iPinsManager);
}

Optional<IPinSetObservable> MediaPlayer::PinSetObservable()
{
    return Optional<IPinSetObservable>(iPinsManager);
}

Optional<IPinsManager> MediaPlayer::PinManager()
{
    return Optional<IPinsManager>(iPinsManager);
}

Optional<RingBufferLogger> MediaPlayer::LogBuffer()
{
    return iLoggerBuffered ? &iLoggerBuffered->LogBuffer() : nullptr;
}

Optional<IRadioPresets> MediaPlayer::RadioPresets()
{
    return Optional<IRadioPresets>(iRadioPresets);
}

void MediaPlayer::SetRadioPresets(IRadioPresets& aPresets)
{
    iRadioPresets = &aPresets;
}

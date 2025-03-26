#include "TestMediaPlayer.h"
#include <OpenHome/Types.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Av/Product.h>
#include <OpenHome/Net/Core/DvDevice.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Media/Pipeline/StarterTimed.h>
#include <OpenHome/Media/Utils/AllocatorInfoLogger.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Media/Codec/CodecFactory.h>
#include <OpenHome/Media/Codec/ContainerFactory.h>
#include <OpenHome/Media/Protocol/ProtocolFactory.h>
#include <OpenHome/Av/SourceFactory.h>
#include <OpenHome/Av/KvpStore.h>
#include "RamStore.h"
#include <OpenHome/Av/UpnpAv/UpnpAv.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Configuration/Tests/ConfigRamStore.h>
#include <OpenHome/Av/Utils/IconDriverSongcastSender.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Av/Debug.h>
#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Media/Pipeline/Pipeline.h>
#include <OpenHome/Web/WebAppFramework.h>
#include <OpenHome/Web/ConfigUi/ConfigUiMediaPlayer.h>
#include <OpenHome/Web/ConfigUi/FileResourceHandler.h>
#include <OpenHome/Av/UpnpAv/FriendlyNameUpnpAv.h>
#include <OpenHome/Net/Odp/DviServerOdp.h>
#include <OpenHome/Net/Odp/DviProtocolOdp.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Media/Codec/Mpeg4.h>

#include <vector>

#undef LPEC_ENABLE

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Av::Test;
using namespace OpenHome::Configuration;
using namespace OpenHome::Media;
using namespace OpenHome::Net;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Web;

// VolumeProfile

TUint VolumeProfile::VolumeMax() const
{
    return kVolumeMax;
}

TUint VolumeProfile::VolumeDefault() const
{
    return kVolumeDefault;
}

TUint VolumeProfile::VolumeUnity() const
{
    return kVolumeUnity;
}

TUint VolumeProfile::VolumeDefaultLimit() const
{
    return kVolumeDefaultLimit;
}

TUint VolumeProfile::VolumeStep() const
{
    return kVolumeStep;
}

TUint VolumeProfile::VolumeMilliDbPerStep() const
{
    return kVolumeMilliDbPerStep;
}

TUint VolumeProfile::ThreadPriority() const
{
    return kThreadPriority;
}

TUint VolumeProfile::BalanceMax() const
{
    return kBalanceMax;
}

TUint VolumeProfile::FadeMax() const
{
    return kFadeMax;
}

TUint VolumeProfile::OffsetMax() const
{
    return kOffsetMax;
}

TBool VolumeProfile::AlwaysOn() const
{
    return kAlwaysOn;
}

IVolumeProfile::StartupVolume VolumeProfile::StartupVolumeConfig() const
{
    return IVolumeProfile::StartupVolume::Both;
}


// VolumeSinkLogger

void VolumeSinkLogger::SetVolume(TUint aVolume)
{
    Log::Print("SetVolume: %u\n", aVolume);
}

void VolumeSinkLogger::SetBalance(TInt aBalance)
{
    Log::Print("SetBalance: %d\n", aBalance);
}

void VolumeSinkLogger::SetFade(TInt aFade)
{
    Log::Print("SetFade: %d\n", aFade);
}


// RebootLogger

void RebootLogger::Reboot(const Brx& aReason)
{
    Log::Print("\n\n\nRebootLogger::Reboot. Reason:\n%.*s\n\n\n", PBUF(aReason));
}


// DummyRaatSignalPath

void DummyRaatSignalPath::RegisterObserver(IRaatSignalPathObserver& aObserver)
{
    aObserver.SignalPathChanged(*this);
}



// TestMediaPlayer

const Brn TestMediaPlayer::kSongcastSenderIconFileName("SongcastSenderIcon");
const TUint TestMediaPlayer::kTrackCount;
const TUint TestMediaPlayer::kNumOdpSessions;
const TUint TestMediaPlayer::kMinWebUiResourceThreads;
const TUint TestMediaPlayer::kMaxWebUiTabs;
const TUint TestMediaPlayer::kUiSendQueueSize;
const TUint TestMediaPlayer::kUiMsgBufCount;
const TUint TestMediaPlayer::kUiMsgBufBytes;
const TUint TestMediaPlayer::kMaxPinsDevice;
const TUint TestMediaPlayer::kFsFlushFreqMs;
const TUint TestMediaPlayer::kDsdMaxSampleRate;
const TUint TestMediaPlayer::kDsdSampleBlockWords;
const TUint TestMediaPlayer::kDsdPadBytesPerChunk;

TestMediaPlayer::TestMediaPlayer(Net::DvStack& aDvStack, Net::CpStack& aCpStack, const Brx& aUdn, const TChar* aRoom, const TChar* aProductName,
                                 const Brx& aTuneInPartnerId, const Brx& aTidalId, const Brx& aQobuzIdSecret, const Brx& aUserAgent,
                                 const TChar* aStoreFile, TBool aEnableDash, TUint aOdpPort, TUint aWebUiPort,
                                 TUint aMinWebUiResourceThreads, TUint aMaxWebUiTabs, TUint aUiSendQueueSize,
                                 TUint aUiMsgBufCount, TUint aUiMsgBufBytes)
    : iPullableClock(nullptr)
    , iPlaylistLoader(nullptr)
    , iRaatSignalPathObservable(nullptr)
    , iSemShutdown("TMPS", 0)
    , iDisabled("test", 0)
    , iTuneInPartnerId(aTuneInPartnerId)
    , iTidalValues(aTidalId)
    , iQobuzIdSecret(aQobuzIdSecret)
    , iUserAgent(aUserAgent)
    , iTxTimestamper(nullptr)
    , iRxTimestamper(nullptr)
    , iStoreFileWriter(nullptr)
    , iEnableDash(aEnableDash)
    , iMpegDRMProvider(nullptr)
    , iOdpPort(aOdpPort)
    , iOdpZeroConf(nullptr)
    , iServerOdp(nullptr)
    , iMinWebUiResourceThreads(aMinWebUiResourceThreads)
    , iMaxWebUiTabs(aMaxWebUiTabs)
    , iUiSendQueueSize(aUiSendQueueSize)
    , iUiMsgBufCount(aUiMsgBufCount)
    , iUiMsgBufBytes(aUiMsgBufBytes)
{
    Log::Print("Shell running on port %u\n", aDvStack.Env().Shell()->Port());
    iInfoLogger = new Media::AllocatorInfoLogger();

    // Do NOT set UPnP friendly name attributes at this stage.
    // (Wait until MediaPlayer is created so that friendly name can be observed.)

    aDvStack.AddProtocolFactory(new DviProtocolFactoryOdp());

    // Create UPnP device.
    // Friendly name not set here.
    iDevice = new DvDeviceStandard(aDvStack, aUdn, *this);
    iDevice->SetAttribute("Upnp.Domain", "av.openhome.org");
    iDevice->SetAttribute("Upnp.Type", "Source");
    iDevice->SetAttribute("Upnp.Version", "1");
    iDevice->SetAttribute("Upnp.Manufacturer", "OpenHome");
    iDevice->SetAttribute("Upnp.ModelName", "TestMediaPlayer");
#ifdef LPEC_ENABLE
    iDevice->SetAttribute("Lpec.Name", "ohPipeline");
#endif
    iDevice->SetAttribute("Odp.Name", "Ds");

    // Create separate UPnP device for standard MediaRenderer.
    Bws<256> buf(aUdn);
    buf.Append("-MediaRenderer");
    iDeviceUpnpAv = new DvDeviceStandard(aDvStack, buf);
    // Friendly name not set here.
    iDeviceUpnpAv->SetAttribute("Upnp.Domain", "upnp.org");
    iDeviceUpnpAv->SetAttribute("Upnp.Type", "MediaRenderer");
    iDeviceUpnpAv->SetAttribute("Upnp.Version", "1");
    iDeviceUpnpAv->SetAttribute("Upnp.Manufacturer", "OpenHome");
    iDeviceUpnpAv->SetAttribute("Upnp.ModelName", "TestMediaPlayer");
#ifdef LPEC_ENABLE
    iDeviceUpnpAv->SetAttribute("Lpec.Name", "MediaRenderer");
#endif
    iDeviceUpnpAv->SetAttribute("Odp.Name", "MediaRenderer");

    // create read/write store.  This creates a number of static (constant) entries automatically
    iRamStore = new RamStore(kSongcastSenderIconFileName);

    // create a read/write store using the new config framework
    iConfigRamStore = new ConfigRamStore();
    if (Brn(aStoreFile).Bytes() > 0) {
        StoreFileReaderJson storeFileReader(aStoreFile);
        storeFileReader.Read(*iConfigRamStore);

        iStoreFileWriter = new StoreFileWriterJson(aStoreFile);
        iConfigRamStore->AddStoreObserver(*iStoreFileWriter);
    }
    else {
        Log::Print("No store file parameter specified - will not attempt to load store values from file, and changes to store values will not be persisted.\n");
    }

    VolumeProfile volumeProfile;
    VolumeConsumer volumeInit;
    volumeInit.SetVolume(iVolumeLogger);
    volumeInit.SetBalance(iVolumeLogger);
    volumeInit.SetFade(iVolumeLogger);

    // Create MediaPlayer.
    // NOTE: If values for Room.Name and Product.Name already exist in the Store,
    //       the aRoom and aProductName default values will be ignored.
    auto pipelineInit = PipelineInitParams::New();
    pipelineInit->SetStarvationRamperMinSize(100 * Jiffies::kPerMs); // larger StarvationRamper size useful for desktop
                                                                     // platforms with slightly unpredictable thread scheduling
    pipelineInit->SetGorgerDuration(pipelineInit->DecodedReservoirJiffies());
    pipelineInit->SetDsdMaxSampleRate(kDsdMaxSampleRate);
    pipelineInit->SetSupportElements(Media::EPipelineSupportElementsValidatorMinimal | Media::EPipelineSupportElementsDecodedAudioValidator | Media::EPipelineSupportElementsRampValidator);
    const Brn kFriendlyNamePrefix("OpenHome ");
    iAudioTime = new AudioTimeCpu(aDvStack.Env());
    auto mpInit = MediaPlayerInitParams::New(Brn(aRoom), Brn(aProductName), kFriendlyNamePrefix);
    mpInit->EnableConfigApp();
    mpInit->EnablePins(kMaxPinsDevice);
    iMediaPlayer = new MediaPlayer(aDvStack, aCpStack, *iDevice, *iRamStore,
                                   *iConfigRamStore, pipelineInit, *iAudioTime,
                                   volumeInit, volumeProfile,
                                   *iInfoLogger,
                                   aUdn, mpInit);
    delete mpInit;
    iPipelineObserver = new LoggingPipelineObserver();
    iMediaPlayer->Pipeline().AddObserver(*iPipelineObserver);

    iFnUpdaterStandard = new FriendlyNameAttributeUpdater(iMediaPlayer->FriendlyNameObservable(), iMediaPlayer->ThreadPool(), *iDevice);
    iFnManagerUpnpAv = new FriendlyNameManagerUpnpAv(kFriendlyNamePrefix, iMediaPlayer->Product());
    iFnUpdaterUpnpAv = new FriendlyNameAttributeUpdater(*iFnManagerUpnpAv, iMediaPlayer->ThreadPool(), *iDeviceUpnpAv);
    iFsFlushPeriodic = new FsFlushPeriodic(iMediaPlayer->Env(), iMediaPlayer->PowerManager(), iMediaPlayer->ThreadPool(), kFsFlushFreqMs);

    // Register with the PowerManager
    IPowerManager& powerManager = iMediaPlayer->PowerManager();
    iPowerObserver = powerManager.RegisterPowerHandler(*this, kPowerPriorityLowest, "TestMediaPlayer");

    // Set up config app.
    WebAppFrameworkInitParams* initParams = new WebAppFrameworkInitParams();
    initParams->SetServerPort(aWebUiPort);
    initParams->SetMinServerThreadsResources(aMinWebUiResourceThreads);
    initParams->SetMaxServerThreadsLongPoll(aMaxWebUiTabs);
    initParams->SetSendQueueSize(aUiSendQueueSize);
    iAppFramework = new WebAppFramework(aDvStack.Env(), initParams, iMediaPlayer->ThreadPool());
}

TestMediaPlayer::~TestMediaPlayer()
{
    delete iAppFramework;
    delete iPowerObserver;
    ASSERT(!iDevice->Enabled());
    delete iServerOdp;
    delete iOdpZeroConf;
    delete iFnUpdaterStandard;
    delete iFnUpdaterUpnpAv;
    delete iFnManagerUpnpAv;
    delete iFsFlushPeriodic;
    delete iMediaPlayer;
    delete iPipelineObserver;
    delete iInfoLogger;
    delete iAudioTime;
    delete iDevice;
    delete iDeviceUpnpAv;
    delete iRaatSignalPathObservable;
    delete iRamStore;
    if (iStoreFileWriter != nullptr) {
        // Store writer will not have been created if store file param not specified.
        iConfigRamStore->RemoveStoreObserver(*iStoreFileWriter);
        delete iStoreFileWriter;
    }
    delete iConfigRamStore;
}

void TestMediaPlayer::SetPullableClock(Media::IPullableClock& aPullableClock)
{
    iPullableClock = &aPullableClock;
}

void TestMediaPlayer::SetSongcastTimestampers(IOhmTimestamper& aTxTimestamper, IOhmTimestamper& aRxTimestamper)
{
    iTxTimestamper = &aTxTimestamper;
    iRxTimestamper = &aRxTimestamper;
}

void TestMediaPlayer::StopPipeline()
{
    TUint waitCount = 0;
    if (TryDisable(*iDevice)) {
        waitCount++;
    }
    if (TryDisable(*iDeviceUpnpAv)) {
        waitCount++;
    }
    while (waitCount > 0) {
        iDisabled.Wait();
        waitCount--;
    }
    iMediaPlayer->Quit();
    iSemShutdown.Signal();
}

void TestMediaPlayer::AddAttribute(const TChar* aAttribute)
{
    iMediaPlayer->AddAttribute(aAttribute);
}

void TestMediaPlayer::Run()
{
    RegisterPlugins(iMediaPlayer->Env());
    AddConfigApp();

    InitialiseLogger();
    iMediaPlayer->Start(iRebootHandler);
    InitialiseSubsystems();

    // Debugging for ConfigManager.
    IConfigManager& configManager = iMediaPlayer->ConfigManager();
    configManager.Print();
    configManager.DumpToStore();

    iAppFramework->Start();

    iServerOdp = new DviServerOdp(iMediaPlayer->DvStack(), kNumOdpSessions, iOdpPort);
    iServerOdp->Start();
    Log::Print("ODP server running on port %u\n", iServerOdp->Port()); // don't use iOdpPort here - if it is 0, iServerOdp->Port() tells us the host assigned port
    iOdpZeroConf = new OdpZeroConf(iMediaPlayer->Env(), *iServerOdp, iMediaPlayer->FriendlyNameObservable());
    iOdpZeroConf->SetZeroConfEnabled(true);

    iMediaPlayer->PowerManager().StandbyDisable(StandbyDisableReason::Boot);
    EnableDevices();
    iFsFlushPeriodic->Start();

    StorePrinter storePrinter(*iConfigRamStore);
    storePrinter.Print();

    Log::Print("\nFull (software) media player\n");
    Log::Print("Intended to be controlled via a separate, standard CP (Kazoo etc.)\n");

    Log::Print("Press <q> followed by <enter> to quit:\n");
    Log::Print("\n");
    while (getchar() != 'q')    // getchar catches stdin, getch does not.....
        ;

    //IPowerManager& powerManager = iMediaPlayer->PowerManager();
    //powerManager.PowerDown(); // FIXME - this should probably be replaced by a normal shutdown procedure
    storePrinter.Print();
}

void TestMediaPlayer::RunWithSemaphore()
{
    RegisterPlugins(iMediaPlayer->Env());
    AddConfigApp();
    iMediaPlayer->Start(iRebootHandler);
    InitialiseSubsystems();

    // Debugging for ConfigManager.
    IConfigManager& configManager = iMediaPlayer->ConfigManager();
    configManager.Print();
    configManager.DumpToStore();

    iAppFramework->Start();
    EnableDevices();

    StorePrinter storePrinter(*iConfigRamStore);
    storePrinter.Print();

    iSemShutdown.Wait();    // FIXME - can Run() and RunWithSemaphore() be refactored out? only difference is how they wait for termination signal

    //IPowerManager& powerManager = iMediaPlayer->PowerManager();
    //powerManager.PowerDown(); // FIXME - this should probably be replaced by a normal shutdown procedure
    storePrinter.Print();
}

void TestMediaPlayer::EnableMpegDRM(Codec::IMpegDRMProvider* aDRMProvider)
{
    iMpegDRMProvider = aDRMProvider;
}

PipelineManager& TestMediaPlayer::Pipeline()
{
    return iMediaPlayer->Pipeline();
}

DvDeviceStandard* TestMediaPlayer::Device()
{
    return iDevice;
}

TUint TestMediaPlayer::DsdMaxSampleRate() const
{
    return kDsdMaxSampleRate;
}

TUint TestMediaPlayer::DsdSampleBlockWords() const
{
    return kDsdSampleBlockWords;
}

TUint TestMediaPlayer::DsdPadBytesPerChunk() const
{
    return kDsdPadBytesPerChunk;
}

void TestMediaPlayer::TryRegisterVorbis()
{
    iMediaPlayer->Add(Codec::CodecFactory::NewVorbis(iMediaPlayer->MimeTypes()));
}

void TestMediaPlayer::RegisterPlugins(Environment& aEnv)
{
    Optional<Codec::IMpegDRMProvider> mpegDRMProvider = iMpegDRMProvider;

    // Add containers
    iMediaPlayer->Add(Codec::ContainerFactory::NewId3v2());
    iMediaPlayer->Add(Codec::ContainerFactory::NewMpeg4(iMediaPlayer->MimeTypes(), mpegDRMProvider));
    iMediaPlayer->Add(Codec::ContainerFactory::NewMpegTs(iMediaPlayer->MimeTypes()));

    // Add codecs
    iMediaPlayer->Add(Codec::CodecFactory::NewFlac(iMediaPlayer->MimeTypes()));
    iMediaPlayer->Add(Codec::CodecFactory::NewWav(iMediaPlayer->MimeTypes()));
    iMediaPlayer->Add(Codec::CodecFactory::NewAiff(iMediaPlayer->MimeTypes()));
    iMediaPlayer->Add(Codec::CodecFactory::NewAifc(iMediaPlayer->MimeTypes()));
    iMediaPlayer->Add(Codec::CodecFactory::NewAacFdkMp4(iMediaPlayer->MimeTypes()));
    iMediaPlayer->Add(Codec::CodecFactory::NewAacFdkAdts(iMediaPlayer->MimeTypes()));
    iMediaPlayer->Add(Codec::CodecFactory::NewAlacApple(iMediaPlayer->MimeTypes()));
    iMediaPlayer->Add(Codec::CodecFactory::NewDsdDsf(iMediaPlayer->MimeTypes(), kDsdSampleBlockWords, kDsdPadBytesPerChunk));
    iMediaPlayer->Add(Codec::CodecFactory::NewDsdDff(iMediaPlayer->MimeTypes(), kDsdSampleBlockWords, kDsdPadBytesPerChunk));
    iMediaPlayer->Add(Codec::CodecFactory::NewPcm());
    iMediaPlayer->Add(Codec::CodecFactory::NewDsdRaw(kDsdSampleBlockWords, kDsdPadBytesPerChunk));
    iMediaPlayer->Add(Codec::CodecFactory::NewOpus(iMediaPlayer->MimeTypes()));

    TryRegisterVorbis();
    // RAOP source must be added towards end of source list.
    // However, must add RAOP codec before MP3 codec to avoid false-positives.
    iMediaPlayer->Add(Codec::CodecFactory::NewRaop());
    // Add MP3 codec last, as it can cause false-positives (with RAOP in particular).
    iMediaPlayer->Add(Codec::CodecFactory::NewMp3(iMediaPlayer->MimeTypes()));
    // iMediaPlayer->Add(Codec::CodecFactory::NewDsdDff(iMediaPlayer->MimeTypes())); This line was included when modification began, but is defined above.

    // Add protocol modules (Radio source can require several stacked Http instances)
    auto& ssl = iMediaPlayer->Ssl();
    static const TUint kNumHttpProtocols = 5;
    for (TUint i=0; i<kNumHttpProtocols; i++) {
        iMediaPlayer->Add(ProtocolFactory::NewHttp(aEnv, ssl, iUserAgent));
    }
    iMediaPlayer->Add(ProtocolFactory::NewHls(aEnv, ssl, iUserAgent));

    if (iEnableDash) {
        Log::Print("!! MPEG Dash Support Enabled !!\n");
        iMediaPlayer->Add(ProtocolFactory::NewDash(aEnv, ssl, *iMediaPlayer));
    }

    // only add Tidal if we have a token to use with login
    if (iTidalValues.Bytes() > 0) {
        Parser p(iTidalValues);
        Brn clientId(p.Next(':'));
        Brn clientSecret(p.Next(':'));
        std::vector<OAuthAppDetails> apps;

        while(!p.Finished())
        {
            Brn appId = p.Next(':');
            Brn appClientId = p.Next(':');
            Brn appClientSecret = p.Next(':');

            apps.push_back(OAuthAppDetails(appId, appClientId, appClientSecret));
        }

        Log::Print("TIDAL: clientId = %.*s, clientSecret = %.*s\n", PBUF(clientId), PBUF(clientSecret));
        for(const auto& v : apps)
        {
            Log::Print("    App: ID: %.*s - ClientId = %.*s, Secret = %.*s\n", PBUF(v.AppId()), PBUF(v.ClientId()), PBUF(v.ClientSecret()));
        }

        iMediaPlayer->Add(ProtocolFactory::NewTidal(aEnv, ssl, clientId, clientSecret, apps, *iMediaPlayer));
    }
    // ...likewise, only add Qobuz if we have ids for login
    if (iQobuzIdSecret.Bytes() > 0) {
        Parser p(iQobuzIdSecret);
        Brn appId(p.Next(':'));
        Brn appSecret(p.Remaining());
        Log::Print("Qobuz: appId = ");
        Log::Print(appId);
        Log::Print(", appSecret = ");
        Log::Print(appSecret);
        Log::Print("\n");
        iMediaPlayer->Add(ProtocolFactory::NewQobuz(appId, appSecret, *iMediaPlayer, iUserAgent));
    }
    iMediaPlayer->Add(ProtocolFactory::NewCalmRadio(aEnv, ssl, iUserAgent, *iMediaPlayer));

    // Add sources
    iMediaPlayer->Add(SourceFactory::NewPlaylist(*iMediaPlayer, Optional<IPlaylistLoader>(iPlaylistLoader)));
    if (iTuneInPartnerId.Bytes() == 0) {
        iMediaPlayer->Add(SourceFactory::NewRadio(*iMediaPlayer));
    }
    else {
        iMediaPlayer->Add(SourceFactory::NewRadio(*iMediaPlayer, iTuneInPartnerId));
    }

    iMediaPlayer->Add(SourceFactory::NewUpnpAv(*iMediaPlayer, *iDeviceUpnpAv));

    Bwh hostName(iDevice->Udn().Bytes()+1); // space for null terminator
    hostName.Replace(iDevice->Udn());
    Bws<12> macAddr;
    MacAddrFromUdn(aEnv, macAddr);

    TUint priorityFiller = 0;
    TUint priorityFlywheelRamper = 0;
    TUint priorityStarvationRamper = 0;
    TUint priorityCodec = 0;
    TUint priorityEvent = 0;
    iMediaPlayer->Pipeline().GetThreadPriorities(priorityFiller, priorityFlywheelRamper, priorityStarvationRamper, priorityCodec, priorityEvent);
    const TUint raopServerPriority = priorityFiller;
    iMediaPlayer->Add(SourceFactory::NewRaop(*iMediaPlayer, Optional<IClockPuller>(nullptr), macAddr, raopServerPriority, *iMediaPlayer->Env().MdnsProvider()));

    iMediaPlayer->Add(SourceFactory::NewReceiver(*iMediaPlayer,
                                                 Optional<IClockPuller>(nullptr),
                                                 Optional<IOhmTimestamper>(iTxTimestamper),
                                                 Optional<IOhmTimestamper>(iRxTimestamper),
                                                 Optional<IOhmMsgProcessor>()));

#ifdef RAAT_ENABLE
    ASSERT(iPullableClock != nullptr);
    if (iRaatSignalPathObservable == nullptr) {
        iRaatSignalPathObservable = new DummyRaatSignalPath();
    }
    iMediaPlayer->Add(SourceFactory::NewRoon(
        *iMediaPlayer,
        *iAudioTime,
        *iPullableClock,
        *iRaatSignalPathObservable,
        Brn("12345"),
        Brn("0.0.1"),
        Brn("http://linn.co.uk/account")));
#else
    iMediaPlayer->Add(SourceFactory::NewScd(*iMediaPlayer, nullptr));
#endif
}

void TestMediaPlayer::InitialiseSubsystems()
{
}

IWebApp* TestMediaPlayer::CreateConfigApp(const std::vector<const Brx*>& aSources, const Brx& aResourceDir, TUint aMinWebUiResourceThreads, TUint aMaxWebUiTabs, TUint aMaxSendQueueSize, TUint aMsgBufCount, TUint aMsgBufBytes)
{
    FileResourceHandlerFactory resourceHandlerFactory;
    return new ConfigAppMediaPlayer(*iInfoLogger, iMediaPlayer->Env(), iMediaPlayer->Product(),
                                    iMediaPlayer->ConfigManager(), resourceHandlerFactory, aSources,
                                    Brn("Softplayer"), aResourceDir,
                                    aMinWebUiResourceThreads, aMaxWebUiTabs, aMaxSendQueueSize,
                                    aMsgBufCount, aMsgBufBytes, iRebootHandler);
}

void TestMediaPlayer::InitialiseLogger()
{
    (void)iMediaPlayer->BufferLogOutput(128 * 1024, *(iMediaPlayer->Env().Shell()), Optional<ILogPoster>(nullptr));
}

void TestMediaPlayer::EnableDevices()
{
    iDevice->SetEnabled();
    iDeviceUpnpAv->SetEnabled();
}

void TestMediaPlayer::DestroyAppFramework()
{
    delete iAppFramework;
    iAppFramework = nullptr;
}

void TestMediaPlayer::WriteResource(const Brx& aUriTail, const TIpAddress& /*aInterface*/, std::vector<char*>& /*aLanguageList*/, IResourceWriter& aResourceWriter)
{
    if (aUriTail == kSongcastSenderIconFileName) {
        aResourceWriter.WriteResourceBegin(sizeof(kIconDriverSongcastSender), kIconDriverSongcastSenderMimeType);
        aResourceWriter.WriteResource(kIconDriverSongcastSender, sizeof(kIconDriverSongcastSender));
        aResourceWriter.WriteResourceEnd();
    }
}

void TestMediaPlayer::PowerUp()
{
    // FIXME - enable UPnP devices here?
    // - would need to account for two-stage create->run process either by
    //  - setting a flag here which is checked in Run() OR
    //  - registering with IPowerManager in Run() call
    //iDevice->SetEnabled();
    //iDeviceUpnpAv->SetEnabled();
}

void TestMediaPlayer::PowerDown()
{
    Log::Print("TestMediaPlayer::PowerDown\n");
    PowerDownDisable(*iDevice);
    PowerDownDisable(*iDeviceUpnpAv);
}

//void TestMediaPlayer::Add(IWebApp* aWebApp, FunctorPresentationUrl aFunctor)
//{
//    // Last added WebApp will be set as presentation page.
//    iAppFramework->Add(aWebApp, aFunctor);
//}

void TestMediaPlayer::AddConfigApp()
{
    std::vector<const Brx*> sourcesBufs;
    Product& product = iMediaPlayer->Product();
    for (TUint i=0; i<product.SourceCount(); i++) {
        Bws<ISource::kMaxSystemNameBytes> systemName;
        Bws<ISource::kMaxSourceNameBytes> name;
        Bws<ISource::kMaxSourceTypeBytes> type;
        TBool visible;
        product.GetSourceDetails(i, systemName, type, name, visible);
        sourcesBufs.push_back(new Brh(systemName));
    }
    // FIXME - take resource dir as param or copy res dir to build dir
    auto configUi = CreateConfigApp(sourcesBufs, Brn("res/"), iMinWebUiResourceThreads, iMaxWebUiTabs, iUiSendQueueSize, iUiMsgBufCount, iUiMsgBufBytes);
    iAppFramework->Add(configUi, MakeFunctorGeneric(*this, &TestMediaPlayer::PresentationUrlChanged));
    iAppFramework->SetDefaultApp(configUi->ResourcePrefix());
    for (TUint i=0;i<sourcesBufs.size(); i++) {
        delete sourcesBufs[i];
    }
}

TUint TestMediaPlayer::Hash(const Brx& aBuf)
{
    TUint hash = 0;
    for (TUint i=0; i<aBuf.Bytes(); i++) {
        hash += aBuf[i];
    }
    return hash;
}

void TestMediaPlayer::GenerateMacAddr(Environment& aEnv, TUint aSeed, Bwx& aMacAddr)
{
    // Generate a 48-bit, 12-byte hex string.
    // Method:
    // - Generate two random numbers in the range 0 - 2^24
    // - Get the hex representation of these numbers
    // - Combine the two hex representations into the output buffer, aMacAddr
    const TUint maxLimit = 0x01000000;
    Bws<8> macBuf1;
    Bws<8> macBuf2;

    aEnv.SetRandomSeed(aSeed);
    TUint mac1 = aEnv.Random(maxLimit, 0);
    TUint mac2 = aEnv.Random(maxLimit, 0);

    Ascii::AppendHex(macBuf1, mac1);
    Ascii::AppendHex(macBuf2, mac2);

    aMacAddr.Append(macBuf1.Split(2));
    aMacAddr.Append(macBuf2.Split(2));
}

void TestMediaPlayer::MacAddrFromUdn(Environment& aEnv, Bwx& aMacAddr)
{
    TUint hash = Hash(iDevice->Udn());
    GenerateMacAddr(aEnv, hash, aMacAddr);
}

void TestMediaPlayer::PresentationUrlChanged(const Brx& aUrl)
{
    iPresentationUrl.Replace(aUrl);
    iDevice->SetAttribute("Upnp.PresentationUrl", iPresentationUrl.PtrZ());
    iMediaPlayer->Product().SetConfigAppUrl(iPresentationUrl);
}

void TestMediaPlayer::PowerDownDisable(DvDevice& aDevice)
{
    if (aDevice.Enabled()) {
        aDevice.SetDisabled(MakeFunctor(*this, &TestMediaPlayer::PowerDownUpnpCallback));
    }
}

void TestMediaPlayer::PowerDownUpnpCallback()
{
    // do nothing; only exists to avoid lengthy Upnp shutdown waits during power fail
}

TBool TestMediaPlayer::TryDisable(DvDevice& aDevice)
{
    if (aDevice.Enabled()) {
        aDevice.SetDisabled(MakeFunctor(*this, &TestMediaPlayer::Disabled));
        return true;
    }
    return false;
}

void TestMediaPlayer::Disabled()
{
    iDisabled.Signal();
}


// TestMediaPlayerInit

OpenHome::Net::Library* TestMediaPlayerInit::CreateLibrary(
    const TChar* aRoom,
    TBool aLoopback,
    TUint aAdapter,
    TUint aShellPort
)
{
    InitialisationParams* initParams = InitialisationParams::Create();
    initParams->SetDvEnableBonjour(aRoom, true);
    if (aLoopback == true) {
        initParams->SetUseLoopbackNetworkAdapter();
    }
    initParams->SetEnableShell(aShellPort);
    initParams->SetIPv6Supported(false);
#ifdef LPEC_ENABLE
    initParams->SetDvNumLpecThreads(4);
    initParams->SetDvLpecServerPort(2324);
#endif

    Debug::SetLevel(Debug::kPipeline);
    Debug::AddLevel(Debug::kSources);
    Debug::AddLevel(Debug::kMedia);
    Debug::AddLevel(Debug::kRaat);
    Debug::AddLevel(Debug::kAdapterChange);
    //Debug::AddLevel(Debug::kSongcast);
    Debug::AddLevel(Debug::kOAuth);
    Debug::SetSeverity(Debug::kSeverityInfo);
    Net::Library* lib = new Net::Library(initParams);
    //Net::DvStack* dvStack = lib->StartDv();
    std::vector<NetworkAdapter*>* subnetList = lib->CreateSubnetList();
    const TUint adapterIndex = aAdapter;
    if (subnetList->size() <= adapterIndex) {
        Log::Print("ERROR: adapter %u doesn't exist\n", adapterIndex);
        ASSERTS();
    }
    Log::Print ("adapter list:\n");
    for (unsigned i=0; i<subnetList->size(); ++i) {
        TIpAddress addr = (*subnetList)[i]->Address();
        Bws<TIpAddressUtils::kMaxAddressBytes> addressBuf;
        TIpAddressUtils::ToString(addr, addressBuf);
        Log::Print ("  %d: %.*s\n", i, PBUF(addressBuf));
    }
    //TIpAddress address = (*subnetList)[adapterIndex]->Address();
    TIpAddress subnet = (*subnetList)[adapterIndex]->Subnet();
    Library::DestroySubnetList(subnetList);
    lib->SetCurrentSubnet(subnet);

    Bws<TIpAddressUtils::kMaxAddressBytes> addressBuf;
    TIpAddressUtils::ToString(subnet, addressBuf);
    Log::Print("using subnet %.*s\n", PBUF(addressBuf));
    return lib;
}

void TestMediaPlayerInit::SeedRandomNumberGenerator(Environment& aEnv, const Brx& aRoom, TIpAddress aAddress, DviServerUpnp& aServer)
{
    if (aRoom == Brx::Empty()) {
        Log::Print("ERROR: room must be set\n");
        ASSERTS();
    }
    // Re-seed random number generator with hash of (unique) room name + UPnP
    // device server port to avoid UDN clashes.
    TUint port = aServer.Port(aAddress);
    Log::Print("UPnP DV server using port: %u\n", port);
    TUint hash = 0;
    for (TUint i=0; i<aRoom.Bytes(); i++) {
        hash += aRoom[i];
    }
    hash += port;
    Log::Print("Seeding random number generator with: %u\n", hash);
    aEnv.SetRandomSeed(hash);
}

void TestMediaPlayerInit::AppendUniqueId(Environment& aEnv, const Brx& aUserUdn, const Brx& aDefaultUdn, Bwh& aOutput)
{
    if (aUserUdn.Bytes() == 0) {
        if (aOutput.MaxBytes() < aDefaultUdn.Bytes()) {
            aOutput.Grow(aDefaultUdn.Bytes());
        }
        aOutput.Replace(aDefaultUdn);
        RandomiseUdn(aEnv, aOutput);
    }
    else {
        if (aUserUdn.Bytes() > aOutput.MaxBytes()) {
            aOutput.Grow(aUserUdn.Bytes());
        }
        aOutput.Replace(aUserUdn);
    }
}

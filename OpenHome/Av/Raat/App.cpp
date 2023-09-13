#include <OpenHome/Av/Raat/App.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Timer.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Av/SourceFactory.h>
#include <OpenHome/Av/VolumeManager.h>
#include <OpenHome/Av/Raat/Output.h>
#include <OpenHome/Av/Raat/Volume.h>
#include <OpenHome/Av/Raat/SourceSelection.h>
#include <OpenHome/Av/Raat/Transport.h>
#include <OpenHome/Media/Debug.h>

#include <uv.h>
#include <raat_device.h> 
#include <raat_info.h> 
#include <rc_guid.h>
#include <raat_base.h>

extern "C"
void raat_thread(void* arg)
{
    reinterpret_cast<OpenHome::Av::RaatApp*>(arg)->RaatThread();
}

using namespace OpenHome;
using namespace OpenHome::Av;

RaatApp::RaatApp(
    Environment& aEnv,
    IMediaPlayer& aMediaPlayer,
    ISourceRaat& aSourceRaat,
    Media::IAudioTime& aAudioTime,
    Media::IPullableClock& aPullableClock,
    IRaatSignalPathObservable& aSignalPathObservable,
    const Brx& aSerialNumber,
    const Brx& aSoftwareVersion)
    : iMediaPlayer(aMediaPlayer)
    , iDevice(nullptr)
    , iInfo(nullptr)
    , iSerialNumber(aSerialNumber)
    , iSoftwareVersion(aSoftwareVersion)
{
    iTimer = new Timer(aEnv, MakeFunctor(*this, &RaatApp::StartPlugins), "RaatApp");
    iOutput = new RaatOutput(aMediaPlayer, aSourceRaat, aAudioTime, aPullableClock, aSignalPathObservable);
    if (aMediaPlayer.ConfigManager().HasNum(VolumeConfig::kKeyLimit)) {
        iVolume = RaatVolume::New(aMediaPlayer);
    }
    else {
        iVolume = nullptr;
    }
    iTransport = new RaatTransport(aMediaPlayer, *iOutput);
    iSourceSelection = new RaatSourceSelection(aMediaPlayer, SourceFactory::kSourceNameRaat, *iTransport);
    int err = uv_thread_create(&iThread, raat_thread, this);
    ASSERT(err == 0);
}

RaatApp::~RaatApp()
{
    if (iDevice != nullptr) {
        RAAT__device_stop(iDevice);
    }
    delete iTimer;
    (void)uv_thread_join(&iThread);
    RAAT__device_delete(iDevice);
    delete iSourceSelection;
    delete iTransport;
    delete iVolume;
    delete iOutput;
}

IRaatReader& RaatApp::Reader()
{
    ASSERT(iOutput != nullptr);
    return *iOutput;
}

IRaatTransport& RaatApp::Transport()
{
    ASSERT(iTransport != nullptr);
    return *iTransport;
}

static void Raat_Log(RAAT__LogEntry* entry, void* /*userdata*/) {
    LOG(kMedia, "RAAT: [%07d] %lld %s\n", entry->seq, entry->time, entry->message);
}

static void SetInfo(RAAT__Info* aInfo, const char* aKey, const Brx& aValue)
{
    Bws<RAAT__INFO_MAX_VALUE_LEN> val(aValue);
    auto status = RAAT__info_set(aInfo, aKey, val.PtrZ());
    ASSERT(RC__STATUS_IS_SUCCESS(status));
}

void RaatApp::RaatThread()
{
    RAAT__static_init();
    RAAT__Log    *log;
    auto status = RAAT__log_new(RC__ALLOCATOR_DEFAULT, RAAT__LOG_DEFAULT_SIZE, &log);
    ASSERT(RC__STATUS_IS_SUCCESS(status));
    RAAT__log_add_callback(log, Raat_Log, NULL);

    status = RAAT__device_new(RC__ALLOCATOR_DEFAULT, log, &iDevice);
    ASSERT(RC__STATUS_IS_SUCCESS(status));

    iInfo = RAAT__device_get_info(iDevice);

    SetInfo(iInfo, RAAT__INFO_KEY_UNIQUE_ID, iMediaPlayer.Device().Udn());
    Brn name;
    Brn info;
    Bws<128> vendorModel;
    Bws<Product::kMaxUriBytes> url;
    Bws<Product::kMaxUriBytes> imageUrl;
    Bws<Product::kMaxUriBytes> imageHiresUrl;
    auto& product = iMediaPlayer.Product();
    product.GetManufacturerDetails(name, info, url, imageUrl);
    SetInfo(iInfo, RAAT__INFO_KEY_VENDOR, name);
    vendorModel.Append(name);
    vendorModel.Append(' ');

    product.GetModelDetails(name, info, url, imageUrl);
    SetInfo(iInfo, RAAT__INFO_KEY_MODEL, name);
    vendorModel.AppendThrow(name);
    SetInfo(iInfo, RAAT__INFO_KEY_VENDOR_MODEL, vendorModel);

    SetInfo(iInfo, RAAT__INFO_KEY_SERIAL, iSerialNumber);
    SetInfo(iInfo, RAAT__INFO_KEY_VERSION, iSoftwareVersion);
    
    // FIXME - following info is all stubbed out
    // RAAT__INFO_KEY_CONFIG_URL

    RAAT__device_set_output_plugin(iDevice, iOutput->Plugin());
    if (iVolume != nullptr) {
        RAAT__device_set_volume_plugin(iDevice, iVolume->Plugin());
    }
    RAAT__device_set_source_selection_plugin(iDevice, iSourceSelection->Plugin());
    RAAT__device_set_transport_plugin(iDevice, iTransport->Plugin());
    iTimer->FireIn(250); /* raat's lua interpreter crashes (memory overwrites?)
                            if evented updates are delivered during startup.
                            Delay all eventing for a short time to allow time
                            for the device to be started below */

    status = RAAT__device_run(iDevice);
    if (!RC__STATUS_IS_SUCCESS(status)) {
        Log::Print("RAAT server exited with error\n");
    }
}

void RaatApp::StartPlugins()
{
    if (iVolume != nullptr) {
        iVolume->Start();
    }
    iSourceSelection->Start();
}

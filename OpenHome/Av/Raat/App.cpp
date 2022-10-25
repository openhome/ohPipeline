#include <OpenHome/Av/Raat/App.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Av/SourceFactory.h>
#include <OpenHome/Av/VolumeManager.h>
#include <OpenHome/Av/Raat/Output.h>
#include <OpenHome/Av/Raat/Volume.h>
#include <OpenHome/Av/Raat/SourceSelection.h>
#include <OpenHome/Av/Raat/Transport.h>
#include <OpenHome/Media/Debug.h>

#include <raat_device.h> 
#include <raat_info.h> 
#include <rc_guid.h>
#include <raat_base.h>

using namespace OpenHome;
using namespace OpenHome::Av;

RaatApp::RaatApp(
    Environment& aEnv,
    IMediaPlayer& aMediaPlayer,
    ISourceRaat& aSourceRaat,
    IRaatTime& aRaatTime,
    IRaatSignalPathObservable& aSignalPathObservable,
    const Brx& aSerialNumber,
    const Brx& aSoftwareVersion)
    : iMediaPlayer(aMediaPlayer)
    , iDevice(nullptr)
    , iInfo(nullptr)
    , iFriendlyNameId(IFriendlyNameObservable::kIdInvalid)
    , iSerialNumber(aSerialNumber)
    , iSoftwareVersion(aSoftwareVersion)
{
    iThread = new ThreadFunctor("Raat", MakeFunctor(*this, &RaatApp::RaatThread));
    iOutput = new RaatOutput(aEnv, aMediaPlayer.Pipeline(), aSourceRaat, aRaatTime, aSignalPathObservable);
    if (aMediaPlayer.ConfigManager().HasNum(VolumeConfig::kKeyLimit)) {
        iVolume = RaatVolume::New(aMediaPlayer);
    }
    iSourceSelection = new RaatSourceSelection(aMediaPlayer, SourceFactory::kSourceNameRaat);
    iTransport = new RaatTransport(aMediaPlayer);
    iThread->Start();
}

RaatApp::~RaatApp()
{
    if (iDevice != nullptr) {
        RAAT__device_stop(iDevice);
    }
    iMediaPlayer.FriendlyNameObservable().DeregisterFriendlyNameObserver(iFriendlyNameId);
    delete iThread;
    RAAT__device_delete(iDevice);
    delete iTransport;
    delete iSourceSelection;
    delete iVolume;
    delete iOutput;
}

IRaatReader& RaatApp::Reader()
{
    return *iOutput;
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
    auto& product = iMediaPlayer.Product();
    product.GetManufacturerDetails(name, info, url, imageUrl);
    SetInfo(iInfo, RAAT__INFO_KEY_VENDOR, name);
    vendorModel.Append(name);
    vendorModel.Append(' ');

    product.GetModelDetails(name, info, url, imageUrl);
    SetInfo(iInfo, RAAT__INFO_KEY_MODEL, name);

    Bws<Product::kMaxRoomBytes> room;
    Bws<Product::kMaxNameBytes> userName;
    product.GetProductDetails(room, userName, info, imageUrl);
    vendorModel.AppendThrow(room);
    SetInfo(iInfo, RAAT__INFO_KEY_VENDOR_MODEL, vendorModel);

    // register observer whose callback allows us to set RAAT__INFO_KEY_AUTO_NAME
    iFriendlyNameId = iMediaPlayer.FriendlyNameObservable().RegisterFriendlyNameObserver(
        MakeFunctorGeneric<const Brx&>(*this, &RaatApp::FriendlyNameChanged)
    );

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

    status = RAAT__device_run(iDevice);
    if (!RC__STATUS_IS_SUCCESS(status)) {
        Log::Print("RAAT server exited with error\n");
    }
}

void RaatApp::FriendlyNameChanged(const Brx& aName)
{
    SetInfo(iInfo, RAAT__INFO_KEY_AUTO_NAME, aName);
}

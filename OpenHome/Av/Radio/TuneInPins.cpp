#include <OpenHome/Av/Radio/TuneInPins.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Net/Core/CpDeviceDv.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/ThreadPool.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Net;

// Pin mode
static const TChar* kPinModeTuneIn = "tunein";

// Pin types
static const TChar* kPinTypeStation = "station";
static const TChar* kPinTypeStream = "stream";
static const TChar* kPinTypePodcast = "podcast";

// Pin params
static const TChar* kPinKeyStationId = "id";
static const TChar* kPinKeyStreamUrl = "path";

TuneInPins::TuneInPins(DvDeviceStandard& aDevice, Media::TrackFactory& aTrackFactory, CpStack& aCpStack, Configuration::IStoreReadWrite& aStore, IThreadPool& aThreadPool, const Brx& aPartnerId)
    : iLock("IPIN")
    , iPodcastPinsEpisode(nullptr)
    , iPin(iPinIdProvider)
{
    CpDeviceDv* cpDevice = CpDeviceDv::New(aCpStack, aDevice);
    iCpRadio = new CpProxyAvOpenhomeOrgRadio2(*cpDevice);
    cpDevice->RemoveRef(); // iProxy will have claimed a reference to the device so no need for us to hang onto another
    iPodcastPinsEpisode = new PodcastPinsLatestEpisodeTuneIn(aDevice, aTrackFactory, aCpStack, aStore, aPartnerId);
    iThreadPoolHandle = aThreadPool.CreateHandle(MakeFunctor(*this, &TuneInPins::Invoke),
                                                 "TuneInPins", ThreadPoolPriority::Medium);
}

TuneInPins::~TuneInPins()
{
    iThreadPoolHandle->Destroy();
    delete iCpRadio;
    delete iPodcastPinsEpisode;
}

void TuneInPins::BeginInvoke(const IPin& aPin, Functor aCompleted)
{
    if (aPin.Mode() != Brn(kPinModeTuneIn)) {
        return;
    }
    AutoPinComplete completion(aCompleted);
    if (aPin.Type() == Brn(kPinTypePodcast)) { 
        iPodcastPinsEpisode->Cancel(false);
    }
    (void)iPin.TryUpdate(aPin.Mode(), aPin.Type(), aPin.Uri(), aPin.Title(),
                         aPin.Description(), aPin.ArtworkUri(), aPin.Shuffle());
    completion.Cancel();
    iCompleted = aCompleted;
    (void)iThreadPoolHandle->TrySchedule();
}

void TuneInPins::Invoke()
{
    AutoFunctor _(iCompleted);
    TBool res = false;
    try {
        PinUri pinUri(iPin);
        if (Brn(pinUri.Type()) == Brn(kPinTypeStation)) { 
            Brn stationId;
            if (pinUri.TryGetValue(kPinKeyStationId, stationId)) {
                res = LoadStation(stationId, iPin);
            }
            else {
                THROW(PinUriMissingRequiredParameter);
            }
        }
        else if (Brn(pinUri.Type()) == Brn(kPinTypeStream)) { 
            Brn streamUrl;
            if (pinUri.TryGetValue(kPinKeyStreamUrl, streamUrl)) {
                res = LoadStream(streamUrl, iPin);
            }
            else {
                THROW(PinUriMissingRequiredParameter);
            }
        }
        else if (Brn(pinUri.Type()) == Brn(kPinTypePodcast)) { 
            iPodcastPinsEpisode->LoadPodcast(iPin);
            return;
        }
        else {
            THROW(PinTypeNotSupported);
        }
    }
    catch (PinUriMissingRequiredParameter&) {
        LOG_ERROR(kPipeline, "TuneInPins::Invoke - missing parameter in %.*s\n", PBUF(iPin.Uri()));
        throw;
    }

    if (!res) {
        THROW(PinInvokeError);
    }
}

void TuneInPins::Cancel()
{
    if (iPin.Type() == Brn(kPinTypePodcast)) { 
        iPodcastPinsEpisode->Cancel(true);
    }
}

const TChar* TuneInPins::Mode() const
{
    return kPinModeTuneIn;
}

TBool TuneInPins::SupportsVersion(TUint version) const
{
    return version >= kMinSupportedVersion && version <= kMaxSupportedVersion;
}


TBool TuneInPins::LoadStation(const Brx& aStation, const IPin& aPin)
{
    try {
        Bwh stream(1024);
        TuneIn::SetPathFromId(stream, aStation);
        LoadStream(stream, aPin);
        return true;
    }
    catch (Exception& ex) {
        Log::Print("%s in TuneInPins::LoadStation\n", ex.Message());
        return false;
    }
}

TBool TuneInPins::LoadStream(const Brx& aStream, const IPin& aPin)
{
    try {
        Bwh uri(1024);
        Uri::Unescape(uri, aStream);
        Bwh metadata(1024 * 5);
        PinMetadata::GetDidlLite(aPin, metadata);
        iCpRadio->SyncSetChannel(uri, metadata);
        Thread::Sleep(300);
        iCpRadio->SyncPlay();
        return true;
    }
    catch (Exception& ex) {
        Log::Print("%s in TuneInPins::LoadStream\n", ex.Message());
        return false;
    }
}

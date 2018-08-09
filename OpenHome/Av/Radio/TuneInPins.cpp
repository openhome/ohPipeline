#include <OpenHome/Av/Radio/TuneInPins.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Net/Core/CpDeviceDv.h>
#include <OpenHome/Media/Pipeline/Msg.h>

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

TuneInPins::TuneInPins(DvDeviceStandard& aDevice, Media::TrackFactory& aTrackFactory, CpStack& aCpStack, Configuration::IStoreReadWrite& aStore, const Brx& aPartnerId)
    : iLock("IPIN")
    , iCpStack(aCpStack)
    , iPartnerId(aPartnerId)
    , iPodcastPinsEpisode(nullptr)
{
    CpDeviceDv* cpDevice = CpDeviceDv::New(iCpStack, aDevice);
    iCpRadio = new CpProxyAvOpenhomeOrgRadio1(*cpDevice);
    cpDevice->RemoveRef(); // iProxy will have claimed a reference to the device so no need for us to hang onto another
    iPodcastPinsEpisode = new PodcastPinsLatestEpisodeTuneIn(aDevice, aTrackFactory, aCpStack, aStore);
}

TuneInPins::~TuneInPins()
{
    delete iCpRadio;
    delete iPodcastPinsEpisode;
}

void TuneInPins::BeginInvoke(const IPin& aPin, Functor aCompleted)
{
    AutoFunctor _(aCompleted);
    PinUri pin(aPin);
    TBool res = false;
    if (Brn(pin.Mode()) == Brn(kPinModeTuneIn)) {
        if (Brn(pin.Type()) == Brn(kPinTypeStation)) { 
            Brn stationId;
            if (pin.TryGetValue(kPinKeyStationId, stationId)) {
                res = LoadStation(stationId, aPin);
            }
            else {
                THROW(PinUriMissingRequiredParameter);
            }
        }
        else if (Brn(pin.Type()) == Brn(kPinTypeStream)) { 
            Brn streamUrl;
            if (pin.TryGetValue(kPinKeyStreamUrl, streamUrl)) {
                res = LoadStream(streamUrl, aPin);
            }
            else {
                THROW(PinUriMissingRequiredParameter);
            }
        }
        else if (Brn(pin.Type()) == Brn(kPinTypePodcast)) { 
            iPodcastPinsEpisode->Invoke(aPin);
        }
        else {
            THROW(PinTypeNotSupported);
        }
        if (!res) {
            THROW(PinInvokeError);
        }
    }
}

void TuneInPins::Cancel()
{
}

const TChar* TuneInPins::Mode() const
{
    return kPinModeTuneIn;
}

TBool TuneInPins::LoadStation(const Brx& aStation, const IPin& aPin)
{
    try {
        Bwh stream(1024);
        stream.AppendPrintf("http://opml.radiotime.com/Tune.ashx?id=%.*s&formats=mp3,aac,ogg,hls&partnerId=%.*s", PBUF(aStation), PBUF(iPartnerId));
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
        iCpRadio->SyncPlay();
        return true;
    }
    catch (Exception& ex) {
        Log::Print("%s in TuneInPins::LoadStream\n", ex.Message());
        return false;
    }
}
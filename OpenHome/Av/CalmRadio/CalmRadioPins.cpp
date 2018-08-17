#include <OpenHome/Av/CalmRadio/CalmRadioPins.h>
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
static const TChar* kPinModeCalmRadio = "calmradio";

// Pin types
static const TChar* kPinTypeStation = "station";
static const TChar* kPinTypeStream = "stream";

// Pin params
static const TChar* kPinKeyStationId = "id";
static const TChar* kPinKeyStreamUrl = "path";

CalmRadioPins::CalmRadioPins(CalmRadio& aCalmRadio, DvDeviceStandard& aDevice, CpStack& aCpStack, IThreadPool& aThreadPool)
    : iLock("IPIN")
    , iCalmRadio(aCalmRadio)
    , iPin(iPinIdProvider)
{
    CpDeviceDv* cpDevice = CpDeviceDv::New(aCpStack, aDevice);
    iCpRadio = new CpProxyAvOpenhomeOrgRadio1(*cpDevice);
    cpDevice->RemoveRef(); // iProxy will have claimed a reference to the device so no need for us to hang onto another
    iThreadPoolHandle = aThreadPool.CreateHandle(MakeFunctor(*this, &CalmRadioPins::Invoke),
                                                 "CalmRadioPins", ThreadPoolPriority::Medium);
}

CalmRadioPins::~CalmRadioPins()
{
    iThreadPoolHandle->Destroy();
    delete iCpRadio;
}

void CalmRadioPins::BeginInvoke(const IPin& aPin, Functor aCompleted)
{
    if (aPin.Mode() != Brn(kPinModeCalmRadio)) {
        return;
    }
    AutoPinComplete completion(aCompleted);
    iCalmRadio.Login(iToken);
    (void)iPin.TryUpdate(aPin.Mode(), aPin.Type(), aPin.Uri(), aPin.Title(),
                         aPin.Description(), aPin.ArtworkUri(), aPin.Shuffle());
    completion.Cancel();
    iCompleted = aCompleted;
    (void)iThreadPoolHandle->TrySchedule();
}

void CalmRadioPins::Invoke()
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
        else {
            THROW(PinTypeNotSupported);
        }
    }
    catch (PinUriMissingRequiredParameter&) {
        LOG_ERROR(kPipeline, "CalmRadioPins::Invoke - missing parameter in %.*s\n", PBUF(iPin.Uri()));
        throw;
    }

    if (!res) {
        THROW(PinInvokeError);
    }
}

void CalmRadioPins::Cancel()
{
    iCalmRadio.Interrupt(true);
}

const TChar* CalmRadioPins::Mode() const
{
    return kPinModeCalmRadio;
}

TBool CalmRadioPins::LoadStation(const Brx& aStation, const IPin& aPin)
{
    try {
        Bwh stream(1024);
        stream.AppendPrintf("https://streams.calmradio.com/api/%.*s/320/stream", PBUF(aStation));
        LoadStream(stream, aPin);
        return true;
    }
    catch (Exception& ex) {
        Log::Print("%s in CalmRadioPins::LoadStation\n", ex.Message());
        return false;
    }
}

TBool CalmRadioPins::LoadStream(const Brx& aStream, const IPin& aPin)
{
    try {
        Bwh uri(1024);
        uri.Replace("calmradio://stream?");
        Uri::Unescape(uri, aStream);
        Bwh metadata(1024 * 5);
        PinMetadata::GetDidlLite(aPin, metadata);
        iCpRadio->SyncSetChannel(uri, metadata);
        iCpRadio->SyncPlay();
        return true;
    }
    catch (Exception& ex) {
        Log::Print("%s in CalmRadioPins::LoadStream\n", ex.Message());
        return false;
    }
}
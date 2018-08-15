#include <OpenHome/Av/CalmRadio/CalmRadioPins.h>
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
static const TChar* kPinModeCalmRadio = "calmradio";

// Pin types
static const TChar* kPinTypeStation = "station";
static const TChar* kPinTypeStream = "stream";

// Pin params
static const TChar* kPinKeyStationId = "id";
static const TChar* kPinKeyStreamUrl = "path";

CalmRadioPins::CalmRadioPins(CalmRadio& aCalmRadio, DvDeviceStandard& aDevice, CpStack& aCpStack)
    : iLock("IPIN")
    , iCalmRadio(aCalmRadio)
    , iCpStack(aCpStack)
{
    CpDeviceDv* cpDevice = CpDeviceDv::New(iCpStack, aDevice);
    iCpRadio = new CpProxyAvOpenhomeOrgRadio1(*cpDevice);
    cpDevice->RemoveRef(); // iProxy will have claimed a reference to the device so no need for us to hang onto another
}

CalmRadioPins::~CalmRadioPins()
{
    delete iCpRadio;
}

void CalmRadioPins::BeginInvoke(const IPin& aPin, Functor aCompleted)
{
    AutoFunctor _(aCompleted);
    PinUri pin(aPin);
    TBool res = false;
    if (Brn(pin.Mode()) == Brn(kPinModeCalmRadio)) {
        Bwh token(128);
        iCalmRadio.Login(token);
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
        else {
            THROW(PinTypeNotSupported);
        }
        if (!res) {
            THROW(PinInvokeError);
        }
    }
}

void CalmRadioPins::Cancel()
{
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
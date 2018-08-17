#include <OpenHome/Av/Pins/UrlPins.h>
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
static const TChar* kPinModeUrl = "url";

// Pin types
static const TChar* kPinTypeStream = "stream";

// Pin params
static const TChar* kPinKeyPath = "path";

UrlPins::UrlPins(DvDeviceStandard& aDevice, CpStack& aCpStack, IThreadPool& aThreadPool)
    : iLock("IPIN")
    , iPin(iPinIdProvider)
{
    CpDeviceDv* cpDevice = CpDeviceDv::New(aCpStack, aDevice);
    iCpRadio = new CpProxyAvOpenhomeOrgRadio1(*cpDevice);
    cpDevice->RemoveRef(); // iProxy will have claimed a reference to the device so no need for us to hang onto another
    iThreadPoolHandle = aThreadPool.CreateHandle(MakeFunctor(*this, &UrlPins::Invoke),
                                                 "UrlPins", ThreadPoolPriority::Medium);
}

UrlPins::~UrlPins()
{
    iThreadPoolHandle->Destroy();
    delete iCpRadio;
}

void UrlPins::BeginInvoke(const IPin& aPin, Functor aCompleted)
{
    if (aPin.Mode() != Brn(kPinModeUrl)) {
        return;
    }
    AutoPinComplete completion(aCompleted);
    (void)iPin.TryUpdate(aPin.Mode(), aPin.Type(), aPin.Uri(), aPin.Title(),
                         aPin.Description(), aPin.ArtworkUri(), aPin.Shuffle());
    completion.Cancel();
    iCompleted = aCompleted;
    (void)iThreadPoolHandle->TrySchedule();
}

void UrlPins::Invoke()
{
    AutoFunctor _(iCompleted);
    TBool res = false;
    try {
        PinUri pinUri(iPin);
        if (Brn(pinUri.Type()) == Brn(kPinTypeStream)) { 
            Brn stream;
            if (pinUri.TryGetValue(kPinKeyPath, stream)) {
                res = LoadStream(stream, iPin);
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
        LOG_ERROR(kPipeline, "UrlPins::Invoke - missing parameter in %.*s\n", PBUF(iPin.Uri()));
        throw;
    }

    if (!res) {
        THROW(PinInvokeError);
    }
}

void UrlPins::Cancel()
{
}

const TChar* UrlPins::Mode() const
{
    return kPinModeUrl;
}

TBool UrlPins::LoadStream(const Brx& aStream, const IPin& aPin)
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
        Log::Print("%s in UrlPins::LoadStream\n", ex.Message());
        return false;
    }
}
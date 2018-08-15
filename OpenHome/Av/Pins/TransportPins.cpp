#include <OpenHome/Av/Pins/TransportPins.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Net/Core/CpDeviceDv.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Net;

// Pin mode
static const TChar* kPinModeTransport = "transport";

// Pin types
static const TChar* kPinTypeSource = "source";

// Pin params
static const TChar* kPinKeySourceSystemName = "id";

TransportPins::TransportPins(DvDeviceStandard& aDevice, CpStack& aCpStack)
    : iLock("IPIN")
    , iCpStack(aCpStack)
{
    CpDeviceDv* cpDevice = CpDeviceDv::New(iCpStack, aDevice);
    iCpProduct = new CpProxyAvOpenhomeOrgProduct2(*cpDevice);
    iCpTransport = new CpProxyAvOpenhomeOrgTransport1(*cpDevice);
    cpDevice->RemoveRef(); // iProxy will have claimed a reference to the device so no need for us to hang onto another
}

TransportPins::~TransportPins()
{
    delete iCpProduct;
    delete iCpTransport;
}

void TransportPins::BeginInvoke(const IPin& aPin, Functor aCompleted)
{
    AutoFunctor _(aCompleted);
    PinUri pin(aPin);
    TBool res = false;
    if (Brn(pin.Mode()) == Brn(kPinModeTransport)) {
        if (Brn(pin.Type()) == Brn(kPinTypeSource)) { 
            Brn sourceSystemName;
            if (pin.TryGetValue(kPinKeySourceSystemName, sourceSystemName)) {
                res = SelectLocalInput(sourceSystemName);
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

void TransportPins::Cancel()
{
}

const TChar* TransportPins::Mode() const
{
    return kPinModeTransport;
}

TBool TransportPins::SelectLocalInput(const Brx& aSourceSystemName)
{
    Bws<20> input;
    Uri::Unescape(input, aSourceSystemName);
    try {
        iCpProduct->SyncSetSourceBySystemName(input);
        iCpTransport->SyncPlay();
        return true;
    }
    catch (Exception& ex) {
        Log::Print("%s in Pins::SelectLocalInput\n", ex.Message());
        return false;
    }
}
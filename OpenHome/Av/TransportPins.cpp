#include <OpenHome/Av/TransportPins.h>
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

TransportPins::TransportPins(DvDeviceStandard& aDevice, CpStack& aCpStack)
    : iLock("IPIN")
    , iCpStack(aCpStack)
{
    CpDeviceDv* cpDevice = CpDeviceDv::New(iCpStack, aDevice);
    iCpTransport = new CpProxyAvOpenhomeOrgTransport1(*cpDevice);
    cpDevice->RemoveRef(); // iProxy will have claimed a reference to the device so no need for us to hang onto another
}

TransportPins::~TransportPins()
{
    delete iCpTransport;
}

void TransportPins::Invoke(const IPin& aPin)
{
    PinUri pin(aPin);
    TBool res = false;
    if (Brn(pin.Mode()) == Brn(kPinModeTransport)) {
        if (Brn(pin.Type()) == Brn(kPinTypeSource)) { 
            res = SelectLocalInput(pin.Value());
        }
        else {
            THROW(PinTypeNotSupported);
        }
        if (!res) {
            THROW(PinInvokeError);
        }
    }
}

const TChar* TransportPins::Mode() const
{
    return kPinModeTransport;
}

TBool TransportPins::SelectLocalInput(const Brx& aSourceSystemName)
{
    Bws<20> input;
    try {
        if (aSourceSystemName == Brn("Songcast")) {
            input.ReplaceThrow(Brn("Receiver"));
        }
        else if (aSourceSystemName == Brn("Net Aux") || aSourceSystemName == Brn("Airplay")) {
            input.ReplaceThrow(Brn("RAOP"));
        }
        else if (aSourceSystemName == Brn("UPnP AV")) {
            input.ReplaceThrow(Brn("UpnpAv"));
        }
        else {
            input.ReplaceThrow(aSourceSystemName);
        }
        iCpTransport->SyncPlayAs(input, Brx::Empty());
        return true;
    }
    catch (Exception& ex) {
        Log::Print("%s in Pins::SelectLocalInput\n", ex.Message());
        return false;
    }
}
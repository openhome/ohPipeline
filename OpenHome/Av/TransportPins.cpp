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
    if (pin.Mode() == PinUri::EMode::eTransport) {
        switch (pin.Type()) {
            case PinUri::EType::eSource: SelectLocalInput(pin.Value()); break;
            default: {
                return;
            }
        }
    }
}

const TChar* TransportPins::Mode() const
{
    return PinUri::GetModeString(PinUri::EMode::eTransport);
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
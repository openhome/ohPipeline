#include <OpenHome/Av/Radio/RadioPins.h>
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
static const TChar* kPinModeRadio = "radio";

// Pin types
static const TChar* kPinTypePreset = "preset";

// Pin params
static const TChar* kPinKeyPresetNumber = "id";

RadioPins::RadioPins(DvDeviceStandard& aDevice, CpStack& aCpStack)
    : iLock("IPIN")
    , iCpStack(aCpStack)
{
    CpDeviceDv* cpDevice = CpDeviceDv::New(iCpStack, aDevice);
    iCpTransport = new CpProxyAvOpenhomeOrgTransport1(*cpDevice);
    cpDevice->RemoveRef(); // iProxy will have claimed a reference to the device so no need for us to hang onto another
}

RadioPins::~RadioPins()
{
    delete iCpTransport;
}

void RadioPins::BeginInvoke(const IPin& aPin, Functor aCompleted)
{
    AutoFunctor _(aCompleted);
    PinUri pin(aPin);
    TBool res = false;
    if (Brn(pin.Mode()) == Brn(kPinModeRadio)) {
        if (Brn(pin.Type()) == Brn(kPinTypePreset)) { 
            Brn presetNum;
            if (pin.TryGetValue(kPinKeyPresetNumber, presetNum)) {
                res = LoadPreset(presetNum);
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

void RadioPins::Cancel()
{
}

const TChar* RadioPins::Mode() const
{
    return kPinModeRadio;
}

TBool RadioPins::LoadPreset(const Brx& aPreset)
{
    try {
        return LoadPreset(Ascii::Uint(aPreset));
    }
    catch (Exception& ex) {
        Log::Print("%s in RadioPins::LoadPreset\n", ex.Message());
        return false;
    }
}

TBool RadioPins::LoadPreset(TUint aPreset)
{
    try {
        if (aPreset > 0) {
            // expect preset number from kazoo (1-100)
            Bws<10> preset("index=");
            Ascii::AppendDec(preset, (aPreset-1));
            iCpTransport->SyncPlayAs(Brn("Radio"), preset);
            return true;
        }
        else {
            return false;
        }
    }
    catch (Exception& ex) {
        Log::Print("%s in RadioPins::LoadPreset\n", ex.Message());
        return false;
    }
}
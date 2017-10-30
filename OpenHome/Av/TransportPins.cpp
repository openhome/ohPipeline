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

TBool TransportPins::SelectLocalInput(const Brx& aSourceSystemName)
{
    Bwh input(20);
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

TBool TransportPins::Test(const Brx& aType, const Brx& aInput, IWriterAscii& aWriter)
{
    if (aType == Brn("help")) {
        aWriter.Write(Brn("select_input (input: source system name to select)"));
        aWriter.Write(Brn(" "));
        aWriter.WriteNewline(); // can't get this to work
        return true;
    }
    else if (aType == Brn("select_input")) {
        aWriter.Write(Brn("Complete"));
        return SelectLocalInput(aInput);
    }
    return false;
}
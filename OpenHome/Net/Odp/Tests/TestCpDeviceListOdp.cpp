#include <OpenHome/Types.h>
#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/OptionParser.h>
#include <OpenHome/Net/Core/OhNet.h>
#include <OpenHome/Net/Private/MdnsProvider.h>
#include <OpenHome/Net/Odp/CpDeviceOdp.h>
#include <OpenHome/Net/Core/FunctorCpDevice.h>
#include <OpenHome/Debug-ohMediaPlayer.h>

#include <vector>

using namespace OpenHome;
using namespace OpenHome::Net;
using namespace OpenHome::TestFramework;

class DeviceListLogger
{
public:
    DeviceListLogger();
    void Added(CpDevice& aDevice);
    void Removed(CpDevice& aDevice);
private:
    void PrintDeviceInfo(const char* aPrologue, const CpDevice& aDevice);
private:
    Mutex iLock;
};

DeviceListLogger::DeviceListLogger()
    : iLock("DLLM")
{
}

void DeviceListLogger::Added(CpDevice& aDevice)
{
    PrintDeviceInfo("Added", aDevice);
}

void DeviceListLogger::Removed(CpDevice& aDevice)
{
    PrintDeviceInfo("Removed", aDevice);
}

void DeviceListLogger::PrintDeviceInfo(const char* aPrologue, const CpDevice& aDevice)
{
    iLock.Wait();
    Brh val;
    if (aDevice.Udn() == Brn("4c494e4e-0026-0f22-26ce-01453289013f")) {
        Print("ODP Device %s\n", aPrologue);
        Print("    udn   = %.*s\n", PBUF(aDevice.Udn()));
        aDevice.GetAttribute("Odp.Location", val);
        Print("    locat = %.*s\n", PBUF(val));
        aDevice.GetAttribute("Odp.FriendlyName", val);
        Print("    fname = %.*s\n", PBUF(val));
        aDevice.GetAttribute("Odp.UglyName", val);
        Print("    uname = %.*s\n", PBUF(val));
        aDevice.GetAttribute("Odp.Type", val);
        Print("    type  = %.*s\n", PBUF(val));
    }
    iLock.Signal();
}

void OpenHome::TestFramework::Runner::Main(TInt aArgc, TChar* aArgv[], Net::InitialisationParams* aInitParams)
{
    OptionParser parser;
    OptionUint adapter("-a", "--adapter", 0, "[0...n] Adpater index to use");
    parser.AddOption(&adapter);
    if (!parser.Parse(aArgc, aArgv) || parser.HelpDisplayed()) {
        return;
    }

    aInitParams->SetDvEnableBonjour("TestCpDeviceListOdp", true);
    Library* lib = new Library(aInitParams);
    std::vector<NetworkAdapter*>* subnetList = lib->CreateSubnetList();
    TIpAddress subnet = (*subnetList)[adapter.Value()]->Subnet();
    Library::DestroySubnetList(subnetList);
    //Debug::SetLevel(Debug::kBonjour);
    //Debug::SetSeverity(Debug::kSeverityTrace);
    Debug::SetLevel(Debug::kOdp);

    Bws<Endpoint::kMaxAddressBytes> addr;
    Endpoint::AppendAddress(addr, subnet);
    Log::Print("Subnet in use: ");
    Log::Print(addr);
    Log::Print("\n");
    
    // combined
    CpStack* cpStack = NULL;
    DvStack* dvStack = NULL;
    lib->StartCombined(subnet, cpStack, dvStack);

    DeviceListLogger logger;
    FunctorCpDevice added = MakeFunctorCpDevice(logger, &DeviceListLogger::Added);
    FunctorCpDevice removed = MakeFunctorCpDevice(logger, &DeviceListLogger::Removed);
    CpDeviceListOdpAll* deviceList = new CpDeviceListOdpAll(*cpStack, added, removed);

    Thread::Sleep(10 * 1000);

    delete deviceList;
    delete lib;
}
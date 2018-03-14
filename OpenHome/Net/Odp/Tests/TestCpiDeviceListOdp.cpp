#include <OpenHome/Types.h>
#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/OptionParser.h>
#include <OpenHome/Net/Core/OhNet.h>
#include <OpenHome/Net/Private/MdnsProvider.h>
#include <OpenHome/Net/Odp/Tests/CpiDeviceOdp.h>
#include <OpenHome/Net/Private/FunctorCpiDevice.h>

#include <vector>

using namespace OpenHome;
using namespace OpenHome::Net;
using namespace OpenHome::TestFramework;

class DeviceListLogger
{
public:
    DeviceListLogger();
    void Added(CpiDevice& aDevice);
    void Removed(CpiDevice& aDevice);
private:
    void PrintDeviceInfo(const char* aPrologue, const CpiDevice& aDevice);
private:
    Mutex iLock;
};

DeviceListLogger::DeviceListLogger()
    : iLock("DLLM")
{
}

void DeviceListLogger::Added(CpiDevice& aDevice)
{
    PrintDeviceInfo("Added", aDevice);
}

void DeviceListLogger::Removed(CpiDevice& aDevice)
{
    PrintDeviceInfo("Removed", aDevice);
}

void DeviceListLogger::PrintDeviceInfo(const char* aPrologue, const CpiDevice& aDevice)
{
    iLock.Wait();
    Print("ODP Device %s\n", aPrologue);
    Print("    udn   = %.*s\n", PBUF(aDevice.Udn()));
    Brh val;
    aDevice.GetAttribute("Odp.Location", val);
    Print("    locat = %.*s\n", PBUF(val));
    aDevice.GetAttribute("Odp.FriendlyName", val);
    Print("    fname = %.*s\n", PBUF(val));
    aDevice.GetAttribute("Odp.UglyName", val);
    Print("    uname = %.*s\n", PBUF(val));
    aDevice.GetAttribute("Odp.Type", val);
    Print("    type  = %.*s\n", PBUF(val));
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

    aInitParams->SetDvEnableBonjour("TestCpiDeviceListOdp");
    Library* lib = new Library(aInitParams);
    std::vector<NetworkAdapter*>* subnetList = lib->CreateSubnetList();
    TIpAddress subnet = (*subnetList)[adapter.Value()]->Subnet();
    Library::DestroySubnetList(subnetList);
    //Debug::SetLevel(Debug::kBonjour);
    //Debug::SetSeverity(Debug::kSeverityTrace);

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
    FunctorCpiDevice added = MakeFunctorCpiDevice(logger, &DeviceListLogger::Added);
    FunctorCpiDevice removed = MakeFunctorCpiDevice(logger, &DeviceListLogger::Removed);
    CpiDeviceListOdpAll* deviceList = new CpiDeviceListOdpAll(*cpStack, added, removed);
    deviceList->Start();

    Thread::Sleep(10 * 1000);

    delete deviceList;
    delete lib;
}
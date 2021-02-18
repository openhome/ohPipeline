#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Net/Private/CpiStack.h>
#include <OpenHome/Net/Private/DviStack.h>


extern void TestConfigUi(OpenHome::Net::CpStack& aCpStack, OpenHome::Net::DvStack& aDvStack);

void OpenHome::TestFramework::Runner::Main(TInt /*aArgc*/, TChar* /*aArgv*/[], Net::InitialisationParams* aInitParams)
{
    aInitParams->SetUseLoopbackNetworkAdapter();
    aInitParams->SetDvEnableBonjour("TestConfigUi", false);
    Net::Library* lib = new Net::Library(aInitParams);

    // Set a subnet.
    std::vector<NetworkAdapter*>* subnetList = lib->CreateSubnetList();
    ASSERT(subnetList->size() > 0);
    Log::Print ("adapter list:\n");
    for (unsigned i=0; i<subnetList->size(); ++i) {
        TIpAddress addr = (*subnetList)[i]->Address();
        Bws<TIpAddressUtils::kMaxAddressBytes> addressBuf;
        TIpAddressUtils::ToString(addr, addressBuf);
        Log::Print ("  %d: %.*s\n", i, PBUF(addressBuf));
    }
    TIpAddress subnet = (*subnetList)[0]->Subnet();
    Net::Library::DestroySubnetList(subnetList);
    lib->SetCurrentSubnet(subnet);

    Bws<TIpAddressUtils::kMaxAddressBytes> addressBuf;
    TIpAddressUtils::ToString(subnet, addressBuf);
    Log::Print("using subnet %.*s\n", PBUF(addressBuf));

    Net::CpStack* cpStack;
    Net::DvStack* dvStack;
    lib->StartCombined(subnet, cpStack, dvStack);
    TestConfigUi(*cpStack, *dvStack);
    delete lib;
}

#include <OpenHome/Av/DeviceAnnouncerMdns.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Private/Env.h>
#include <OpenHome/Av/Product.h>
#include <OpenHome/Net/Private/MdnsProvider.h>
#include <OpenHome/Private/NetworkAdapterList.h>
#include <OpenHome/Net/Core/OhNet.h>
#include <OpenHome/Net/Core/DvDevice.h>
#include <OpenHome/Net/Private/DviDevice.h>
#include <OpenHome/Net/Private/DviProtocolUpnp.h>
#include <OpenHome/Net/Private/DviServerUpnp.h>
#include <OpenHome/Net/Private/DviServer.h>

using namespace OpenHome;
using namespace OpenHome::Av;

DeviceAnnouncerMdns::DeviceAnnouncerMdns(
    Net::DvStack& aDvStack,
    Net::DvDevice& aDevice,
    IFriendlyNameObservable& aFriendlyNameObservable)
    : iLock("DAMd")
    , iDvStack(aDvStack)
    , iDevice(aDevice.Device())
    , iFriendlyNameObservable(aFriendlyNameObservable)
    , iCurrentSubnet(kIpAddressV4AllAdapters)
    , iName("OpenHome MediaPlayer") // default name should never be seen
    , iRegistered(false)
{
    auto& env = iDvStack.Env();
    auto mdnsProvider = env.MdnsProvider();
    ASSERT(mdnsProvider != nullptr);
    iHandleMdns = mdnsProvider->MdnsCreateService();
    iIdFriendlyName = iFriendlyNameObservable.RegisterFriendlyNameObserver(
            MakeFunctorGeneric<const Brx&>(*this, &DeviceAnnouncerMdns::NameChanged)); // NameChanged is called within this
    iIdAdapterChange = env.NetworkAdapterList().AddCurrentChangeListener(
            MakeFunctor(*this, &DeviceAnnouncerMdns::CurrentAdapterChanged), "DeviceAnnouncerMdns", false); // CurrentAdapterChanged is NOT called within this
    CurrentAdapterChanged();
}

DeviceAnnouncerMdns::~DeviceAnnouncerMdns()
{
    iDvStack.Env().NetworkAdapterList().RemoveCurrentChangeListener(iIdAdapterChange);
    iFriendlyNameObservable.DeregisterFriendlyNameObserver(iIdFriendlyName);
    Deregister();
}

void DeviceAnnouncerMdns::CurrentAdapterChanged()
{
    AutoMutex _(iLock);
    Deregister();
    AutoNetworkAdapterRef ar(iDvStack.Env(), "DeviceAnnouncerMdns");
    const auto current = ar.Adapter();
    if (current == nullptr) {
        iCurrentSubnet = kIpAddressV4AllAdapters;
    }
    else {
        const TIpAddress subnet = current->Subnet();
        if (TIpAddressUtils::Equals(subnet, iCurrentSubnet)) {
            return;
        }
        iCurrentSubnet = kIpAddressV4AllAdapters;
        Register(current);
        iCurrentSubnet = subnet;
    }
}

void DeviceAnnouncerMdns::Register(NetworkAdapter* aCurrent)
{
    auto& env = iDvStack.Env();
    AutoNetworkAdapterRef ar(env, "DeviceAnnouncerMdns");
    const auto current = ar.Adapter();
    if (aCurrent == nullptr) {
        iCurrentSubnet = kIpAddressV4AllAdapters;
        return;
    }
    const TIpAddress subnet = current->Subnet();
    if (TIpAddressUtils::Equals(subnet, iCurrentSubnet)) {
        return;
    }
    iCurrentSubnet = kIpAddressV4AllAdapters;
    const auto& serverAddr = current->Address();
    const TUint serverPort = iDvStack.ServerUpnp().Port(serverAddr);
    Bws<200> uri;
    iDevice.GetUriBase(uri, serverAddr, serverPort, Net::DviProtocolUpnp::kProtocolName);
    uri.Append(Net::DviProtocolUpnp::kDeviceXmlName);
    auto mdnsProvider = env.MdnsProvider();
    Bws<200> info;
    mdnsProvider->MdnsAppendTxtRecord(info, "upnp", uri.PtrZ());
    mdnsProvider->MdnsRegisterService(iHandleMdns, iName.PtrZ(), "_openhome._tcp", serverAddr, serverPort, info.PtrZ());
    iRegistered = true;
}

void DeviceAnnouncerMdns::Deregister()
{
    if (iRegistered) {
        iDvStack.Env().MdnsProvider()->MdnsDestroyService(iHandleMdns);
        iRegistered = false;
    }
}

void DeviceAnnouncerMdns::NameChanged(const Brx& aName)
{
    AutoMutex _(iLock);
    const auto wasRegistered = iRegistered;
    Deregister();
    iName.Replace(aName);
    ASSERT(iName.Bytes() < iName.MaxBytes()); // space for nul terminator (added by PtrZ())
    if (wasRegistered) {
        AutoNetworkAdapterRef ar(iDvStack.Env(), "DeviceAnnouncerMdns::NameChanged");
        Register(ar.Adapter());
    }
}

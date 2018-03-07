#include <OpenHome/Net/Odp/DviProtocolOdp.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Private/DviDevice.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Private/NetworkAdapterList.h>

using namespace OpenHome;
using namespace OpenHome::Net;

// DviProtocolFactoryOdp

IDvProtocol* DviProtocolFactoryOdp::CreateProtocol(DviDevice& aDevice)
{
    return new DviProtocolOdp(aDevice);
}


// DviProtocolOdp

const Brn DviProtocolOdp::kProtocolName("Odp");

DviProtocolOdp::DviProtocolOdp(DviDevice& aDevice)
    : iDevice(aDevice)
    , iEnv(aDevice.GetDvStack().Env())
    , iProvider(*aDevice.GetDvStack().Env().MdnsProvider())
    //, iFriendlyNameObservable(aFriendlyNameObservable)
    , iHandleOdp(iProvider.MdnsCreateService())
    , iRegistered(false)
    , iLock("ODPL")
{
    // FIXME: See DviProtocolOdp for handling multiple adapters
    NetworkAdapterList& adapterList = iEnv.NetworkAdapterList();
    Functor functor = MakeFunctor(*this, &DviProtocolOdp::HandleInterfaceChange);
    iCurrentAdapterChangeListenerId = adapterList.AddCurrentChangeListener(functor, "DviProtocolOdp-current");
    iSubnetListChangeListenerId = adapterList.AddSubnetListChangeListener(functor, "DviProtocolOdp-subnet");

    iName.Replace(iDevice.Udn());
    //iFriendlyNameId = iFriendlyNameObservable.RegisterFriendlyNameObserver(MakeFunctorGeneric<const Brx&>(*this, &DviProtocolOdp::NameChanged));
    HandleInterfaceChange();
}

DviProtocolOdp::~DviProtocolOdp()
{
    //iFriendlyNameObservable.DeregisterFriendlyNameObserver(iFriendlyNameId);
    Deregister();

    iEnv.NetworkAdapterList().RemoveCurrentChangeListener(iCurrentAdapterChangeListenerId);
    iEnv.NetworkAdapterList().RemoveSubnetListChangeListener(iSubnetListChangeListenerId);
}

void DviProtocolOdp::HandleInterfaceChange()
{
    AutoMutex a(iLock);
    NetworkAdapterList& adapterList = iEnv.NetworkAdapterList();
    std::vector<NetworkAdapter*>* subnetList = adapterList.CreateSubnetList();
    AutoNetworkAdapterRef ref(iEnv, "DviProtocolOdp HandleInterfaceChange");
    const NetworkAdapter* current = ref.Adapter();

    if (current != NULL) {
        iEndpoint.SetAddress(current->Address());
    }
    else {
        NetworkAdapter* subnet = (*subnetList)[0]; // FIXME: this is obviously not good enough but should help get something up and running for now
        iEndpoint.SetAddress(subnet->Address());
    }
    NetworkAdapterList::DestroySubnetList(subnetList);

    if (iRegistered) {
        DeregisterLocked();
        RegisterLocked();
    }
}

void DviProtocolOdp::Register()
{
    AutoMutex a(iLock);
    RegisterLocked();
}

void DviProtocolOdp::Deregister()
{
    AutoMutex a(iLock);
    DeregisterLocked();
}

void DviProtocolOdp::RegisterLocked()
{
    // if (iRegistered || iEndpoint.Address() == 0) {
    //     return;
    // }

    // // get Odp port from device attributes
    // const TChar* val;
    // iDevice.GetAttribute("Odp.Port", &val);
    // if (val == NULL) {
    //     return;
    // }
    // TUint odpPort = Ascii::Uint(Brn(val));
    // if (odpPort == 0) {
    //     return;
    // }
    // iEndpoint.SetPort(odpPort);

    // Bws<Endpoint::kMaxAddressBytes> addr;
    // Endpoint::AppendAddress(addr, iEndpoint.Address());
    // Log::Print("Odp Endpoint (%.*s): %.*s:%d\n", PBUF(iDevice.Udn()), PBUF(addr), odpPort);

    // Bws<200> info;
    // iProvider.MdnsAppendTxtRecord(info, "CPath", "/test.html");
    // iProvider.MdnsRegisterService(iHandleOdp, iName.PtrZ(), "_odp._tcp", iEndpoint.Address(), iEndpoint.Port(), info.PtrZ());
    // iRegistered = true;
}

void DviProtocolOdp::DeregisterLocked()
{
    // if (!iRegistered) {
    //     return;
    // }
    // iProvider.MdnsDeregisterService(iHandleOdp);
    // iRegistered = false;
}

void DviProtocolOdp::NameChanged(const Brx& aName)
{
    AutoMutex a(iLock);
    if (iRegistered) {
        DeregisterLocked();
        iName.Replace(aName);
        ASSERT(iName.Bytes() < iName.MaxBytes());   // space for '\0'
        RegisterLocked();
    }
    else { // Nothing registered, so nothing to deregister. Just update name.
        iName.Replace(aName);
        ASSERT(iName.Bytes() < iName.MaxBytes());   // space for '\0'
    }
}

void DviProtocolOdp::WriteResource(const Brx& /*aUriTail*/,
                                   TIpAddress /*aAdapter*/,
                                   std::vector<char*>& /*aLanguageList*/,
                                   IResourceWriter& /*aResourceWriter*/)
{
    ASSERTS(); // resources aren't served over Odp
}

void DviProtocolOdp::Enable()
{
    Register();
}

void DviProtocolOdp::Disable(Functor& aComplete)
{
    Deregister();
    aComplete();
}

const Brx& DviProtocolOdp::ProtocolName() const
{
    return kProtocolName;
}

void DviProtocolOdp::GetAttribute(const TChar* aKey, const TChar** aValue) const
{
    *aValue = iAttributeMap.Get(aKey);
}

void DviProtocolOdp::SetAttribute(const TChar* aKey, const TChar* aValue)
{
    iAttributeMap.Set(aKey, aValue);
}

void DviProtocolOdp::SetCustomData(const TChar* /*aTag*/, void* /*aData*/)
{
    ASSERTS(); // can't think why this'd be called
}

void DviProtocolOdp::GetResourceManagerUri(const NetworkAdapter& /*aAdapter*/, Brh& /*aUri*/)
{
    // don't supply any resources over Odp so leave aUri unchanged
}

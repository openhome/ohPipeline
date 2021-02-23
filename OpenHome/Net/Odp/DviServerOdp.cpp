#include <OpenHome/Net/Odp/DviServerOdp.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/TIpAddressUtils.h>
#include <OpenHome/Net/Private/DviServer.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Net/Odp/DviOdp.h>
#include <OpenHome/Net/Odp/Odp.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Debug-ohMediaPlayer.h>
#include <OpenHome/Private/NetworkAdapterList.h>

using namespace OpenHome;
using namespace OpenHome::Net;

// OdpDevice

const TChar* OdpDevice::kAdapterCookie = "OdpDevice";

OdpDevice::OdpDevice(Net::IMdnsProvider& aMdnsProvider, NetworkAdapter& aAdapter, Av::IFriendlyNameObservable& aFriendlyNameObservable, Endpoint& aEndpoint)
    : iProvider(aMdnsProvider)
    , iAdapter(aAdapter)
    , iFriendlyNameObservable(aFriendlyNameObservable)
    , iEndpoint(aEndpoint)
    , iHandle(iProvider.MdnsCreateService())
    , iRegistered(false)
    , iLock("ODPL")
{
    iAdapter.AddRef(kAdapterCookie);
    iFriendlyNameId = iFriendlyNameObservable.RegisterFriendlyNameObserver(MakeFunctorGeneric<const Brx&>(*this, &OdpDevice::NameChanged));   // Functor is called within this.
}

OdpDevice::~OdpDevice()
{
    iFriendlyNameObservable.DeregisterFriendlyNameObserver(iFriendlyNameId);
    Deregister();
    iAdapter.RemoveRef(kAdapterCookie);
}

void OdpDevice::Register()
{
    AutoMutex a(iLock);
    RegisterLocked();
}

void OdpDevice::Deregister()
{
    AutoMutex a(iLock);
    DeregisterLocked();
}

TBool OdpDevice::NetworkAdapterAndPortMatch(const NetworkAdapter& aAdapter, TUint aZeroConfPort) const
{
    if (TIpAddressUtils::Equals(aAdapter.Address(), iAdapter.Address())
        && TIpAddressUtils::Equals(aAdapter.Subnet(), iAdapter.Subnet())
        && (strcmp(aAdapter.Name(), iAdapter.Name()) == 0)
        && aZeroConfPort == iEndpoint.Port()) {
        return true;
    }
    return false;
}

void OdpDevice::RegisterLocked()
{
    Endpoint::EndpointBuf epBuf;
    iEndpoint.AppendEndpoint(epBuf);
    LOG(kBonjour, "OdpDevice::RegisterLocked iRegistered: %u, iEndpoint: %.*s\n", iRegistered, PBUF(epBuf));

    if (iRegistered || TIpAddressUtils::IsZero(iEndpoint.Address())) {
        return;
    }

    Bws<200> info;
    iProvider.MdnsAppendTxtRecord(info, "CPath", "/test.html");
    iProvider.MdnsRegisterService(iHandle, iName.PtrZ(), "_odp._tcp", iEndpoint.Address(), iEndpoint.Port(), info.PtrZ());
    iRegistered = true;
}

void OdpDevice::DeregisterLocked()
{
    if (!iRegistered) {
        return;
    }

    iProvider.MdnsDeregisterService(iHandle);
    iRegistered = false;
}

void OdpDevice::NameChanged(const Brx& aName)
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

// DviSessionOdp

const Brn DviSessionOdp::kUserAgentDefault("Odp");

DviSessionOdp::DviSessionOdp(DvStack& aDvStack, TIpAddress aAdapter)
    : iAdapter(aAdapter)
    , iWriteLock("Odp1")
    , iShutdownSem("Odp2", 1)
{
    iReadBuffer = new Srs<1024>(*this);
    iReaderUntil = new ReaderUntilS<kMaxReadBytes>(*iReadBuffer);
    iWriteBuffer = new Sws<kWriteBufferBytes>(*this);
    iProtocol = new DviOdp(aDvStack, *this);
}

DviSessionOdp::~DviSessionOdp()
{
    iReadBuffer->ReadInterrupt();
    iShutdownSem.Wait();
    iWriteLock.Wait();
    /* Nothing to do inside this lock.  Taking it after calling iProtocol->Disable() confirms
       that no evented update is currently using iWriteBuffer. */
    iWriteLock.Signal();
    delete iProtocol;
    delete iReaderUntil;
    delete iReadBuffer;
    delete iWriteBuffer;
}

void DviSessionOdp::Run()
{
    //LogVerbose(true);
    iShutdownSem.Wait();

    try {
        iProtocol->Announce();
        for (;;) {
            Brn request = iReaderUntil->ReadUntil(Ascii::kLf);
            try {
                iProtocol->Process(request);
            }
            catch (AssertionFailed&) {
                throw;
            }
            catch (Exception& ex) {
                LOG_ERROR(kBonjour, "DviSessionOdp::Run - %s parsing request:\n%.*s\n", ex.Message(), PBUF(request));
            }
        }
    }
    catch (AssertionFailed&) {
        throw;
    }
    catch (Exception&) {
    }

    iProtocol->Disable();
    iShutdownSem.Signal();
}

IWriter& DviSessionOdp::WriteLock()
{
    iWriteLock.Wait();
    return *iWriteBuffer;
}

void DviSessionOdp::WriteUnlock()
{
    iWriteLock.Signal();
}

void DviSessionOdp::WriteEnd()
{
    iWriteBuffer->Write(Ascii::kLf);
    iWriteBuffer->WriteFlush();
}

const TIpAddress& DviSessionOdp::Adapter() const
{
    return iAdapter;
}

const Brx& DviSessionOdp::ClientUserAgentDefault() const
{
    return kUserAgentDefault;
}


// DviServerOdp

DviServerOdp::DviServerOdp(DvStack& aDvStack, TUint aNumSessions, TUint aPort)
    : DviServer(aDvStack)
    , iNumSessions(aNumSessions)
    , iPort(aPort)
{
}

DviServerOdp::~DviServerOdp()
{
    Deinitialise();
}

void DviServerOdp::Start()
{
    Initialise();
}

TUint DviServerOdp::Port() const
{
    return iPort;
}

void DviServerOdp::SetServerCreatedCallback(Functor aCallback)
{
    iServerCreated = aCallback;
}

SocketTcpServer* DviServerOdp::CreateServer(const NetworkAdapter& aNif)
{
    auto server = new SocketTcpServer(iDvStack.Env(), "OdpServer", iPort, aNif.Address());
    if (iPort == 0) { 
        iPort = server->Port(); 
    }
    for (TUint i=0; i<iNumSessions; i++) {
        Bws<Thread::kMaxNameBytes+1> thName;
        thName.AppendPrintf("OdpSession%d", i);
        thName.PtrZ();
        auto session = new DviSessionOdp(iDvStack, aNif.Address());
        server->Add(reinterpret_cast<const TChar*>(thName.Ptr()), session);
    }

    return server;
}

void DviServerOdp::NotifyServerDeleted(const TIpAddress& /*aInterface*/) 
{ 
} 

void DviServerOdp::NotifyServerCreated(const TIpAddress& /*aInterface*/)
{
    if (iServerCreated) {
        iServerCreated();
    }
}

// OdpZeroConfDevices

OdpZeroConfDevices::OdpZeroConfDevices(Net::IMdnsProvider& aMdnsProvider, Av::IFriendlyNameObservable& aFriendlyNameObservable)
    : iMdnsProvider(aMdnsProvider)
    , iFriendlyNameObservable(aFriendlyNameObservable)
    , iEnabled(false)
    , iLock("ODPD")
{
    // Owner of this class must call NetworkAdaptersChanged() to initialise devices.
}

OdpZeroConfDevices::~OdpZeroConfDevices()
{
    AutoMutex _(iLock);
    for (TUint i=0; i<iDevices.size(); i++) {
        delete iDevices[i];
    }
}

void OdpZeroConfDevices::SetEnabled(TBool aEnabled)
{
    AutoMutex _(iLock);
    LOG(kBonjour, "OdpZeroConfDevices::SetEnabled aEnabled: %u, iEnabled: %u, iDevices.size(): %u\n", aEnabled, iEnabled, iDevices.size());
    if (aEnabled != iEnabled) {
        iEnabled = aEnabled;

        for (auto* d : iDevices) {
            if (iEnabled) {
                d->Register();
            }
            else {
                d->Deregister();
            }
        }
    }
}

void OdpZeroConfDevices::NetworkAdaptersChanged(std::vector<NetworkAdapter*>& aNetworkAdapters, Optional<NetworkAdapter>& aCurrent, TUint aZeroConfPort)
{
    NetworkAdapter* current = nullptr;
    if (aCurrent.Ok()) {
        current = &aCurrent.Unwrap();
    }

    AutoMutex _(iLock);
    // On interface change:
    // - if single interface selected:
    // -- remove all subnets that are not selected
    // -- if no subnets remain, add the current interface
    // - else:
    // -- remove all subnets that no longer exist
    // -- add new subnets

    if (current != NULL) {
        // Single interface selected. Register only on this interface.
        // First, remove any other interfaces.
        TUint i = 0;
        while (i<iDevices.size()) {
            if (!iDevices[i]->NetworkAdapterAndPortMatch(*current, aZeroConfPort)) {
                delete iDevices[i];
                iDevices.erase(iDevices.begin()+i);
            }
            else {
                i++;
            }
        }

        // If this interface isn't registered, add it.
        if (iDevices.size() == 0) {
            AddAdapterLocked(*current, aZeroConfPort);
        }
    }
    else {
        // No interface selected. Advertise on all interfaces.
        // First, remove any subnets that have disappeared from the new subnet list.
        TUint i = 0;
        while (i<iDevices.size()) {
            if (OdpDeviceAdapterInCurrentAdapters(*iDevices[i], aNetworkAdapters, aZeroConfPort) == -1) {
                delete iDevices[i];
                iDevices.erase(iDevices.begin()+i);
            }
            else {
                i++;
            }
        }

        // Then, add any new interfaces.
        for (TUint j=0; j<aNetworkAdapters.size(); j++) {
            NetworkAdapter* subnet = (aNetworkAdapters)[j];
            if (AdapterInCurrentOdpDeviceAdapters(*subnet, aZeroConfPort) == -1) {
                AddAdapterLocked(*subnet, aZeroConfPort);
            }
        }
    }
}

TInt OdpZeroConfDevices::AdapterInCurrentOdpDeviceAdapters(const NetworkAdapter& aAdapter, TUint aZeroConfPort)
{
    for (TUint i=0; i<(TUint)iDevices.size(); i++) {
        if (iDevices[i]->NetworkAdapterAndPortMatch(aAdapter, aZeroConfPort)) {
            return i;
        }
    }
    return -1;
}

TInt OdpZeroConfDevices::OdpDeviceAdapterInCurrentAdapters(const OdpDevice& aDevice, const std::vector<NetworkAdapter*>& aList, TUint aZeroConfPort)
{
    for (TUint i=0; i<(TUint)aList.size(); i++) {
        if (aDevice.NetworkAdapterAndPortMatch(*(aList[i]), aZeroConfPort)) {
            return i;
        }
    }
    return -1;
}

void OdpZeroConfDevices::AddAdapterLocked(NetworkAdapter& aAdapter, TUint aZeroConfPort)
{
    Endpoint::AddressBuf addrBuf;
    Endpoint::AppendAddress(addrBuf, aAdapter.Address());
    LOG(kBonjour, "OdpZeroConfDevices::AddAdapter %.*s, aZeroConfPort: %u, iEnabled: %u", PBUF(addrBuf), aZeroConfPort, iEnabled);

    Endpoint ep(aZeroConfPort, aAdapter.Address());
    OdpDevice* device = new OdpDevice(iMdnsProvider, aAdapter, iFriendlyNameObservable, ep);
    iDevices.push_back(device);

    if (iEnabled) {
        device->Register();
    }
}

// OdpZeroConf

OdpZeroConf::OdpZeroConf(Environment& aEnv, DviServerOdp& aServerOdp, Av::IFriendlyNameObservable& aFriendlyNameObservable)
    : iEnv(aEnv)
    , iZeroConfServer(aServerOdp)
    , iZeroConfDevices(*iEnv.MdnsProvider(), aFriendlyNameObservable)
    , iEnabled(false)
    , iLock("SZCL")
{
    Functor functor = MakeFunctor(*this, &OdpZeroConf::OdpServerCreated);
    iZeroConfServer.SetServerCreatedCallback(functor);

    // An initial callback is not made after adding the listener above.
    // So, call the callback method here to initialise adapters.
    OdpServerCreated();
}

OdpZeroConf::~OdpZeroConf()
{
}

void OdpZeroConf::SetZeroConfEnabled(TBool aEnabled)
{
    AutoMutex _(iLock);
    LOG(kBonjour, "OdpZeroConf::SetZeroConfEnabled aEnabled: %u, iEnabled: %u\n", aEnabled, iEnabled);
    if (aEnabled != iEnabled) {
        iEnabled = aEnabled;
        iZeroConfDevices.SetEnabled(iEnabled);
    }
}

void OdpZeroConf::OdpServerCreated()
{
    AutoMutex _(iLock);
    NetworkAdapterList& nifList = iEnv.NetworkAdapterList();
    std::vector<NetworkAdapter*>* subnetList = nifList.CreateSubnetList();
    AutoNetworkAdapterRef ref(iEnv, "OdpZeroConf::HandleInterfaceChange");
    NetworkAdapter* current = ref.Adapter();

    /*
     * The zeroconf server and MDNS devices must be updated simultaneously,
     * using the same network adapter list and current adapter.
     *
     * The MDNS devices are required to know what port the zeroconf server
     * is advertising on, so the zeroconf server must be updated first.
     */
    Optional<NetworkAdapter> optCurrent(current);
    const TUint port = iZeroConfServer.Port();
    iZeroConfDevices.NetworkAdaptersChanged(*subnetList, optCurrent, port);

    NetworkAdapterList::DestroySubnetList(subnetList);
}
#include <OpenHome/Net/Odp/DviServerOdp.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Network.h>
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
                LOG_ERROR(kOdp, "DviSessionOdp::Run - %s parsing request:\n%.*s\n", ex.Message(), PBUF(request));
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

TIpAddress DviSessionOdp::Adapter() const
{
    return iAdapter;
}

const Brx& DviSessionOdp::ClientUserAgentDefault() const
{
    return kUserAgentDefault;
}


// DviServerOdp

DviServerOdp::DviServerOdp(DvStack& aDvStack, Av::IFriendlyNameObservable& aFriendlyNameObservable, TUint aNumSessions, TUint aPort)
    : DviServer(aDvStack)
    , iNumSessions(aNumSessions)
    , iPort(aPort)
    , iEnv(aDvStack.Env())
    , iProvider(*iEnv.MdnsProvider())
    , iFriendlyNameObservable(aFriendlyNameObservable)
    , iHandleOdp(iProvider.MdnsCreateService())
    , iRegistered(false)
    , iLock("ODPL")
{
    Initialise();

    // FIXME: See ZeroConf for handling multiple adapters
    NetworkAdapterList& adapterList = iEnv.NetworkAdapterList();
    Functor functor = MakeFunctor(*this, &DviServerOdp::HandleInterfaceChange);
    iCurrentAdapterChangeListenerId = adapterList.AddCurrentChangeListener(functor, "DviServerOdp-current");
    iSubnetListChangeListenerId = adapterList.AddSubnetListChangeListener(functor, "DviServerOdp-subnet");

    iFriendlyNameId = iFriendlyNameObservable.RegisterFriendlyNameObserver(MakeFunctorGeneric<const Brx&>(*this, &DviServerOdp::NameChanged));
    HandleInterfaceChange();
}

DviServerOdp::~DviServerOdp()
{
    Deinitialise();

    iFriendlyNameObservable.DeregisterFriendlyNameObserver(iFriendlyNameId);
    Deregister();

    iEnv.NetworkAdapterList().RemoveCurrentChangeListener(iCurrentAdapterChangeListenerId);
    iEnv.NetworkAdapterList().RemoveSubnetListChangeListener(iSubnetListChangeListenerId);
}

TUint DviServerOdp::Port() const
{
    return iPort;
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

void DviServerOdp::NotifyServerDeleted(TIpAddress /*aInterface*/)
{
}

void DviServerOdp::HandleInterfaceChange()
{
    AutoMutex a(iLock);
    NetworkAdapterList& adapterList = iEnv.NetworkAdapterList();
    std::vector<NetworkAdapter*>* subnetList = adapterList.CreateSubnetList();
    AutoNetworkAdapterRef ref(iEnv, "DviServerOdp HandleInterfaceChange");
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
    }
    RegisterLocked();
}

void DviServerOdp::Register()
{
    AutoMutex a(iLock);
    RegisterLocked();
}

void DviServerOdp::Deregister()
{
    AutoMutex a(iLock);
    DeregisterLocked();
}

void DviServerOdp::RegisterLocked()
{
    if (iRegistered || iEndpoint.Address() == 0) {
        return;
    }
    iEndpoint.SetPort(iPort);

    Bws<Endpoint::kMaxAddressBytes> addr;
    Endpoint::AppendAddress(addr, iEndpoint.Address());
    //Log::Print("Odp Endpoint (%.*s): %.*s:%d\n", PBUF(iName), PBUF(addr), iEndpoint.Port());

    Bws<200> info;
    iProvider.MdnsAppendTxtRecord(info, "CPath", "/test.html");
    iProvider.MdnsRegisterService(iHandleOdp, iName.PtrZ(), "_odp._tcp", iEndpoint.Address(), iEndpoint.Port(), info.PtrZ());
    iRegistered = true;
}

void DviServerOdp::DeregisterLocked()
{
    if (!iRegistered) {
        return;
    }
    iProvider.MdnsDeregisterService(iHandleOdp);
    iRegistered = false;
}

void DviServerOdp::NameChanged(const Brx& aName)
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

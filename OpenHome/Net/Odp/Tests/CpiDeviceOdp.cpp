#include <OpenHome/Net/Odp/Tests/CpiDeviceOdp.h>
#include <OpenHome/Net/Odp/Odp.h>
#include <OpenHome/Net/Private/CpiDevice.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Json.h>
#include <OpenHome/Net/Private/Service.h>
#include <OpenHome/Net/Private/CpiService.h>
#include <OpenHome/Net/Private/CpiSubscription.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Debug-ohMediaPlayer.h>
#include <OpenHome/Private/NetworkAdapterList.h>

#include <vector>

using namespace OpenHome;
using namespace OpenHome::Net;

// CpiDeviceOdp

CpiDeviceOdp::CpiDeviceOdp(CpStack& aCpStack, Endpoint aLocation, const Brx& aAlias, Functor aStateChanged)
    : iCpStack(aCpStack)
    , iLock("CLP1")
    , iLocation(aLocation)
    , iAlias(aAlias)
    , iStateChanged(aStateChanged)
    , iDevice(nullptr)
    , iResponseHandler(nullptr)
    , iConnected(false)
    , iExiting(false)
    , iDeviceConnected("SODP", 0)
{
    iReadBuffer = new Srs<1024>(iSocket);
    iReaderUntil = new ReaderUntilS<kMaxReadBufferBytes>(*iReadBuffer);
    iWriteBuffer = new Sws<kMaxWriteBufferBytes>(iSocket);
    iThread = new ThreadFunctor("OdpClient", MakeFunctor(*this, &CpiDeviceOdp::OdpReaderThread));
    iInvocable = new CpiOdpInvocable(*this);
    iThread->Start();
    // to accomadate a device list, constructor needs to provide the cpidevice in a ready state
    try {
        iDeviceConnected.Wait(5*1000);
    }
    catch (Timeout&) {
        // device will be null, should be ignored
    }
}

CpiDeviceOdp::~CpiDeviceOdp()
{
    iExiting = true;
    iReadBuffer->ReadInterrupt();
    delete iThread;
    delete iInvocable;
    delete iWriteBuffer;
    delete iReaderUntil;
    delete iReadBuffer;
    iSocket.Close();
}

void CpiDeviceOdp::Destroy()
{
    iLock.Wait();
    iStateChanged = Functor();
    iDevice->RemoveRef();
    iLock.Signal();
}

CpiDevice* CpiDeviceOdp::Device()
{
    return iDevice;
}

TBool CpiDeviceOdp::Connected() const
{
    return iConnected;
}

void CpiDeviceOdp::OdpReaderThread()
{
    try {
        iSocket.Open(iCpStack.Env());
        iSocket.Connect(iLocation, iCpStack.Env().InitParams()->TcpConnectTimeoutMs());
        for (;;) {
            Brn line = iReaderUntil->ReadUntil(Ascii::kLf);
            JsonParser parser;
            parser.Parse(line);
            Brn type = parser.String(Odp::kKeyType);
            if (!iConnected) {
                if (type != Odp::kTypeAnnouncement) {
                    LOG_ERROR(kOdp, "Odp: no announcement on connect\n");
                    THROW(ReaderError);
                }

                // We don't have a proper parser for json arrays.  Quick/dirty alternative follows...
                // Note that this relies on each device listing "id" before "type"
                Parser p(parser.String(Odp::kKeyDevices));
                Brn udn;
                for (;;) {
                    if (p.Finished()) {
                        LOG_ERROR(kOdp, "Odp: unable to find device %.*s, exiting thread\n", PBUF(iAlias));
                        if (iStateChanged) {
                            iStateChanged();
                        }
                        THROW(ReaderError);
                    }
                    Brn buf = p.Next('\"');
                    if (buf == Odp::kKeyId) {
                        (void)p.Next('\"');
                        udn.Set(p.Next('\"'));
                    }
                    else if (buf == Odp::kKeyAlias) {
                        (void)p.Next('\"');
                        buf.Set(p.Next('\"'));
                        if (buf == iAlias) {
                            break;
                        }
                    }
                }

                iDevice = new CpiDevice(iCpStack, udn, *this, *this, nullptr);
                iConnected = true;
                if (iStateChanged) {
                    iStateChanged();
                }
                iDeviceConnected.Signal();
            }
            else if (type == Odp::kTypeNotify) {
                HandleEventedUpdate(parser);
            }
            else if (iResponseHandler == nullptr || !iResponseHandler->HandleOdpResponse(parser)) {
                LOG_ERROR(kOdp, "Unexpected Odp message: %.*s\n", PBUF(line));
            }
        }
    }
    catch (AssertionFailed&) {
        throw;
    }
    catch (ReaderError&) {
        if (!iExiting) {
            LogError("ReaderError");
        }
    }
    catch (Exception& ex) {
        LogError(ex.Message());
    }
    iConnected = false;
    iDeviceConnected.Signal();
}

void CpiDeviceOdp::LogError(const TChar* aError)
{
    LOG_ERROR(kOdp, "Odp: error %s for device %.*s, exiting thread\n", aError, PBUF(iAlias));
    if (iStateChanged) {
        iStateChanged();
    }
}

void CpiDeviceOdp::HandleEventedUpdate(JsonParser& aParser)
{
    Brn sid = aParser.String(Odp::kKeySid);
    CpiSubscription* subscription = iCpStack.SubscriptionManager().FindSubscription(sid);
    if (subscription == nullptr) {
        LOG_ERROR(kOdp, "Odp: event from unknown subscription - %.*s\n", PBUF(sid));
        return;
    }
    Brn propsBuf = aParser.String(Odp::kKeyProperties);
    auto parserProps = JsonParserArray::Create(propsBuf);
    subscription->UpdateSequenceNumber();
    IEventProcessor* processor = static_cast<IEventProcessor*>(subscription);
    processor->EventUpdateStart();
    CpiOdpOutputProcessor outputProcessor;

    try {
        for (;;) {
            JsonParser parserProp;
            Brn obj(parserProps.NextObject());
            parserProp.Parse(obj);
            Brn propName = parserProp.String(Odp::kKeyName);
            Brn propVal;
            if (!parserProp.IsNull(Odp::kKeyValue)) {
                propVal.Set(parserProp.String(Odp::kKeyValue));
            }
            processor->EventUpdate(propName, propVal, outputProcessor);
        }
    }
    catch (JsonArrayEnumerationComplete&) {}

    processor->EventUpdateEnd();
    subscription->Unlock();
    subscription->RemoveRef();
}

void CpiDeviceOdp::InvokeAction(Invocation& aInvocation)
{
    aInvocation.SetInvoker(*iInvocable);
    iCpStack.InvocationManager().Invoke(&aInvocation);
}

TBool CpiDeviceOdp::GetAttribute(const TChar* /*aKey*/, Brh& /*aValue*/) const
{
    // Not obviously required.  The only attribute Odp devices have is their name and we pass this to the c'tor
    return false;
}

TUint CpiDeviceOdp::Subscribe(CpiSubscription& aSubscription, const Uri& /*aSubscriber*/)
{
    CpiOdpSubscriber subscriber(*this);
    subscriber.Subscribe(aSubscription);
    return kSubscriptionDurationSecs;
}

TUint CpiDeviceOdp::Renew(CpiSubscription& /*aSubscription*/)
{
    return kSubscriptionDurationSecs;
}

void CpiDeviceOdp::Unsubscribe(CpiSubscription& /*aSubscription*/, const Brx& aSid)
{
    CpiOdpUnsubscriber unsubscriber(*this);
    unsubscriber.Unsubscribe(aSid);
}

TBool CpiDeviceOdp::OrphanSubscriptionsOnSubnetChange() const
{
    return true;
}

void CpiDeviceOdp::NotifyRemovedBeforeReady()
{
}

TUint CpiDeviceOdp::Version(const TChar* /*aDomain*/, const TChar* /*aName*/, TUint aProxyVersion) const
{
    return aProxyVersion; // FIXME - could store list of remote services and lookup on that
}

void CpiDeviceOdp::Release()
{
    delete this;
}

IWriter& CpiDeviceOdp::WriteLock(ICpiOdpResponse& aResponseHandler)
{
    iLock.Wait();
    iResponseHandler = &aResponseHandler;
    return *iWriteBuffer;
}

void CpiDeviceOdp::WriteUnlock()
{
    iResponseHandler = nullptr;
    iLock.Signal();
}

void CpiDeviceOdp::WriteEnd(IWriter& aWriter)
{
    aWriter.Write(Ascii::kLf);
    aWriter.WriteFlush();
}

const Brx& CpiDeviceOdp::Alias() const
{
    return iAlias;
}

// CpiDeviceListOdp
CpiDeviceListOdp::CpiDeviceListOdp(CpStack& aCpStack, FunctorCpiDevice aAdded, FunctorCpiDevice aRemoved)
    : CpiDeviceList(aCpStack, aAdded, aRemoved)
    , iSsdpLock("CDLO")
    , iEnv(aCpStack.Env())
    , iStarted(false)
    , iNoRemovalsFromRefresh(false)
{

    // FIXME: cleanout un-needed upnp stuff, but for now it works with mdns

    NetworkAdapterList& ifList = aCpStack.Env().NetworkAdapterList();
    AutoNetworkAdapterRef ref(iEnv, "CpiDeviceListOdp ctor");
    const NetworkAdapter* current = ref.Adapter();
    iRefreshTimer = new Timer(iEnv, MakeFunctor(*this, &CpiDeviceListOdp::RefreshTimerComplete), "DeviceListRefresh");
    iResumedTimer = new Timer(iEnv, MakeFunctor(*this, &CpiDeviceListOdp::ResumedTimerComplete), "DeviceListResume");
    iRefreshRepeatCount = 0;
    iInterfaceChangeListenerId = ifList.AddCurrentChangeListener(MakeFunctor(*this, &CpiDeviceListOdp::CurrentNetworkAdapterChanged), "CpiDeviceListOdp-current");
    iSubnetListChangeListenerId = ifList.AddSubnetListChangeListener(MakeFunctor(*this, &CpiDeviceListOdp::SubnetListChanged), "CpiDeviceListOdp-subnet");
    iSsdpLock.Wait();
    if (current == NULL) {
        iInterface = 0;
        iUnicastListener = NULL;
        iMulticastListener = NULL;
        iNotifyHandlerId = 0;
    }
    else {
        iInterface = current->Address();
        iUnicastListener = new SsdpListenerUnicast(iCpStack.Env(), *this, iInterface);
        iMulticastListener = &(iCpStack.Env().MulticastListenerClaim(iInterface));
        iNotifyHandlerId = iMulticastListener->AddNotifyHandler(this);
    }
    iSsdpLock.Signal();
    iCpStack.Env().AddResumeObserver(*this);

    iCpStack.Env().MdnsProvider()->AddMdnsDeviceListener(*this);
    iCpStack.Env().MdnsProvider()->FindDevices("_odp._tcp");
}

CpiDeviceListOdp::~CpiDeviceListOdp()
{
    //StopListeners();
    iCpStack.Env().RemoveResumeObserver(*this);
    iResumedTimer->Cancel();
    iLock.Wait();
    iActive = false;
    iLock.Signal();
    iCpStack.Env().NetworkAdapterList().RemoveCurrentChangeListener(iInterfaceChangeListenerId);
    iCpStack.Env().NetworkAdapterList().RemoveSubnetListChangeListener(iSubnetListChangeListenerId);
    /*iLock.Wait();
    CpDeviceMap::iterator it = iMap.begin();
    while (it != iMap.end()) {
        reinterpret_cast<CpiDeviceOdp*>(it->second->OwnerData())->InterruptXmlFetch();
        it++;
    }
    for (TUint i=0; i<(TUint)iPendingRemove.size(); i++) {
        reinterpret_cast<CpiDeviceOdp*>(iPendingRemove[i]->OwnerData())->InterruptXmlFetch();
    }
    iLock.Signal();*/
    delete iRefreshTimer;
    delete iResumedTimer;
}

void CpiDeviceListOdp::DeviceAdded(const Brx& /*aFriendlyName*/, const Brx& /*aUglyName*/, const Brx&  aIpAddress, TUint aPort)
{
    Endpoint* ep = new Endpoint(aPort, aIpAddress);
    CpiDeviceOdp* dev = new CpiDeviceOdp(iCpStack, *ep, Brn("Ds"), MakeFunctor(*this, &CpiDeviceListOdp::DeviceReady));
    if (dev != nullptr) {
        Add(dev->Device());  
    } 
}

void CpiDeviceListOdp::DeviceReady()
{
}

void CpiDeviceListOdp::StopListeners()
{
    iSsdpLock.Wait();
    SsdpListenerUnicast* uListener = iUnicastListener;
    iUnicastListener = NULL;
    iSsdpLock.Signal();
    if (uListener != NULL) {
        delete uListener;
        if (iMulticastListener != NULL) {
            iMulticastListener->RemoveNotifyHandler(iNotifyHandlerId);
            iNotifyHandlerId = 0;
            iCpStack.Env().MulticastListenerRelease(iInterface);
            iMulticastListener = NULL;
        }
    }
}

TBool CpiDeviceListOdp::Update(const Brx& /*aUdn*/, const Brx& aLocation, TUint /*aMaxAge*/)
{
    if (!IsLocationReachable(aLocation)) {
        return false;
    }
    /*iLock.Wait();
    CpiDevice* device = RefDeviceLocked(aUdn);
    if (device != NULL) {
        CpiDeviceOdp* deviceOdp = reinterpret_cast<CpiDeviceOdp*>(device->OwnerData());
        if (deviceOdp->Location() != aLocation) {
            // Device appears to have moved to a new location.
            //   Ask it to check whether the old location is still contactable.  If it is,
            //   stick with the older location; if it isn't, remove the old device and add
            //   a new one.
            iLock.Signal();
            CpiDeviceOdp* newDevice = new CpiDeviceOdp(iCpStack, aUdn, aLocation, aMaxAge, *this, *this);
            deviceOdp->CheckStillAvailable(newDevice);
            device->RemoveRef();
            return true;
        }
        deviceOdp->UpdateMaxAge(aMaxAge);
        iLock.Signal();
        device->RemoveRef();
        return !iRefreshing;
    }
    iLock.Signal();*/
    return false;
}

void CpiDeviceListOdp::DoStart()
{
    iActive = true;
    iLock.Wait();
    TBool needsStart = !iStarted;
    iStarted = true;
    iLock.Signal();
    if (needsStart) {
        AutoMutex a(iSsdpLock);
        if (iUnicastListener != NULL) {
            iUnicastListener->Start();
        }
    }
}

void CpiDeviceListOdp::Start()
{
    Refresh();
}

void CpiDeviceListOdp::Refresh()
{
    if (StartRefresh()) {
        return;
    }
    Mutex& lock = iCpStack.Env().Mutex();
    lock.Wait();
    /* Always attempt multiple refreshes.
        Poor quality wifi (particularly on iOS) means that we risk MSEARCHes not being sent otherwise. */
    iRefreshRepeatCount = kRefreshRetries;
    lock.Signal();
    DoRefresh();
}

void CpiDeviceListOdp::DoRefresh()
{
    Start();
    TUint delayMs = iCpStack.Env().InitParams()->MsearchTimeSecs() * 1000;
    delayMs += 500; /* allow slightly longer to cope with wifi delays and devices
                       which send out Alive messages at the last possible moment */
    iRefreshTimer->FireIn(delayMs);
    /*  during refresh...
            on every Add():
                add to iRefreshMap
                check device against iMap, adding it and reporting as new if necessary
        on completion...
            on timer expiry:
                check each item in iMap
                    if it appears in iRefreshMap do nowt
                    else remove it from iMap and report this to observer */
}

TBool CpiDeviceListOdp::IsDeviceReady(CpiDevice& /*aDevice*/)
{
    //reinterpret_cast<CpiDeviceOdp*>(aDevice.OwnerData())->FetchXml();
    return false;
}

TBool CpiDeviceListOdp::IsLocationReachable(const Brx& aLocation) const
{
    /* linux's filtering of multicast messages appears to be buggy and messages
       received by all interfaces are sometimes delivered to sockets which are
       bound to / members of a group on a single (different) interface.  It'd be
       more correct to filter these out in SsdpListenerMulticast but that would
       require API changes which would be more inconvenient than just moving the
       filtering here.
       This should be reconsidered if we ever add more clients of SsdpListenerMulticast */
    TBool reachable = false;
    Uri uri;
    try {
        uri.Replace(aLocation);
    }
    catch (UriError&) {
        return false;
    }
    iLock.Wait();
    Endpoint endpt(0, uri.Host());
    NetworkAdapter* nif = iCpStack.Env().NetworkAdapterList().CurrentAdapter("CpiDeviceListOdp::IsLocationReachable");
    if (nif != NULL) {
        if (nif->Address() == iInterface && nif->ContainsAddress(endpt.Address())) {
            reachable = true;
        }
        nif->RemoveRef("CpiDeviceListOdp::IsLocationReachable");
    }
    iLock.Signal();
    return reachable;
}

void CpiDeviceListOdp::RefreshTimerComplete()
{
    if (--iRefreshRepeatCount == 0) {
        RefreshComplete(!iNoRemovalsFromRefresh);
        iNoRemovalsFromRefresh = false;
    }
    else {
        DoRefresh();
    }
}

void CpiDeviceListOdp::ResumedTimerComplete()
{
    iNoRemovalsFromRefresh = iEnv.InitParams()->IsHostUdpLowQuality();
    Refresh();
}

void CpiDeviceListOdp::CurrentNetworkAdapterChanged()
{
    HandleInterfaceChange();
}

void CpiDeviceListOdp::SubnetListChanged()
{
    HandleInterfaceChange();
}

void CpiDeviceListOdp::HandleInterfaceChange()
{
    NetworkAdapter* current = iCpStack.Env().NetworkAdapterList().CurrentAdapter("CpiDeviceListOdp::HandleInterfaceChange");
    if (current != NULL && current->Address() == iInterface) {
        // list of subnets has changed but our interface is still available so there's nothing for us to do here
        current->RemoveRef("CpiDeviceListOdp::HandleInterfaceChange");
        return;
    }
    StopListeners();

    if (current == NULL) {
        iLock.Wait();
        iInterface = 0;
        iLock.Signal();
        RemoveAll();
        return;
    }

    // we used to only remove devices for subnet changes
    // its not clear why this was correct - any interface change will result in control/event urls changing
    RemoveAll();
    
    iLock.Wait();
    iInterface = current->Address();
    iLock.Signal();
    current->RemoveRef("CpiDeviceListOdp::HandleInterfaceChange");

    {
        AutoMutex a(iSsdpLock);
        iUnicastListener = new SsdpListenerUnicast(iCpStack.Env(), *this, iInterface);
        iUnicastListener->Start();
        iMulticastListener = &(iCpStack.Env().MulticastListenerClaim(iInterface));
        iNotifyHandlerId = iMulticastListener->AddNotifyHandler(this);
    }
    Refresh();
}

void CpiDeviceListOdp::RemoveAll()
{
    iRefreshTimer->Cancel();
    CancelRefresh();
    iLock.Wait();
    std::vector<CpiDevice*> devices;
    CpDeviceMap::iterator it = iMap.begin();
    while (it != iMap.end()) {
        devices.push_back(it->second);
        it->second->AddRef();
        it++;
    }
    iLock.Signal();
    for (TUint i=0; i<(TUint)devices.size(); i++) {
        Remove(devices[i]->Udn());
        devices[i]->RemoveRef();
    }
}

void CpiDeviceListOdp::XmlFetchCompleted(CpiDeviceOdp& /*aDevice*/, TBool /*aError*/)
{
    /*if (aError) {
        const Brx& udn = aDevice.Udn();
        const Brx& location = aDevice.Location();
        LOG_ERROR(kDevice, "Device xml fetch error {udn{%.*s}, location{%.*s}}\n",
                             PBUF(udn), PBUF(location));
        Remove(aDevice.Udn());
    }
    else {
        SetDeviceReady(aDevice.Device());
    }*/
}

void CpiDeviceListOdp::DeviceLocationChanged(CpiDeviceOdp* /*aOriginal*/, CpiDeviceOdp* /*aNew*/)
{
    /*Remove(aOriginal->Udn());
    Add(&aNew->Device());*/
}

void CpiDeviceListOdp::SsdpNotifyRootAlive(const Brx& aUuid, const Brx& aLocation, TUint aMaxAge)
{
    (void)Update(aUuid, aLocation, aMaxAge);
}

void CpiDeviceListOdp::SsdpNotifyUuidAlive(const Brx& aUuid, const Brx& aLocation, TUint aMaxAge)
{
    (void)Update(aUuid, aLocation, aMaxAge);
}

void CpiDeviceListOdp::SsdpNotifyDeviceTypeAlive(const Brx& aUuid, const Brx& /*aDomain*/, const Brx& /*aType*/,
                                                 TUint /*aVersion*/, const Brx& aLocation, TUint aMaxAge)
{
    (void)Update(aUuid, aLocation, aMaxAge);
}

void CpiDeviceListOdp::SsdpNotifyServiceTypeAlive(const Brx& aUuid, const Brx& /*aDomain*/, const Brx& /*aType*/,
                                                  TUint /*aVersion*/, const Brx& aLocation, TUint aMaxAge)
{
    (void)Update(aUuid, aLocation, aMaxAge);
}

void CpiDeviceListOdp::SsdpNotifyRootByeBye(const Brx& aUuid)
{
    Remove(aUuid);
}

void CpiDeviceListOdp::SsdpNotifyUuidByeBye(const Brx& aUuid)
{
    Remove(aUuid);
}

void CpiDeviceListOdp::SsdpNotifyDeviceTypeByeBye(const Brx& aUuid, const Brx& /*aDomain*/, const Brx& /*aType*/, TUint /*aVersion*/)
{
    Remove(aUuid);
}

void CpiDeviceListOdp::SsdpNotifyServiceTypeByeBye(const Brx& aUuid, const Brx& /*aDomain*/, const Brx& /*aType*/, TUint /*aVersion*/)
{
    Remove(aUuid);
}

void CpiDeviceListOdp::NotifyResumed()
{
    /* UDP sockets don't seem usable immediately after we resume
       ...so wait a short while before doing anything */
    iResumedTimer->FireIn(kResumeDelayMs);
}

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

CpiDeviceOdp::CpiDeviceOdp(CpStack& aCpStack, MdnsDevice& aDev, const Brx& aAlias, Functor aStateChanged)
    : iCpStack(aCpStack)
    , iLock("CLP1")
    , iAlias(aAlias)
    , iStateChanged(aStateChanged)
    , iDevice(nullptr)
    , iResponseHandler(nullptr)
    , iConnected(false)
    , iExiting(false)
    , iDeviceConnected("SODP", 0)
    , iFriendlyName(aDev.FriendlyName())
    , iUglyName(aDev.UglyName())
    , iIpAddress(aDev.IpAddress())
    , iMdnsType(aDev.Type())
    , iPort(aDev.Port())
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
        Endpoint ep(iPort, iIpAddress);
        iSocket.Connect(ep, iCpStack.Env().InitParams()->TcpConnectTimeoutMs());
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

TBool CpiDeviceOdp::GetAttribute(const char* aKey, Brh& aValue) const
{
    Brn key(aKey);
    
    Parser parser(key);
    
    if (parser.Next('.') == Brn("Odp")) {
        Brn property = parser.Remaining();

        if (property == Brn("FriendlyName")) {
            aValue.Set(iFriendlyName);
            return (true);
        }
        if (property == Brn("Type")) {
            aValue.Set(iMdnsType);
            return (true);
        }
        if (property == Brn("Location")) {
            Bws<30> loc(iIpAddress);
            loc.Append(":");
            Ascii::AppendDec(loc, iPort);
            aValue.Set(loc);
            return (true);
        }
        if (property == Brn("UglyName")) {
            aValue.Set(iUglyName);
            return (true);
        }
    }

    const Brx& Type();
    const Brx& FriendlyName();
    const Brx& UglyName();
    const Brx& IpAddress();
    const TUint Port();

    return (false);
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
    , iEnv(aCpStack.Env())
    , iStarted(false)
    , iNoRemovalsFromRefresh(false)
{

    NetworkAdapterList& ifList = aCpStack.Env().NetworkAdapterList();
    AutoNetworkAdapterRef ref(iEnv, "CpiDeviceListOdp ctor");
    const NetworkAdapter* current = ref.Adapter();
    iRefreshTimer = new Timer(iEnv, MakeFunctor(*this, &CpiDeviceListOdp::RefreshTimerComplete), "DeviceListRefresh");
    iResumedTimer = new Timer(iEnv, MakeFunctor(*this, &CpiDeviceListOdp::ResumedTimerComplete), "DeviceListResume");
    iRefreshRepeatCount = 0;
    iInterfaceChangeListenerId = ifList.AddCurrentChangeListener(MakeFunctor(*this, &CpiDeviceListOdp::CurrentNetworkAdapterChanged), "CpiDeviceListOdp-current");
    iSubnetListChangeListenerId = ifList.AddSubnetListChangeListener(MakeFunctor(*this, &CpiDeviceListOdp::SubnetListChanged), "CpiDeviceListOdp-subnet");
    if (current == NULL) {
        iInterface = 0;
    }
    else {
        iInterface = current->Address();
    }
    iCpStack.Env().AddResumeObserver(*this);

    iCpStack.Env().MdnsProvider()->AddMdnsDeviceListener(this);
}

CpiDeviceListOdp::~CpiDeviceListOdp()
{
    iCpStack.Env().RemoveResumeObserver(*this);
    iResumedTimer->Cancel();
    iLock.Wait();
    iActive = false;
    iLock.Signal();
    iCpStack.Env().NetworkAdapterList().RemoveCurrentChangeListener(iInterfaceChangeListenerId);
    iCpStack.Env().NetworkAdapterList().RemoveSubnetListChangeListener(iSubnetListChangeListenerId);
    delete iRefreshTimer;
    delete iResumedTimer;
}

void CpiDeviceListOdp::DeviceAdded(MdnsDevice& aDev)
{
    CpiDeviceOdp* dev = new CpiDeviceOdp(iCpStack, aDev, Brn("Ds"), MakeFunctor(*this, &CpiDeviceListOdp::DeviceReady));
    if (dev != nullptr) {
        Add(dev->Device());  
    } 
}

void CpiDeviceListOdp::DeviceReady()
{
}

void CpiDeviceListOdp::DoStart()
{
    iActive = true;
    iLock.Wait();
    iStarted = true;
    iLock.Signal();
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
    return true;
}

TBool CpiDeviceListOdp::IsLocationReachable(const Brx& aLocation) const
{
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


void CpiDeviceListOdp::NotifyResumed()
{
    /* UDP sockets don't seem usable immediately after we resume
       ...so wait a short while before doing anything */
    iResumedTimer->FireIn(kResumeDelayMs);
}

// CpiDeviceListOdpAll

CpiDeviceListOdpAll::CpiDeviceListOdpAll(CpStack& aCpStack, FunctorCpiDevice aAdded, FunctorCpiDevice aRemoved)
    : CpiDeviceListOdp(aCpStack, aAdded, aRemoved)
    , iCpStack(aCpStack)
{
}

CpiDeviceListOdpAll::~CpiDeviceListOdpAll()
{
}

void CpiDeviceListOdpAll::Start()
{
    CpiDeviceListOdp::DoStart();
    iCpStack.Env().MdnsProvider()->FindDevices("_odp._tcp");
}

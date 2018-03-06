#pragma once

#include <OpenHome/Net/Odp/CpiOdp.h>
#include <OpenHome/Net/Private/CpiDevice.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Net/Private/MdnsProvider.h>

namespace OpenHome {
    class JsonParser;
namespace Net {
    class CpiSubscription;

class CpiDeviceOdp : private ICpiProtocol
                   , private ICpiDeviceObserver
                   , private ICpiOdpDevice
{
    static const TUint kSubscriptionDurationSecs = 60 * 60 * 24; // arbitrarily chosen largish value
public:
    CpiDeviceOdp(CpStack& aCpStack,
                 MdnsDevice& aDev,
                 const Brx& aAlias,
                 Functor aStateChanged);
    void Destroy();
    CpiDevice* Device();
    TBool Connected() const;
private:
    ~CpiDeviceOdp();
    void OdpReaderThread();
    void LogError(const TChar* aError);
    void HandleEventedUpdate(JsonParser& aParser);
private: // from ICpiProtocol
    void InvokeAction(Invocation& aInvocation) override;
    TBool GetAttribute(const TChar* aKey, Brh& aValue) const override;
    TUint Subscribe(CpiSubscription& aSubscription, const Uri& aSubscriber) override;
    TUint Renew(CpiSubscription& aSubscription) override;
    void Unsubscribe(CpiSubscription& aSubscription, const Brx& aSid) override;
    TBool OrphanSubscriptionsOnSubnetChange() const override;
    void NotifyRemovedBeforeReady() override;
    TUint Version(const TChar* aDomain, const TChar* aName, TUint aProxyVersion) const override;
private: // from ICpiDeviceObserver
    void Release() override;
private: // from ICpiOdpDevice
    IWriter& WriteLock(ICpiOdpResponse& aResponseHandler) override;
    void WriteUnlock() override;
    void WriteEnd(IWriter& aWriter) override;
    const Brx& Alias() const override;
private:
    static const TUint kMaxReadBufferBytes = 100 * 1024;
    static const TUint kMaxWriteBufferBytes = 12 * 1024;

    CpStack& iCpStack;
    Mutex iLock;
    SocketTcpClient iSocket;
    Srx* iReadBuffer;
    ReaderUntilS<kMaxReadBufferBytes>* iReaderUntil;
    Sws<kMaxWriteBufferBytes>* iWriteBuffer;
    Bws<64> iAlias;
    Functor iStateChanged;
    CpiDevice* iDevice;
    ThreadFunctor* iThread;
    IInvocable* iInvocable;
    ICpiOdpResponse* iResponseHandler;
    TBool iConnected;
    TBool iExiting;
    Semaphore iDeviceConnected;
    Bws<64> iFriendlyName;
    Bws<64> iUglyName;
    Bws<64> iIpAddress;
    Bws<64> iMdnsType;
    TUint iPort;
};

class CpiDeviceListOdp : public CpiDeviceList, public ISsdpNotifyHandler, private IResumeObserver, private IMdnsDeviceListener
{
public:
    void XmlFetchCompleted(CpiDeviceOdp& aDevice, TBool aError);
    void DeviceLocationChanged(CpiDeviceOdp* aOriginal, CpiDeviceOdp* aNew);

    CpiDeviceListOdp(CpStack& aCpStack, FunctorCpiDevice aAdded, FunctorCpiDevice aRemoved);
    ~CpiDeviceListOdp();
protected:
    void StopListeners();

    /**
     * Checks to see if a device is already in the list and updates its maxage
     * timeout if it is.
     * Returns true if an existing device was found.  Returning false implies
     * that this is a new device which should be added to the list.
     * false will always be returned while a list is being refreshed.
     */
    TBool Update(const Brx& aUdn, const Brx& aLocation, TUint aMaxAge);
    void DoStart();
    void DoRefresh();
protected: // from CpiDeviceList
    void Start();
    void Refresh();
    TBool IsDeviceReady(CpiDevice& aDevice);
    TBool IsLocationReachable(const Brx& aLocation) const;
protected: // from ISsdpNotifyHandler
    void SsdpNotifyRootAlive(const Brx& aUuid, const Brx& aLocation, TUint aMaxAge);
    void SsdpNotifyUuidAlive(const Brx& aUuid, const Brx& aLocation, TUint aMaxAge);
    void SsdpNotifyDeviceTypeAlive(const Brx& aUuid, const Brx& aDomain, const Brx& aType, TUint aVersion,
                                   const Brx& aLocation, TUint aMaxAge);
    void SsdpNotifyServiceTypeAlive(const Brx& aUuid, const Brx& aDomain, const Brx& aType, TUint aVersion,
                                    const Brx& aLocation, TUint aMaxAge);
    void SsdpNotifyRootByeBye(const Brx& aUuid);
    void SsdpNotifyUuidByeBye(const Brx& aUuid);
    void SsdpNotifyDeviceTypeByeBye(const Brx& aUuid, const Brx& aDomain, const Brx& aType, TUint aVersion);
    void SsdpNotifyServiceTypeByeBye(const Brx& aUuid, const Brx& aDomain, const Brx& aType, TUint aVersion);
private: // IResumeObserver
    void NotifyResumed();
private: // IMdnsDeviceListener
    void DeviceAdded(MdnsDevice& aDev);
private:
    void RefreshTimerComplete();
    void ResumedTimerComplete();
    void CurrentNetworkAdapterChanged();
    void SubnetListChanged();
    void HandleInterfaceChange();
    void RemoveAll();
    void DeviceReady();
protected:
    SsdpListenerUnicast* iUnicastListener;
    Mutex iSsdpLock;
private:
    static const TUint kMaxMsearchRetryForNewAdapterSecs = 60;
    static const TUint kResumeDelayMs = 2 * 1000;
    static const TUint kRefreshRetries = 4;
    Environment& iEnv;
    TIpAddress iInterface;
    SsdpListenerMulticast* iMulticastListener;
    TInt iNotifyHandlerId;
    TUint iInterfaceChangeListenerId;
    TUint iSubnetListChangeListenerId;
    TBool iStarted;
    TBool iNoRemovalsFromRefresh;
    Timer* iRefreshTimer;
    Timer* iResumedTimer;
    TUint iRefreshRepeatCount;
};

class CpiDeviceListOdpAll : public CpiDeviceListOdp
{
public:
    CpiDeviceListOdpAll(CpStack& aCpStack, FunctorCpiDevice aAdded, FunctorCpiDevice aRemoved);
    ~CpiDeviceListOdpAll();
    void Start();
};

} // namespace Net
} // namespace OpenHome

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

#include <atomic>
#include <map>

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
    void NotifyDestroy() override;
    TUint Version(const TChar* aDomain, const TChar* aName, TUint aProxyVersion) const override;
public: // from ICpiDeviceObserver
    void Release() override;
private: // from ICpiOdpDevice
    IWriter& WriteLock() override;
    void WriteUnlock() override;
    void WriteEnd(IWriter& aWriter) override;
    TUint RegisterResponseHandler(ICpiOdpResponse& aResponseHandler) override;
    const Brx& Alias() const override;
    const Brx& Udn() const override;
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
    Fifo<IInvocable*> iQueueInvocable;
    TBool iConnected;
    TBool iExiting;
    Semaphore iDeviceConnected;
    Bws<64> iFriendlyName;
    Bws<64> iUglyName;
    Bws<64> iIpAddress;
    Bws<64> iMdnsType;
    TUint iPort;
    Mutex iLockResponses;
    std::atomic<TUint> iNextCorrelationId;
    std::map<TUint, ICpiOdpResponse*> iPendingResponses;
};

class CpiDeviceListOdp : public CpiDeviceList, private IResumeObserver, private IMdnsDeviceListener
{
public:
    CpiDeviceListOdp(CpStack& aCpStack, FunctorCpiDevice aAdded, FunctorCpiDevice aRemoved);
    ~CpiDeviceListOdp();
protected:
    void DoStart();
    void DoRefresh();
protected: // from CpiDeviceList
    void Start();
    void Refresh();
    TBool IsDeviceReady(CpiDevice& aDevice);
    TBool IsLocationReachable(const Brx& aLocation) const;
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
private:
    static const TUint kMaxMsearchRetryForNewAdapterSecs = 60;
    static const TUint kResumeDelayMs = 2 * 1000;
    static const TUint kRefreshRetries = 4;
    Environment& iEnv;
    TIpAddress iInterface;
    TUint iInterfaceChangeListenerId;
    TUint iSubnetListChangeListenerId;
    TBool iStarted;
    TBool iNoRemovalsFromRefresh;
    Timer* iRefreshTimer;
    Timer* iResumedTimer;
    std::atomic<TUint> iRefreshRepeatCount;
};

class CpiDeviceListOdpAll : public CpiDeviceListOdp
{
public:
    CpiDeviceListOdpAll(CpStack& aCpStack, FunctorCpiDevice aAdded, FunctorCpiDevice aRemoved);
    ~CpiDeviceListOdpAll();
    void Start();
private:
    CpStack& iCpStack;
};

} // namespace Net
} // namespace OpenHome

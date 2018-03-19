#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Private/DviServer.h>
#include <OpenHome/Net/Odp/DviOdp.h>
#include <OpenHome/Av/Product.h>
#include <OpenHome/Optional.h>

namespace OpenHome {
namespace Net {
    class DvStack;

class OdpDevice
{
private:
    static const TUint kMaxNameBytes = 100;
    static const TChar* kAdapterCookie;
public:
    OdpDevice(Net::IMdnsProvider& aMdnsProvider, NetworkAdapter& aAdapter, Av::IFriendlyNameObservable& aFriendlyNameObservable, Endpoint& aEndpoint);
    ~OdpDevice();
    void Register();
    void Deregister();
    TBool NetworkAdapterAndPortMatch(const NetworkAdapter& aAdapter, TUint aZeroConfPort) const;
private:
    void RegisterLocked();
    void DeregisterLocked();
    void NameChanged(const Brx& aName);
private:
    Net::IMdnsProvider& iProvider;
    NetworkAdapter& iAdapter;
    Av::IFriendlyNameObservable& iFriendlyNameObservable;
    TUint iFriendlyNameId;
    Bws<Av::IFriendlyNameObservable::kMaxFriendlyNameBytes+1> iName;    // Space for '\0'.
    Endpoint iEndpoint;
    const TUint iHandle;
    TBool iRegistered;
    Mutex iLock;
};

class DviSessionOdp : public SocketTcpSession
                    , private IOdpSession
{
    static const Brn kUserAgentDefault;
public:
    DviSessionOdp(DvStack& aDvStack, TIpAddress aAdapter);
    ~DviSessionOdp();
private: // from SocketTcpSession
    void Run() override;
private: // from IOdpSession
    IWriter& WriteLock() override;
    void WriteUnlock() override;
    void WriteEnd() override;
    TIpAddress Adapter() const override;
    const Brx& ClientUserAgentDefault() const override;
private:
    static const TUint kMaxReadBytes = 12 * 1024;
    static const TUint kWriteBufferBytes = 4000;
    TIpAddress iAdapter;
    Mutex iWriteLock;
    Semaphore iShutdownSem;
    Srx* iReadBuffer;
    ReaderUntil* iReaderUntil;
    Sws<kWriteBufferBytes>* iWriteBuffer;
    DviOdp* iProtocol;
};

class DviServerOdp : public DviServer
{
public:
    DviServerOdp(DvStack& aDvStack, TUint aNumSessions, TUint aPort = 0);
    ~DviServerOdp();
    TUint Port() const;
    void SetServerCreatedCallback(Functor aCallback);
private: // from DviServer
    SocketTcpServer* CreateServer(const NetworkAdapter& aNif) override;
    void NotifyServerDeleted(TIpAddress aInterface) override; 
    void NotifyServerCreated(TIpAddress aInterface) override;
private:
    const TUint iNumSessions;
    TUint iPort;
    Functor iServerCreated;
};

class OdpZeroConfDevices : public INonCopyable
{
public:
    OdpZeroConfDevices(Net::IMdnsProvider& aMdnsProvider, Av::IFriendlyNameObservable& aFriendlyNameObservable);
    ~OdpZeroConfDevices();
    void SetEnabled(TBool aEnabled);
    void NetworkAdaptersChanged(std::vector<NetworkAdapter*>& aNetworkAdapters, Optional<NetworkAdapter>& aCurrent, TUint aZeroConfPort);
private:
    TInt AdapterInCurrentOdpDeviceAdapters(const NetworkAdapter& aAdapter, TUint aZeroConfPort);
    TInt OdpDeviceAdapterInCurrentAdapters(const OdpDevice& aDevice, const std::vector<NetworkAdapter*>& aList, TUint aZeroConfPort);
    void AddAdapterLocked(NetworkAdapter& aAdapter, TUint aZeroConfPort);
private:
    Net::IMdnsProvider& iMdnsProvider;
    Av::IFriendlyNameObservable& iFriendlyNameObservable;
    std::vector<OdpDevice*> iDevices;
    TBool iEnabled;
    mutable Mutex iLock;
};

class IZeroConfEnabler
{
public:
    virtual void SetZeroConfEnabled(TBool aEnabled) = 0;
    virtual ~IZeroConfEnabler() {}
};

class OdpZeroConf : public IZeroConfEnabler
{
private:
    static const Brn kPath;
public:
    OdpZeroConf(Environment& aEnv, DviServerOdp& aServerOdp, Av::IFriendlyNameObservable& aFriendlyNameObservable);
    ~OdpZeroConf();
public: // from IZeroConfEnabler
    void SetZeroConfEnabled(TBool aEnabled);
private:
    void OdpServerCreated();
private:
    Environment& iEnv;
    DviServerOdp& iZeroConfServer;
    OdpZeroConfDevices iZeroConfDevices;
    TBool iEnabled;
    Mutex iLock;
};

} // namespace Net
} // namespace OpenHome

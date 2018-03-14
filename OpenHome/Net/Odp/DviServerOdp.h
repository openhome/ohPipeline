#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Private/DviServer.h>
#include <OpenHome/Net/Odp/DviOdp.h>
#include <OpenHome/Av/Product.h>

namespace OpenHome {
namespace Net {
    class DvStack;

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
    DviServerOdp(DvStack& aDvStack, Av::IFriendlyNameObservable& aFriendlyNameObservable, TUint aNumSessions, TUint aPort = 0);
    ~DviServerOdp();
    TUint Port() const;
private: // from DviServerUpnp
    SocketTcpServer* CreateServer(const NetworkAdapter& aNif) override;
    void NotifyServerDeleted(TIpAddress aInterface) override;
private: // from IResourceManager
    void Register();
    void Deregister();
private:
    void RegisterLocked();
    void DeregisterLocked();
    void NameChanged(const Brx& aName);
    void HandleInterfaceChange();
private:
    const TUint iNumSessions;
    TUint iPort;
    Environment& iEnv;
    IMdnsProvider& iProvider;
    Av::IFriendlyNameObservable& iFriendlyNameObservable;
    TUint iFriendlyNameId;
    Bws<Av::IFriendlyNameObservable::kMaxFriendlyNameBytes+1> iName;    // Space for '\0'.
    const TUint iHandleOdp;
    TBool iRegistered;
    Mutex iLock;
    TInt iCurrentAdapterChangeListenerId;
    TUint iSubnetListChangeListenerId;
    Endpoint iEndpoint;
};

} // namespace Net
} // namespace OpenHome

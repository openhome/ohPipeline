#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Av/QobuzConnect/LocalConfigServer.h>

#include <qobuz_connect.h>

namespace OpenHome {
    class Environment;
    class NetworkAdapter;
namespace Net {
    class IMdnsProvider;
}
namespace Av {

/**
 * Implements the SDK's Advertising delegate via OpenHome's own IMdnsProvider - the same
 * mechanism SourceFactory::NewRaop uses (see SingleCore.cpp), rather than AirPlay2's approach
 * of self-managing its own internal Bonjour registration: Qobuz Connect's explicit
 * start/stop-advertising callback pair means *we* own the mDNS registration, so this mirrors
 * DeviceAnnouncerMdns (OpenHome/Av/DeviceAnnouncerMdns.cpp) - one IMdnsProvider-issued handle,
 * re-registered whenever the current network adapter changes while advertising is active.
 */
class QobuzConnectAdvertising : public IQobuzConnectLocalConfigServerObserver
{
public:
    QobuzConnectAdvertising(Environment& aEnv, Net::IMdnsProvider& aMdnsProvider, QbzDeviceType aDeviceType, IQobuzConnectLocalConfigServerInfo& aLocalConfigServerInfo);
    ~QobuzConnectAdvertising();
public:
    QbzAdvertisingDelegate Delegate();
    void SetCore(QbzConnectCore* aCore);
public: // from IQobuzConnectLocalConfigServerObserver
    void QobuzLocalConfigServerStarted() override;
public:
    // Called by the extern "C" trampoline functions in Advertising.cpp (which aren't members or
    // friends of this class, so these must be public, not private).
    static void StartAdvertisingCb(QbzConnectCore* aCore, const char* aServiceName, const char* aServiceType, void* aUserData);
    static void StopAdvertisingCb(QbzConnectCore* aCore, void* aUserData);
private:
    void HandleStartAdvertising(QbzConnectCore* aCore, const Brx& aServiceName, const Brx& aServiceType);
    void HandleStopAdvertising();
    void CurrentAdapterChanged();
    void Register(NetworkAdapter* aCurrent);
    void Deregister();
private:
    Environment& iEnv;
    Net::IMdnsProvider& iMdnsProvider;
    QbzDeviceType iDeviceType;
    IQobuzConnectLocalConfigServerInfo& iLocalConfigServerInfo;
    Mutex iLock;
    TUint iHandleMdns;
    TUint iIdAdapterChange;
    TBool iAdvertising; // true between HandleStartAdvertising/HandleStopAdvertising - i.e. whether we should be registered at all
    TBool iRegistered;  // true if we currently hold a live mDNS registration (may be false even while iAdvertising, e.g. no adapter yet)
    QbzConnectCore* iCore;
    Bws<64> iServiceName;
    Bws<32> iServiceType;
};

}
}

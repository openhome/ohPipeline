#include <OpenHome/Av/QobuzConnect/Advertising.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Env.h>
#include <OpenHome/Private/NetworkAdapterList.h>
#include <OpenHome/Net/Private/MdnsProvider.h>
#include <OpenHome/Debug-ohMediaPlayer.h>

#include <qobuz_connect.h>

#include <cstring>

using namespace OpenHome;
using namespace OpenHome::Av;

extern "C" {

static void QobuzConnectAdvertising_StartAdvertising(QbzConnectCore* aCore, const char* aServiceName, const char* aServiceType, void* aUserData)
{
    OpenHome::Av::QobuzConnectAdvertising::StartAdvertisingCb(aCore, aServiceName, aServiceType, aUserData);
}

static void QobuzConnectAdvertising_StopAdvertising(QbzConnectCore* aCore, void* aUserData)
{
    OpenHome::Av::QobuzConnectAdvertising::StopAdvertisingCb(aCore, aUserData);
}

} // extern "C"

QobuzConnectAdvertising::QobuzConnectAdvertising(
    Environment& aEnv,
    Net::IMdnsProvider& aMdnsProvider,
    QbzDeviceType aDeviceType,
    IQobuzConnectLocalConfigServerInfo& aLocalConfigServerInfo)
    : iEnv(aEnv)
    , iMdnsProvider(aMdnsProvider)
    , iDeviceType(aDeviceType)
    , iLocalConfigServerInfo(aLocalConfigServerInfo)
    , iLock("QCAv")
    , iIdAdapterChange(0)
    , iAdvertising(false)
    , iRegistered(false)
    , iCore(nullptr)
{
    iHandleMdns = iMdnsProvider.MdnsCreateService();
    iIdAdapterChange = iEnv.NetworkAdapterList().AddCurrentChangeListener(
        MakeFunctor(*this, &QobuzConnectAdvertising::CurrentAdapterChanged), "QobuzConnectAdvertising", false);
}

QobuzConnectAdvertising::~QobuzConnectAdvertising()
{
    iEnv.NetworkAdapterList().RemoveCurrentChangeListener(iIdAdapterChange);
    Deregister();
    iMdnsProvider.MdnsDestroyService(iHandleMdns);
}

QbzAdvertisingDelegate QobuzConnectAdvertising::Delegate()
{
    QbzAdvertisingDelegate delegate;
    delegate.user_data = this;
    delegate.start_advertising_callback = &QobuzConnectAdvertising_StartAdvertising;
    delegate.stop_advertising_callback = &QobuzConnectAdvertising_StopAdvertising;
    return delegate;
}

void QobuzConnectAdvertising::SetCore(QbzConnectCore* aCore)
{
    AutoMutex _(iLock);
    iCore = aCore;
}

void QobuzConnectAdvertising::StartAdvertisingCb(QbzConnectCore* aCore, const char* aServiceName, const char* aServiceType, void* aUserData)
{
    reinterpret_cast<QobuzConnectAdvertising*>(aUserData)->HandleStartAdvertising(
        aCore,
        Brn((const TByte*)aServiceName, (TUint)strlen(aServiceName)),
        Brn((const TByte*)aServiceType, (TUint)strlen(aServiceType)));
}

void QobuzConnectAdvertising::StopAdvertisingCb(QbzConnectCore* /*aCore*/, void* aUserData)
{
    reinterpret_cast<QobuzConnectAdvertising*>(aUserData)->HandleStopAdvertising();
}

void QobuzConnectAdvertising::HandleStartAdvertising(QbzConnectCore* aCore, const Brx& aServiceName, const Brx& aServiceType)
{
    LOG(kQobuzConnect, "QobuzConnectAdvertising::HandleStartAdvertising(%.*s, %.*s)\n", PBUF(aServiceName), PBUF(aServiceType));
    AutoMutex _(iLock);
    iCore = aCore;
    iServiceName.Replace(aServiceName);
    iServiceType.Replace(aServiceType);
    iAdvertising = true;
    AutoNetworkAdapterRef ar(iEnv, "QobuzConnectAdvertising");
    Register(ar.Adapter());
}

void QobuzConnectAdvertising::HandleStopAdvertising()
{
    LOG(kQobuzConnect, "QobuzConnectAdvertising::HandleStopAdvertising()\n");
    AutoMutex _(iLock);
    iAdvertising = false;
    Deregister();
}

void QobuzConnectAdvertising::CurrentAdapterChanged()
{
    AutoMutex _(iLock);
    if (!iAdvertising) {
        return;
    }
    Deregister();
    AutoNetworkAdapterRef ar(iEnv, "QobuzConnectAdvertising");
    Register(ar.Adapter());
}

void QobuzConnectAdvertising::QobuzLocalConfigServerStarted()
{
    // The SDK gives no ordering guarantee between start_advertising_callback and
    // start_local_config_server_callback. If advertising already registered (with whatever
    // Port() returned at the time - 0, if it started first, since that's
    // QobuzConnectLocalConfigServer's default before its listening socket is bound),
    // re-register now that the real port is known. No-op if we're not currently registered -
    // Register() will pick up the correct port on its own if it hasn't run yet.
    AutoMutex _(iLock);
    if (!iAdvertising || !iRegistered) {
        return;
    }
    Deregister();
    AutoNetworkAdapterRef ar(iEnv, "QobuzConnectAdvertising");
    Register(ar.Adapter());
}

void QobuzConnectAdvertising::Register(NetworkAdapter* aCurrent)
{
    // Called with iLock already held.
    if (!iAdvertising || aCurrent == nullptr || iCore == nullptr) {
        return;
    }

    const char* uuid = nullptr;
    (void)qbz_connect_get_device_uuid(iCore, &uuid);
    const char* sdkVersion = qbz_connect_get_sdk_version();
    const char* deviceType = qbz_device_type_to_string(iDeviceType);

    Bws<256> info;
    iMdnsProvider.MdnsAppendTxtRecord(info, "path", Brhz(iLocalConfigServerInfo.PathTail()).CString());
    iMdnsProvider.MdnsAppendTxtRecord(info, "device_uuid", uuid != nullptr ? uuid : "");
    iMdnsProvider.MdnsAppendTxtRecord(info, "sdk_version", sdkVersion != nullptr ? sdkVersion : "");
    iMdnsProvider.MdnsAppendTxtRecord(info, "type", deviceType != nullptr ? deviceType : "");

    iMdnsProvider.MdnsRegisterService(
        iHandleMdns,
        Brhz(iServiceName).CString(),
        Brhz(iServiceType).CString(),
        aCurrent->Address(),
        iLocalConfigServerInfo.Port(),
        info.PtrZ());
    iRegistered = true;
}

void QobuzConnectAdvertising::Deregister()
{
    // Called with iLock already held. Matches DeviceAnnouncerMdns: iHandleMdns is allocated once
    // (ctor) and reused for the object's whole lifetime across repeated Destroy/Register cycles -
    // MdnsDestroyService() doesn't invalidate the handle number itself.
    if (iRegistered) {
        iMdnsProvider.MdnsDestroyService(iHandleMdns);
        iRegistered = false;
    }
}

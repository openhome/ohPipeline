#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Av/Product.h>

namespace OpenHome {
    class NetworkAdapter;
    namespace Net {
        class DvDevice;
        class DviDevice;
        class DvStack;
    }
namespace Av {

class DeviceAnnouncerMdns
{
public:
    DeviceAnnouncerMdns(
        Net::DvStack& aDvStack,
        Net::DvDevice& aDevice,
        IFriendlyNameObservable& aFriendlyNameObservable);
    ~DeviceAnnouncerMdns();
private:
    void CurrentAdapterChanged();
    void Register(NetworkAdapter* aCurrent);
    void Deregister();
    void NameChanged(const Brx& aName);
private:
    Mutex iLock;
    Net::DvStack& iDvStack;
    Net::DviDevice& iDevice;
    Av::IFriendlyNameObservable& iFriendlyNameObservable;
    TUint iHandleMdns;
    TUint iIdAdapterChange;
    TUint iIdFriendlyName;
    TIpAddress iCurrentSubnet;
    Bws<Av::IFriendlyNameObservable::kMaxFriendlyNameBytes + 1> iName;    // Space for '\0'.
    TBool iRegistered;
};

} // Av
} // OpenHome

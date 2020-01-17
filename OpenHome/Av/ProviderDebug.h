#pragma once

#include <OpenHome/Types.h>
#include <Generated/DvAvOpenhomeOrgDebug2.h>
#include <OpenHome/Optional.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Net/Private/Discovery.h>

#include <utility>
#include <vector>

namespace OpenHome {
    class RingBufferLogger;
    namespace Net {
        class DvStack;
}
namespace Av {
    class ILogPoster;

class MSearchObserver : private Net::ISsdpMsearchHandler
{
    static const TUint kMaxAddresses;
public:
    MSearchObserver(Environment& aEnv);
    ~MSearchObserver();
    std::vector<std::pair<TIpAddress, TUint>> RecentSearchers() const;
private:
    void CurrentAdapterChanged();
    void NotifySearch(const Endpoint& aEndpoint);
private: // from ISsdpMsearchHandler
    void SsdpSearchAll(const Endpoint& aEndpoint, TUint aMx);
    void SsdpSearchRoot(const Endpoint& aEndpoint, TUint aMx);
    void SsdpSearchUuid(const Endpoint& aEndpoint, TUint aMx, const Brx& aUuid);
    void SsdpSearchDeviceType(const Endpoint& aEndpoint, TUint aMx, const Brx& aDomain, const Brx& aType, TUint aVersion);
    void SsdpSearchServiceType(const Endpoint& aEndpoint, TUint aMx, const Brx& aDomain, const Brx& aType, TUint aVersion);
private:
    mutable Mutex iLock;
    Environment& iEnv;
    Net::SsdpListenerMulticast* iMulticastListener;
    TInt iMsearchHandlerId;
    TIpAddress iMulticastAdapter;
    std::vector<std::pair<TIpAddress, TUint>> iRecentSearchers;
    TUint iAdapterChangeListenerId;
};

class ProviderDebug : public Net::DvProviderAvOpenhomeOrgDebug2
{
public:
    ProviderDebug(Net::DvDevice& aDevice, RingBufferLogger& aLogger, Optional<ILogPoster> aLogPoster);
private: // from DvProviderAvOpenhomeOrgDebug2
    void GetLog(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseString& aLog) override;
    void SendLog(Net::IDvInvocation& aInvocation, const Brx& aData) override;
    void SendDeviceAnnouncements(Net::IDvInvocation& aInvocation) override;
    void GetRecentMSearches(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseString& aJsonArray) override;
private:
    RingBufferLogger& iLogger;
    Optional<ILogPoster> iLogPoster;
    Net::DvStack& iDvStack;
    MSearchObserver iMSearchObserver;
};

} // namespace Av
} // namespace OpenHome

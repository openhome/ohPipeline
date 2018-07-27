#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Thread.h>
#include <Generated/DvAvOpenhomeOrgPins1.h>
#include <OpenHome/Av/Pins/Pins.h>

#include <vector>

namespace OpenHome {
    class Environment;
    class Timer;
namespace Av {

class ProviderPins : public Net::DvProviderAvOpenhomeOrgPins1
                   , private IPinsObserver
{
    static const TUint kModerationMs;
public:
    ProviderPins(Net::DvDevice& aDevice, Environment& aEnv, IPinsManager& aManager);
    ~ProviderPins();
    void Start();
private: // from IPinsObserver
    void NotifyDevicePinsMax(TUint aMax) override;
    void NotifyAccountPinsMax(TUint aMax) override;
    void NotifyModeAdded(const Brx& aMode) override;
    void NotifyCloudConnected(TBool aConnected) override;
    void NotifyUpdatesDevice(const std::vector<TUint>& aIdArray) override;
    void NotifyUpdatesAccount(const std::vector<TUint>& aIdArray) override;
private:
    void IdArrayModerationCallback();
    void UpdateIdArrayLocked();
private: // from Net::DvProviderAvOpenhomeOrgPins1
    void GetDeviceMax(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseUint& aDeviceMax) override;
    void GetAccountMax(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseUint& aAccountMax) override;
    void GetModes(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseString& aModes) override;
    void GetIdArray(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseString& aIdArray) override;
    void GetCloudConnected(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseBool& aCloudConnected) override;
    void ReadList(Net::IDvInvocation& aInvocation, const Brx& aIds, Net::IDvInvocationResponseString& aList) override;
    void InvokeId(Net::IDvInvocation& aInvocation, TUint aId) override;
    void InvokeIndex(Net::IDvInvocation& aInvocation, TUint aIndex) override;
    void InvokeUri(Net::IDvInvocation& aInvocation, const Brx& aMode, const Brx& aType, const Brx& aUri, TBool aShuffle) override;
    void SetDevice(Net::IDvInvocation& aInvocation, TUint aIndex, const Brx& aMode, const Brx& aType, const Brx& aUri, const Brx& aTitle, const Brx& aDescription, const Brx& aArtworkUri, TBool aShuffle) override;
    void SetAccount(Net::IDvInvocation& aInvocation, TUint aIndex, const Brx& aMode, const Brx& aType, const Brx& aUri, const Brx& aTitle, const Brx& aDescription, const Brx& aArtworkUri, TBool aShuffle) override;
    void Clear(Net::IDvInvocation& aInvocation, TUint aId) override;
    void Swap(Net::IDvInvocation& aInvocation, TUint aIndex1, TUint aIndex2) override;
private:
    Mutex iLock;
    IPinsManager& iManager;
    Timer* iIdArrayModerator;
    TUint iDeviceMax;
    TUint iAccountMax;
    std::vector<Brn> iModes;
    std::vector<TUint> iIdArrayDevice;
    std::vector<TUint> iIdArrayAccount;
    WriterBwh iWriterIdArray;
    TBool iStarted;
};

} // namespace Av
} // namespace OpenHome

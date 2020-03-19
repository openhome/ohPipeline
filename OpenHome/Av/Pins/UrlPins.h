#pragma once

#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <Generated/CpAvOpenhomeOrgRadio1.h>
#include <OpenHome/Av/Pins/Pins.h>
#include <OpenHome/Av/Pins/PodcastPinsTuneIn.h>
        
namespace OpenHome {
    class Environment;
    class IThreadPool;
    class IThreadPoolHandle;
namespace Configuration {
    class IConfigInitialiser;
    class ConfigChoice;
}
namespace Av {

class UrlPins
    : public IPinInvoker
{
    const TUint kMinSupportedVersion = 1;
    const TUint kMaxSupportedVersion = 1;

public:
    UrlPins(Net::DvDeviceStandard& aDevice, Net::CpStack& aCpStack, IThreadPool& aThreadPool);
    ~UrlPins();

private: // from IPinInvoker
    void BeginInvoke(const IPin& aPin, Functor aCompleted) override;
    void Cancel() override;
    const TChar* Mode() const override;
    TBool SupportsVersion(TUint version) const override;
private:
    void Invoke();
    TBool LoadStream(const Brx& aStream, const IPin& aPin);
private:
    Mutex iLock;
    Net::CpProxyAvOpenhomeOrgRadio1* iCpRadio;
    IThreadPoolHandle* iThreadPoolHandle;
    Bws<128> iToken;
    Functor iCompleted;
    PinIdProvider iPinIdProvider;
    Pin iPin;
};

};  // namespace Av
};  // namespace OpenHome



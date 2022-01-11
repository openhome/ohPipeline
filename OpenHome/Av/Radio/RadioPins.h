#pragma once

#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <Generated/CpAvOpenhomeOrgRadio2.h>
#include <OpenHome/Av/Pins/Pins.h>
        
namespace OpenHome {
    class Environment;
namespace Configuration {
    class IConfigInitialiser;
    class ConfigChoice;
}
namespace Av {

class RadioPins
    : public IPinInvoker
{
    const TUint kMinSupportedVersion = 1;
    const TUint kMaxSupportedVersion = 1;

public:
    RadioPins(Net::DvDeviceStandard& aDevice, Net::CpStack& aCpStack);
    ~RadioPins();

private: // from IPinInvoker
    void BeginInvoke(const IPin& aPin, Functor aCompleted) override;
    void Cancel() override;
    const TChar* Mode() const override;
    TBool SupportsVersion(TUint version) const override;
private:
    TBool LoadPreset(TUint aPreset);
    TBool LoadPreset(const Brx& aPreset);
private:
    Mutex iLock;
    Net::CpProxyAvOpenhomeOrgRadio2* iCpRadio;
    Net::CpStack& iCpStack;
};

};  // namespace Av
};  // namespace OpenHome



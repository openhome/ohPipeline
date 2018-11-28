#pragma once

#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <Generated/CpAvOpenhomeOrgRadio1.h>
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
public:
    RadioPins(Net::DvDeviceStandard& aDevice, Net::CpStack& aCpStack);
    ~RadioPins();

private: // from IPinInvoker
    void BeginInvoke(const IPin& aPin, Functor aCompleted) override;
    void Cancel() override;
    const TChar* Mode() const override;
private:
    TBool LoadPreset(TUint aPreset);
    TBool LoadPreset(const Brx& aPreset);
private:
    Mutex iLock;
    Net::CpProxyAvOpenhomeOrgRadio1* iCpRadio;
    Net::CpStack& iCpStack;
};

};  // namespace Av
};  // namespace OpenHome



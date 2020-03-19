#pragma once

#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <Generated/CpAvOpenhomeOrgProduct3.h>
#include <Generated/CpAvOpenhomeOrgTransport1.h>
#include <OpenHome/Av/Pins/Pins.h>
        
namespace OpenHome {
    class Environment;
namespace Configuration {
    class IConfigInitialiser;
    class ConfigChoice;
}
namespace Av {

class TransportPins
    : public IPinInvoker
{
    const TUint kMinSupportedVersion = 1;
    const TUint kMaxSupportedVersion = 1;

public:
    TransportPins(Net::DvDeviceStandard& aDevice, Net::CpStack& aCpStack);
    ~TransportPins();

private: // from IPinInvoker
    void BeginInvoke(const IPin& aPin, Functor aCompleted) override;
    void Cancel() override;
    const TChar* Mode() const override;
    TBool SupportsVersion(TUint version) const override;
private:
    TBool SelectLocalInput(const Brx& aSourceSystemName); // source system name remains constant always
private:
    Mutex iLock;
    Net::CpProxyAvOpenhomeOrgProduct3* iCpProduct;
    Net::CpProxyAvOpenhomeOrgTransport1* iCpTransport;
    Net::CpStack& iCpStack;
};

};  // namespace Av
};  // namespace OpenHome



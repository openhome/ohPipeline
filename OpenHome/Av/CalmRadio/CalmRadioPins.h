#pragma once

#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <Generated/CpAvOpenhomeOrgRadio1.h>
#include <OpenHome/Av/Pins/Pins.h>
#include <OpenHome/Av/CalmRadio/CalmRadio.h>
        
namespace OpenHome {
    class Environment;
namespace Configuration {
    class IConfigInitialiser;
    class ConfigChoice;
}
namespace Av {

class CalmRadioPins
    : public IPinInvoker
{
public:
    CalmRadioPins(CalmRadio& aCalmRadio, Net::DvDeviceStandard& aDevice, Net::CpStack& aCpStack);
    ~CalmRadioPins();

private: // from IPinInvoker
    void BeginInvoke(const IPin& aPin, Functor aCompleted) override;
    void Cancel() override;
    const TChar* Mode() const override;
private:
    TBool LoadStream(const Brx& aStream, const IPin& aPin); // playable stream (CalmRadio url)
    TBool LoadStation(const Brx& aStation, const IPin& aPin); // CalmRadio station id (ie s1234)
private:
    Mutex iLock;
    CalmRadio& iCalmRadio;
    Net::CpProxyAvOpenhomeOrgRadio1* iCpRadio;
    Net::CpStack& iCpStack;
};

};  // namespace Av
};  // namespace OpenHome



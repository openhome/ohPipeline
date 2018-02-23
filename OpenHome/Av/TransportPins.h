#pragma once

#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/DebugManager.h>
#include <Generated/CpAvOpenhomeOrgTransport1.h>
        
namespace OpenHome {
    class Environment;
namespace Configuration {
    class IConfigInitialiser;
    class ConfigChoice;
}
namespace Av {

class TransportPins
    : public IDebugTestHandler
{
public:
    TransportPins(Net::DvDeviceStandard& aDevice, Net::CpStack& aCpStack);
    ~TransportPins();

    TBool SelectLocalInput(const Brx& aSourceSystemName);

public:  // IDebugTestHandler
    TBool Test(const OpenHome::Brx& aType, const OpenHome::Brx& aInput, OpenHome::IWriterAscii& aWriter);
private:
    Mutex iLock;
    Net::CpProxyAvOpenhomeOrgTransport1* iCpTransport;
    Net::CpStack& iCpStack;
};

};  // namespace Av
};  // namespace OpenHome



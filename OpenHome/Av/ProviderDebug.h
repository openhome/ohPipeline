#pragma once

#include <OpenHome/Types.h>
#include <Generated/DvAvOpenhomeOrgDebug1.h>
#include <OpenHome/Optional.h>
#include <OpenHome/DebugManager.h>
#include <OpenHome/Av/PodcastPins.h>

namespace OpenHome {
    class RingBufferLogger;
namespace Av {
    class ILogPoster;

class ProviderDebug : public Net::DvProviderAvOpenhomeOrgDebug1
                    , public IDebugEventObserver
{
public:
    ProviderDebug(Net::DvDevice& aDevice, RingBufferLogger& aLogger, Optional<ILogPoster> aLogPoster, DebugManager& aDebugManager);
private: // from DvProviderAvOpenhomeOrgDebug1
    void GetLog(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseString& aLog) override;
    void SendLog(Net::IDvInvocation& aInvocation, const Brx& aData) override;
    void DebugTest(Net::IDvInvocation& aInvocation, const Brx& aaDebugType, const Brx& aaDebugInput, Net::IDvInvocationResponseString& aaDebugInfo, Net::IDvInvocationResponseBool& aaDebugResult) override;
    void DebugDump(Net::IDvInvocation& aInvocation, const Brx& aaDebugType, Net::IDvInvocationResponseString& aaDebugInfo) override;
private: // from IDebugEventObserver
    void DebugValueChanged(const Brx& aValue) override;  

private:
    RingBufferLogger& iLogger;
    Optional<ILogPoster> iLogPoster;
    DebugManager& iDebugManager;
};

} // namespace Av
} // namespace OpenHome

#pragma once

#include <OpenHome/Types.h>

namespace OpenHome {
    class Environment;
namespace Av {

class IRaatTime
{
public:
    virtual ~IRaatTime() {}
    virtual TUint64 MclkTimeNs() const = 0;
};

// Dummy implementation that uses processor rather than audio clock ticks.
// NOT SUITABLE FOR PRODUCTION USE
class RaatTimeCpu : public IRaatTime
{
public:
    RaatTimeCpu(Environment& aEnv);
private: // from IRaatTime
    TUint64 MclkTimeNs() const override;
private:
    OsContext* iOsCtx;
};

}
}

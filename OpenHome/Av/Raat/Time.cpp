#include <OpenHome/Av/Raat/Time.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Env.h>
#include <OpenHome/OsWrapper.h>

using namespace OpenHome;
using namespace OpenHome::Av;

RaatTimeCpu::RaatTimeCpu(Environment& aEnv)
    : iOsCtx(aEnv.OsCtx())
{
}

TUint64 RaatTimeCpu::MclkTimeNs() const
{
    return Os::TimeInUs(iOsCtx) * 1000;
}

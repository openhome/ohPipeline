#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Standard.h>

namespace OpenHome {

    class Environment;
    class IPowerManager;
    class IThreadPool;
    class IThreadPoolHandle;
    class Timer;

class FsFlushPeriodic : private INonCopyable
{
public:
    FsFlushPeriodic(Environment& aEnv,
                    IPowerManager& aPowerManager,
                    IThreadPool& aThreadPool,
                    TUint aFreqMs);
    ~FsFlushPeriodic();
    void Start();
private:
    void TimerCallback();
    void Flush();
private:
    IPowerManager& iPowerManager;
    const TUint iFreqMs;
    IThreadPoolHandle* iThreadPoolHandle;
    Timer* iTimer;
};

} // namespace OpenHome

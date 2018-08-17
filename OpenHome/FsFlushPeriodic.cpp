#include <OpenHome/FsFlushPeriodic.h>
#include <OpenHome/Types.h>
#include <OpenHome/Functor.h>
#include <OpenHome/PowerManager.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Timer.h>


using namespace OpenHome;


FsFlushPeriodic::FsFlushPeriodic(Environment& aEnv,
                                 IPowerManager& aPowerManager,
                                 IThreadPool& aThreadPool,
                                 TUint aFreqMs)
    : iPowerManager(aPowerManager)
    , iFreqMs(aFreqMs)
{
    iThreadPoolHandle = aThreadPool.CreateHandle(MakeFunctor(*this, &FsFlushPeriodic::Flush),
                                                "FsFlushPeriodic", ThreadPoolPriority::Low);
    iTimer = new Timer(aEnv, MakeFunctor(*this, &FsFlushPeriodic::TimerCallback), "FsFlushPeriodic");
}

FsFlushPeriodic::~FsFlushPeriodic()
{
    iTimer->Cancel();
    iThreadPoolHandle->Destroy();
    delete iTimer;
}

void FsFlushPeriodic::Start()
{
    iTimer->FireIn(iFreqMs);
}

void FsFlushPeriodic::TimerCallback()
{
    (void)iThreadPoolHandle->TrySchedule();
}

void FsFlushPeriodic::Flush()
{
    iTimer->FireIn(iFreqMs);
    iPowerManager.FsFlush();
}

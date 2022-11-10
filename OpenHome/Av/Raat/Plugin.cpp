#include <OpenHome/Av/Raat/Plugin.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Functor.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Thread.h>


using namespace OpenHome;
using namespace OpenHome::Av;


RaatPluginAsync::RaatPluginAsync(IThreadPool& aThreadPool)
    : iLock("RaPl")
    , iStarted(false)
{
    iRaatCallback = aThreadPool.CreateHandle(
        MakeFunctor(*this, &RaatPluginAsync::DoReportState),
        "RaatPluginAsync",
        ThreadPoolPriority::High);
}

RaatPluginAsync::~RaatPluginAsync()
{
    iRaatCallback->Destroy();
}

void RaatPluginAsync::Start()
{
    {
        AutoMutex _(iLock);
        iStarted = true;
    }
    TryReportState();
}

void RaatPluginAsync::TryReportState()
{
    TBool started;
    {
        AutoMutex _(iLock);
        started = iStarted;
    }
    if (started) {
        (void)iRaatCallback->TrySchedule();
    }
}

void RaatPluginAsync::DoReportState()
{
    ReportState();
}

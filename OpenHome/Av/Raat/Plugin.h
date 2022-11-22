#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>

namespace OpenHome {
    class IThreadPool;
    class IThreadPoolHandle;
namespace Av {

class RaatPluginAsync
{
public:
    void Start();
protected:
    RaatPluginAsync(IThreadPool& aThreadPool);
    virtual ~RaatPluginAsync();
    void TryReportState();
private:
    void DoReportState();
    virtual void ReportState() = 0;
protected:
    Mutex iLock;
private:
    IThreadPoolHandle* iRaatCallback;
    TBool iStarted;
};

} // namespace Av
} // namespace OpenHome

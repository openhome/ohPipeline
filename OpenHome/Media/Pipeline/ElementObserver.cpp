#include <OpenHome/Media/Pipeline/ElementObserver.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Standard.h>

#include <atomic>
#include <vector>

using namespace OpenHome;
using namespace OpenHome::Media;

PipelineElementObserverThread::PipelineElementObserverThread(TUint aPriority)
    : iLock("PEOT")
    , iNextId(0)
    , iStarted(false)
{
    iThread = new ThreadFunctor("PipelineEvents", MakeFunctor(*this, &PipelineElementObserverThread::PipelineEventThread), aPriority);
}

PipelineElementObserverThread::~PipelineElementObserverThread()
{
    delete iThread;
    for (auto it=iCallbacks.begin(); it!=iCallbacks.end(); ++it) {
        delete *it;
    }
}

void PipelineElementObserverThread::Start()
{
    ASSERT(!iStarted.load());
    iStarted.store(true);
    iThread->Start();
}

void PipelineElementObserverThread::Stop()
{
    iThread->Kill();
    iThread->Join();
}

void PipelineElementObserverThread::PipelineEventThread()
{
    try {
        for (;;) {
            iThread->Wait();
            for (auto cb : iCallbacks) {
                cb->RunIfPending();
            }
        }
    }
    catch (ThreadKill&) {}
}

TUint PipelineElementObserverThread::Register(Functor aCallback)
{
    ASSERT(!iStarted.load());
    const TUint id = iNextId++;
    auto cb = new Callback(id, aCallback);
    iLock.Wait();
    iCallbacks.push_back(cb);
    iLock.Signal();
    return cb->Id();
}

void PipelineElementObserverThread::Schedule(TUint aId)
{
    for (auto cb : iCallbacks) {
        if (cb->Id() == aId) {
            cb->SetPending();
            iThread->Signal();
            return;
        }
    }
    // only get this far if aId hasn't been registered => programming error
    ASSERTS();
}


// PipelineElementObserverThread::Callback

PipelineElementObserverThread::Callback::Callback(TUint aId, Functor aCallback)
    : iId(aId)
    , iCallback(aCallback)
{
    iPending.store(false);
    ASSERT(iPending.is_lock_free());
}

void PipelineElementObserverThread::Callback::SetPending()
{
    iPending.store(true);
}

void PipelineElementObserverThread::Callback::RunIfPending()
{
    if (iPending.exchange(false)) {
        iCallback();
    }
}


// ElementObserverSync

const TUint ElementObserverSync::kId = 4;

TUint ElementObserverSync::Register(Functor aCallback)
{
    ASSERT(!iCallback);
    iCallback = aCallback;
    return kId;
}

void ElementObserverSync::Schedule(TUint aId)
{
    ASSERT(aId == kId);
    ASSERT(iCallback);
    iCallback();
}

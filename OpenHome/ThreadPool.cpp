#include <OpenHome/ThreadPool.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Debug-ohMediaPlayer.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Private/Thread.h>

#include <atomic>
#include <memory>
#include <vector>

using namespace OpenHome;

// ThreadPool

ThreadPool::ThreadPool(TUint aCountHigh, TUint aCountMedium, TUint aCountLow)
{
    LOG_DEBUG(kThreadPool, "ThreadPool: high=%u, medium=%u, low=%u\n", aCountHigh, aCountMedium, aCountLow);
    iQueueHigh.reset(new PriorityQueue("PoolHigh", aCountHigh, kPriorityHigh));
    iQueueMed.reset(new PriorityQueue("PoolMed", aCountMedium, kPriorityNormal));
    iQueueLow.reset(new PriorityQueue("PoolLow", aCountLow, kPriorityLow));
}

IThreadPoolHandle* ThreadPool::CreateHandle(Functor aCb, const TChar* aId, ThreadPoolPriority aPriority)
{
    LOG_DEBUG(kThreadPool, "ThreadPool::CreateHandle %s\n", aId);
    PriorityQueue* queue = nullptr;
    switch (aPriority)
    {
    case ThreadPoolPriority::Low:
        queue = iQueueLow.get();
        break;
    case ThreadPoolPriority::Medium:
        queue = iQueueMed.get();
        break;
    case ThreadPoolPriority::High:
        queue = iQueueHigh.get();
        break;
    default:
        ASSERTS();
    }
    return queue->CreateHandle(aCb, aId);
}


// ThreadPool::Handle

void ThreadPool::Handle::Destroy()
{
    iQueue.Cancel(*this);
    RemoveRef();
}

TBool ThreadPool::Handle::TrySchedule()
{
    LOG_DEBUG(kThreadPool, "ThreadPool::Handle::TrySchedule %s\n", iId);
    return iQueue.TrySchedule(*this);
}

void ThreadPool::Handle::Cancel()
{
    LOG_DEBUG(kThreadPool, "ThreadPool::Handle::Cancel %s\n", iId);
    iQueue.Cancel(*this);
}

ThreadPool::Handle::Handle(IPriorityQueue& aQueue, Functor aCb, const TChar* aId)
    : iRefCount(1)
    , iLock("TPLH")
    , iQueue(aQueue)
    , iNext(nullptr)
    , iCb(aCb)
    , iId(aId)
    , iPending(false)
    , iCancelled(false)
{
}

ThreadPool::Handle::~Handle()
{
}

void ThreadPool::Handle::Run()
{
    LOG_INFO(kThreadPool, "ThreadPool::Handle::Run %s, iCancelled=%u\n", iId, iCancelled);
    try {
        if (!iCancelled) {
            iCb();
        }
    }
    catch (Exception& ex) {
        LOG_ERROR(kThreadPool, "ThreadPool::Handle::Run %s exception - %s\n", iId, ex.Message());
        iLock.Signal();
        RemoveRef();
        throw;
    }
    iLock.Signal();
    RemoveRef();
}

void ThreadPool::Handle::AddRef()
{
    iRefCount++;
}

void ThreadPool::Handle::RemoveRef()
{
    if (--iRefCount == 0) {
        delete this;
    }
}


// ThreadPool::PriorityQueue

ThreadPool::PriorityQueue::PriorityQueue(const TChar* aNamePrefix, TUint aThCount, TUint aThPriority)
    : iLock("TPL1")
    , iSem("TPL2", 0)
    , iHead(nullptr)
    , iTail(nullptr)
{
    Bws<20> thNameBase(aNamePrefix);
    thNameBase.Append("%u");
    const TChar* fmt = thNameBase.PtrZ();
    for (TUint i = 0; i < aThCount; i++) {
        Bws<20> thName;
        thName.AppendPrintf(fmt, i);
        auto th = new PoolThread(thName.PtrZ(), aThPriority, *this);
        iThreads.push_back(th);
        th->Start();
    }
}

ThreadPool::PriorityQueue::~PriorityQueue()
{
    for (auto it = iThreads.begin(); it != iThreads.end(); ++it) {
        (*it)->Kill();
    }
    for (TUint i = 0; i < iThreads.size(); i++) {
        iSem.Signal();
    }
    for (auto it = iThreads.begin(); it != iThreads.end(); ++it) {
        delete *it;
    }
    if (iHead != nullptr) {
        Log::Print("ThreadPool::PriorityQueue handles leaked:\n");
        auto h = iHead;
        while (h != nullptr) {
            Log::Print("\t%s\n", h->iId);
            h = h->iNext;
        }
    }
}

IThreadPoolHandle* ThreadPool::PriorityQueue::CreateHandle(Functor aCb, const TChar* aId)
{
    return new ThreadPool::Handle(*this, aCb, aId);
}

TBool ThreadPool::PriorityQueue::TrySchedule(Handle& aHandle)
{
    AutoMutex _(iLock);
    if (aHandle.iPending) {
        return false;
    }
    aHandle.iPending = true;
    aHandle.iCancelled = false;
    if (iHead == nullptr) {
        iHead = &aHandle;
    }
    else {
        iTail->iNext = &aHandle;
    }
    iTail = &aHandle;
    iSem.Signal();
    return true;
}

void ThreadPool::PriorityQueue::Cancel(Handle& aHandle)
{
    AutoMutex _(aHandle.iLock);
    aHandle.iCancelled = true;
    AutoMutex __(iLock);
    auto h = iHead;
    Handle* prev = nullptr;
    while (h != nullptr) {
        if (h == &aHandle) {
            break;
        }
        prev = h;
        h = h->iNext;
    }
    if (h != nullptr) {
        if (h == iHead) {
            iHead = h->iNext;
        }
        if (prev != nullptr) {
            prev->iNext = h->iNext;
        }
        if (h == iTail) {
            iTail = prev;
        }
        h->iNext = nullptr;
        h->iPending = false;
    }
}

ThreadPool::ICallback* ThreadPool::PriorityQueue::Dequeue()
{
    iSem.Wait();
    ThreadPool::Handle* h = nullptr;
    {
        AutoMutex _(iLock);
        h = iHead;
        if (h != nullptr) {
            iHead = h->iNext;
            if (iHead == nullptr) {
                iTail = nullptr;
            }
            h->iNext = nullptr;
            h->iPending = false;
            h->AddRef();
        }
    }
    if (h != nullptr) {
        h->iLock.Wait();
    }
    return h;
}


// ThreadPool::PoolThread

ThreadPool::PoolThread::PoolThread(const TChar* aName, TUint aPriority, IQueueReader& aQueueReader)
    : Thread(aName, aPriority)
    , iQueueReader(aQueueReader)
{
}

void ThreadPool::PoolThread::Run()
{
    for (;;) {
        auto cb = iQueueReader.Dequeue();
        if (cb != nullptr) {
            cb->Run();
        }
        CheckForKill();
    }
}

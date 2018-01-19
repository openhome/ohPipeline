#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Private/Thread.h>

#include <atomic>
#include <memory>
#include <vector>

namespace OpenHome {

class IThreadPoolHandle
{
public:
    virtual void Destroy() = 0;
    virtual TBool TrySchedule() = 0;
    virtual void Cancel() = 0;
protected:
    virtual ~IThreadPoolHandle() {}
};

enum class ThreadPoolPriority
{
    Low,
    Medium,
    High
};

class IThreadPool
{
public:
    virtual ~IThreadPool() {}
    virtual IThreadPoolHandle* CreateHandle(Functor aCb, const TChar* aId, ThreadPoolPriority aPriority) = 0;
};

class ThreadPool : public IThreadPool
{
    friend class SuitePriorityQueue;
public:
    ThreadPool(TUint aCountHigh, TUint aCountMedium, TUint aCountLow);
public: // from IThreadPool
    IThreadPoolHandle* CreateHandle(Functor aCb, const TChar* aId, ThreadPoolPriority aPriority) override;
private:
    class IPriorityQueue;
    class PriorityQueue;
    class ICallback
    {
    public:
        virtual ~ICallback() {}
        virtual void Run() = 0;
    };
    class Handle : public IThreadPoolHandle, private ICallback
    {
        friend class PriorityQueue;
    public: // from IThreadPoolHandle
        void Destroy() override;
        TBool TrySchedule() override;
        void Cancel() override;
    private: // from ICallback
        void Run() override;
    private:
        Handle(IPriorityQueue& aQueue, Functor aCb, const TChar* aId);
        ~Handle();
        void AddRef();
        void RemoveRef();
    private:
        std::atomic<TUint> iRefCount;
        Mutex iLock;
        IPriorityQueue& iQueue;
        Handle* iNext;
        Functor iCb;
        const TChar* iId;
        TBool iPending;
        TBool iCancelled;
    };
    class IPriorityQueue
    {
    public:
        virtual ~IPriorityQueue() {}
        virtual TBool TrySchedule(Handle& aHandle) = 0;
        virtual void Cancel(Handle& aHandle) = 0;
    };
    class IQueueReader
    {
    public:
        virtual ~IQueueReader() {}
        virtual ICallback* Dequeue() = 0;
    };
    class PoolThread;
    class PriorityQueue : private IPriorityQueue, private IQueueReader
    {
        friend class SuitePriorityQueue;
    public:
        PriorityQueue(const TChar* aNamePrefix, TUint aThCount, TUint aThPriority);
        ~PriorityQueue();
        IThreadPoolHandle* CreateHandle(Functor aCb, const TChar* aId);
    private: // from IPriorityQueue
        TBool TrySchedule(Handle& aHandle) override;
        void Cancel(Handle& aHandle) override;
    private: // from IQueueReader
        ICallback* Dequeue() override;
    private:
        std::vector<PoolThread*> iThreads;
        Mutex iLock;
        Semaphore iSem;
        Handle* iHead;
        Handle* iTail;
    };
    class PoolThread : public Thread
    {
    public:
        PoolThread(const TChar* aName, TUint aPriority, IQueueReader& aQueueReader);
    private: // from Thread
        void Run();
    private:
        IQueueReader& iQueueReader;
    };
private:
    std::unique_ptr<PriorityQueue> iQueueHigh;
    std::unique_ptr<PriorityQueue> iQueueMed;
    std::unique_ptr<PriorityQueue> iQueueLow;
};

} // namespace OpenHome

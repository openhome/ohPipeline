#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/SuiteUnitTest.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Private/Thread.h>

using namespace OpenHome;
using namespace OpenHome::TestFramework;

namespace OpenHome {

class SuitePriorityQueue : public SuiteUnitTest
{
public:
    SuitePriorityQueue();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    void Cb1();
    void Cb2();
    void Cb3();
    void Cb4();
    void Cb5();
private:
    void TestScheduleThenRun();
    void TestScheduleRunRepeat();
    void TestScheduleThenCancelFromHead();
    void TestScheduleThenCancelFromMiddle();
    void TestScheduleThenCancelFromTail();
    void TestScheduleCancelScheduleRun();
    void TestScheduleWhilePending();
    void TestScheduleWhileRunning();
    void TestScheduleFromCallback();
private:
    ThreadPool::PriorityQueue* iQueue;
    IThreadPoolHandle* iHandleCbs[5];
    Semaphore iSemCb1;
    Semaphore iSemCb2Entry;
    Semaphore iSemCb2Exit;
    Semaphore iSemCb3;
    Semaphore iSemCb4;
    Semaphore iSemCb5;
    TUint iCountCbs[5];
};

class SuiteThreadPool : public Suite
{
public:
    SuiteThreadPool();
    ~SuiteThreadPool();
private: // from Suite
    void Test() override;
private:
    void CbHigh();
    void CbMedium();
    void CbLow();
private:
    ThreadPool* iPool;
    IThreadPoolHandle* iHandleHigh;
    IThreadPoolHandle* iHandleMedium;
    IThreadPoolHandle* iHandleLow;
    Semaphore iSemCbHigh;
    Semaphore iSemCb2Medium;
    Semaphore iSemCb2Low;
    TUint iCountCbHigh;
    TUint iCountCbMedium;
    TUint iCountCbLow;
};

} // namespace OpenHome


// SuitePriorityQueue

SuitePriorityQueue::SuitePriorityQueue()
    : SuiteUnitTest("PriorityQueue")
    , iSemCb1("SPQ1", 0)
    , iSemCb2Entry("SPQ2", 0)
    , iSemCb2Exit("SPQ3", 0)
    , iSemCb3("SPQ4", 0)
    , iSemCb4("SPQ5", 0)
    , iSemCb5("SPQ6", 0)
{
    AddTest(MakeFunctor(*this, &SuitePriorityQueue::TestScheduleThenRun), "TestScheduleThenRun");
    AddTest(MakeFunctor(*this, &SuitePriorityQueue::TestScheduleRunRepeat), "TestScheduleRunRepeat");
    AddTest(MakeFunctor(*this, &SuitePriorityQueue::TestScheduleThenCancelFromHead), "TestScheduleThenCancelFromHead");
    AddTest(MakeFunctor(*this, &SuitePriorityQueue::TestScheduleThenCancelFromMiddle), "TestScheduleThenCancelFromMiddle");
    AddTest(MakeFunctor(*this, &SuitePriorityQueue::TestScheduleThenCancelFromTail), "TestScheduleThenCancelFromTail");
    AddTest(MakeFunctor(*this, &SuitePriorityQueue::TestScheduleCancelScheduleRun), "TestScheduleCancelScheduleRun");
    AddTest(MakeFunctor(*this, &SuitePriorityQueue::TestScheduleWhilePending), "TestScheduleWhilePending");
    AddTest(MakeFunctor(*this, &SuitePriorityQueue::TestScheduleWhileRunning), "TestScheduleWhileRunning");
    AddTest(MakeFunctor(*this, &SuitePriorityQueue::TestScheduleFromCallback), "TestScheduleFromCallback");
}

void SuitePriorityQueue::Setup()
{
    iQueue = new ThreadPool::PriorityQueue("TestPriorityQueue", 1, kPriorityNormal);
    iHandleCbs[0] = iQueue->CreateHandle(MakeFunctor(*this, &SuitePriorityQueue::Cb1), "Cb1");
    iHandleCbs[1] = iQueue->CreateHandle(MakeFunctor(*this, &SuitePriorityQueue::Cb2), "Cb2");
    iHandleCbs[2] = iQueue->CreateHandle(MakeFunctor(*this, &SuitePriorityQueue::Cb3), "Cb3");
    iHandleCbs[3] = iQueue->CreateHandle(MakeFunctor(*this, &SuitePriorityQueue::Cb4), "Cb4");
    iHandleCbs[4] = iQueue->CreateHandle(MakeFunctor(*this, &SuitePriorityQueue::Cb5), "Cb5");
    (void)iSemCb1.Clear();
    (void)iSemCb2Entry.Clear();
    (void)iSemCb2Exit.Clear();
    (void)iSemCb3.Clear();
    (void)iSemCb4.Clear();
    (void)iSemCb5.Clear();
    iCountCbs[0] = iCountCbs[1] = iCountCbs[2] = iCountCbs[3] = iCountCbs[4] = 0;
}

void SuitePriorityQueue::TearDown()
{
    for (TUint i = 0; i < 5; i++) {
        iHandleCbs[i]->Destroy();
    }
    delete iQueue;
}

void SuitePriorityQueue::Cb1()
{
    iCountCbs[0]++;
    iSemCb1.Signal();
}

void SuitePriorityQueue::Cb2()
{
    iCountCbs[1]++;
    iSemCb2Entry.Signal();
    iSemCb2Exit.Wait();
}

void SuitePriorityQueue::Cb3()
{
    iCountCbs[2]++;
    iSemCb3.Signal();
}

void SuitePriorityQueue::Cb4()
{
    iCountCbs[3]++;
    iSemCb4.Signal();
}

void SuitePriorityQueue::Cb5()
{
    if ((iCountCbs[4] & 1) == 0) {
        TEST(iHandleCbs[4]->TrySchedule());
    }
    iCountCbs[4]++;
    iSemCb5.Signal();
}

void SuitePriorityQueue::TestScheduleThenRun()
{
    TEST(iHandleCbs[0]->TrySchedule());
    iSemCb1.Wait();
    TEST(iCountCbs[0] == 1);
}

void SuitePriorityQueue::TestScheduleRunRepeat()
{
    TEST(iHandleCbs[0]->TrySchedule());
    iSemCb1.Wait();
    TEST(iCountCbs[0] == 1);
    TEST(iHandleCbs[0]->TrySchedule());
    iSemCb1.Wait();
    TEST(iCountCbs[0] == 2);
}

void SuitePriorityQueue::TestScheduleThenCancelFromHead()
{
    TEST(iHandleCbs[1]->TrySchedule());
    TEST(iHandleCbs[0]->TrySchedule());
    iSemCb2Entry.Wait();
    TEST(iCountCbs[1] == 1);
    iHandleCbs[0]->Cancel();
    iSemCb2Exit.Signal();
    TEST_THROWS(iSemCb1.Wait(50), Timeout);
    TEST(iCountCbs[0] == 0);
    TEST(iCountCbs[1] == 1);
}

void SuitePriorityQueue::TestScheduleThenCancelFromMiddle()
{
    TEST(iHandleCbs[1]->TrySchedule());
    TEST(iHandleCbs[0]->TrySchedule());
    TEST(iHandleCbs[2]->TrySchedule());
    TEST(iHandleCbs[3]->TrySchedule());
    iHandleCbs[2]->Cancel();
    iSemCb2Exit.Signal();
    iSemCb4.Wait();
    TEST(iCountCbs[0] == 1);
    TEST(iCountCbs[1] == 1);
    TEST(iCountCbs[2] == 0);
    TEST(iCountCbs[3] == 1);
}

void SuitePriorityQueue::TestScheduleThenCancelFromTail()
{
    TEST(iHandleCbs[1]->TrySchedule());
    TEST(iHandleCbs[0]->TrySchedule());
    TEST(iHandleCbs[2]->TrySchedule());
    TEST(iHandleCbs[3]->TrySchedule());
    iHandleCbs[3]->Cancel();
    iSemCb2Exit.Signal();
    iSemCb3.Wait();
    TEST_THROWS(iSemCb4.Wait(50), Timeout);
    TEST(iCountCbs[0] == 1);
    TEST(iCountCbs[1] == 1);
    TEST(iCountCbs[2] == 1);
    TEST(iCountCbs[3] == 0);
}

void SuitePriorityQueue::TestScheduleCancelScheduleRun()
{
    TEST(iHandleCbs[1]->TrySchedule());
    TEST(iHandleCbs[0]->TrySchedule());
    iHandleCbs[0]->Cancel();
    TEST(iHandleCbs[0]->TrySchedule());
    iSemCb2Exit.Signal();
    iSemCb1.Wait();
    TEST_THROWS(iSemCb1.Wait(50), Timeout);
    TEST(iCountCbs[0] == 1);
    TEST(iCountCbs[1] == 1);
}

void SuitePriorityQueue::TestScheduleWhilePending()
{
    TEST(iHandleCbs[1]->TrySchedule());
    TEST(iHandleCbs[0]->TrySchedule());
    TEST(!iHandleCbs[0]->TrySchedule());
    iSemCb2Exit.Signal();
    iSemCb1.Wait();
    TEST_THROWS(iSemCb1.Wait(50), Timeout);
    TEST(iCountCbs[0] == 1);
    TEST(iCountCbs[1] == 1);
}

void SuitePriorityQueue::TestScheduleWhileRunning()
{
    TEST(iHandleCbs[1]->TrySchedule());
    iSemCb2Entry.Wait();
    TEST(iHandleCbs[1]->TrySchedule());
    iSemCb2Exit.Signal();
    iSemCb2Entry.Wait();
    iSemCb2Exit.Signal();
    TEST(iCountCbs[1] == 2);
}

void SuitePriorityQueue::TestScheduleFromCallback()
{
    TEST(iHandleCbs[4]->TrySchedule());
    iSemCb5.Wait();
    iSemCb5.Wait();
    TEST(iCountCbs[4] == 2);
}


// SuiteThreadPool

SuiteThreadPool::SuiteThreadPool()
    : Suite("ThreadPool")
    , iSemCbHigh("STP1", 0)
    , iSemCb2Medium("STP2", 0)
    , iSemCb2Low("STP3", 0)
{
    iPool = new ThreadPool(4, 4, 2);
    iHandleHigh = iPool->CreateHandle(MakeFunctor(*this, &SuiteThreadPool::CbHigh), "CbHigh", ThreadPoolPriority::High);
    iHandleMedium = iPool->CreateHandle(MakeFunctor(*this, &SuiteThreadPool::CbMedium), "CbMedium", ThreadPoolPriority::Medium);
    iHandleLow = iPool->CreateHandle(MakeFunctor(*this, &SuiteThreadPool::CbLow), "CbLow", ThreadPoolPriority::Low);
    iCountCbHigh = iCountCbMedium = iCountCbLow = 0;
}

SuiteThreadPool::~SuiteThreadPool()
{
    iHandleHigh->Destroy();
    iHandleLow->Destroy();
    iHandleMedium->Destroy();
    delete iPool;
}

void SuiteThreadPool::Test()
{
    TEST(iHandleMedium->TrySchedule());
    TEST(iHandleLow->TrySchedule());
    TEST(iHandleHigh->TrySchedule());
    // wait on all 3 semaphores - not all platforms offer proper priority based scheduling
    iSemCbHigh.Wait();
    iSemCb2Medium.Wait();
    iSemCb2Low.Wait();
    TEST(iCountCbHigh == 1);
    TEST(iCountCbMedium == 1);
    TEST(iCountCbLow == 1);
}

void SuiteThreadPool::CbHigh()
{
    iCountCbHigh++;
    iSemCbHigh.Signal();
}

void SuiteThreadPool::CbMedium()
{
    iCountCbMedium++;
    iSemCb2Medium.Signal();
}

void SuiteThreadPool::CbLow()
{
    iCountCbLow++;
    iSemCb2Low.Signal();
}



void TestThreadPool()
{
    Runner runner("ThreadPool tests\n");
    runner.Add(new SuitePriorityQueue());
    runner.Add(new SuiteThreadPool());
    runner.Run();
}

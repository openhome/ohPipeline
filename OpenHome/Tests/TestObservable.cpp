#include <OpenHome/Observable.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/SuiteUnitTest.h>

using namespace OpenHome;
using namespace OpenHome::TestFramework;

namespace OpenHome {

class MockObserver
{
    public:
        MockObserver();
        ~MockObserver();

    public:
        void Notify();

        TUint CallCount() const;
        void Reset();

    private:
        TUint iCallCount;
};


class SuiteObservable : public Suite
{
    public:
        SuiteObservable();
        ~SuiteObservable();

    public: // Suite
        void Test() override;
};


}; // namespace OpenHome


// MockObserver
MockObserver::MockObserver()
    : iCallCount(0)
{ }

MockObserver::~MockObserver()
{ }

void MockObserver::Notify()
{
    ++iCallCount;
}

TUint MockObserver::CallCount() const
{
    return iCallCount;
}

void MockObserver::Reset()
{
    iCallCount = 0;
}


// SuiteObserver
SuiteObservable::SuiteObservable()
    : Suite("TestObservable")
{ }

SuiteObservable::~SuiteObservable()
{ }

void SuiteObservable::Test()
{
    MockObserver observerA, observerB, observerC;
    Observable<MockObserver> subject;

    // Creating local here instead of specifying in every call. There is no reason why we can't
    // specify lambda directly at point of calling NotifyAll()
    std::function<void (MockObserver&)> notifyFunc = [] (MockObserver& o) {
        o.Notify();
    };

    subject.AddObserver(observerA);
    subject.AddObserver(observerB);
    subject.NotifyAll(notifyFunc);

    TEST(observerA.CallCount() == 1);
    TEST(observerB.CallCount() == 1);
    TEST(observerC.CallCount() == 0);


    subject.AddObserver(observerC);
    subject.NotifyAll(notifyFunc);

    TEST(observerA.CallCount() == 2);
    TEST(observerB.CallCount() == 2);
    TEST(observerC.CallCount() == 1);


    subject.RemoveObserver(observerA);
    subject.NotifyAll(notifyFunc);

    TEST(observerA.CallCount() == 2);
    TEST(observerB.CallCount() == 3);
    TEST(observerC.CallCount() == 2);


    subject.RemoveObserver(observerB);
    subject.RemoveObserver(observerC);
    subject.NotifyAll(notifyFunc);

    TEST(observerA.CallCount() == 2);
    TEST(observerB.CallCount() == 3);
    TEST(observerC.CallCount() == 2);
}


void TestObservable()
{
    Runner runner("Observable tests\n");
    runner.Add(new SuiteObservable());
    runner.Run();
}

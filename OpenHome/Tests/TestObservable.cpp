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

    private:
        void NotifyObserver(MockObserver& aObserver);
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

void SuiteObservable::NotifyObserver(MockObserver& aObserver)
{
    aObserver.Notify();
}

void SuiteObservable::Test()
{
    MockObserver observerA, observerB, observerC;
    Observable<MockObserver> subject;

    // Creating local here instead of specifying in every call. There is no reason why we can't
    // specify lambda directly at point of calling NotifyAll()
    std::function<void (MockObserver&)> notifyFunc = [] (MockObserver& o) {
        o.Notify();
    };

    subject.AddObserver(observerA, "foo");
    subject.AddObserver(observerB, "bar");
    subject.NotifyAll(notifyFunc);

    TEST(observerA.CallCount() == 1);
    TEST(observerB.CallCount() == 1);
    TEST(observerC.CallCount() == 0);


    subject.AddObserver(observerC, "foobar");
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

    // Reset before trying with a FunctorGeneric
    observerA.Reset();
    observerB.Reset();
    observerC.Reset();

    // Calling with a FunctorGeneric instead of a Lambda
    FunctorGeneric<MockObserver&> notifyFunc2 = MakeFunctorGeneric<MockObserver&>(*this, &SuiteObservable::NotifyObserver);

    subject.AddObserver(observerA, "Test-A");
    subject.AddObserver(observerB, "Test-B");
    subject.AddObserver(observerC, "Test-C");

    subject.NotifyAll(notifyFunc2);

    TEST(observerA.CallCount() == 1);
    TEST(observerB.CallCount() == 1);
    TEST(observerC.CallCount() == 1);

    // And finally cleanup to remove observers otherwise we'll assert
    subject.RemoveObserver(observerA);
    subject.RemoveObserver(observerB);
    subject.RemoveObserver(observerC);
}


void TestObservable()
{
    Runner runner("Observable tests\n");
    runner.Add(new SuiteObservable());
    runner.Run();
}

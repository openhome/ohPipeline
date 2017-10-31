#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/SuiteUnitTest.h>
#include <OpenHome/Av/VolumeManager.h>

#include <limits>

namespace OpenHome {
namespace Av {
namespace Test {

class MockVolumeReporter : public IVolumeReporter
{
public:
    MockVolumeReporter();
    const IVolumeObserver& Observer() const;
    void Clear();
public: // from IVolumeReporter
    void AddVolumeObserver(IVolumeObserver& aObserver) override;
private:
    IVolumeObserver* iObserver;
};

class MockVolumeOffset : public IVolumeSourceOffset
{
public:
    MockVolumeOffset();
    TInt Offset() const;
public: // from IVolumeSourceOffset
    void SetVolumeOffset(TInt aOffset) override;
private:
    TInt iOffset;
};

} // namespace Test

class SuiteVolumeScaler : public TestFramework::SuiteUnitTest
{
public:
    SuiteVolumeScaler();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    void TestRangeOverflow();
    void TestEnable();
    void TestUserVolumeInvalid();
    void TestExternalVolumeInvalid();
    void TestLimits();
    void TestUserVolumeChanges();
    void TestExternalVolumeChanges();
private:
    Test::MockVolumeReporter* iReporter;
    Test::MockVolumeOffset* iOffset;
};

class SuiteVolumeMuterStepped : public TestFramework::SuiteUnitTest
                        , private IVolume
{
    static const TUint kVolumeMilliDbPerStep = 1024;
    static const TUint kVolumeInvalid = UINT_MAX;
public:
    SuiteVolumeMuterStepped();
private: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private: // from IVolume
    void SetVolume(TUint aVolume) override;
private:
    void SetVolumeSync(TUint aVolume);
    void WaitForVolumeChange();
private:
    void TestVolumePassedThruWhenRunning();
    void TestVolumeNotPassedWhenMuting();
    void TestVolumeStepsWhileMuting();
    void TestVolumeChangesOnSetMuted();
    void TestVolumeChangesOnSetUnmuted();
    void TestCompletionReportedWhenMuted();
    void TestVolumeNotPassedWhenMuted();
    void TestVolumeNotPassedWhenUnmuting();
    void TestVolumeStepsWhileUnmuting();
    void TestCompletionReportedWhenUnmuted();
    void TestVolumePassedOnceUnmuted();
private:
    VolumeMuterStepped* iVolumeMuterStepped;
    TUint iVolume;
    Mutex iLock;
    Semaphore iSem;
};

} // namespace Av
} // namespace OpenHome


using namespace OpenHome;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Av;
using namespace OpenHome::Av::Test;


// MockVolumeReporter

MockVolumeReporter::MockVolumeReporter()
    : iObserver(nullptr)
{
}

const IVolumeObserver& MockVolumeReporter::Observer() const
{
    return *iObserver;
}

void MockVolumeReporter::Clear()
{
    iObserver = nullptr;
}

void MockVolumeReporter::AddVolumeObserver(IVolumeObserver& aObserver)
{
    ASSERT(iObserver == nullptr);
    iObserver = &aObserver;
}


// MockVolumeOffset

MockVolumeOffset::MockVolumeOffset()
    : iOffset(0)
{
}

TInt MockVolumeOffset::Offset() const
{
    return iOffset;
}

void MockVolumeOffset::SetVolumeOffset(TInt aOffset)
{
    iOffset = aOffset;
}


// SuiteVolumeScaler

SuiteVolumeScaler::SuiteVolumeScaler()
    : SuiteUnitTest("SuiteVolumeScaler")
{
    AddTest(MakeFunctor(*this, &SuiteVolumeScaler::TestRangeOverflow), "TestRangeOverflow");
    AddTest(MakeFunctor(*this, &SuiteVolumeScaler::TestEnable), "TestEnable");
    AddTest(MakeFunctor(*this, &SuiteVolumeScaler::TestUserVolumeInvalid), "TestUserVolumeInvalid");
    AddTest(MakeFunctor(*this, &SuiteVolumeScaler::TestExternalVolumeInvalid), "TestExternalVolumeInvalid");
    AddTest(MakeFunctor(*this, &SuiteVolumeScaler::TestLimits), "TestLimits");
    AddTest(MakeFunctor(*this, &SuiteVolumeScaler::TestUserVolumeChanges), "TestUserVolumeChanges");
    AddTest(MakeFunctor(*this, &SuiteVolumeScaler::TestExternalVolumeChanges), "TestExternalVolumeChanges");
}

void SuiteVolumeScaler::Setup()
{
    iReporter = new MockVolumeReporter();
    iOffset = new MockVolumeOffset();
}

void SuiteVolumeScaler::TearDown()
{
    delete iOffset;
    delete iReporter;
}

void SuiteVolumeScaler::TestRangeOverflow()
{
    static const TUint kMaxUint = std::numeric_limits<TUint>::max();
    // Test on TUint max limit. Shouldn't assert.
    {
        VolumeScaler(*iReporter, *iOffset, kMaxUint, 1);
        iReporter->Clear();
    }
    {
        // 65535^2 is as close as possible to kMaxUint without overflow.
        VolumeScaler(*iReporter, *iOffset, 1, 65535);
        iReporter->Clear();
    }
    {
        VolumeScaler(*iReporter, *iOffset, kMaxUint/4, 2);
        iReporter->Clear();
    }

    // Test small overflows.
    TEST_THROWS(VolumeScaler(*iReporter, *iOffset, kMaxUint/2+1, 2), AssertionFailed);
    
    // Test larger overflows.
    TEST_THROWS(VolumeScaler(*iReporter, *iOffset, kMaxUint, 2), AssertionFailed);
    TEST_THROWS(VolumeScaler(*iReporter, *iOffset, 2, kMaxUint), AssertionFailed);
    TEST_THROWS(VolumeScaler(*iReporter, *iOffset, kMaxUint, kMaxUint), AssertionFailed);
}

void SuiteVolumeScaler::TestEnable()
{
    // Assume 1024 binary-milli-Db per step. So 100*1024 = 102400 binary-milli-Db max.
    VolumeScaler scaler(*iReporter, *iOffset, 102400, 50);
    TEST(iOffset->Offset() == 0);
    const VolumeValue vol(50, 51200);
    scaler.VolumeChanged(vol);
    scaler.SetVolume(25);
    TEST(iOffset->Offset() == 0);
    scaler.SetVolumeEnabled(true);
    TEST(iOffset->Offset() == -25600);

    scaler.SetVolumeEnabled(false);
    TEST(iOffset->Offset() == 0);
}

void SuiteVolumeScaler::TestUserVolumeInvalid()
{
    VolumeScaler scaler(*iReporter, *iOffset, 102400, 50);
    const VolumeValue vol1(101, 103424);
    TEST_THROWS(scaler.VolumeChanged(vol1), AssertionFailed);
    const VolumeValue vol2(999, 1022976);
    TEST_THROWS(scaler.VolumeChanged(vol2), AssertionFailed);
}

void SuiteVolumeScaler::TestExternalVolumeInvalid()
{
    VolumeScaler scaler(*iReporter, *iOffset, 102400, 50);
    TEST_THROWS(scaler.SetVolume(51), AssertionFailed);
    TEST_THROWS(scaler.SetVolume(999), AssertionFailed);
}

void SuiteVolumeScaler::TestLimits()
{
    // Max user vol > max external vol.
    {
        VolumeScaler scaler(*iReporter, *iOffset, 102400, 50);
        scaler.SetVolumeEnabled(true);
        // User: 0, external: 0.
        const VolumeValue vol0(0, 0);
        scaler.VolumeChanged(vol0);
        scaler.SetVolume(0);
        TEST(iOffset->Offset() == 0);
        // User: 100, external: 50.
        const VolumeValue vol100(100, 102400);
        scaler.VolumeChanged(vol100);
        scaler.SetVolume(50);
        TEST(iOffset->Offset() == 0);
        iReporter->Clear();
    }
    // Max user vol < max external vol.
    {
        VolumeScaler scaler(*iReporter, *iOffset, 51200, 100);
        scaler.SetVolumeEnabled(true);
        // User: 0, external: 0.
        const VolumeValue vol0(0, 0);
        scaler.VolumeChanged(vol0);
        scaler.SetVolume(0);
        // User: 50, external: 100.
        const VolumeValue vol50(50, 51200);
        scaler.VolumeChanged(vol50);
        scaler.SetVolume(100);
        TEST(iOffset->Offset() == 0);
        iReporter->Clear();
    }
    // Max user vol == max external vol.
    {
        VolumeScaler scaler(*iReporter, *iOffset, 102400, 100);
        scaler.SetVolumeEnabled(true);
        // User: 0, external: 0.
        const VolumeValue vol0(0, 0);
        scaler.VolumeChanged(vol0);
        scaler.SetVolume(0);
        TEST(iOffset->Offset() == 0);
        // User: 100, external: 100.
        const VolumeValue vol100(100, 102400);
        scaler.VolumeChanged(vol100);
        scaler.SetVolume(100);
        TEST(iOffset->Offset() == 0);
        iReporter->Clear();
    }
}

void SuiteVolumeScaler::TestUserVolumeChanges()
{
    VolumeScaler scaler(*iReporter, *iOffset, 102400, 100);
    scaler.SetVolumeEnabled(true);

    const VolumeValue vol0(0, 0);
    const VolumeValue vol25(25, 25600);
    const VolumeValue vol50(50, 51200);
    const VolumeValue vol75(75, 76800);
    const VolumeValue vol100(100, 102400);

    // External vol at 0.
    scaler.SetVolume(0);
    scaler.VolumeChanged(vol0);
    TEST(iOffset->Offset() == 0);
    scaler.VolumeChanged(vol25);
    TEST(iOffset->Offset() == -25600);
    scaler.VolumeChanged(vol50);
    TEST(iOffset->Offset() == -51200);
    scaler.VolumeChanged(vol75);
    TEST(iOffset->Offset() == -76800);
    scaler.VolumeChanged(vol100);
    TEST(iOffset->Offset() == -102400);

    // External vol at 50.
    scaler.SetVolume(50);
    scaler.VolumeChanged(vol0);
    TEST(iOffset->Offset() == 0);
    scaler.VolumeChanged(vol25);
    TEST(iOffset->Offset() == -12800);
    scaler.VolumeChanged(vol50);
    TEST(iOffset->Offset() == -25600);
    scaler.VolumeChanged(vol75);
    TEST(iOffset->Offset() == -38400);
    scaler.VolumeChanged(vol100);
    TEST(iOffset->Offset() == -51200);

    // External vol at 100.
    scaler.SetVolume(100);
    scaler.VolumeChanged(vol0);
    TEST(iOffset->Offset() == 0);
    scaler.VolumeChanged(vol25);
    TEST(iOffset->Offset() == 0);
    scaler.VolumeChanged(vol50);
    TEST(iOffset->Offset() == 0);
    scaler.VolumeChanged(vol75);
    TEST(iOffset->Offset() == 0);
    scaler.VolumeChanged(vol100);
    TEST(iOffset->Offset() == 0);
}

void SuiteVolumeScaler::TestExternalVolumeChanges()
{
    VolumeScaler scaler(*iReporter, *iOffset, 102400, 100);
    scaler.SetVolumeEnabled(true);

    // User vol at 0.
    const VolumeValue vol0(0, 0);
    scaler.VolumeChanged(vol0);
    scaler.SetVolume(0);
    TEST(iOffset->Offset() == 0);
    scaler.SetVolume(25);
    TEST(iOffset->Offset() == 0);
    scaler.SetVolume(50);
    TEST(iOffset->Offset() == 0);
    scaler.SetVolume(75);
    TEST(iOffset->Offset() == 0);
    scaler.SetVolume(100);
    TEST(iOffset->Offset() == 0);

    // User vol at 50.
    const VolumeValue vol50(50, 51200);
    scaler.VolumeChanged(vol50);
    scaler.SetVolume(0);
    TEST(iOffset->Offset() == -51200);
    scaler.SetVolume(25);
    TEST(iOffset->Offset() == -38400);
    scaler.SetVolume(50);
    TEST(iOffset->Offset() == -25600);
    scaler.SetVolume(75);
    TEST(iOffset->Offset() == -12800);
    scaler.SetVolume(100);
    TEST(iOffset->Offset() == 0);

    // User vol at 100.
    const VolumeValue vol100(100, 102400);
    scaler.VolumeChanged(vol100);
    scaler.SetVolume(0);
    TEST(iOffset->Offset() == -102400);
    scaler.SetVolume(25);
    TEST(iOffset->Offset() == -76800);
    scaler.SetVolume(50);
    TEST(iOffset->Offset() == -51200);
    scaler.SetVolume(75);
    TEST(iOffset->Offset() == -25600);
    scaler.SetVolume(100);
    TEST(iOffset->Offset() == 0);
}


// SuiteVolumeMuterStepped

SuiteVolumeMuterStepped::SuiteVolumeMuterStepped()
    : SuiteUnitTest("VolumeMuterStepped")
    , iLock("SVR1")
    , iSem("SVR2", 0)
{
    AddTest(MakeFunctor(*this, &SuiteVolumeMuterStepped::TestVolumePassedThruWhenRunning), "TestVolumePassedThruWhenRunning");
    AddTest(MakeFunctor(*this, &SuiteVolumeMuterStepped::TestVolumeNotPassedWhenMuting), "TestVolumeNotPassedWhenMuting");
    AddTest(MakeFunctor(*this, &SuiteVolumeMuterStepped::TestVolumeStepsWhileMuting), "TestVolumeStepsWhileMuting");
    AddTest(MakeFunctor(*this, &SuiteVolumeMuterStepped::TestVolumeChangesOnSetMuted), "TestVolumeChangesOnSetMuted");
    AddTest(MakeFunctor(*this, &SuiteVolumeMuterStepped::TestVolumeChangesOnSetUnmuted), "TestVolumeChangesOnSetUnmuted");
    AddTest(MakeFunctor(*this, &SuiteVolumeMuterStepped::TestCompletionReportedWhenMuted), "TestCompletionReportedWhenMuted");
    AddTest(MakeFunctor(*this, &SuiteVolumeMuterStepped::TestVolumeNotPassedWhenMuted), "TestVolumeNotPassedWhenMuted");
    AddTest(MakeFunctor(*this, &SuiteVolumeMuterStepped::TestVolumeNotPassedWhenUnmuting), "TestVolumeNotPassedWhenUnmuting");
    AddTest(MakeFunctor(*this, &SuiteVolumeMuterStepped::TestVolumeStepsWhileUnmuting), "TestVolumeStepsWhileUnmuting");
    AddTest(MakeFunctor(*this, &SuiteVolumeMuterStepped::TestCompletionReportedWhenUnmuted), "TestCompletionReportedWhenUnmuted");
    AddTest(MakeFunctor(*this, &SuiteVolumeMuterStepped::TestVolumePassedOnceUnmuted), "TestVolumePassedOnceUnmuted");
}

void SuiteVolumeMuterStepped::Setup()
{
    iVolumeMuterStepped = new VolumeMuterStepped(*this, kVolumeMilliDbPerStep, kPriorityNormal);
    iVolume = kVolumeInvalid;
    (void)iSem.Clear();
}

void SuiteVolumeMuterStepped::TearDown()
{
    delete iVolumeMuterStepped;
}

void SuiteVolumeMuterStepped::SetVolume(TUint aVolume)
{
    AutoMutex _(iLock);
    iVolume = aVolume;
    iSem.Signal();
}

void SuiteVolumeMuterStepped::SetVolumeSync(TUint aVolume)
{
    iVolumeMuterStepped->SetVolume(aVolume);
    WaitForVolumeChange();
}

void SuiteVolumeMuterStepped::WaitForVolumeChange()
{
    iSem.Wait();
}

void SuiteVolumeMuterStepped::TestVolumePassedThruWhenRunning()
{
    static const TUint kVolume = 50 * kVolumeMilliDbPerStep;
    TEST(iVolume != kVolume);
    SetVolumeSync(kVolume);
    TEST(iVolume == kVolume);
}

void SuiteVolumeMuterStepped::TestVolumeNotPassedWhenMuting()
{
    static const TUint kVolumeInitial = 50 * kVolumeMilliDbPerStep;
    static const TUint kVolumeUpdated = 49 * kVolumeMilliDbPerStep;
    SetVolumeSync(kVolumeInitial);
    const auto pending = iVolumeMuterStepped->iPendingVolume;
    TEST(iVolumeMuterStepped->BeginMute() == Media::IVolumeMuterStepped::Status::eInProgress);
    iVolumeMuterStepped->SetVolume(kVolumeUpdated);
    TEST(iVolumeMuterStepped->iPendingVolume == pending);
    TEST_THROWS(iSem.Wait(10), Timeout);
    TEST(iVolumeMuterStepped->iPendingVolume == pending);
    TEST(iVolume == kVolumeInitial);
}

void SuiteVolumeMuterStepped::TestVolumeStepsWhileMuting()
{
    static const TUint kVolumeInitial = 50 * kVolumeMilliDbPerStep;
    SetVolumeSync(kVolumeInitial);
    auto prevVolume = iVolume;
    static const TUint kJiffies = (5 * Media::Jiffies::kPerMs) - 1;
    auto pending = iVolumeMuterStepped->iPendingVolume;
    TEST(iVolumeMuterStepped->BeginMute() == Media::IVolumeMuterStepped::Status::eInProgress);
    do {
        TEST(iVolumeMuterStepped->StepMute(kJiffies) == Media::IVolumeMuterStepped::Status::eInProgress);
        if (pending != iVolumeMuterStepped->iPendingVolume) {
            WaitForVolumeChange();
            TEST(iVolume < prevVolume);
            pending = iVolumeMuterStepped->iPendingVolume;
            prevVolume = iVolume;
        }
    } while (iVolume > 0);
}

void SuiteVolumeMuterStepped::TestVolumeChangesOnSetMuted()
{
    static const TUint kVolumeInitial = 50 * kVolumeMilliDbPerStep;
    SetVolumeSync(kVolumeInitial);
    iVolumeMuterStepped->SetMuted();
    WaitForVolumeChange();
    TEST(iVolume == 0);
}

void SuiteVolumeMuterStepped::TestVolumeChangesOnSetUnmuted()
{
    static const TUint kVolumeInitial = 50 * kVolumeMilliDbPerStep;
    SetVolumeSync(kVolumeInitial);
    iVolumeMuterStepped->SetMuted();
    WaitForVolumeChange();
    iVolumeMuterStepped->SetUnmuted();
    WaitForVolumeChange();
    TEST(iVolume == kVolumeInitial);
}

void SuiteVolumeMuterStepped::TestCompletionReportedWhenMuted()
{
    static const TUint kVolumeInitial = 10 * kVolumeMilliDbPerStep;
    SetVolumeSync(kVolumeInitial);
    static const TUint kJiffies = 10 * Media::Jiffies::kPerMs;
    auto pending = iVolumeMuterStepped->iPendingVolume;
    TEST(iVolumeMuterStepped->BeginMute() == Media::IVolumeMuterStepped::Status::eInProgress);
    do {
        TEST(iVolumeMuterStepped->StepMute(kJiffies) == Media::IVolumeMuterStepped::Status::eInProgress);
        if (pending != iVolumeMuterStepped->iPendingVolume) {
            WaitForVolumeChange();
            pending = iVolumeMuterStepped->iPendingVolume;
        }
    } while (iVolume > 0);
    TEST(iVolumeMuterStepped->StepMute(kJiffies) == Media::IVolumeMuterStepped::Status::eComplete);
}

void SuiteVolumeMuterStepped::TestVolumeNotPassedWhenMuted()
{
    static const TUint kVolumeInitial = 50 * kVolumeMilliDbPerStep;
    SetVolumeSync(kVolumeInitial);
    iVolumeMuterStepped->SetMuted();
    WaitForVolumeChange();
    TEST(iVolume == 0);
    static const TUint kVolumeUpdated = 35 * kVolumeMilliDbPerStep;
    iVolumeMuterStepped->SetVolume(kVolumeUpdated);
    TEST_THROWS(iSem.Wait(10), Timeout);
}

void SuiteVolumeMuterStepped::TestVolumeNotPassedWhenUnmuting()
{
    static const TUint kVolumeInitial = 50 * kVolumeMilliDbPerStep;
    SetVolumeSync(kVolumeInitial);
    iVolumeMuterStepped->SetMuted();
    WaitForVolumeChange();
    TEST(iVolumeMuterStepped->BeginUnmute() == Media::IVolumeMuterStepped::Status::eInProgress);

    static const TUint kVolumeUpdated = 35 * kVolumeMilliDbPerStep;
    iVolumeMuterStepped->SetVolume(kVolumeUpdated);
    TEST_THROWS(iSem.Wait(10), Timeout);

    static const TUint kJiffies = (5 * Media::Jiffies::kPerMs) - 1;
    TEST(iVolumeMuterStepped->StepUnmute(kJiffies) == Media::IVolumeMuterStepped::Status::eInProgress);
    TEST_THROWS(iSem.Wait(10), Timeout);
}

void SuiteVolumeMuterStepped::TestVolumeStepsWhileUnmuting()
{
    static const TUint kVolumeInitial = 50 * kVolumeMilliDbPerStep;
    SetVolumeSync(kVolumeInitial);
    iVolumeMuterStepped->SetMuted();
    WaitForVolumeChange();
    auto prevVolume = iVolume;
    static const TUint kJiffies = (5 * Media::Jiffies::kPerMs) - 1;
    auto pending = iVolumeMuterStepped->iPendingVolume;
    TEST(iVolumeMuterStepped->BeginUnmute() == Media::IVolumeMuterStepped::Status::eInProgress);
    do {
        TEST(iVolumeMuterStepped->StepUnmute(kJiffies) == Media::IVolumeMuterStepped::Status::eInProgress);
        if (pending != iVolumeMuterStepped->iPendingVolume) {
            WaitForVolumeChange();
            TEST(iVolume > prevVolume);
            pending = iVolumeMuterStepped->iPendingVolume;
            prevVolume = iVolume;
        }
    } while (iVolume < kVolumeInitial);
}

void SuiteVolumeMuterStepped::TestCompletionReportedWhenUnmuted()
{
    static const TUint kVolumeInitial = 10 * kVolumeMilliDbPerStep;
    SetVolumeSync(kVolumeInitial);
    iVolumeMuterStepped->SetMuted();
    WaitForVolumeChange();
    TEST(iVolume == 0);
    static const TUint kJiffies = 10 * Media::Jiffies::kPerMs;
    auto pending = iVolumeMuterStepped->iPendingVolume;
    TEST(iVolumeMuterStepped->BeginUnmute() == Media::IVolumeMuterStepped::Status::eInProgress);
    do {
        TEST(iVolumeMuterStepped->StepUnmute(kJiffies) == Media::IVolumeMuterStepped::Status::eInProgress);
        if (pending != iVolumeMuterStepped->iPendingVolume) {
            WaitForVolumeChange();
            pending = iVolumeMuterStepped->iPendingVolume;
        }
    } while (iVolume < kVolumeInitial);
    TEST(iVolumeMuterStepped->StepUnmute(kJiffies) == Media::IVolumeMuterStepped::Status::eComplete);
}

void SuiteVolumeMuterStepped::TestVolumePassedOnceUnmuted()
{
    static const TUint kVolumeInitial = 50 * kVolumeMilliDbPerStep;
    SetVolumeSync(kVolumeInitial);
    iVolumeMuterStepped->SetMuted();
    WaitForVolumeChange();
    TEST(iVolume == 0);
    iVolumeMuterStepped->SetUnmuted();
    WaitForVolumeChange();
    TEST(iVolume == kVolumeInitial);
    static const TUint kVolumeUpdated = 35 * kVolumeMilliDbPerStep;
    SetVolumeSync(kVolumeUpdated);
    TEST(iVolume == kVolumeUpdated);
}



void TestVolumeManager()
{
    Runner runner("VolumeManager tests\n");
    runner.Add(new SuiteVolumeScaler());
    runner.Add(new SuiteVolumeMuterStepped());
    runner.Run();
}

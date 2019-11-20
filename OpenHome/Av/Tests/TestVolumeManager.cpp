#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/SuiteUnitTest.h>
#include <OpenHome/Av/VolumeManager.h>
#include <OpenHome/Configuration/Tests/ConfigRamStore.h>
#include <OpenHome/Configuration/IStore.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/PowerManager.h>
#include <OpenHome/Media/MuteManager.h>
#include <OpenHome/Av/Trim.h>
#include <OpenHome/Av/VolumeOffsets.h>
#include <OpenHome/Av/Product.h>
#include <OpenHome/Av/KvpStore.h>
#include <OpenHome/Net/Core/OhNet.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Av/Source.h>
#include <OpenHome/Av/StringIds.h>

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

class MockVolumeObserver : public IVolumeObserver
{
public:
    MockVolumeObserver();
    TUint GetVolumeUser();
    TUint GetVolumeBinaryMilliDb();
public: // from IVolumeObserver
    void VolumeChanged(const IVolumeValue& aVolume) override;
private:
    TUint iVolumeUser;
    TUint iVolumeBinaryMilliDb;
};

class MockUnityGainObserver : public IUnityGainObserver
{
public:
    MockUnityGainObserver();
    TBool GetUnityGainStatus();
public: // from IUnityGainObserver
    void UnityGainChanged(TBool aValue) override;
private:
    TBool iUnityGainStatus;
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

class MockVolume : public IVolume
{
public:
    MockVolume();
    TUint GetVolume() const;
    void ExceptionThrowActive(TBool aActive);
    void NotSupportedOrOutOfRange(TBool aNotSupported);
    void ThrowExceptionIfActive();
public: // from IVolume
    void SetVolume(TUint aVolume) override;
private:
    TUint iVolume;
    TBool iActive;
    TBool iNotSupported;
};

class MockBalance : public IBalance
{
public:
    MockBalance();
    TInt GetBalance();
public: // from IBalance
    void SetBalance(TInt aBalance) override;
private:
    TUint iBalance;
};

class MockFade : public IFade
{
public:
    MockFade();
    TInt GetFade();
public: // from IFade
    void SetFade(TInt aFade) override;
private:
    TInt iFade;
};

class MockMute : public Media::IMute
{
public:
    MockMute();
    TBool GetState();
public: // from IMute
    void Mute() override;
    void Unmute() override;
private:
    TBool iMuted;
};

class MockMuteObserver : public Media::IMuteObserver
{
public:
    MockMuteObserver();
    TBool GetMuteStatus();
public: // from IMuteObserver
    void MuteChanged(TBool aValue) override;
private:
    TBool iMuted;
};

class MockVolumeOffsetter : public IVolumeOffsetter
{
public:
    MockVolumeOffsetter();
public: // from IVolumeOffsetter
    void SetVolumeOffset(const Brx& aChannel, TInt aOffsetBinaryMilliDb) override;
    TInt GetVolumeOffset(const Brx& aChannel) override;
    void AddVolumeOffsetterObserver(IVolumeOffsetterObserver& aObserver) override;
};

class MockTrim : public ITrim
{
public:
    MockTrim();
public: // from ITrim
    TUint TrimChannelCount() const override;
    void SetTrim(const Brx& aChannel, TInt aTrimBinaryMilliDb) override;
    TInt GetTrim(const Brx& aChannel) override;
    void AddTrimObserver(ITrimObserver& aObserver) override;
};

class MockVolumeProfile : public IVolumeProfile
{
public:
    MockVolumeProfile(TUint aVolumeMax, TUint aVolumeDefault, TUint aVolumeDefaultLimit,
                        TUint aBalanceMax, TUint aFadeMax, TBool aAlwaysOn);
public: // from IVolumeProfile
    TUint VolumeMax() const override;
    TUint VolumeDefault() const override;
    TUint VolumeUnity() const override;
    TUint VolumeDefaultLimit() const override;
    TUint VolumeStep() const override;
    TUint VolumeMilliDbPerStep() const override;
    TUint ThreadPriority() const override;
    TUint BalanceMax() const override;
    TUint FadeMax() const override;
    TUint OffsetMax() const override;
    TBool AlwaysOn() const override;
    StartupVolume StartupVolumeConfig() const override;
private:
    TUint iVolumeMax;
    TUint iVolumeDefault;
    TUint iVolumeDefaultLimit;
    TUint iBalanceMax;
    TUint iFadeMax;
    TBool iAlwaysOn;
};

class MockReadStore : public IReadStore
{
public:
    MockReadStore();
private: // from IReadStore
    TBool TryReadStoreStaticItem(const Brx& aKey, Brn& aValue) override;
};

class MockSource : public SourceBase
{
public:
    MockSource(const Brx& aSystemName, const TChar* aType);
public: // from ISource
    TBool TryActivateNoPrefetch(const Brx& aMode) override;
    void StandbyEnabled() override;
    void PipelineStopped() override;
};

} // namespace Test

class SuiteVolumeConsumer : public TestFramework::SuiteUnitTest
{
public:
    SuiteVolumeConsumer();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    void ConsumeReturnVolumeComponents();
private:
    VolumeConsumer* iConsumer;
    Test::MockVolume* iVolume;
    Test::MockBalance* iBalance;
    Test::MockFade* iFade;
    Test::MockVolumeOffsetter* iOffset;
    Test::MockTrim* iTrim;
};

class SuiteVolumeUser : public TestFramework::SuiteUnitTest
{
private:
    static const TUint kMilliDbPerStep = 1024;
public:
    SuiteVolumeUser(Environment& aEnv);
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    void SetVolumeInLimits();
    void SetVolumeAtLimits();
    void SetVolumeOutsideLimits();
    void TestApplyStartupVolume();
    void TestExceptionThrow();
private:
    Environment & iEnv;
    Test::MockVolume* iVolume;
    VolumeUser* iUser;
    PowerManager* iPowerManager;
    Configuration::ConfigRamStore* iStore;
    Configuration::ConfigManager* iConfigManager;
    Configuration::ConfigNum* iConfigNum;
    Configuration::ConfigChoice* iConfigStartupEnabled;
    StoreInt* iLastVolume;
};

class SuiteVolumeLimiter : public TestFramework::SuiteUnitTest
{
public:
    SuiteVolumeLimiter();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    void TestVolumeInsideLimits();
    void TestVolumeOutsideLimits();
    void TestExceptionThrow();
private:
    Test::MockVolume* iVolume;
    VolumeLimiter* iLimiter;
    Configuration::ConfigRamStore* iStore;
    Configuration::ConfigManager* iConfigManager;
    Configuration::ConfigNum* iConfigNum;
};

class SuiteVolumeValue : public TestFramework::SuiteUnitTest
{
public:
    SuiteVolumeValue();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    void TestReturnValues();
private:
    VolumeValue* iValue;
};

class SuiteVolumeReporter : public TestFramework::SuiteUnitTest
{
public:
    SuiteVolumeReporter();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    void TestAddVolumeObserver();
    void TestExceptionThrow();
private:
    Test::MockVolumeObserver* iObserver;
    Test::MockVolumeObserver* iObserver2;
    Test::MockVolume* iVolume;
    VolumeReporter* iReporter;
};

class SuiteVolumeSourceOffset : public TestFramework::SuiteUnitTest
{
public:
    SuiteVolumeSourceOffset();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    void TestPositiveSourceOffset();
    void TestNegativeSourceOffset();
    void TestNeutralSourceOffset();
    void TestExceptionThrow();
private:
    Test::MockVolume* iVolume;
    VolumeSourceOffset* iOffset;
};

class SuiteVolumeSurroundBoost : public TestFramework::SuiteUnitTest
{
public:
    SuiteVolumeSurroundBoost();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    void TestAdditiveVolumeBoost();
    void TestSubtractiveVolumeBoost();
    void TestNeutralVolumeBoost();
    void TestExceptionThrow();
private:
    Test::MockVolume* iVolume;
    VolumeSurroundBoost* iBooster;
};

class SuiteVolumeUnityGain : public TestFramework::SuiteUnitTest
{
public:
    SuiteVolumeUnityGain();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    void TestUnityGainEnabled();
private:
    Test::MockVolume* iVolume;
    Configuration::ConfigRamStore* iStore;
    Configuration::ConfigManager* iConfigManager;
    Configuration::ConfigChoice* iConfigChoice;

};

class SuiteVolumeSourceUnityGain : public TestFramework::SuiteUnitTest
{
public:
    SuiteVolumeSourceUnityGain();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    void TestSetUnityGain();
    void TestAddUnityGainObserver();
private:
    Test::MockVolume* iVolume;
    Configuration::ConfigRamStore* iStore;
    Configuration::ConfigManager* iConfigManager;
    VolumeSourceUnityGain* iSourceUnityGain;
    Test::MockUnityGainObserver* iObserver;
    Test::MockUnityGainObserver* iObserver2;
};

class SuiteVolumeRamperPipeline : public TestFramework::SuiteUnitTest
{
public:
    SuiteVolumeRamperPipeline();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    void TestVolumeRamperSetVolumeWithinLimits();
    void TestVolumeRamperSetVolumeAtLimits();
    void TestVolumeRamperZeroMultiplier();
    void TestVolumeMultiplierEqual();
    void TestVolumeMultiplierInLimits();
    void TestExceptionThrow();
private:
    Test::MockVolume* iVolume;
    VolumeRamperPipeline* iRamper;
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

class SuiteVolumeMuter : public TestFramework::SuiteUnitTest
{
public:
    SuiteVolumeMuter();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    void TestVolumeUnmuted();
    void TestVolumeMuted();
    void TestVolumeFalseMute();
    void TestSetVolumeWhileMuted();
    void TestExceptionThrow();
private:
    Test::MockVolume* iVolume;
    VolumeMuter* iMuter;
};

class SuiteVolumeBalanceUser : public TestFramework::SuiteUnitTest
{
public:
    SuiteVolumeBalanceUser();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    void TestValidBalance();
    void TestInvalidBalance();
    void TestBalanceSetFromConfigManagerWithinLimits();
    void TestBalanceSetFromConfigManagerOnLimits();
private:
    Configuration::ConfigRamStore* iStore;
    Configuration::ConfigManager* iConfigManager;
    Configuration::ConfigNum* iConfigNum;
    Test::MockBalance* iBalance;
    BalanceUser* iBalanceUser;
};

class SuiteVolumeFadeUser : public TestFramework::SuiteUnitTest
{
public:
    SuiteVolumeFadeUser();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    void TestValidFade();
    void TestInvalidFade();
    void TestFadeSetFromConfigManagerWithinLimits();
    void TestFadeSetFromConfigManagerOnLimits();
private:
    Configuration::ConfigRamStore* iStore;
    Configuration::ConfigManager* iConfigManager;
    Configuration::ConfigNum* iConfigNum;
    Test::MockFade* iFade;
    FadeUser* iFadeUser;
};

class SuiteVolumeMuteUser : public TestFramework::SuiteUnitTest
{
public:
    SuiteVolumeMuteUser();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    void TestMuteUnmute();
private:
    Configuration::ConfigRamStore* iStore;
    Configuration::ConfigManager* iConfigManager;
    PowerManager* iPowerManager;
    Test::MockMute* iMute;
    MuteUser* iMuteUser;
};

class SuiteVolumeMuteReporter : public TestFramework::SuiteUnitTest
{
public:
    SuiteVolumeMuteReporter();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
public:
    void TestMuteUnmute();
    void TestMuteObserversUpdated();
private:
    Test::MockMute* iMute;
    Test::MockMuteObserver* iObserver;
    Test::MockMuteObserver* iObserver2;
    Test::MockMuteObserver* iObserver3;
    Test::MockMuteObserver* iObserver4;
    MuteReporter* iMuteReporter;
};

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

class SuiteVolumeConfig : public TestFramework::SuiteUnitTest
{
public:
    SuiteVolumeConfig();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
public:
    void TestVolumeControlEnabled();
    void TestVolumeControlNotEnabled();
    void TestNoBalanceNoFade();
private:
    Configuration::ConfigRamStore* iStore;
    Configuration::ConfigManager* iConfig;
    PowerManager* iPowerManager;
};

class SuiteVolumeManager : public TestFramework::SuiteUnitTest
{
    static const Brn kSystemName;
    static const TChar kType;
public:
    SuiteVolumeManager(Net::DvStack& aDvStack);
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
public:
    void TestAllComponentsInitialize();
    void TestNoVolumeControlNoMute();
    void TestNoVolumeComponent();
    void TestNoVolumeControl();
    void TestNoMuteComponents();
    void TestNoBalanceNoFadeComponents();
    void TestNoVolumeNoBalanceNoFadeComponents();
private:
    Net::DvStack& iDvStack;
    Net::DvDeviceStandard* iDvDevice;
    Test::MockReadStore* iReadStore;
    Configuration::ConfigRamStore* iStore;
    Configuration::ConfigManager* iConfig;
    PowerManager* iPowerManager;
    Product* iProduct;
    Test::MockVolumeProfile* iVolumeProfile;
    VolumeConfig* iVolumeConfig;
    VolumeConsumer* iVolumeConsumer;
    Test::MockMute* iMute;
    Configuration::ConfigText* iConfigText;
    Configuration::ConfigText* iConfigText2;
    Configuration::ConfigText* iConfigText3;
    SourceBase* iSource;
    Test::MockVolume* iVolume;
    Test::MockBalance* iBalance;
    Test::MockFade* iFade;
};

} // namespace Av
} // namespace OpenHome


using namespace OpenHome;
using namespace OpenHome::Net;
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

// MockVolumeObserver

MockVolumeObserver::MockVolumeObserver()
{
}

TUint MockVolumeObserver::GetVolumeUser()
{
    return iVolumeUser;
}

TUint MockVolumeObserver::GetVolumeBinaryMilliDb()
{
    return iVolumeBinaryMilliDb;
}

void MockVolumeObserver::VolumeChanged(const IVolumeValue& aVolume)
{
    iVolumeUser = aVolume.VolumeUser();
    iVolumeBinaryMilliDb = aVolume.VolumeBinaryMilliDb();
}

// MockUnityGainOberver

MockUnityGainObserver::MockUnityGainObserver()
{
}

void MockUnityGainObserver::UnityGainChanged(TBool aValue)
{
    iUnityGainStatus = aValue;
}

TBool MockUnityGainObserver::GetUnityGainStatus()
{
    return iUnityGainStatus;
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


// MockVolume

MockVolume::MockVolume()
    : iVolume(0)
    , iActive(false)
    , iNotSupported(true)
{
}

TUint MockVolume::GetVolume() const
{
    return iVolume;
}

void MockVolume::ExceptionThrowActive(TBool aActive)
{
    iActive = aActive;
}

void MockVolume::NotSupportedOrOutOfRange(TBool aNotSupported)
{
    iNotSupported = aNotSupported;
}

void MockVolume::ThrowExceptionIfActive()
{
    if (iActive) {
        if (iNotSupported) {
            THROW(VolumeNotSupported);
        }
        else {
            THROW(VolumeOutOfRange);
        }
    }
}

void MockVolume::SetVolume(TUint aVolume)
{
    ThrowExceptionIfActive();
    iVolume = aVolume;
}

// MockBalance

MockBalance::MockBalance()
    : iBalance(0)
{
}

TInt MockBalance::GetBalance()
{
    return iBalance;
}

void MockBalance::SetBalance(TInt aBalance)
{
    iBalance = aBalance;
}

// MockFade

MockFade::MockFade()
    : iFade(0)
{
}

TInt MockFade::GetFade()
{
    return iFade;
}

void MockFade::SetFade(TInt aFade)
{
    iFade = aFade;
}

// MockMute

MockMute::MockMute()
    : iMuted(false)
{
}

TBool MockMute::GetState()
{
    return iMuted;
}

void MockMute::Mute()
{
    iMuted = true;
}

void MockMute::Unmute()
{
    iMuted = false;
}

// MockMuteObserver

MockMuteObserver::MockMuteObserver()
    : iMuted(false)
{
}

TBool MockMuteObserver::GetMuteStatus()
{
    return iMuted;
}

void MockMuteObserver::MuteChanged(TBool aValue)
{
    iMuted = aValue;
}


// MockVolumeOffsetter

MockVolumeOffsetter::MockVolumeOffsetter()
{
}

void MockVolumeOffsetter::SetVolumeOffset(const Brx& /* aChannel */, TInt /* aOffsetBinaryMilliDb */)
{
    ASSERTS();
}

TInt MockVolumeOffsetter::GetVolumeOffset(const Brx& /* aChannel */)
{
    ASSERTS();
    return 0;
}

void MockVolumeOffsetter::AddVolumeOffsetterObserver(IVolumeOffsetterObserver& /* aObserver */)
{
    ASSERTS();
}

// MockTrim

MockTrim::MockTrim()
{
}

TUint MockTrim::TrimChannelCount() const
{
    ASSERTS();
    return 0;
}

void MockTrim::SetTrim(const Brx& /* aChannel */, TInt /* aTrimBinaryMilliDb */)
{
    ASSERTS();
}

TInt MockTrim::GetTrim(const Brx& /* aChannel */)
{
    ASSERTS();
    return 0;
}

void MockTrim::AddTrimObserver(ITrimObserver& /*aObserver*/)
{
    ASSERTS();
}


// MockVolumeProfile

MockVolumeProfile::MockVolumeProfile(TUint aVolumeMax, TUint aVolumeDefault, TUint aVolumeDefaultLimit,
                                        TUint aBalanceMax, TUint aFadeMax, TBool aAlwaysOn)
    : iVolumeMax(aVolumeMax)
    , iVolumeDefault(aVolumeDefault)
    , iVolumeDefaultLimit(aVolumeDefaultLimit)
    , iBalanceMax(aBalanceMax)
    , iFadeMax(aFadeMax)
    , iAlwaysOn(aAlwaysOn)
{
}

TUint MockVolumeProfile::VolumeMax() const
{
    return iVolumeMax;
}

TUint MockVolumeProfile::VolumeDefault() const
{
    return iVolumeDefault;
}

TUint MockVolumeProfile::VolumeUnity() const
{
    return 256;
}

TUint MockVolumeProfile::VolumeDefaultLimit() const
{
    return iVolumeDefaultLimit;
}

TUint MockVolumeProfile::VolumeStep() const
{
    return 0;
}

TUint MockVolumeProfile::VolumeMilliDbPerStep() const
{
    return 1024;
}

TUint MockVolumeProfile::ThreadPriority() const
{
    return 1;
}

TUint MockVolumeProfile::BalanceMax() const
{
    return iBalanceMax;
}

TUint MockVolumeProfile::FadeMax() const
{
    return iFadeMax;
}

TUint MockVolumeProfile::OffsetMax() const
{
    return 0;
}

TBool MockVolumeProfile::AlwaysOn() const
{
    return iAlwaysOn;
}

IVolumeProfile::StartupVolume MockVolumeProfile::StartupVolumeConfig() const
{
    return IVolumeProfile::StartupVolume::Both;
}


// MockReadStore

MockReadStore::MockReadStore()
{
}

TBool MockReadStore::TryReadStoreStaticItem(const Brx& /* aKey */, Brn& /* aValue */)
{
    return true;
}

// MockSource

MockSource::MockSource(const Brx& aSystemName, const TChar* aType)
    : SourceBase(aSystemName, aType)
{
}

TBool MockSource::TryActivateNoPrefetch(const Brx& /*aMode*/)
{
    return true;
}

void MockSource::StandbyEnabled()
{
}

void MockSource::PipelineStopped()
{
}


// SuiteVolumeConsumer

SuiteVolumeConsumer::SuiteVolumeConsumer()
    : SuiteUnitTest("SuiteVolumeConsumer")
{
    AddTest(MakeFunctor(*this, &SuiteVolumeConsumer::ConsumeReturnVolumeComponents), "TestConsumeReturnVolumeComponents");
}

void SuiteVolumeConsumer::Setup()
{
    iConsumer = new VolumeConsumer();
    iVolume = new MockVolume();
    iBalance = new MockBalance();
    iFade = new MockFade();
    iOffset = new MockVolumeOffsetter();
    iTrim = new MockTrim();
}

void SuiteVolumeConsumer::TearDown()
{
    delete iTrim;
    delete iOffset;
    delete iFade;
    delete iBalance;
    delete iVolume;
    delete iConsumer;
}

void SuiteVolumeConsumer::ConsumeReturnVolumeComponents()
{
    iConsumer->SetVolume(*iVolume);
    iConsumer->SetBalance(*iBalance);
    iConsumer->SetFade(*iFade);
    iConsumer->SetVolumeOffsetter(*iOffset);
    iConsumer->SetTrim(*iTrim);

    TEST(iConsumer->Volume() == iVolume);
    TEST(iConsumer->Balance() == iBalance);
    TEST(iConsumer->Fade() == iFade);
    TEST(iConsumer->VolumeOffsetter() == iOffset);
    TEST(iConsumer->Trim() == iTrim);
}

// SuiteVolumeUser

SuiteVolumeUser::SuiteVolumeUser(Environment& aEnv)
    : SuiteUnitTest("SuiteVolumeUser")
    , iEnv(aEnv)
{
    AddTest(MakeFunctor(*this, &SuiteVolumeUser::SetVolumeInLimits), "TestVolumeUserInLimits");
    AddTest(MakeFunctor(*this, &SuiteVolumeUser::SetVolumeAtLimits), "SetVolumeAtLimits");
    AddTest(MakeFunctor(*this, &SuiteVolumeUser::SetVolumeOutsideLimits), "TestVolumeUserOutsideLimits");
    AddTest(MakeFunctor(*this, &SuiteVolumeUser::TestExceptionThrow), "TestExceptionThrow");
}

void SuiteVolumeUser::Setup()
{
    iVolume = new MockVolume();
    iStore = new Configuration::ConfigRamStore();
    iConfigManager = new Configuration::ConfigManager(*iStore);
    iConfigNum = new Configuration::ConfigNum(*iConfigManager, Brn("Volume.StartupValue"), 0, 100, 80);
    std::vector<TUint> choices;
    choices.push_back(eStringIdYes);
    choices.push_back(eStringIdNo);
    iConfigStartupEnabled = new Configuration::ConfigChoice(*iConfigManager, VolumeConfig::kKeyStartupEnabled, choices, eStringIdYes);
    iPowerManager = new PowerManager(*iConfigManager);
    iLastVolume = new StoreInt(*iStore, *iPowerManager, kPowerPriorityLowest, Brn("SuiteVolumeUser.LastVolume"), 0);
    iUser = new VolumeUser(*iVolume, *iConfigManager, *iPowerManager, iEnv, *iLastVolume, 100, kMilliDbPerStep);
}

void SuiteVolumeUser::TearDown()
{
    delete iUser;
    delete iLastVolume;
    delete iPowerManager;
    delete iConfigStartupEnabled;
    delete iConfigNum;
    delete iConfigManager;
    delete iStore;
    delete iVolume;
}

void SuiteVolumeUser::SetVolumeInLimits()
{
    iUser->SetVolume(25);
    TEST(iVolume->GetVolume() == 25);

    iUser->SetVolume(50);
    TEST(iVolume->GetVolume() == 50);

    iUser->SetVolume(75);
    TEST(iVolume->GetVolume() == 75);
}

void SuiteVolumeUser::SetVolumeAtLimits()
{
    iUser->SetVolume(0);
    TEST(iVolume->GetVolume() == 0);

    iUser->SetVolume(100);
    TEST(iVolume->GetVolume() == 100);
}

void SuiteVolumeUser::SetVolumeOutsideLimits()
{
    iUser->SetVolume(80);
    TEST_THROWS(iUser->SetVolume(101), VolumeOutOfRange);
    TEST(iVolume->GetVolume() == 80);
}

void SuiteVolumeUser::TestApplyStartupVolume()
{
    Configuration::ConfigNum::KvpNum kvp(Brn("Startup.Volume"), 60);
    iUser->StartupVolumeChanged(kvp);
    iUser->StandbyDisabled(StandbyDisableReason::Product);
    TEST(iVolume->GetVolume() == 60);
    TEST_THROWS(iUser->SetVolume(101), VolumeOutOfRange);
    TEST(iVolume->GetVolume() == 60);
}

void SuiteVolumeUser::TestExceptionThrow()
{
    iVolume->ExceptionThrowActive(true);
    iVolume->NotSupportedOrOutOfRange(true);
    TEST_THROWS(iUser->SetVolume(0), VolumeNotSupported);

    iVolume->NotSupportedOrOutOfRange(false);
    TEST_THROWS(iUser->SetVolume(0), VolumeOutOfRange);
}


// SuiteVolumeLimiter

SuiteVolumeLimiter::SuiteVolumeLimiter()
    : SuiteUnitTest("SuiteVolumeLimiter")
{
    AddTest(MakeFunctor(*this, &SuiteVolumeLimiter::TestVolumeInsideLimits), "TestVolumeInsideLimits");
    AddTest(MakeFunctor(*this, &SuiteVolumeLimiter::TestVolumeOutsideLimits), "TestVolumeOutsideLimits");
    AddTest(MakeFunctor(*this, &SuiteVolumeLimiter::TestExceptionThrow), "TestExceptionThrow");
}

void SuiteVolumeLimiter::Setup()
{
    iVolume = new MockVolume();
    iStore = new Configuration::ConfigRamStore();
    iConfigManager = new Configuration::ConfigManager(*iStore);
    iConfigNum = new Configuration::ConfigNum(*iConfigManager, Brn("Volume.Limit"), 0, 100, 100);
    iLimiter = new VolumeLimiter(*iVolume, 1024, *iConfigManager);

}

void SuiteVolumeLimiter::TearDown()
{
    delete iLimiter;
    delete iConfigNum;
    delete iConfigManager;
    delete iStore;
    delete iVolume;
}

void SuiteVolumeLimiter::TestVolumeInsideLimits()
{
    TEST(iLimiter->iLimit == 102400);

    iLimiter->SetVolume(81920);
    TEST(iVolume->GetVolume() == 81920);

    iLimiter->SetVolume(0);
    TEST(iVolume->GetVolume() == 0);

    iLimiter->SetVolume(102400);
    TEST(iVolume->GetVolume() == 102400);
}

void SuiteVolumeLimiter::TestVolumeOutsideLimits()
{
    Configuration::ConfigNum::KvpNum kvp(Brn("Volume.Limit"), 100);
    iLimiter->LimitChanged(kvp);

    // This class will cap any volume that exceeds iLimit. Once this happens 
    // iCurrentVolume is set equal to iLimit. Any attempts after this point to 
    // exceed iLimit will throw VolumeOutOfRange.
    iLimiter->SetVolume(102401);
    TEST(iVolume->GetVolume() == 102400);

    TEST_THROWS(iLimiter->SetVolume(102401), VolumeOutOfRange);

    Configuration::ConfigNum::KvpNum kvp2(Brn("Volume.Limit"), 80);
    iLimiter->LimitChanged(kvp2);

    TEST(iVolume->GetVolume() == 81920);
    TEST_THROWS(iLimiter->SetVolume(102400), VolumeOutOfRange);
}

void SuiteVolumeLimiter::TestExceptionThrow()
{
    iVolume->ExceptionThrowActive(true);
    iVolume->NotSupportedOrOutOfRange(true);
    TEST_THROWS(iLimiter->SetVolume(0), VolumeNotSupported);

    iVolume->NotSupportedOrOutOfRange(false);
    TEST_THROWS(iLimiter->SetVolume(0), VolumeOutOfRange);

    // VolumeLimiter::LimitChanged() is expected to catch VolumeNotSupported and VolumeOutOfRange
    Configuration::ConfigNum::KvpNum exceptionKvp(Brn("Volume.Limit"), 80);
    iVolume->NotSupportedOrOutOfRange(true);
    iLimiter->LimitChanged(exceptionKvp);

    iVolume->NotSupportedOrOutOfRange(false);
    iLimiter->LimitChanged(exceptionKvp);
}


// SuiteVolumeValue

SuiteVolumeValue::SuiteVolumeValue()
    : SuiteUnitTest("SuiteVolumeValue")
{
    AddTest(MakeFunctor(*this, &SuiteVolumeValue::TestReturnValues), "TestReturnValues");
}

void SuiteVolumeValue::Setup()
{
    iValue = new VolumeValue(1, 1024);
}

void SuiteVolumeValue::TearDown()
{
    delete iValue;
}

void SuiteVolumeValue::TestReturnValues()
{
    TEST(iValue->VolumeUser() == 1);
    TEST(iValue->VolumeBinaryMilliDb() == 1024);
}


// SuiteVolumeReporter

SuiteVolumeReporter::SuiteVolumeReporter()
    : SuiteUnitTest("SuiteVolumeReporter")
{
    AddTest(MakeFunctor(*this, &SuiteVolumeReporter::TestAddVolumeObserver), "TestAddVolumeReporter");
    AddTest(MakeFunctor(*this, &SuiteVolumeReporter::TestExceptionThrow), "TestExceptionThrow");
}

void SuiteVolumeReporter::Setup()
{
    iObserver = new MockVolumeObserver();
    iObserver2 = new MockVolumeObserver();
    iVolume = new MockVolume();
    iReporter = new VolumeReporter(*iVolume, 1024);
}

void SuiteVolumeReporter::TearDown()
{
    delete iReporter;
    delete iVolume;
    delete iObserver2;
    delete iObserver;
}

void SuiteVolumeReporter::TestAddVolumeObserver()
{
    iReporter->AddVolumeObserver(*iObserver);
    TEST(iObserver->GetVolumeUser() == 0);
    TEST(iObserver->GetVolumeBinaryMilliDb() == 0);

    iReporter->SetVolume(102400);
    TEST(iObserver->GetVolumeUser() == 100);
    TEST(iObserver->GetVolumeBinaryMilliDb() == 102400);

    iReporter->AddVolumeObserver(*iObserver2);
    TEST(iObserver2->GetVolumeUser() == 100);
    TEST(iObserver2->GetVolumeBinaryMilliDb() == 102400);

    iReporter->SetVolume(65536);
    TEST(iObserver->GetVolumeUser() == 64);
    TEST(iObserver->GetVolumeBinaryMilliDb() == 65536);
    TEST(iObserver2->GetVolumeUser() == 64);
    TEST(iObserver2->GetVolumeBinaryMilliDb() == 65536);
}

void SuiteVolumeReporter::TestExceptionThrow()
{
    iVolume->ExceptionThrowActive(true);
    iVolume->NotSupportedOrOutOfRange(true);
    TEST_THROWS(iReporter->SetVolume(0), VolumeNotSupported);

    iVolume->NotSupportedOrOutOfRange(false);
    TEST_THROWS(iReporter->SetVolume(0), VolumeOutOfRange);
}

// SuiteVolumeSourceOffset

SuiteVolumeSourceOffset::SuiteVolumeSourceOffset()
    : SuiteUnitTest("SuiteVolumeSourceOffset")
{
    AddTest(MakeFunctor(*this, &SuiteVolumeSourceOffset::TestPositiveSourceOffset), "TestPositiveSourceOffset");
    AddTest(MakeFunctor(*this, &SuiteVolumeSourceOffset::TestNegativeSourceOffset), "TestNegativeSourceOffset");
    AddTest(MakeFunctor(*this, &SuiteVolumeSourceOffset::TestNeutralSourceOffset), "TestNeutralSourceOffset");
    AddTest(MakeFunctor(*this, &SuiteVolumeSourceOffset::TestExceptionThrow), "TestExceptionThrow");
}

void SuiteVolumeSourceOffset::Setup()
{
    iVolume = new MockVolume();
    iOffset = new VolumeSourceOffset(*iVolume);
}

void SuiteVolumeSourceOffset::TearDown()
{
    delete iOffset;
    delete iVolume;
}

void SuiteVolumeSourceOffset::TestPositiveSourceOffset()
{
    iOffset->SetVolume(50);
    iOffset->SetVolumeOffset(30);
    TEST(iVolume->GetVolume() == 80);

    iOffset->SetVolume(0);
    iOffset->SetVolumeOffset(30);
    TEST(iVolume->GetVolume() == 0);
}

void SuiteVolumeSourceOffset::TestNegativeSourceOffset()
{
    iOffset->SetVolume(50);
    iOffset->SetVolumeOffset(-30);
    TEST(iVolume->GetVolume() == 20);

    iOffset->SetVolume(10);
    iOffset->SetVolumeOffset(-20);
    TEST(iVolume->GetVolume() == 0);

    iOffset->SetVolume(10);
    iOffset->SetVolumeOffset(-9);
    TEST(iVolume->GetVolume() == 1);
}

void SuiteVolumeSourceOffset::TestNeutralSourceOffset()
{
    iOffset->SetVolume(0);
    iOffset->SetVolumeOffset(0);
    TEST(iVolume->GetVolume() == 0);

    iOffset->SetVolume(50);
    iOffset->SetVolumeOffset(0);
    TEST(iVolume->GetVolume() == 50);
}

void SuiteVolumeSourceOffset::TestExceptionThrow()
{
    iVolume->ExceptionThrowActive(true);
    iVolume->NotSupportedOrOutOfRange(true);
    TEST_THROWS(iOffset->SetVolume(0), VolumeNotSupported);

    iVolume->NotSupportedOrOutOfRange(false);
    TEST_THROWS(iOffset->SetVolume(0), VolumeOutOfRange);

    // VolumeSourceOffset::SetVolumeOffset() is expected to catch VolumeNotSupported
    iVolume->NotSupportedOrOutOfRange(true);
    iOffset->SetVolumeOffset(0);

    iVolume->NotSupportedOrOutOfRange(false);
    TEST_THROWS(iOffset->SetVolumeOffset(0), VolumeOutOfRange);
}

// SuiteVolumeSurroundBoost

SuiteVolumeSurroundBoost::SuiteVolumeSurroundBoost()
    : SuiteUnitTest("SuiteVolumeSurroundBoost")
{
    AddTest(MakeFunctor(*this, &SuiteVolumeSurroundBoost::TestAdditiveVolumeBoost), "TestAdditiveVolumeBoost");
    AddTest(MakeFunctor(*this, &SuiteVolumeSurroundBoost::TestSubtractiveVolumeBoost), "TestSubtractiveVolumeBoost");
    AddTest(MakeFunctor(*this, &SuiteVolumeSurroundBoost::TestNeutralVolumeBoost), "TestNeutralVolumeBoost");
    AddTest(MakeFunctor(*this, &SuiteVolumeSurroundBoost::TestExceptionThrow), "TestExceptionThrow");
}

void SuiteVolumeSurroundBoost::Setup()
{
    iVolume = new MockVolume();
    iBooster = new VolumeSurroundBoost(*iVolume);
}

void SuiteVolumeSurroundBoost::TearDown()
{
    delete iBooster;
    delete iVolume;
}

void SuiteVolumeSurroundBoost::TestAdditiveVolumeBoost()
{
    iBooster->SetVolume(50);
    iBooster->SetVolumeBoost(30);
    TEST(iVolume->GetVolume() == 80);

    iBooster->SetVolume(0);
    iBooster->SetVolumeBoost(30);
    TEST(iVolume->GetVolume() == 0);
}

void SuiteVolumeSurroundBoost::TestSubtractiveVolumeBoost()
{
    iBooster->SetVolume(50);
    iBooster->SetVolumeBoost(-30);
    TEST(iVolume->GetVolume() == 20);

    iBooster->SetVolume(10);
    iBooster->SetVolumeBoost(-20);
    TEST(iVolume->GetVolume() == 0);

    iBooster->SetVolume(10);
    iBooster->SetVolumeBoost(-11);
    TEST(iVolume->GetVolume() == 0);

    iBooster->SetVolume(10);
    iBooster->SetVolumeBoost(-9);
    TEST(iVolume->GetVolume() == 1);
}

void SuiteVolumeSurroundBoost::TestNeutralVolumeBoost()
{
    iBooster->SetVolume(0);
    iBooster->SetVolumeBoost(0);
    TEST(iVolume->GetVolume() == 0);

    iBooster->SetVolume(50);
    iBooster->SetVolumeBoost(0);
    TEST(iVolume->GetVolume() == 50);
}

void SuiteVolumeSurroundBoost::TestExceptionThrow()
{
    iVolume->ExceptionThrowActive(true);
    iVolume->NotSupportedOrOutOfRange(true);
    TEST_THROWS(iBooster->SetVolume(0), VolumeNotSupported);

    iVolume->NotSupportedOrOutOfRange(false);
    TEST_THROWS(iBooster->SetVolume(0), VolumeOutOfRange);

    // VolumeSurroundBoost::SetVolumeBoost() is expected to catch VolumeNotSupported
    iVolume->NotSupportedOrOutOfRange(true);
    iBooster->SetVolumeBoost(0);

    iVolume->NotSupportedOrOutOfRange(false);
    TEST_THROWS(iBooster->SetVolumeBoost(0), VolumeOutOfRange);
}


// SuiteVolumeUnityGain

SuiteVolumeUnityGain::SuiteVolumeUnityGain()
    :SuiteUnitTest("SuiteVolumeUnityGain")
{
    AddTest(MakeFunctor(*this, &SuiteVolumeUnityGain::TestUnityGainEnabled), "TestUnityGainEnabled");
}

void SuiteVolumeUnityGain::Setup()
{
    iVolume = new MockVolume();
    iStore = new Configuration::ConfigRamStore();
    iConfigManager = new Configuration::ConfigManager(*iStore);
    std::vector<TUint> choices;
    choices.push_back(eStringIdYes);
    choices.push_back(eStringIdNo);
    iConfigChoice = new Configuration::ConfigChoice(*iConfigManager, Brn("Volume.Enabled"), choices, eStringIdYes);

}

void SuiteVolumeUnityGain::TearDown()
{
    delete iConfigChoice;
    delete iConfigManager;
    delete iStore;
    delete iVolume;
}

void SuiteVolumeUnityGain::TestUnityGainEnabled()
{
    VolumeUnityGain unityGain(*iVolume, *iConfigManager, 256);
    TEST(unityGain.VolumeControlEnabled() == true);

    iConfigChoice->Set(eStringIdNo);

    VolumeUnityGain unityGain2(*iVolume, *iConfigManager, 256);
    TEST(unityGain2.VolumeControlEnabled() == false);
}


// SuiteVolumeSourceUnityUnityGain

SuiteVolumeSourceUnityGain::SuiteVolumeSourceUnityGain()
    :SuiteUnitTest("SuiteVolumeSourceUnityGain")
{
    AddTest(MakeFunctor(*this, &SuiteVolumeSourceUnityGain::TestSetUnityGain), "TestSetUnityGain");
    AddTest(MakeFunctor(*this, &SuiteVolumeSourceUnityGain::TestAddUnityGainObserver), "TestAddUnityGainObserver");
}

void SuiteVolumeSourceUnityGain::Setup()
{
    iVolume = new MockVolume();
    iStore = new Configuration::ConfigRamStore();
    iConfigManager = new Configuration::ConfigManager(*iStore);
    iSourceUnityGain = new VolumeSourceUnityGain(*iVolume, 256);
    iObserver = new MockUnityGainObserver();
    iObserver2 = new MockUnityGainObserver();
}

void SuiteVolumeSourceUnityGain::TearDown()
{
    delete iObserver2;
    delete iObserver;
    delete iSourceUnityGain;
    delete iConfigManager;
    delete iStore;
    delete iVolume;
}

void SuiteVolumeSourceUnityGain::TestSetUnityGain()
{
    iSourceUnityGain->SetUnityGain(true);
    TEST(iSourceUnityGain->VolumeControlEnabled() == false);
    iSourceUnityGain->SetUnityGain(false);
    TEST(iSourceUnityGain->VolumeControlEnabled() == true);
}

void SuiteVolumeSourceUnityGain::TestAddUnityGainObserver()
{
    iSourceUnityGain->SetVolumeControlEnabled(true);
    iSourceUnityGain->AddUnityGainObserver(*iObserver);
    TEST(iObserver->GetUnityGainStatus() == false);

    iSourceUnityGain->AddUnityGainObserver(*iObserver2);
    TEST(iObserver2->GetUnityGainStatus() == false);

    iSourceUnityGain->SetUnityGain(true);
    TEST(iObserver->GetUnityGainStatus() == true);
    TEST(iObserver2->GetUnityGainStatus() == true);
}



// SuiteVolumeRamperPipeline

SuiteVolumeRamperPipeline::SuiteVolumeRamperPipeline()
    : SuiteUnitTest("SuiteVolumeRamperPipeline")
{
    AddTest(MakeFunctor(*this, &SuiteVolumeRamperPipeline::TestVolumeRamperSetVolumeWithinLimits), "TestVolumeRamperSetVolumeWithinLimits");
    AddTest(MakeFunctor(*this, &SuiteVolumeRamperPipeline::TestVolumeRamperSetVolumeAtLimits), "TestVolumeRamperSetVolumeAtLimits");
    AddTest(MakeFunctor(*this, &SuiteVolumeRamperPipeline::TestVolumeRamperZeroMultiplier), "TestVolumeRamperZeroMultiplier");
    AddTest(MakeFunctor(*this, &SuiteVolumeRamperPipeline::TestVolumeMultiplierEqual), "TestVolumeMultiplierEqual");
    AddTest(MakeFunctor(*this, &SuiteVolumeRamperPipeline::TestVolumeMultiplierInLimits), "TestVolumeMultiplierInLimits");
    AddTest(MakeFunctor(*this, &SuiteVolumeRamperPipeline::TestExceptionThrow), "TestExceptionThrow");
}

void SuiteVolumeRamperPipeline::Setup()
{
    iVolume = new MockVolume();
    iRamper = new VolumeRamperPipeline(*iVolume);
}

void SuiteVolumeRamperPipeline::TearDown()
{
    delete iRamper;
    delete iVolume;
}

void SuiteVolumeRamperPipeline::TestVolumeRamperSetVolumeWithinLimits()
{
    iRamper->SetVolume(25);
    TEST(iVolume->GetVolume() == 25);
    iRamper->SetVolume(50);
    TEST(iVolume->GetVolume() == 50);
    iRamper->SetVolume(75);
    TEST(iVolume->GetVolume() == 75);
}

void SuiteVolumeRamperPipeline::TestVolumeRamperSetVolumeAtLimits()
{
    iRamper->SetVolume(0);
    TEST(iVolume->GetVolume() == 0);
    iRamper->SetVolume(100);
    TEST(iVolume->GetVolume() == 100);
}

void SuiteVolumeRamperPipeline::TestVolumeRamperZeroMultiplier()
{
    iRamper->ApplyVolumeMultiplier(0);
    iRamper->SetVolume(0);
    TEST(iVolume->GetVolume() == 0);
    iRamper->SetVolume(25);
    TEST(iVolume->GetVolume() == 0);
    iRamper->SetVolume(50);
    TEST(iVolume->GetVolume() == 0);
    iRamper->SetVolume(75);
    TEST(iVolume->GetVolume() == 0);
    iRamper->SetVolume(100);
    TEST(iVolume->GetVolume() == 0);
}

void SuiteVolumeRamperPipeline::TestVolumeMultiplierEqual()
{
    iRamper->SetVolume(50);
    iRamper->ApplyVolumeMultiplier(1u<<15);
    TEST(iVolume->GetVolume() == 50);
}

void SuiteVolumeRamperPipeline::TestVolumeMultiplierInLimits()
{
    iRamper->SetVolume(50);
    iRamper->ApplyVolumeMultiplier(65536);
    TEST(iVolume->GetVolume() == 100);

    iRamper->SetVolume(50);
    iRamper->ApplyVolumeMultiplier(16384);
    TEST(iVolume->GetVolume() == 25);

    iRamper->SetVolume(50);
    iRamper->ApplyVolumeMultiplier(49152);
    TEST(iVolume->GetVolume() == 75);
}

void SuiteVolumeRamperPipeline::TestExceptionThrow()
{
    iVolume->ExceptionThrowActive(true);
    iVolume->NotSupportedOrOutOfRange(true);
    TEST_THROWS(iRamper->SetVolume(0), VolumeNotSupported);

    iVolume->NotSupportedOrOutOfRange(false);
    TEST_THROWS(iRamper->SetVolume(0), VolumeOutOfRange);
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


// SuiteVolumeMuter

SuiteVolumeMuter::SuiteVolumeMuter()
    : SuiteUnitTest("SuiteVolumeMuter")
{
    AddTest(MakeFunctor(*this, &SuiteVolumeMuter::TestVolumeUnmuted), "TestVolumeUnmuted");
    AddTest(MakeFunctor(*this, &SuiteVolumeMuter::TestVolumeMuted), "TestVolumeMuted");
    AddTest(MakeFunctor(*this, &SuiteVolumeMuter::TestVolumeFalseMute), "TestVolumeFalseMute");
    AddTest(MakeFunctor(*this, &SuiteVolumeMuter::TestSetVolumeWhileMuted), "TestSetVolumeWhileMuted");
    AddTest(MakeFunctor(*this, &SuiteVolumeMuter::TestExceptionThrow), "TestExceptionThrow");
}

void SuiteVolumeMuter::Setup()
{
    iVolume = new MockVolume();
    iMuter = new VolumeMuter(iVolume);
}

void SuiteVolumeMuter::TearDown()
{
    delete iMuter;
    delete iVolume;
}

void SuiteVolumeMuter::TestVolumeUnmuted()
{
    iMuter->SetVolume(80);
    TEST(iVolume->GetVolume() == 80);
}

void SuiteVolumeMuter::TestVolumeMuted()
{
    iMuter->SetVolume(80);
    iMuter->SetVolumeMuted(true);
    TEST(iVolume->GetVolume() == 0);
}

void SuiteVolumeMuter::TestVolumeFalseMute()
{
    iMuter->SetVolume(80);
    iMuter->SetVolumeMuted(false);
    TEST(iVolume->GetVolume() == 80);
}

void SuiteVolumeMuter::TestSetVolumeWhileMuted()
{
    iMuter->SetVolume(80);
    iMuter->SetVolumeMuted(true);
    TEST(iVolume->GetVolume() == 0);
    iMuter->SetVolume(60);
    TEST(iVolume->GetVolume() == 0);
    iMuter->SetVolumeMuted(false);
    TEST(iVolume->GetVolume() == 60);
}

void SuiteVolumeMuter::TestExceptionThrow()
{
    iVolume->ExceptionThrowActive(true);
    iVolume->NotSupportedOrOutOfRange(true);
    TEST_THROWS(iMuter->SetVolume(0), VolumeNotSupported);

    iVolume->NotSupportedOrOutOfRange(false);
    TEST_THROWS(iMuter->SetVolume(0), VolumeOutOfRange);
}


// SuiteBalanceUser

SuiteVolumeBalanceUser::SuiteVolumeBalanceUser()
    : SuiteUnitTest("SuiteVolumeBalanceUser")
{
    AddTest(MakeFunctor(*this, &SuiteVolumeBalanceUser::TestValidBalance), "TestValidBalance");
    AddTest(MakeFunctor(*this, &SuiteVolumeBalanceUser::TestInvalidBalance), "TestInvalidBalance");
    AddTest(MakeFunctor(*this, &SuiteVolumeBalanceUser::TestBalanceSetFromConfigManagerWithinLimits), "TestBalanceSetFromConfigManagerWithinLimits");
    AddTest(MakeFunctor(*this, &SuiteVolumeBalanceUser::TestBalanceSetFromConfigManagerOnLimits), "TestBalanceSetFromConfigManagerOnLimits");
}

void SuiteVolumeBalanceUser::Setup()
{
    iStore = new Configuration::ConfigRamStore();
    iConfigManager = new Configuration::ConfigManager(*iStore);
    iConfigNum = new Configuration::ConfigNum(*iConfigManager, Brn("Volume.Balance"), -10, 10, 0);
    iBalance = new MockBalance();
    iBalanceUser = new BalanceUser(*iBalance, *iConfigManager);
}

void SuiteVolumeBalanceUser::TearDown()
{
    delete iBalanceUser;
    delete iBalance;
    delete iConfigNum;
    delete iConfigManager;
    delete iStore;
}

void SuiteVolumeBalanceUser::TestValidBalance()
{
    iBalanceUser->SetBalance(-10);
    TEST(iBalance->GetBalance() == -10);

    iBalanceUser->SetBalance(10);
    TEST(iBalance->GetBalance() == 10);

    iBalanceUser->SetBalance(-3);
    TEST(iBalance->GetBalance() == -3);

    iBalanceUser->SetBalance(8);
    TEST(iBalance->GetBalance() == 8);
}

void SuiteVolumeBalanceUser::TestInvalidBalance()
{
    TEST_THROWS(iBalanceUser->SetBalance(-11), BalanceOutOfRange);
    TEST_THROWS(iBalanceUser->SetBalance(11), BalanceOutOfRange);

    TEST_THROWS(iBalanceUser->SetBalance(-99), BalanceOutOfRange);
    TEST_THROWS(iBalanceUser->SetBalance(50), BalanceOutOfRange);
}

void SuiteVolumeBalanceUser::TestBalanceSetFromConfigManagerWithinLimits()
{
    iConfigNum->Set(-5);
    TEST(iBalance->GetBalance() ==-5);
    iConfigNum->Set(0);
    TEST(iBalance->GetBalance() == 0);
    iConfigNum->Set(5);
    TEST(iBalance->GetBalance() == 5);
}

void SuiteVolumeBalanceUser::TestBalanceSetFromConfigManagerOnLimits()
{
    iConfigNum->Set(-10);
    TEST(iBalance->GetBalance() == -10);
    iConfigNum->Set(10);
    TEST(iBalance->GetBalance() == 10);
}


// SuiteFadeUser

SuiteVolumeFadeUser::SuiteVolumeFadeUser()
    : SuiteUnitTest("SuiteVolumeFadeUser")
{
    AddTest(MakeFunctor(*this, &SuiteVolumeFadeUser::TestValidFade), "TestValidFade");
    AddTest(MakeFunctor(*this, &SuiteVolumeFadeUser::TestInvalidFade), "TestInvalidFade");
    AddTest(MakeFunctor(*this, &SuiteVolumeFadeUser::TestFadeSetFromConfigManagerWithinLimits), "TestFadeSetFromConfigManagerWithinLimits");
    AddTest(MakeFunctor(*this, &SuiteVolumeFadeUser::TestFadeSetFromConfigManagerOnLimits), "TestFadeSetFromConfigManagerOnLimits");
}

void SuiteVolumeFadeUser::Setup()
{
    iStore = new Configuration::ConfigRamStore();
    iConfigManager = new Configuration::ConfigManager(*iStore);
    iConfigNum = new Configuration::ConfigNum(*iConfigManager, Brn("Volume.Fade"), -10, 10, 0);
    iFade = new MockFade();
    iFadeUser = new FadeUser(*iFade, *iConfigManager);
}

void SuiteVolumeFadeUser::TearDown()
{
    delete iFadeUser;
    delete iFade;
    delete iConfigNum;
    delete iConfigManager;
    delete iStore;
}

void SuiteVolumeFadeUser::TestValidFade()
{
    iFadeUser->SetFade(-10);
    TEST(iFade->GetFade() == -10);

    iFadeUser->SetFade(10);
    TEST(iFade->GetFade() == 10);

    iFadeUser->SetFade(-3);
    TEST(iFade->GetFade() == -3);

    iFadeUser->SetFade(8);
    TEST(iFade->GetFade() == 8);
}

void SuiteVolumeFadeUser::TestInvalidFade()
{
    TEST_THROWS(iFadeUser->SetFade(-11), FadeOutOfRange);
    TEST_THROWS(iFadeUser->SetFade(11), FadeOutOfRange);

    TEST_THROWS(iFadeUser->SetFade(-99), FadeOutOfRange);
    TEST_THROWS(iFadeUser->SetFade(50), FadeOutOfRange);
}

void SuiteVolumeFadeUser::TestFadeSetFromConfigManagerWithinLimits()
{
    iConfigNum->Set(-5);
    TEST(iFade->GetFade() ==-5);
    iConfigNum->Set(0);
    TEST(iFade->GetFade() == 0);
    iConfigNum->Set(5);
    TEST(iFade->GetFade() == 5);
}

void SuiteVolumeFadeUser::TestFadeSetFromConfigManagerOnLimits()
{
    iConfigNum->Set(-10);
    TEST(iFade->GetFade() == -10);
    iConfigNum->Set(10);
    TEST(iFade->GetFade() == 10);
}


// SuiteMuteUser

SuiteVolumeMuteUser::SuiteVolumeMuteUser()
    : SuiteUnitTest("SuiteVolumeMuteUser")
{
    AddTest(MakeFunctor(*this, &SuiteVolumeMuteUser::TestMuteUnmute), "TestMuteUnmute");
}

void SuiteVolumeMuteUser::Setup()
{
    iStore = new Configuration::ConfigRamStore();
    iConfigManager = new Configuration::ConfigManager(*iStore);
    iPowerManager = new PowerManager(*iConfigManager);
    iMute = new MockMute();
    iMuteUser = new MuteUser(*iMute, *iPowerManager);
}

void SuiteVolumeMuteUser::TearDown()
{
    delete iMuteUser;
    delete iMute;
    delete iPowerManager;
    delete iConfigManager;
    delete iStore;
}

void SuiteVolumeMuteUser::TestMuteUnmute()
{
    iMuteUser->Mute();
    TEST(iMute->GetState() == true);

    iMuteUser->Mute();
    TEST(iMute->GetState() == true);

    iMuteUser->Unmute();
    TEST(iMute->GetState() == false);

    iMuteUser->Unmute();
    TEST(iMute->GetState() == false);

    iMuteUser->Mute();
    TEST(iMute->GetState() == true);

    iMuteUser->StandbyDisabled(StandbyDisableReason::Product);
    TEST(iMute->GetState() == false);

    iMuteUser->Unmute();
    TEST(iMute->GetState() == false);

    iMuteUser->StandbyDisabled(StandbyDisableReason::Product);
    TEST(iMute->GetState() == false);
}


// SuiteVolumeMuteReporter

SuiteVolumeMuteReporter::SuiteVolumeMuteReporter()
    : SuiteUnitTest("SuiteVolumeMuteReporter")
{
    AddTest(MakeFunctor(*this, &SuiteVolumeMuteReporter::TestMuteUnmute), "TestMuteUnmute");
    AddTest(MakeFunctor(*this, &SuiteVolumeMuteReporter::TestMuteObserversUpdated), "TestMuteObserversUpdated");
}

void SuiteVolumeMuteReporter::Setup()
{
    iMute = new MockMute();
    iObserver = new MockMuteObserver();
    iObserver2 = new MockMuteObserver();
    iObserver3 = new MockMuteObserver();
    iObserver4 = new MockMuteObserver();
    iMuteReporter = new MuteReporter(*iMute);
}

void SuiteVolumeMuteReporter::TearDown()
{
    delete iMuteReporter;
    delete iObserver4;
    delete iObserver3;
    delete iObserver2;
    delete iObserver;
    delete iMute;
}

void SuiteVolumeMuteReporter::TestMuteUnmute()
{
    iMuteReporter->Mute();
    TEST(iMute->GetState() == true);

    iMuteReporter->Unmute();
    TEST(iMute->GetState() == false);
}

void SuiteVolumeMuteReporter::TestMuteObserversUpdated()
{
    TEST(iMute->GetState() == false);
    iMuteReporter->AddMuteObserver(*iObserver);
    iMuteReporter->AddMuteObserver(*iObserver2);
    iMuteReporter->AddMuteObserver(*iObserver3);
    iMuteReporter->AddMuteObserver(*iObserver4);

    TEST(iObserver->GetMuteStatus() == false);
    TEST(iObserver2->GetMuteStatus() == false);
    TEST(iObserver3->GetMuteStatus() == false);
    TEST(iObserver4->GetMuteStatus() == false);

    TEST(iMute->GetState() == false);
    // expect nothing to happen
    TEST(iObserver->GetMuteStatus() == false);
    TEST(iObserver2->GetMuteStatus() == false);
    TEST(iObserver3->GetMuteStatus() == false);
    TEST(iObserver4->GetMuteStatus() == false);

    iMuteReporter->Mute();
    TEST(iMute->GetState() == true);
    TEST(iObserver->GetMuteStatus() == true);
    TEST(iObserver2->GetMuteStatus() == true);
    TEST(iObserver3->GetMuteStatus() == true);
    TEST(iObserver4->GetMuteStatus() == true);
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

// SuiteVolumeConfig

SuiteVolumeConfig::SuiteVolumeConfig()
    : SuiteUnitTest("SuiteVolumeConfig")
{
    AddTest(MakeFunctor(*this, &SuiteVolumeConfig::TestVolumeControlNotEnabled), "TestVolumeControlNotEnabled");
    AddTest(MakeFunctor(*this, &SuiteVolumeConfig::TestVolumeControlEnabled), "TestVolumeControlEnabled");
    AddTest(MakeFunctor(*this, &SuiteVolumeConfig::TestNoBalanceNoFade), "TestNoBalanceNoFade");
}

void SuiteVolumeConfig::Setup()
{
    iStore = new Configuration::ConfigRamStore();
    iConfig = new Configuration::ConfigManager(*iStore);
    iPowerManager = new PowerManager(nullptr);
}

void SuiteVolumeConfig::TearDown()
{
    delete iPowerManager;
    delete iConfig;
    delete iStore;
}

void SuiteVolumeConfig::TestVolumeControlNotEnabled()
{
    Bws<4> volControlEnabledBuf;
    WriterBuffer writerBuffer(volControlEnabledBuf);
    WriterBinary writerBinary(writerBuffer);
    writerBinary.WriteUint32Be(eStringIdNo);
    iStore->Write(VolumeConfig::kKeyEnabled, volControlEnabledBuf);

    MockVolumeProfile volumeProfile(100, 80, 100, 10, 10, false);
    VolumeConfig volumeConfig(*iStore, *iConfig, *iPowerManager, volumeProfile);
    TEST(iConfig->HasChoice(VolumeConfig::kKeyEnabled) == true);

    TEST(iConfig->HasNum(VolumeConfig::kKeyStartupValue) == false);
    TEST(iConfig->HasNum(VolumeConfig::kKeyLimit) == false);
    TEST(iConfig->HasNum(VolumeConfig::kKeyBalance) == false);
    TEST(iConfig->HasNum(VolumeConfig::kKeyFade) == false);
}

void SuiteVolumeConfig::TestVolumeControlEnabled()
{
    MockVolumeProfile volumeProfile(100, 80, 100, 10, 10, true);
    VolumeConfig volumeConfig(*iStore, *iConfig, *iPowerManager, volumeProfile);
    TEST(iConfig->HasChoice(VolumeConfig::kKeyEnabled) == false);

    TEST(iConfig->HasNum(VolumeConfig::kKeyStartupValue) == true);
    TEST(iConfig->HasNum(VolumeConfig::kKeyLimit) == true);
    TEST(iConfig->HasNum(VolumeConfig::kKeyBalance) == true);
    TEST(iConfig->HasNum(VolumeConfig::kKeyFade) == true);
}

void SuiteVolumeConfig::TestNoBalanceNoFade()
{
    MockVolumeProfile volumeProfile(100, 80, 100, 0, 0, true);
    VolumeConfig volumeConfig(*iStore, *iConfig, *iPowerManager, volumeProfile);
    TEST(iConfig->HasChoice(VolumeConfig::kKeyEnabled) == false);

    TEST(iConfig->HasNum(VolumeConfig::kKeyStartupValue) == true);
    TEST(iConfig->HasNum(VolumeConfig::kKeyLimit) == true);
    TEST(volumeConfig.iBalance == nullptr);
    TEST(volumeConfig.iFade == nullptr);

    TEST(iConfig->HasNum(VolumeConfig::kKeyBalance) == false);
    TEST(iConfig->HasNum(VolumeConfig::kKeyFade) == false);
}

// SuiteVolumeManager

const Brn SuiteVolumeManager::kSystemName("System.Name");
const TChar SuiteVolumeManager::kType = ' ';

SuiteVolumeManager::SuiteVolumeManager(DvStack& aDvStack)
    : SuiteUnitTest("SuiteVolumeManager")
    , iDvStack(aDvStack)
{
    AddTest(MakeFunctor(*this, &SuiteVolumeManager::TestAllComponentsInitialize), "TestAllComponentsInitialize");
    AddTest(MakeFunctor(*this, &SuiteVolumeManager::TestNoVolumeControlNoMute), "TestNoVolumeControlNoMute");
    AddTest(MakeFunctor(*this, &SuiteVolumeManager::TestNoVolumeComponent), "TestNoVolumeComponent");
    AddTest(MakeFunctor(*this, &SuiteVolumeManager::TestNoVolumeControl), "TestNoVolumeControl");
    AddTest(MakeFunctor(*this, &SuiteVolumeManager::TestNoMuteComponents), "TestNoMuteComponents");
    AddTest(MakeFunctor(*this, &SuiteVolumeManager::TestNoBalanceNoFadeComponents), "TestNoBalanceNoFadeComponents");
    AddTest(MakeFunctor(*this, &SuiteVolumeManager::TestNoVolumeNoBalanceNoFadeComponents), "TestNoVolumeNoBalanceNoFadeComponents");
}

void SuiteVolumeManager::Setup()
{
    Bwh udn("VolumeManagerTests");
    iDvDevice = new DvDeviceStandard(iDvStack, udn);
    iReadStore = new MockReadStore();
    iStore = new Configuration::ConfigRamStore();
    iConfig = new Configuration::ConfigManager(*iStore);

    iConfigText = new Configuration::ConfigText(*iConfig, Brn("Product.Room"), 1, 32, Brn("Product.Room"));
    iConfigText2 = new Configuration::ConfigText(*iConfig, Brn("Product.Name"), 1, 32, Brn("Product.Name"));
    iConfigText3 = new Configuration::ConfigText(*iConfig, Brn("Source.StartupName"), 1, 32, Brn("Last Used"));

    iPowerManager = new PowerManager(*iConfig);
    iProduct = new Product(iDvStack.Env(), *iDvDevice, *iReadStore, *iStore, *iConfig, *iConfig, *iPowerManager);
    iSource = new MockSource(kSystemName, &kType);
    iProduct->AddSource(iSource);
    iVolumeConsumer = new VolumeConsumer();
    iMute = new MockMute();
    iVolume = new MockVolume();
    iBalance = new MockBalance();
    iFade = new MockFade();
    iVolumeProfile = new MockVolumeProfile(100, 80, 100, 10, 10, false);
    iVolumeConfig = new VolumeConfig(*iStore, *iConfig, *iPowerManager, *iVolumeProfile);
}

void SuiteVolumeManager::TearDown()
{
    delete iVolumeConfig;
    delete iVolumeProfile;
    delete iFade;
    delete iBalance;
    delete iVolume;
    delete iMute;
    delete iVolumeConsumer;
    delete iProduct;
    delete iPowerManager;
    delete iConfigText3;
    delete iConfigText2;
    delete iConfigText;
    delete iConfig;
    delete iStore;
    delete iReadStore;
    delete iDvDevice;
}

void SuiteVolumeManager::TestAllComponentsInitialize()
{
    iVolumeConfig->iVolumeControlEnabled = true;
    iVolumeConsumer->SetBalance(*iBalance);
    iVolumeConsumer->SetFade(*iFade);
    iVolumeConsumer->SetVolume(*iVolume);
    VolumeManager volumeManager(*iVolumeConsumer, iMute, *iVolumeConfig, *iDvDevice, *iProduct, *iConfig, *iPowerManager, iDvStack.Env());
    iProduct->Start();

    TEST(volumeManager.iBalanceUser != nullptr);
    TEST(volumeManager.iFadeUser != nullptr);
    TEST(volumeManager.iMuteReporter != nullptr);
    TEST(volumeManager.iMuteUser != nullptr);
    TEST(volumeManager.iVolumeSourceUnityGain != nullptr);
    TEST(volumeManager.iVolumeUnityGain != nullptr);
    TEST(volumeManager.iVolumeSourceOffset != nullptr);
    TEST(volumeManager.iVolumeReporter != nullptr);
    TEST(volumeManager.iVolumeLimiter != nullptr);
    TEST(volumeManager.iVolumeUser != nullptr);
    TEST(volumeManager.iProviderVolume != nullptr);
}

void SuiteVolumeManager::TestNoVolumeControlNoMute()
{
    delete iVolumeConfig;
    Bws<4> volControlEnabledBuf;
    WriterBuffer writerBuffer(volControlEnabledBuf);
    WriterBinary writerBinary(writerBuffer);
    writerBinary.WriteUint32Be(eStringIdNo);
    iStore->Write(VolumeConfig::kKeyEnabled, volControlEnabledBuf);
    iVolumeConfig = new VolumeConfig(*iStore, *iConfig, *iPowerManager, *iVolumeProfile);

    iVolumeConsumer->SetBalance(*iBalance);
    iVolumeConsumer->SetFade(*iFade);
    iVolumeConsumer->SetVolume(*iVolume);
    VolumeManager volumeManager(*iVolumeConsumer, nullptr, *iVolumeConfig, *iDvDevice, *iProduct, *iConfig, *iPowerManager, iDvStack.Env());
    iProduct->Start();

    TEST(volumeManager.iBalanceUser == nullptr);
    TEST(volumeManager.iFadeUser == nullptr);
    TEST(volumeManager.iMuteReporter == nullptr);
    TEST(volumeManager.iMuteUser == nullptr);
    TEST(volumeManager.iVolumeSourceUnityGain == nullptr);
    TEST(volumeManager.iVolumeUnityGain == nullptr);
    TEST(volumeManager.iVolumeSourceOffset == nullptr);
    TEST(volumeManager.iVolumeReporter == nullptr);
    TEST(volumeManager.iVolumeLimiter == nullptr);
    TEST(volumeManager.iVolumeUser == nullptr);
    TEST(volumeManager.iProviderVolume == nullptr);
}

void SuiteVolumeManager::TestNoVolumeComponent()
{
    iVolumeConfig->iVolumeControlEnabled = true;
    iVolumeConsumer->SetBalance(*iBalance);
    iVolumeConsumer->SetFade(*iFade);
    VolumeManager volumeManager(*iVolumeConsumer, iMute, *iVolumeConfig, *iDvDevice, *iProduct, *iConfig, *iPowerManager, iDvStack.Env());
    iProduct->Start();

    TEST(volumeManager.iBalanceUser != nullptr);
    TEST(volumeManager.iFadeUser != nullptr);
    TEST(volumeManager.iMuteReporter != nullptr);
    TEST(volumeManager.iMuteUser != nullptr);

    TEST(volumeManager.iVolumeSourceUnityGain == nullptr);
    TEST(volumeManager.iVolumeUnityGain == nullptr);
    TEST(volumeManager.iVolumeSourceOffset == nullptr);
    TEST(volumeManager.iVolumeReporter == nullptr);
    TEST(volumeManager.iVolumeLimiter == nullptr);
    TEST(volumeManager.iVolumeUser == nullptr);
    TEST(volumeManager.iProviderVolume == nullptr);
}

void SuiteVolumeManager::TestNoVolumeControl()
{
    Bws<4> volControlEnabledBuf;
    WriterBuffer writerBuffer(volControlEnabledBuf);
    WriterBinary writerBinary(writerBuffer);
    writerBinary.WriteUint32Be(eStringIdNo);
    iStore->Write(VolumeConfig::kKeyEnabled, volControlEnabledBuf);

    Configuration::ConfigManager configManager(*iStore);
    MockVolumeProfile volumeProfile(100, 80, 100, 10, 10, false);
    VolumeConfig volumeConfig(*iStore, configManager, *iPowerManager, volumeProfile);

    iVolumeConsumer->SetBalance(*iBalance);
    iVolumeConsumer->SetFade(*iFade);
    iVolumeConsumer->SetVolume(*iVolume);
    VolumeManager volumeManager(*iVolumeConsumer, iMute, volumeConfig, *iDvDevice, *iProduct, *iConfig, *iPowerManager, iDvStack.Env());
    iProduct->Start();

    TEST(volumeManager.iBalanceUser == nullptr);
    TEST(volumeManager.iFadeUser == nullptr);
    TEST(volumeManager.iMuteReporter != nullptr);
    TEST(volumeManager.iMuteUser != nullptr);

    TEST(volumeManager.iVolumeSourceUnityGain == nullptr);
    TEST(volumeManager.iVolumeUnityGain == nullptr);
    TEST(volumeManager.iVolumeSourceOffset == nullptr);
    TEST(volumeManager.iVolumeReporter == nullptr);
    TEST(volumeManager.iVolumeLimiter == nullptr);
    TEST(volumeManager.iVolumeUser == nullptr);
    TEST(volumeManager.iProviderVolume == nullptr);
}

void SuiteVolumeManager::TestNoMuteComponents()
{
    iVolumeConfig->iVolumeControlEnabled = true;
    iVolumeConsumer->SetBalance(*iBalance);
    iVolumeConsumer->SetFade(*iFade);
    iVolumeConsumer->SetVolume(*iVolume);
    VolumeManager volumeManager(*iVolumeConsumer, nullptr, *iVolumeConfig, *iDvDevice, *iProduct, *iConfig, *iPowerManager, iDvStack.Env());
    iProduct->Start();

    TEST(volumeManager.iMuteReporter == nullptr);
    TEST(volumeManager.iMuteUser == nullptr);

    TEST(volumeManager.iBalanceUser != nullptr);
    TEST(volumeManager.iFadeUser != nullptr);
    TEST(volumeManager.iVolumeSourceUnityGain != nullptr);
    TEST(volumeManager.iVolumeUnityGain != nullptr);
    TEST(volumeManager.iVolumeSourceOffset != nullptr);
    TEST(volumeManager.iVolumeReporter != nullptr);
    TEST(volumeManager.iVolumeLimiter != nullptr);
    TEST(volumeManager.iVolumeUser != nullptr);
    TEST(volumeManager.iProviderVolume != nullptr);
}

void SuiteVolumeManager::TestNoBalanceNoFadeComponents()
{
    iVolumeConfig->iVolumeControlEnabled = true;
    iVolumeConsumer->SetVolume(*iVolume);
    VolumeManager volumeManager(*iVolumeConsumer, iMute, *iVolumeConfig, *iDvDevice, *iProduct, *iConfig, *iPowerManager, iDvStack.Env());
    iProduct->Start();

    TEST(volumeManager.iBalanceUser == nullptr);
    TEST(volumeManager.iFadeUser == nullptr);

    TEST(volumeManager.iMuteReporter != nullptr);
    TEST(volumeManager.iMuteUser != nullptr);

    TEST(volumeManager.iVolumeSourceUnityGain != nullptr);
    TEST(volumeManager.iVolumeUnityGain != nullptr);
    TEST(volumeManager.iVolumeSourceOffset != nullptr);
    TEST(volumeManager.iVolumeReporter != nullptr);
    TEST(volumeManager.iVolumeLimiter != nullptr);
    TEST(volumeManager.iVolumeUser != nullptr);
    TEST(volumeManager.iProviderVolume != nullptr);
}

void SuiteVolumeManager::TestNoVolumeNoBalanceNoFadeComponents()
{
    iVolumeConfig->iVolumeControlEnabled = true;
    VolumeManager volumeManager(*iVolumeConsumer, iMute, *iVolumeConfig, *iDvDevice, *iProduct, *iConfig, *iPowerManager, iDvStack.Env());
    iProduct->Start();

    TEST(volumeManager.iBalanceUser == nullptr);
    TEST(volumeManager.iFadeUser == nullptr);

    TEST(volumeManager.iMuteReporter != nullptr);
    TEST(volumeManager.iMuteUser != nullptr);

    TEST(volumeManager.iVolumeSourceUnityGain == nullptr);
    TEST(volumeManager.iVolumeUnityGain == nullptr);
    TEST(volumeManager.iVolumeSourceOffset == nullptr);
    TEST(volumeManager.iVolumeReporter == nullptr);
    TEST(volumeManager.iVolumeLimiter == nullptr);
    TEST(volumeManager.iVolumeUser == nullptr);
    TEST(volumeManager.iProviderVolume == nullptr);
}

void TestVolumeManager(CpStack& /* aCpStack */, DvStack& aDvStack)
{
    Runner runner("VolumeManager tests\n");
    runner.Add(new SuiteVolumeConsumer());
    runner.Add(new SuiteVolumeUser(aDvStack.Env()));
    runner.Add(new SuiteVolumeLimiter());
    runner.Add(new SuiteVolumeValue());
    runner.Add(new SuiteVolumeReporter());
    runner.Add(new SuiteVolumeSourceOffset());
    runner.Add(new SuiteVolumeSurroundBoost());
    runner.Add(new SuiteVolumeUnityGain());
    runner.Add(new SuiteVolumeSourceUnityGain());
    runner.Add(new SuiteVolumeRamperPipeline());
    runner.Add(new SuiteVolumeMuterStepped());
    runner.Add(new SuiteVolumeMuter());
    runner.Add(new SuiteVolumeBalanceUser());
    runner.Add(new SuiteVolumeFadeUser());
    runner.Add(new SuiteVolumeMuteUser());
    runner.Add(new SuiteVolumeMuteReporter());
    runner.Add(new SuiteVolumeScaler());
    runner.Add(new SuiteVolumeConfig());
    runner.Add(new SuiteVolumeManager(aDvStack));
    runner.Run();
}

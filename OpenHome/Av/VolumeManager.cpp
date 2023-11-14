#include <OpenHome/Av/VolumeManager.h>
#include <OpenHome/Types.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Timer.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/PowerManager.h>
#include <OpenHome/Av/StringIds.h>
#include <OpenHome/Av/Product.h>
#include <OpenHome/Av/ProviderVolume.h>
#include <OpenHome/Media/MuteManager.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Av/Debug.h>
#include <OpenHome/Av/VolumeOffsets.h>

#include <vector>
#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Configuration;

// VolumeConsumer

VolumeConsumer::VolumeConsumer()
    : iVolume(nullptr)
    , iBalance(nullptr)
    , iFade(nullptr)
    , iVolumeOffsetter(nullptr)
    , iTrim(nullptr)
{
}

void VolumeConsumer::SetVolume(IVolume& aVolume)
{
    iVolume = &aVolume;
}

void VolumeConsumer::SetBalance(IBalance& aBalance)
{
    iBalance = &aBalance;
}

void VolumeConsumer::SetFade(IFade& aFade)
{
    iFade = &aFade;
}

void VolumeConsumer::SetVolumeOffsetter(IVolumeOffsetter& aVolumeOffsetter)
{
    iVolumeOffsetter = &aVolumeOffsetter;
}

void VolumeConsumer::SetTrim(ITrim& aTrim)
{
    iTrim = &aTrim;
}

IVolume* VolumeConsumer::Volume()
{
    return iVolume;
}

IBalance* VolumeConsumer::Balance()
{
    return iBalance;
}

IFade* VolumeConsumer::Fade()
{
    return iFade;
}

IVolumeOffsetter* VolumeConsumer::VolumeOffsetter()
{
    return iVolumeOffsetter;
}

ITrim* VolumeConsumer::Trim()
{
    return iTrim;
}


// VolumeNull

void VolumeNull::SetVolume(TUint /*aVolume*/)
{
}


// VolumeUser

const Brn VolumeUser::kStartupVolumeKey("Startup.Volume");
const TUint VolumeUser::kLastUsedWriteDelayMs = 10 * 1000; // 10 seconds

VolumeUser::VolumeUser(
    IVolume& aVolume,
    IConfigManager& aConfigReader,
    IPowerManager& aPowerManager,
    Environment& aEnv,
    StoreInt& aStoreUserVolume,
    TUint aMaxVolume,
    TUint aMilliDbPerStep)
    : iVolume(aVolume)
    , iStoreUserVolume(aStoreUserVolume)
    , iStartupVolumeReported(false)
    , iMaxVolume(aMaxVolume)
    , iMilliDbPerStep(aMilliDbPerStep)
{
    iLastUsedWriter = new Timer(aEnv, MakeFunctor(*this, &VolumeUser::WriteLastUsedVolume), "VolumeUser");
    if (aConfigReader.HasNum(VolumeConfig::kKeyStartupValue)) {
        iConfigStartupVolume = &aConfigReader.GetNum(VolumeConfig::kKeyStartupValue);
        iSubscriberIdStartupVolume = iConfigStartupVolume->Subscribe(MakeFunctorConfigNum(*this, &VolumeUser::StartupVolumeChanged));
    }
    else {
        iConfigStartupVolume = nullptr;
        iSubscriberIdStartupVolume = IConfigManager::kSubscriptionIdInvalid;
    }
    if (aConfigReader.HasChoice(VolumeConfig::kKeyStartupEnabled)) {
        iConfigStartupVolumeEnabled = &aConfigReader.GetChoice(VolumeConfig::kKeyStartupEnabled);
        iSubscriberIdStartupVolumeEnabled = iConfigStartupVolumeEnabled->Subscribe(MakeFunctorConfigChoice(*this, &VolumeUser::StartupVolumeEnabledChanged));
    }
    else {
        iConfigStartupVolumeEnabled = nullptr;
        iSubscriberIdStartupVolumeEnabled = IConfigManager::kSubscriptionIdInvalid;
        iStartupVolumeEnabled = (iConfigStartupVolume != nullptr); // startup at last used volume if user can't specify a startup level
    }
    iStandbyObserver = aPowerManager.RegisterStandbyHandler(*this, kStandbyHandlerPriorityNormal, "VolumeUser");
    if (!iStartupVolumeReported) {
        ApplyStartupVolume(); // set volume immediately to avoid reporting volume==0 until we exit standby
    }
}

VolumeUser::~VolumeUser()
{
    delete iStandbyObserver;
    if (iConfigStartupVolume != nullptr) {
        iConfigStartupVolume->Unsubscribe(iSubscriberIdStartupVolume);
    }
    if (iConfigStartupVolumeEnabled != nullptr) {
        iConfigStartupVolumeEnabled->Unsubscribe(iSubscriberIdStartupVolumeEnabled);
    }
    delete iLastUsedWriter;
}

void VolumeUser::SetVolume(TUint aVolume)
{
    LOG(kVolume, "VolumeUser::SetVolume aVolume: %u\n", aVolume);
    if (aVolume > iMaxVolume) {
        const TUint currentVolume = iStoreUserVolume.Get();
        if (currentVolume < iMaxVolume) {
            aVolume = iMaxVolume;
        }
        else {
            THROW(VolumeOutOfRange);
        }
    }
    iVolume.SetVolume(aVolume);
    iStoreUserVolume.Set(aVolume);
    iLastUsedWriter->FireIn(kLastUsedWriteDelayMs);
}

void VolumeUser::StandbyEnabled()
{
    // no need to change volume when we enter standby
}

void VolumeUser::StandbyTransitioning()
{
}

void VolumeUser::StandbyDisabled(StandbyDisableReason /*aReason*/)
{
    ApplyStartupVolume();
}

void VolumeUser::StartupVolumeChanged(ConfigNum::KvpNum& aKvp)
{
    iStartupVolume = aKvp.Value();
}

void VolumeUser::StartupVolumeEnabledChanged(ConfigChoice::KvpChoice& aKvp)
{
    iStartupVolumeEnabled = (aKvp.Value() == eStringIdYes);
}

void VolumeUser::ApplyStartupVolume()
{
    TUint startupVolume;
    if (iStartupVolumeEnabled) {
        startupVolume = iStartupVolume * iMilliDbPerStep;
    }
    else {
        startupVolume = iStoreUserVolume.Get();
    }
    try {
        iVolume.SetVolume(startupVolume);
        iStartupVolumeReported = true;
    }
    catch (VolumeNotSupported&) {}
    catch (VolumeOutOfRange&) {} // ignore any errors caused by volume limit being set lower than startup volume
}

void VolumeUser::WriteLastUsedVolume()
{
    iStoreUserVolume.Write();
}


// VolumeLimiter

VolumeLimiter::VolumeLimiter(IVolume& aVolume, TUint aMilliDbPerStep, IConfigManager& aConfigReader)
    : iLock("VLMT")
    , iVolume(aVolume)
    , iMilliDbPerStep(aMilliDbPerStep)
    , iConfigLimit(aConfigReader.GetNum(VolumeConfig::kKeyLimit))
    , iCurrentVolume(0)
{
    iSubscriberIdLimit = iConfigLimit.Subscribe(MakeFunctorConfigNum(*this, &VolumeLimiter::LimitChanged));
}

VolumeLimiter::~VolumeLimiter()
{
    iConfigLimit.Unsubscribe(iSubscriberIdLimit);
}

void VolumeLimiter::SetVolume(TUint aValue)
{
    LOG(kVolume, "VolumeLimiter::SetVolume aValue: %u\n", aValue);
    AutoMutex _(iLock);
    if (aValue > iLimit && iCurrentVolume >= iLimit) {
        THROW(VolumeOutOfRange);
    }
    iCurrentVolume = aValue;
    DoSetVolume();
}

void VolumeLimiter::LimitChanged(ConfigNum::KvpNum& aKvp)
{
    AutoMutex _(iLock);
    iLimit = aKvp.Value() * iMilliDbPerStep;
    try {
        DoSetVolume();
    }
    catch (VolumeNotSupported&) {}
    catch (VolumeOutOfRange&) {}
}

void VolumeLimiter::DoSetVolume()
{
    iCurrentVolume = std::min(iCurrentVolume, iLimit);
    iVolume.SetVolume(iCurrentVolume);
}


// VolumeValue

VolumeValue::VolumeValue(TUint aVolumeUser, TUint aBinaryMilliDb)
    : iVolumeUser(aVolumeUser)
    , iVolumeBinaryMilliDb(aBinaryMilliDb)
{
}

TUint VolumeValue::VolumeUser() const
{
    return iVolumeUser;
}

TUint VolumeValue::VolumeBinaryMilliDb() const
{
    return iVolumeBinaryMilliDb;
}


// VolumeReporter

VolumeReporter::VolumeReporter(IVolume& aVolume, TUint aMilliDbPerStep)
    : iVolume(aVolume)
    , iMilliDbPerStep(aMilliDbPerStep)
    , iUpstreamVolume(0)
{
}

void VolumeReporter::AddVolumeObserver(IVolumeObserver& aObserver)
{
    const TUint volUser = (iMilliDbPerStep > 0 ? iUpstreamVolume / iMilliDbPerStep : 0);
    const VolumeValue vol(volUser, iUpstreamVolume);
    aObserver.VolumeChanged(vol);
    iObservers.push_back(&aObserver);
}

void VolumeReporter::SetVolume(TUint aVolume)
{
    LOG(kVolume, "VolumeReporter::SetVolume aVolume: %u\n", aVolume);
    iVolume.SetVolume(aVolume);
    iUpstreamVolume = aVolume;
    const TUint volUser = (iMilliDbPerStep > 0 ? iUpstreamVolume / iMilliDbPerStep : 0);
    const VolumeValue vol(volUser, iUpstreamVolume);
    for (auto it=iObservers.begin(); it!=iObservers.end(); ++it) {
        (*it)->VolumeChanged(vol);
    }
}


// VolumeSourceOffset

VolumeSourceOffset::VolumeSourceOffset(IVolume& aVolume)
    : iLock("VSOF")
    , iVolume(aVolume)
    , iUpstreamVolume(0)
    , iSourceOffset(0)
{
}

void VolumeSourceOffset::SetVolume(TUint aValue)
{
    LOG(kVolume, "VolumeSourceOffset::SetVolume aValue: %u\n", aValue);
    AutoMutex _(iLock);
    DoSetVolume(aValue);
    iUpstreamVolume = aValue;
}

void VolumeSourceOffset::SetVolumeOffset(TInt aOffset)
{
    AutoMutex _(iLock);
    iSourceOffset = aOffset;
    try {
        DoSetVolume(iUpstreamVolume);
    }
    catch (VolumeNotSupported&) {}
}

void VolumeSourceOffset::DoSetVolume(TUint aValue)
{
    TUint volume = aValue + iSourceOffset;
    if (aValue == 0) {
        volume = 0; // upstream volume of 0 should mean we output silence
    }
    if (volume > aValue && iSourceOffset < 0) {
        volume = 0;
    }
    else if (volume < aValue && iSourceOffset > 0) {
        volume = aValue;
    }
    iVolume.SetVolume(volume);
}


// VolumeSurroundBoost

VolumeSurroundBoost::VolumeSurroundBoost(IVolume& aVolume)
    : iLock("VSOF")
    , iVolume(aVolume)
    , iUpstreamVolume(0)
    , iBoost(0)
{
}

void VolumeSurroundBoost::SetVolume(TUint aValue)
{
    LOG(kVolume, "VolumeSurroundBoost::SetVolume aValue: %u\n", aValue);
    AutoMutex _(iLock);
    iUpstreamVolume = aValue;
    DoSetVolume();
}

void VolumeSurroundBoost::SetVolumeBoost(TInt aBoost)
{
    LOG(kVolume, "VolumeSurroundBoost::SetVolumeBoost aBoost: %d\n", aBoost);
    AutoMutex _(iLock);
    iBoost = aBoost;
    try {
        DoSetVolume();
    }
    catch (VolumeNotSupported&) {}
}

void VolumeSurroundBoost::DoSetVolume()
{
    TUint volume = iUpstreamVolume;
    if (volume != 0) {
        if (iBoost < 0 && (TUint)-iBoost > volume) {
            volume = 0;
        }
        else {
            volume += iBoost;
        }
    }
    iVolume.SetVolume(volume);
}


// VolumeUnityGainBase

VolumeUnityGainBase::VolumeUnityGainBase(IVolume& aVolume, TUint aUnityGainValue)
    : iLock("VUGN")
    , iVolume(aVolume)
    , iUnityGain(aUnityGainValue)
    , iUpstreamVolume(0)
{
}

void VolumeUnityGainBase::SetVolume(TUint aValue)
{
    LOG(kVolume, "VolumeUnityGainBase::SetVolume aValue: %u\n", aValue);
    AutoMutex _(iLock);
    if (!iVolumeControlEnabled) {
        THROW(VolumeNotSupported);
    }
    iVolume.SetVolume(aValue);
    iUpstreamVolume = aValue;
}

void VolumeUnityGainBase::SetVolumeControlEnabled(TBool aEnabled)
{
    AutoMutex _(iLock);
    iVolumeControlEnabled = aEnabled;
    try {
        if (iVolumeControlEnabled) {
            iVolume.SetVolume(iUpstreamVolume);
        }
        else {
            iVolume.SetVolume(iUnityGain);
        }
    }
    catch (VolumeNotSupported&) {}
}

TBool VolumeUnityGainBase::VolumeControlEnabled() const
{
    AutoMutex _(iLock);
    return iVolumeControlEnabled;
}


// VolumeUnityGain

VolumeUnityGain::VolumeUnityGain(IVolume& aVolume, IConfigManager& aConfigReader, TUint aUnityGainValue)
    : VolumeUnityGainBase(aVolume, aUnityGainValue)
    , iConfigVolumeControlEnabled(aConfigReader.GetChoice(VolumeConfig::kKeyEnabled))
{
    TUint subscriberId = iConfigVolumeControlEnabled.Subscribe(MakeFunctorConfigChoice(*this, &VolumeUnityGain::EnabledChanged));
    iConfigVolumeControlEnabled.Unsubscribe(subscriberId);
}

VolumeUnityGain::~VolumeUnityGain()
{

}

void VolumeUnityGain::EnabledChanged(ConfigChoice::KvpChoice& aKvp)
{
    const TBool enabled = (aKvp.Value() == eStringIdYes);
    SetVolumeControlEnabled(enabled);
}


// VolumeSourceUnityGain

VolumeSourceUnityGain::VolumeSourceUnityGain(IVolume& aVolume, TUint aUnityGainValue)
    : VolumeUnityGainBase(aVolume, aUnityGainValue)
{
    SetVolumeControlEnabled(true);
}

void VolumeSourceUnityGain::SetUnityGain(TBool aEnable)
{
    SetVolumeControlEnabled(!aEnable);
    const TBool unityGain = !VolumeControlEnabled();
    for (auto observer: iObservers){
        observer.get().UnityGainChanged(unityGain);
    }
}

void VolumeSourceUnityGain::AddUnityGainObserver(IUnityGainObserver& aObserver)
{
    const TBool unityGain = !VolumeControlEnabled();
    aObserver.UnityGainChanged(unityGain);
    iObservers.push_back(aObserver);
}


// VolumeRamperPipeline

VolumeRamperPipeline::VolumeRamperPipeline(IVolume& aVolume)
    : iVolume(aVolume)
    , iLock("ABVR")
    , iUpstreamVolume(0)
    , iMultiplier(IVolumeRamper::kMultiplierFull)
{
}

void VolumeRamperPipeline::SetVolume(TUint aValue)
{
    LOG(kVolume, "VolumeRamperPipeline::SetVolume aValue: %u\n", aValue);
    AutoMutex _(iLock);
    iUpstreamVolume = aValue;
    SetVolume();
}

void VolumeRamperPipeline::ApplyVolumeMultiplier(TUint aValue)
{
    AutoMutex _(iLock);
    if (iMultiplier == aValue) {
        return;
    }
    LOG(kVolume, "VolumeRamperPipeline::ApplyVolumeMultiplier aValue: %u\n", aValue);
    iMultiplier = aValue;
    SetVolume();
}

void VolumeRamperPipeline::SetVolume()
{
    if (iMultiplier == IVolumeRamper::kMultiplierFull) {
        iVolume.SetVolume(iUpstreamVolume);
    }
    else {
        TUint64 volume = iUpstreamVolume;
        volume *= iMultiplier;
        volume /= IVolumeRamper::kMultiplierFull;
        iVolume.SetVolume((TUint)volume);
    }
}


// VolumeMuterStepped

const TUint VolumeMuterStepped::kJiffiesPerVolumeStep = 10 * Media::Jiffies::kPerMs;

VolumeMuterStepped::VolumeMuterStepped(IVolume& aVolume, TUint aMilliDbPerStep, TUint aThreadPriority)
    : iVolume(aVolume)
    , iLock("VOLR")
    , iMilliDbPerStep(aMilliDbPerStep)
    , iUpstreamVolume(0)
    , iPendingVolume(0)
    , iCurrentVolume(0)
    , iJiffiesUntilStep(0)
    , iStatus(Status::eRunning)
    , iMuted(false)
{
    iThread = new ThreadFunctor("VolumeMuterStepped", MakeFunctor(*this, &VolumeMuterStepped::Run), aThreadPriority);
    iThread->Start();
}

VolumeMuterStepped::~VolumeMuterStepped()
{
    delete iThread;
}

void VolumeMuterStepped::SetVolume(TUint aValue)
{
    AutoMutex _(iLock);
    iUpstreamVolume = aValue;
    if (iStatus == Status::eRunning) {
        iPendingVolume = aValue;
        iThread->Signal();
    }
}

inline TUint VolumeMuterStepped::VolumeStepLocked() const
{
    // values and threshold for change copied from Linn volkano1
    if (iCurrentVolume < 20 * iMilliDbPerStep) {
        return 4 * iMilliDbPerStep;
    }
    return 2 * iMilliDbPerStep;
}

Media::IVolumeMuterStepped::Status VolumeMuterStepped::BeginMute()
{
    AutoMutex _(iLock);
    if (iStatus == Status::eMuted) {
        return IVolumeMuterStepped::Status::eComplete;
    }
    else if (iStatus != Status::eMuting) {
        iJiffiesUntilStep = kJiffiesPerVolumeStep;
        iStatus = Status::eMuting;
    }
    return IVolumeMuterStepped::Status::eInProgress;
}

Media::IVolumeMuterStepped::Status VolumeMuterStepped::StepMute(TUint aJiffies)
{
    AutoMutex _(iLock);
    if (iStatus == Status::eMuted) {
        return IVolumeMuterStepped::Status::eComplete;
    }
    if (iJiffiesUntilStep <= aJiffies) {
        aJiffies -= iJiffiesUntilStep;
        const auto step = VolumeStepLocked();
        if (step > iPendingVolume) {
            iPendingVolume = 0;
        }
        else {
            iPendingVolume -= step;
        }
        iThread->Signal();

        ASSERT(aJiffies < kJiffiesPerVolumeStep); // we're not effectively ramping volume if a single call here results in a large volume adjustment
        iJiffiesUntilStep = kJiffiesPerVolumeStep;
    }
    iJiffiesUntilStep -= aJiffies;
    return IVolumeMuterStepped::Status::eInProgress;
}

void VolumeMuterStepped::SetMuted()
{
    AutoMutex _(iLock);
    iStatus = Status::eMuted;
    if (iPendingVolume != 0) {
        iPendingVolume = 0;
        iThread->Signal();
    }
}

Media::IVolumeMuterStepped::Status VolumeMuterStepped::BeginUnmute()
{
    AutoMutex _(iLock);
    if (iStatus == Status::eRunning) {
        return IVolumeMuterStepped::Status::eComplete;
    }
    else if (iStatus != Status::eUnmuting) {
        iJiffiesUntilStep = kJiffiesPerVolumeStep;
        iStatus = Status::eUnmuting;
    }
    return IVolumeMuterStepped::Status::eInProgress;
}

Media::IVolumeMuterStepped::Status VolumeMuterStepped::StepUnmute(TUint aJiffies)
{
    AutoMutex _(iLock);
    if (iStatus == Status::eRunning) {
        return IVolumeMuterStepped::Status::eComplete;
    }
    if (iJiffiesUntilStep <= aJiffies) {
        aJiffies -= iJiffiesUntilStep;
        iPendingVolume += VolumeStepLocked();
        iPendingVolume = std::min(iPendingVolume, iUpstreamVolume);
        iThread->Signal();

        ASSERT(aJiffies < kJiffiesPerVolumeStep); // we're not effectively ramping volume if a single call here results in a large volume adjustment
        iJiffiesUntilStep = kJiffiesPerVolumeStep;
    }
    iJiffiesUntilStep -= aJiffies;
    return IVolumeMuterStepped::Status::eInProgress;
}

void VolumeMuterStepped::SetUnmuted()
{
    AutoMutex _(iLock);
    iStatus = Status::eRunning;
    if (iPendingVolume != iUpstreamVolume) {
        iPendingVolume = iUpstreamVolume;
        iThread->Signal();
    }
}

void VolumeMuterStepped::Run()
{
    try {
        for (;;) {
            iThread->Wait();
            TUint pendingVolume;
            {
                AutoMutex _(iLock);
                pendingVolume = iPendingVolume;
                iCurrentVolume = iPendingVolume;
                if (iStatus == Status::eMuting) {
                    if (iCurrentVolume == 0) {
                        iStatus = Status::eMuted;
                    }
                }
                else if (iStatus == Status::eUnmuting) {
                    if (iCurrentVolume == iUpstreamVolume) {
                        iStatus = Status::eRunning;
                    }
                }
            }
            iVolume.SetVolume(pendingVolume);
        }
    }
    catch (ThreadKill&) {}
}


// VolumeMuter

VolumeMuter::VolumeMuter(IVolume* aVolume)
    : iVolume(aVolume)
    , iLock("VMUT")
    , iUpstreamVolume(0)
    , iMuted(false)
{
}

void VolumeMuter::SetVolume(TUint aValue)
{
    LOG(kVolume, "VolumeMuter::SetVolume(%u)\n", aValue);
    AutoMutex _(iLock);
    iUpstreamVolume = aValue;
    DoSetVolume();
}

void VolumeMuter::SetVolumeMuted(TBool aMuted)
{
    LOG(kVolume, "VolumeMuter::SetVolumeMuted(%u)\n", aMuted);
    AutoMutex _(iLock);
    iMuted = aMuted;
    DoSetVolume();
}

void VolumeMuter::DoSetVolume()
{
    if (iVolume != nullptr) {
        const TUint volume = (iMuted ? 0 : iUpstreamVolume);
        iVolume->SetVolume(volume);
    }
}


// BalanceUser

BalanceUser::BalanceUser(IBalance& aBalance, IConfigManager& aConfigReader)
    : iBalance(aBalance)
    , iConfigBalance(aConfigReader.GetNum(VolumeConfig::kKeyBalance))
{
    iSubscriberIdBalance = iConfigBalance.Subscribe(MakeFunctorConfigNum(*this, &BalanceUser::BalanceChanged));
}

BalanceUser::~BalanceUser()
{
    iConfigBalance.Unsubscribe(iSubscriberIdBalance);
}

void BalanceUser::SetBalance(TInt aBalance)
{
    try {
        iConfigBalance.Set(aBalance);
    }
    catch (ConfigValueOutOfRange&) {
        THROW(BalanceOutOfRange);
    }
}

void BalanceUser::BalanceChanged(ConfigNum::KvpNum& aKvp)
{
    try {
        iBalance.SetBalance(aKvp.Value());
    }
    catch (BalanceNotSupported&) {}
}


// FadeUser

FadeUser::FadeUser(IFade& aFade, IConfigManager& aConfigReader)
    : iFade(aFade)
    , iConfigFade(aConfigReader.GetNum(VolumeConfig::kKeyFade))
{
    iSubscriberIdFade = iConfigFade.Subscribe(MakeFunctorConfigNum(*this, &FadeUser::FadeChanged));
}

FadeUser::~FadeUser()
{
    iConfigFade.Unsubscribe(iSubscriberIdFade);
}

void FadeUser::SetFade(TInt aFade)
{
    try {
        iConfigFade.Set(aFade);
    }
    catch (ConfigValueOutOfRange&) {
        THROW(FadeOutOfRange);
    }
}

void FadeUser::FadeChanged(ConfigNum::KvpNum& aKvp)
{
    try {
        iFade.SetFade(aKvp.Value());
    }
    catch (FadeNotSupported&) {}
}


// MuteUser

MuteUser::MuteUser(Media::IMute& aMute, IPowerManager& aPowerManager)
    : iMute(aMute)
{
    iStandbyObserver = aPowerManager.RegisterStandbyHandler(*this, kStandbyHandlerPriorityNormal, "MuteUser");
}

MuteUser::~MuteUser()
{
    delete iStandbyObserver;
}

void MuteUser::Mute()
{
    iMute.Mute();
}

void MuteUser::Unmute()
{
    iMute.Unmute();
}

void MuteUser::StandbyEnabled()
{
    // no need to change mute when we enter standby
}

void MuteUser::StandbyTransitioning()
{
}

void MuteUser::StandbyDisabled(StandbyDisableReason /*aReason*/)
{
    // clear any previous mute when we leave standby
    Unmute();
}


// MuteReporter

MuteReporter::MuteReporter(Media::IMute& aMute)
    : iLock("MRep")
    , iMute(aMute)
    , iMuted(false)
{
}

void MuteReporter::AddMuteObserver(Media::IMuteObserver& aObserver)
{
    aObserver.MuteChanged(iMuted);
    iObservers.push_back(&aObserver);
}

void MuteReporter::Mute()
{
    if (Report(true)) {
        iMute.Mute();
    }
}

void MuteReporter::Unmute()
{
    if (Report(false)) {
        iMute.Unmute();
    }
}

TBool MuteReporter::Report(TBool aMuted)
{
    AutoMutex _(iLock);
    if (aMuted == iMuted) {
        return false;
    }
    iMuted = aMuted;
    for (auto it=iObservers.begin(); it!=iObservers.end(); ++it) {
        (*it)->MuteChanged(iMuted);
    }
    return true;
}


// VolumeConfig

const Brn VolumeConfig::kKeyStartupVolume("Last.Volume");
const Brn VolumeConfig::kKeyStartupValue("Volume.StartupValue");
const Brn VolumeConfig::kKeyStartupEnabled("Volume.StartupEnabled");
const Brn VolumeConfig::kKeyLimit("Volume.Limit");
const Brn VolumeConfig::kKeyEnabled("Volume.Enabled");
const Brn VolumeConfig::kKeyBalance("Volume.Balance");
const Brn VolumeConfig::kKeyFade("Volume.Fade");

VolumeConfig::VolumeConfig(
    IStoreReadWrite& aStore,
    IConfigInitialiser& aConfigInit,
    IPowerManager& aPowerManager,
    const IVolumeProfile& aProfile)
    : iStoreUserVolume(
        aStore,
        aPowerManager,
        kPowerPriorityHighest,
        kKeyStartupVolume,
        aProfile.VolumeDefault() * aProfile.VolumeMilliDbPerStep())
    , iVolumeStartup(nullptr)
    , iVolumeLimit(nullptr)
    , iVolumeEnabled(nullptr)
    , iBalance(nullptr)
    , iFade(nullptr)
{
    iVolumeMax            = aProfile.VolumeMax();
    iVolumeDefault        = aProfile.VolumeDefault();
    iVolumeUnity          = aProfile.VolumeUnity();
    iVolumeDefaultLimit   = aProfile.VolumeDefaultLimit();
    iVolumeStep           = aProfile.VolumeStep();
    iVolumeMilliDbPerStep = aProfile.VolumeMilliDbPerStep();
    iThreadPriority       = aProfile.ThreadPriority();
    iBalanceMax           = aProfile.BalanceMax();
    iFadeMax              = aProfile.FadeMax();
    iOffsetMax            = aProfile.OffsetMax();
    iAlwaysOn             = aProfile.AlwaysOn();
    iStartupVolumeConfig = aProfile.StartupVolumeConfig();


    std::vector<TUint> choices;
    choices.push_back(eStringIdYes);
    choices.push_back(eStringIdNo);

    if (iAlwaysOn) {
        iVolumeControlEnabled = true;
    }
    else if (iVolumeMax == 0) {
        // If maximum volume is 0 no sensible volume control can exist.
        // Flag volume control as disabled, and do not create ConfigVals to allow toggling or configuring volume control.
        iVolumeControlEnabled = false;
    }
    else {
        iVolumeEnabled = new ConfigChoice(aConfigInit, kKeyEnabled, choices, eStringIdYes, true);

        const TUint id = iVolumeEnabled->Subscribe(MakeFunctorConfigChoice(*this, &VolumeConfig::EnabledChanged));
        // EnabledChanged runs inside the call to Subscribe().
        // We don't support runtime change of this value so can immediately unsubscribe.
        iVolumeEnabled->Unsubscribe(id);
    }

    if (iVolumeControlEnabled)
    {
        if (aProfile.StartupVolumeConfig() != IVolumeProfile::StartupVolume::LastUsed) {
            iVolumeStartup = new ConfigNum(aConfigInit, kKeyStartupValue, 0, iVolumeMax, iVolumeDefault);
        }
        else {
            iVolumeStartup = nullptr;
        }
        if (aProfile.StartupVolumeConfig() == IVolumeProfile::StartupVolume::Both) {
            iVolumeStartupEnabled = new ConfigChoice(aConfigInit, kKeyStartupEnabled, choices, eStringIdYes);
        }
        else {
            iVolumeStartupEnabled = nullptr;
        }
        iVolumeLimit = new ConfigNum(aConfigInit, kKeyLimit, 0, iVolumeMax, iVolumeDefaultLimit);

        const TInt maxBalance = iBalanceMax;
        if (maxBalance == 0) {
            iBalance = nullptr;
        }
        else {
            iBalance = new ConfigNum(aConfigInit, kKeyBalance, -maxBalance, maxBalance, 0);
        }
        const TInt maxFade = iFadeMax;
        if (maxFade == 0) {
            iFade = nullptr;
        }
        else {
            iFade = new ConfigNum(aConfigInit, kKeyFade, -maxFade, maxFade, 0);
        }
    }
}

VolumeConfig::~VolumeConfig()
{
    if (iVolumeControlEnabled) {
        delete iVolumeStartup;
        delete iVolumeStartupEnabled;
        delete iVolumeLimit;
        delete iBalance;
        delete iFade;
    }

    if (!iAlwaysOn) {
        delete iVolumeEnabled;
    }
}

StoreInt& VolumeConfig::StoreUserVolume()
{
    return iStoreUserVolume;
}

TBool VolumeConfig::VolumeControlEnabled() const
{
    return iVolumeControlEnabled;
}

TUint VolumeConfig::VolumeMax() const
{
    return iVolumeMax;
}

TUint VolumeConfig::VolumeDefault() const
{
    return iVolumeDefault;
}

TUint VolumeConfig::VolumeUnity() const
{
    return iVolumeUnity;
}

TUint VolumeConfig::VolumeDefaultLimit() const
{
    return iVolumeDefaultLimit;
}

TUint VolumeConfig::VolumeStep() const
{
    return iVolumeStep;
}

TUint VolumeConfig::VolumeMilliDbPerStep() const
{
    return iVolumeMilliDbPerStep;
}

TUint VolumeConfig::ThreadPriority() const
{
    return iThreadPriority;
}

TUint VolumeConfig::BalanceMax() const
{
    return iBalanceMax;
}

TUint VolumeConfig::FadeMax() const
{
    return iFadeMax;
}

TUint VolumeConfig::OffsetMax() const
{
    return iOffsetMax;
}

TBool VolumeConfig::AlwaysOn() const
{
    return iAlwaysOn;
}

IVolumeProfile::StartupVolume VolumeConfig::StartupVolumeConfig() const
{
    return iStartupVolumeConfig;
}

void VolumeConfig::EnabledChanged(Configuration::ConfigChoice::KvpChoice& aKvp)
{
    iVolumeControlEnabled = (aKvp.Value() == eStringIdYes);
}


// VolumeManager

VolumeManager::VolumeManager(VolumeConsumer& aVolumeConsumer, IMute* aMute, VolumeConfig& aVolumeConfig,
                             Net::DvDevice& aDevice, Product& aProduct, IConfigManager& aConfigReader,
                             IPowerManager& aPowerManager, Environment& aEnv)
    : iVolumeConfig(aVolumeConfig)
{
    TBool volumeControlEnabled = aVolumeConfig.VolumeControlEnabled();

    iBalanceUser = nullptr;
    iFadeUser = nullptr;

    if (volumeControlEnabled)
    {
        IBalance* balance = aVolumeConsumer.Balance();
        if (balance != nullptr) {
            iBalanceUser = new BalanceUser(*balance, aConfigReader);
        }

        IFade* fade = aVolumeConsumer.Fade();
        if (fade != nullptr) {
            iFadeUser = new FadeUser(*fade, aConfigReader);
        }
    }

    if (aMute == nullptr) {
        iMuteReporter = nullptr;
        iMuteUser = nullptr;
    }
    else {
        iMuteReporter = new MuteReporter(*aMute);
        iMuteUser = new MuteUser(*iMuteReporter, aPowerManager);
    }
    const TUint milliDbPerStep = iVolumeConfig.VolumeMilliDbPerStep();
    const TUint volumeUnity = iVolumeConfig.VolumeUnity() * milliDbPerStep;
    iVolumeMuter = new VolumeMuter(aVolumeConsumer.Volume());
    iVolumeMuterStepped = new VolumeMuterStepped(*iVolumeMuter, milliDbPerStep, iVolumeConfig.ThreadPriority());
    iVolumeRamperPipeline = new VolumeRamperPipeline(*iVolumeMuterStepped);
    iVolumeSurroundBoost = new VolumeSurroundBoost(*iVolumeRamperPipeline);
    if (aVolumeConfig.VolumeControlEnabled() && aVolumeConsumer.Volume() != nullptr) {
        if (iVolumeConfig.AlwaysOn()) {
            iVolumeSourceUnityGain = new VolumeSourceUnityGain(*iVolumeSurroundBoost, volumeUnity);
        }
        else {
            iVolumeUnityGain = new VolumeUnityGain(*iVolumeSurroundBoost, aConfigReader, volumeUnity);
            iVolumeSourceUnityGain = new VolumeSourceUnityGain(*iVolumeUnityGain, volumeUnity);
        }
        iVolumeSourceOffset = new VolumeSourceOffset(*iVolumeSourceUnityGain);
        iVolumeReporter = new VolumeReporter(*iVolumeSourceOffset, milliDbPerStep);
        iVolumeLimiter = new VolumeLimiter(*iVolumeReporter, milliDbPerStep, aConfigReader);
        iVolumeUser = new VolumeUser(*iVolumeLimiter, aConfigReader, aPowerManager, aEnv,
                                     aVolumeConfig.StoreUserVolume(),
                                     iVolumeConfig.VolumeMax() * milliDbPerStep, milliDbPerStep);
        iProviderVolume = new ProviderVolume(aDevice, aConfigReader, *this, iBalanceUser, iFadeUser, aVolumeConsumer.VolumeOffsetter(), aVolumeConsumer.Trim());
        aProduct.AddAttribute("Volume");
    }
    else {
        iVolumeSourceUnityGain = nullptr;
        iVolumeUnityGain = nullptr;
        iVolumeSourceOffset = nullptr;
        iVolumeReporter = nullptr;
        iVolumeLimiter = nullptr;
        iVolumeUser = nullptr;
        iProviderVolume = nullptr;
        static_cast<IVolume*>(iVolumeSurroundBoost)->SetVolume(volumeUnity);
    }
}

VolumeManager::~VolumeManager()
{
    delete iProviderVolume;
    delete iMuteReporter;
    delete iMuteUser;
    delete iFadeUser;
    delete iBalanceUser;
    delete iVolumeUser;
    delete iVolumeLimiter;
    delete iVolumeReporter;
    delete iVolumeSourceOffset;
    delete iVolumeSurroundBoost;
    delete iVolumeUnityGain;
    delete iVolumeSourceUnityGain;
    delete iVolumeRamperPipeline;
    delete iVolumeMuterStepped;
    delete iVolumeMuter;
}

void VolumeManager::AddVolumeObserver(IVolumeObserver& aObserver)
{
    if (iVolumeReporter == nullptr) {
        const VolumeValue vol(0, 0);
        aObserver.VolumeChanged(vol);
    }
    else {
        iVolumeReporter->AddVolumeObserver(aObserver);
    }
}

void VolumeManager::AddMuteObserver(Media::IMuteObserver& aObserver)
{
    if (iMuteReporter == nullptr) {
        aObserver.MuteChanged(false);
    }
    else {
        iMuteReporter->AddMuteObserver(aObserver);
    }
}

void VolumeManager::AddUnityGainObserver(IUnityGainObserver& aObserver)
{
    if(iVolumeSourceUnityGain == nullptr){
        aObserver.UnityGainChanged(false);
    }
    else{
        iVolumeSourceUnityGain->AddUnityGainObserver(aObserver);
    }
}
void VolumeManager::SetVolumeOffset(TInt aValue)
{
    if (iVolumeSourceOffset != nullptr) {
        iVolumeSourceOffset->SetVolumeOffset(aValue);
    }
}

void VolumeManager::SetVolumeBoost(TInt aBoost)
{
    if (iVolumeSurroundBoost != nullptr) {
        iVolumeSurroundBoost->SetVolumeBoost(aBoost);
    }
}

void VolumeManager::SetUnityGain(TBool aEnable)
{
    if (iVolumeSourceUnityGain != nullptr) {
        iVolumeSourceUnityGain->SetUnityGain(aEnable);
    }
}

TUint VolumeManager::VolumeMax() const
{
    return iVolumeConfig.VolumeMax();
}

TUint VolumeManager::VolumeDefault() const
{
    return iVolumeConfig.VolumeDefault();
}

TUint VolumeManager::VolumeUnity() const
{
    return iVolumeConfig.VolumeUnity();
}

TUint VolumeManager::VolumeDefaultLimit() const
{
    return iVolumeConfig.VolumeDefaultLimit();
}

TUint VolumeManager::VolumeStep() const
{
    return iVolumeConfig.VolumeStep();
}

TUint VolumeManager::VolumeMilliDbPerStep() const
{
    return iVolumeConfig.VolumeMilliDbPerStep();
}

TUint VolumeManager::ThreadPriority() const
{
    return iVolumeConfig.ThreadPriority();
}

TUint VolumeManager::BalanceMax() const
{
    return iVolumeConfig.BalanceMax();
}

TUint VolumeManager::FadeMax() const
{
    return iVolumeConfig.FadeMax();
}

TUint VolumeManager::OffsetMax() const
{
    return iVolumeConfig.OffsetMax();
}

TBool VolumeManager::AlwaysOn() const
{
    return iVolumeConfig.AlwaysOn();
}

IVolumeProfile::StartupVolume VolumeManager::StartupVolumeConfig() const
{
    return iVolumeConfig.StartupVolumeConfig();
}

void VolumeManager::SetVolume(TUint aValue)
{
    LOG(kVolume, "VolumeManager::SetVolume aValue: %u\n", aValue);
    if (iVolumeUser == nullptr) {
        THROW(VolumeNotSupported);
    }

    // OpenHome Volume service is expected to unmute
    // UPnP AV RenderingControl doesn't want to unmute but that seems ill-considered
    // unmute here to both sources of volume changes the same behaviour
    if (iMuteUser != nullptr) {
        iMuteUser->Unmute();
    }

    const TUint volume = aValue * iVolumeConfig.VolumeMilliDbPerStep();
    iVolumeUser->SetVolume(volume);
}

void VolumeManager::SetVolumeNoUnmute(TUint aVolume)
{
    // copy of SetVolume, minus consideration of iMuteUser
    LOG(kVolume, "VolumeManager::SetVolumeNoUnmute aValue: %u\n", aVolume);
    if (iVolumeUser == nullptr) {
        THROW(VolumeNotSupported);
    }
    const TUint volume = aVolume * iVolumeConfig.VolumeMilliDbPerStep();
    iVolumeUser->SetVolume(volume);
}

void VolumeManager::SetBalance(TInt aBalance)
{
    if (iBalanceUser == nullptr) {
        THROW(BalanceNotSupported);
    }
    iBalanceUser->SetBalance(aBalance);
}

void VolumeManager::SetFade(TInt aFade)
{
    if (iFadeUser == nullptr) {
        THROW(FadeNotSupported);
    }
    iFadeUser->SetFade(aFade);
}

void VolumeManager::ApplyVolumeMultiplier(TUint aValue)
{
    static_cast<IVolumeRamper*>(iVolumeRamperPipeline)->ApplyVolumeMultiplier(aValue);
}

Media::IVolumeMuterStepped::Status VolumeManager::BeginMute()
{
    return static_cast<IVolumeMuterStepped*>(iVolumeMuterStepped)->BeginMute();
}

Media::IVolumeMuterStepped::Status VolumeManager::StepMute(TUint aJiffies)
{
    return static_cast<IVolumeMuterStepped*>(iVolumeMuterStepped)->StepMute(aJiffies);
}

void VolumeManager::SetMuted()
{
    static_cast<IVolumeMuterStepped*>(iVolumeMuterStepped)->SetMuted();
}

Media::IVolumeMuterStepped::Status VolumeManager::BeginUnmute()
{
    return static_cast<IVolumeMuterStepped*>(iVolumeMuterStepped)->BeginUnmute();
}

Media::IVolumeMuterStepped::Status VolumeManager::StepUnmute(TUint aJiffies)
{
    return static_cast<IVolumeMuterStepped*>(iVolumeMuterStepped)->StepUnmute(aJiffies);
}

void VolumeManager::SetUnmuted()
{
    static_cast<IVolumeMuterStepped*>(iVolumeMuterStepped)->SetUnmuted();
}

void VolumeManager::SetVolumeMuted(TBool aMuted)
{
    static_cast<IVolumeMuter*>(iVolumeMuter)->SetVolumeMuted(aMuted);
}

void VolumeManager::Mute()
{
    if (iMuteUser == nullptr) {
        THROW(MuteNotSupported);
    }
    iMuteUser->Mute();
}

void VolumeManager::Unmute()
{
    if (iMuteUser == nullptr) {
        THROW(MuteNotSupported);
    }
    iMuteUser->Unmute();
}


// VolumeScaler

VolumeScaler::VolumeScaler(IVolumeReporter& aVolumeReporter, IVolumeSourceOffset& aVolumeOffset, TUint aVolMaxMilliDb, TUint aVolMaxExternal)
    : iVolumeOffset(aVolumeOffset)
    , iVolMaxMilliDb(aVolMaxMilliDb)
    , iVolMaxExternal(aVolMaxExternal)
    , iEnabled(false)
    , iVolUser(0)
    , iVolExternal(0)
    , iLock("VSCL")
{
    // Check there is no overflow if max values of both ranges are multiplied together.
    const TUint prod = iVolMaxMilliDb * iVolMaxExternal;
    const TUint div = prod/iVolMaxMilliDb;
    ASSERT(div == iVolMaxExternal);
    aVolumeReporter.AddVolumeObserver(*this);
}

void VolumeScaler::SetVolume(TUint aVolume)
{
    LOG(kVolume, "VolumeScaler::SetVolume aVolume: %u\n", aVolume);
    ASSERT(aVolume <= iVolMaxExternal);
    // Scale volume to within range of system volume.
    AutoMutex a(iLock);
    iVolExternal = aVolume;
    if (iEnabled) {
        UpdateOffsetLocked();
    }
}

void VolumeScaler::SetVolumeEnabled(TBool aEnabled)
{
    AutoMutex a(iLock);
    if (aEnabled) {
        if (iEnabled != aEnabled) {
            iEnabled = aEnabled;
            UpdateOffsetLocked();
        }
    }
    else {
        if (iEnabled != aEnabled) {
            iEnabled = aEnabled;
            iVolumeOffset.SetVolumeOffset(0);
        }
    }
}

void VolumeScaler::VolumeChanged(const IVolumeValue& aVolume)
{
    ASSERT(aVolume.VolumeBinaryMilliDb() <= iVolMaxMilliDb);
    AutoMutex a(iLock);
    iVolUser = aVolume.VolumeBinaryMilliDb();
    if (iEnabled) {
        UpdateOffsetLocked();
    }
}

void VolumeScaler::UpdateOffsetLocked()
{
    // Already know from check in constructor that this can't overflow.
    const TUint volProd = iVolExternal * iVolUser;

    const TUint vol = volProd / iVolMaxExternal;
    ASSERT(iVolUser >= vol);    // Scaled vol must be within user vol.
    const TInt offset = (iVolUser - vol) * -1;

    iVolumeOffset.SetVolumeOffset(offset);
}

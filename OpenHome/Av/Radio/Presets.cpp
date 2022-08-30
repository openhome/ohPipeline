#include <OpenHome/Av/Radio/TuneIn.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Timer.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Functor.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Private/Env.h>
#include <OpenHome/Av/OhMetadata.h>
#include <OpenHome/Net/Core/OhNet.h>
#include <OpenHome/Av/Radio/PresetDatabase.h>
#include <OpenHome/Av/Debug.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/DnsChangeNotifier.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Media/MimeTypeList.h>
#include <OpenHome/Private/NetworkAdapterList.h>

#include <limits.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Configuration;


// RefreshTimer

const TUint RefreshTimer::kRefreshRateMs = 5 * 60 * 1000; // 5 minutes
const std::vector<TUint> RefreshTimer::kRetryDelaysMs = { 100, 200, 400, 800, 1600, 3200, 5000, 10000, 20000, 20000, 30000 }; // roughly 90s worth of retries

RefreshTimer::RefreshTimer(ITimer& aTimer)
    : iTimer(aTimer)
    , iNextDelayIdx(0)
{
}

void RefreshTimer::BackOffRetry()
{
    TUint delayMs = kRefreshRateMs;
    if (iNextDelayIdx < kRetryDelaysMs.size()) {
        delayMs = kRetryDelaysMs[iNextDelayIdx];
        iNextDelayIdx++;
    }
    else {
        // Exhausted retry steps. Revert to standard refresh rate.
        iNextDelayIdx = 0;
    }
    iTimer.FireIn(delayMs);
}

void RefreshTimer::StandardRefresh()
{
    iNextDelayIdx = 0;
    iTimer.FireIn(kRefreshRateMs);
}

void RefreshTimer::Reset()
{
    // Reset retry idx. Don't cancel any pending timer.
    iNextDelayIdx = 0;
}


// AutoRefreshTimer

AutoRefreshTimer::AutoRefreshTimer(RefreshTimer& aTimer)
    : iTimer(aTimer)
    , iTriggered(false)
{
}

AutoRefreshTimer::~AutoRefreshTimer()
{
    if (!iTriggered) {
        iTimer.StandardRefresh();
    }
}

void AutoRefreshTimer::BackOffRetry()
{
    iTriggered = true;
    iTimer.BackOffRetry();
}

void AutoRefreshTimer::StandardRefresh()
{
    iTriggered = true;
    iTimer.StandardRefresh();
}


// RadioPresets

RadioPresets::RadioPresets(
    Environment& aEnv,
    Configuration::IConfigInitialiser& aConfigInit,
    IPresetDatabaseWriter& aDbWriter,
    IThreadPool& aThreadPool)
    : iLock("RPre")
    , iEnv(aEnv)
    , iConfigInit(aConfigInit)
    , iDbWriter(aDbWriter)
    , iConfigChoiceProvider(nullptr)
    , iListenerProvider(IConfigManager::kSubscriptionIdInvalid)
    , iActiveProvider(nullptr)
{
    iThreadPoolHandle = aThreadPool.CreateHandle(MakeFunctor(*this, &RadioPresets::DoRefresh),
                                                 "TuneInRefresh", ThreadPoolPriority::Low);
    iRefreshTimer = new Timer(aEnv, MakeFunctor(*this, &RadioPresets::TimerCallback), "RadioPresets");
    iRefreshTimerWrapper.reset(new RefreshTimer(*iRefreshTimer));

    iNacnId = iEnv.NetworkAdapterList().AddCurrentChangeListener(MakeFunctor(*this, &RadioPresets::CurrentAdapterChanged), "TuneIn", false);
    iDnsId = iEnv.DnsChangeNotifier()->Register(MakeFunctor(*this, &RadioPresets::DnsChanged));
}

RadioPresets::~RadioPresets()
{
    if (iActiveProvider != nullptr) {
        iActiveProvider->Deactivate();
    }
    iRefreshTimerWrapper.reset();
    iRefreshTimer->Cancel();
    if (iConfigChoiceProvider != nullptr) {
        iConfigChoiceProvider->Unsubscribe(iListenerProvider);
        delete iConfigChoiceProvider;
    }
    iThreadPoolHandle->Destroy();
    iEnv.DnsChangeNotifier()->Deregister(iDnsId);
    iEnv.NetworkAdapterList().RemoveCurrentChangeListener(iNacnId);
    delete iRefreshTimer;
    for (auto p : iProviders) {
        delete p;
    }
}

void RadioPresets::Start()
{
    if (iConfigChoiceProvider != nullptr) {
        iListenerProvider = iConfigChoiceProvider->Subscribe(MakeFunctorConfigText(*this, &RadioPresets::ProviderChanged));
    }
    else if (iProviders.size() == 1) {
        UpdateProvider(iProviders[0]);
    }
}

void RadioPresets::Refresh()
{
    (void)iThreadPoolHandle->TrySchedule();
}

void RadioPresets::AddProvider(IRadioPresetProvider* aProvider)
{
    iLock.Wait();
    iProviders.push_back(aProvider);
    const TBool createConfigVal = iProviders.size() == 2;
    iLock.Signal();
    if (createConfigVal) {
        // we now have a choice of providers; expose a config value that'll list however many are eventually added
        Brn defaultName(iProviders[0]->DisplayName());
        iConfigChoiceProvider = new Configuration::ConfigTextChoice(
            iConfigInit,
            Brn("Radio.PresetProvider"),
            *this,
            1 /*aMinLength*/,
            32 /*aMaxLength*/,
            defaultName);
    }
}

void RadioPresets::ProviderChanged(Configuration::KeyValuePair<const Brx&>& aKvp)
{
    const Brx& name = aKvp.Value();
    auto provider = Provider(name);
    UpdateProvider(provider);
}

IRadioPresetProvider* RadioPresets::Provider(const Brx& aName) const
{
    AutoMutex _(iLock);
    for (auto p : iProviders) {
        if (p->DisplayName() == aName) {
            return p;
        }
    }
    return nullptr;
}

void RadioPresets::UpdateProvider(IRadioPresetProvider* aProvider)
{
    AutoMutex _(iLock);
    if (aProvider != nullptr && aProvider != iActiveProvider) {
        if (iActiveProvider != nullptr) {
            iActiveProvider->Deactivate();
        }
        iActiveProvider = aProvider;
        iActiveProvider->Activate(*this);
        Refresh();
    }
}

void RadioPresets::CurrentAdapterChanged()
{
    iRefreshTimerWrapper->Reset();
    Refresh();
}

void RadioPresets::DnsChanged()
{
    iRefreshTimerWrapper->Reset();
    Refresh();
}

void RadioPresets::TimerCallback()
{
    Refresh();
}

void RadioPresets::DoRefresh()
{
    const TUint maxPresets = iDbWriter.MaxNumPresets();
    if (iAllocatedPresets.size() == 0) {
        iAllocatedPresets.reserve(maxPresets);
        for (TUint i = 0; i < maxPresets; i++) {
            iAllocatedPresets.push_back(0);
        }
    }
    else {
        std::fill(iAllocatedPresets.begin(), iAllocatedPresets.end(), 0);
    }

    // Auto class to set timer to fire at normal refresh rate if this method returns without having set timer.
    AutoRefreshTimer refreshTimer(*iRefreshTimerWrapper);

    try {
        {
            AutoMutex _(iLock);
            if (iActiveProvider != nullptr) {
                iActiveProvider->RefreshPresets();
            }
        }

        for (TUint i = 0; i < maxPresets; i++) {
            if (iAllocatedPresets[i] == 0) {
                iDbWriter.ClearPreset(i);
            }
        }
    }
    catch (AssertionFailed&) {
        throw;
    }
    catch (PresetIndexOutOfRange&) {}
    catch (Exception& ex) {
        Log::Print("%s from %s:%d\n", ex.Message(), ex.File(), ex.Line());
        refreshTimer.BackOffRetry();
    }
    iDbWriter.EndSetPresets();
}

void RadioPresets::AcceptChoicesVisitor(Configuration::IConfigTextChoicesVisitor& aVisitor)
{
    AutoMutex _(iLock);
    for (auto p : iProviders) {
        aVisitor.VisitConfigTextChoice(p->DisplayName());
    }
}

TBool RadioPresets::IsValid(const Brx& aBuf) const
{
    auto provider = Provider(aBuf);
    return provider != nullptr;
}

void RadioPresets::ScheduleRefresh()
{
    iRefreshTimerWrapper->Reset();
    Refresh();
}

void RadioPresets::SetPreset(TUint aIndex, const Brx& aStreamUri, const Brx& aTitle, const Brx& aImageUri, TUint aByterate)
{
    iDidlLite.SetBytes(0);

    static const Brn kProtocolInfo("*:*:*:*");
    WriterBuffer w(iDidlLite);
    WriterDIDLLite writer(Brx::Empty(), DIDLLite::kItemTypeAudioItem, w);
    writer.WriteTitle(aTitle);

    WriterDIDLLite::StreamingDetails details;
    details.byteRate = aByterate;

    writer.WriteStreamingDetails(kProtocolInfo, details, aStreamUri);
    writer.WriteArtwork(aImageUri);

    writer.WriteEnd();

    //Log::Print("++ Add preset #%u: %.*s\n", presetIndex, PBUF(iPresetUrl));
    iDbWriter.SetPreset(aIndex, aStreamUri, iDidlLite);
    iAllocatedPresets[aIndex] = 1; // must come after iDbWriter.SetPreset in case that throws
}

#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Timer.h>
#include <OpenHome/Av/Radio/PresetDatabase.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Configuration/ConfigManager.h>

#include <atomic>
#include <memory>
#include <vector>

namespace OpenHome {
    class Environment;
    class IThreadPool;
    class IThreadPoolHandle;
    class Parser;
namespace Configuration {
    class IConfigInitialiser;
    class ConfigText;
}
namespace Media {
    class PipelineManager;
    class MimeTypeList;
}
namespace Av {

class RefreshTimer
{
private:
    static const TUint kRefreshRateMs;// = 5 * 60 * 1000; // 5 minutes
    static const std::vector<TUint> kRetryDelaysMs;
public:
    RefreshTimer(OpenHome::ITimer& aTimer);
    /*
     * Move to next retry back-off. If all retries have been exhausted, default to normal refresh rate.
     */
    void BackOffRetry();
    /*
     * Trigger refresh at standard rate.
     */
    void StandardRefresh();
    void Reset();
private:
    OpenHome::ITimer& iTimer;
    std::atomic<TUint> iNextDelayIdx;
};

/*
 * Helper class to use as a local variable to ensure timer is always triggered.
 *
 * If a call to BackOffRetry()/StandardRefresh() is not made, the destructor of this class performs a call to StandardRefresh().
 */
class AutoRefreshTimer
{
public:
    AutoRefreshTimer(RefreshTimer& aTimer);
    ~AutoRefreshTimer();
    void BackOffRetry();
    void StandardRefresh();
private:
    RefreshTimer& iTimer;
    std::atomic<TBool> iTriggered;
};

class IRadioPresetWriter
{
public:
    virtual ~IRadioPresetWriter() {}
    virtual void ScheduleRefresh() = 0;
    virtual void SetPreset(TUint aIndex, const Brx& aStreamUri, const Brx& aTitle, const Brx& aImageUri, TUint aByterate = 0) = 0;
};

class IRadioPresetProvider
{
public:
    virtual ~IRadioPresetProvider() {}
    virtual const Brx& DisplayName() const = 0;
    virtual void Activate(IRadioPresetWriter& aWriter) = 0;
    virtual void Deactivate() = 0;
    virtual void RefreshPresets() = 0;
};

class IRadioPresets
{
public:
    virtual ~IRadioPresets() {}
    virtual void AddProvider(IRadioPresetProvider* aProvider) = 0;;
};

class RadioPresets :
    public IRadioPresets,
    private IRadioPresetWriter,
    private Configuration::IConfigTextChoices
{
private:
    static const TUint kRefreshRateMs = 5 * 60 * 1000; // 5 minutes
    static const TUint kMaxPresetTitleBytes = 256;
public:
    RadioPresets(
        Environment& aEnv,
        Configuration::IConfigInitialiser& aConfigInit,
        IPresetDatabaseWriter& aDbWriter,
        IThreadPool& aThreadPool,
        IRadioPresetProvider* aDefaultProvider);
    ~RadioPresets();
    void Start();
private: // from IRadioPresets
    void AddProvider(IRadioPresetProvider* aProvider);
private:
    void Refresh();
    void ProviderChanged(Configuration::KeyValuePair<const Brx&>& aKvp);
    IRadioPresetProvider* Provider(const Brx& aName) const;
    void CurrentAdapterChanged();
    void DnsChanged();
    void TimerCallback();
    void DoRefresh();
private: // from Configuration::IConfigTextChoices
    void AcceptChoicesVisitor(Configuration::IConfigTextChoicesVisitor& aVisitor) override;
    TBool IsValid(const Brx& aBuf) const override;
private: // from IRadioPresetWriter
    void ScheduleRefresh() override;
    void SetPreset(TUint aIndex, const Brx& aStreamUri, const Brx& aTitle, const Brx& aImageUri, TUint aByterate = 0) override;
private:
    mutable Mutex iLock;
    Environment& iEnv;
    IPresetDatabaseWriter& iDbWriter;
    Configuration::ConfigTextChoice* iConfigChoiceProvider;
    TUint iListenerProvider;
    std::vector<IRadioPresetProvider*> iProviders;
    IRadioPresetProvider* iActiveProvider;
    Timer* iRefreshTimer;
    std::unique_ptr<RefreshTimer> iRefreshTimerWrapper;
    IThreadPoolHandle* iThreadPoolHandle;
    TUint iNacnId;
    TUint iDnsId;
    Bws<Media::kTrackMetaDataMaxBytes> iDidlLite;
    std::vector<TUint> iAllocatedPresets;
};

} // namespace Av
} // namespace OpenHome


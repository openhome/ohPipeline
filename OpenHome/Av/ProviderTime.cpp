#include <OpenHome/Av/ProviderTime.h>
#include <Generated/DvAvOpenhomeOrgTime1.h>
#include <OpenHome/Net/Core/DvInvocationResponse.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Media/PipelineObserver.h>
#include <OpenHome/Private/Thread.h>


using namespace OpenHome;
using namespace OpenHome::Net;
using namespace OpenHome::Av;
using namespace OpenHome::Media;

ProviderTime::ProviderTime(DvDevice& aDevice, PipelineManager& aPipelineManager)
    : DvProviderAvOpenhomeOrgTime1(aDevice)
    , iPipelineManager(aPipelineManager)
    , iLock("PrTm")
{
    EnablePropertyTrackCount();
    EnablePropertyDuration();
    EnablePropertySeconds();

    SetPropertyTrackCount(0);
    SetPropertyDuration(0);
    SetPropertySeconds(0);

    EnableActionTime();

    iPipelineManager.AddObserver(*this);
}

void ProviderTime::Time(IDvInvocation& aInvocation, IDvInvocationResponseUint& aTrackCount, IDvInvocationResponseUint& aDuration, IDvInvocationResponseUint& aSeconds)
{
    TUint propTrackCount = 0;
    TUint propDuration = 0;
    TUint propSeconds = 0;

    AutoMutex mutex(iLock);

    GetPropertyTrackCount(propTrackCount);
    GetPropertyDuration(propDuration);
    GetPropertySeconds(propSeconds);

    aInvocation.StartResponse();
    aTrackCount.Write(propTrackCount);
    aDuration.Write(propDuration);
    aSeconds.Write(propSeconds);
    aInvocation.EndResponse();
}

void ProviderTime::NotifyPipelineState(EPipelineState aState)
{
    if (aState == EPipelineStopped) {
        NotifyTime(0);
    }
}

void ProviderTime::NotifyMode(const Brx& /*aMode*/,
                              const ModeInfo& /*aInfo*/,
                              const ModeTransportControls& /*aTransportControls*/)
{
}

void ProviderTime::NotifyTrack(Track& /*aTrack*/, TBool /*aStartOfStream*/)
{
    TUint n = 0;

    AutoMutex mutex(iLock);

    GetPropertyTrackCount(n);
    SetPropertyTrackCount(n + 1);
}

void ProviderTime::NotifyMetaText(const Brx& /*aText*/)
{
    // NOP -- textual metadata
}

void ProviderTime::NotifyTime(TUint aSeconds)
{
    AutoMutex mutex(iLock);

    SetPropertySeconds(aSeconds);
}

void ProviderTime::NotifyStreamInfo(const DecodedStreamInfo& aStreamInfo)
{
    AutoMutex mutex(iLock);

    const auto seconds = (TUint)((aStreamInfo.SampleStart() * Media::Jiffies::PerSample(aStreamInfo.SampleRate())) / Jiffies::kPerSecond);
    const auto trackDurationSeconds = (TUint)(aStreamInfo.TrackLength() / Jiffies::kPerSecond);
    PropertiesLock();
    SetPropertySeconds(seconds);
    SetPropertyDuration(trackDurationSeconds);
    PropertiesUnlock();
}

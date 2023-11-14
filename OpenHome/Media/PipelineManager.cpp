#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Media/Pipeline/Pipeline.h>
#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Media/Filler.h>
#include <OpenHome/Media/IdManager.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/ThreadPool.h>

#include <limits.h>

using namespace OpenHome;
using namespace OpenHome::Media;

// PriorityArbitratorPipeline

PriorityArbitratorPipeline::PriorityArbitratorPipeline(TUint aOpenHomeMax)
    : iOpenHomeMax(aOpenHomeMax)
{
}

TUint PriorityArbitratorPipeline::Priority(const TChar* /*aId*/, TUint aRequested, TUint aHostMax)
{
    return aHostMax - (iOpenHomeMax - aRequested);
}

TUint PriorityArbitratorPipeline::OpenHomeMin() const
{
    return iOpenHomeMax - kNumThreads + 1;
}

TUint PriorityArbitratorPipeline::OpenHomeMax() const
{
    return iOpenHomeMax;
}

TUint PriorityArbitratorPipeline::HostRange() const
{
    return kNumThreads;
}


// PipelineManager

PipelineManager::PipelineManager(
    PipelineInitParams* aInitParams,
    IInfoAggregator& aInfoAggregator,
    TrackFactory& aTrackFactory,
    Optional<IAudioTime> aAudioTime)
    : iLock("PLM1")
    , iPublicLock("PLM2")
    , iLockObservers("PLM3")
    , iModeObserver(nullptr)
    , iPipelineState(EPipelineStopped)
    , iPipelineStoppedSem("PLM3", 1)
{
    iPrefetchObserver = new PrefetchObserver();
    iPipeline = new Pipeline(aInitParams, aInfoAggregator, aTrackFactory,
                             *this, *iPrefetchObserver, *this, *this, aAudioTime);
    iIdManager = new IdManager(*iPipeline);
    TUint min, max;
    iPipeline->GetThreadPriorityRange(min, max);
    iFillerPriority = min-1;
    iFiller = new Filler(*iPipeline, *iIdManager, *iIdManager, *iPipeline,
                         iPipeline->Factory(), aTrackFactory, *iPrefetchObserver,
                         *iIdManager, PhaseAdjuster(), iFillerPriority,
                         iPipeline->SenderMinLatencyMs() * Jiffies::kPerMs);
    iProtocolManager = new ProtocolManager(*iFiller, iPipeline->Factory(), *iIdManager, *iPipeline);
    iFiller->Start(*iProtocolManager);
}

PipelineManager::~PipelineManager()
{
    delete iPipeline;
    delete iPrefetchObserver;
    delete iProtocolManager;
    delete iFiller;
    delete iIdManager;
    for (TUint i=0; i<iUriProviders.size(); i++) {
        delete iUriProviders[i];
    }
}

void PipelineManager::Quit()
{
    LOG(kPipeline, "> PipelineManager::Quit()\n");
    AutoMutex _(iPublicLock);
    iLock.Wait();
    iPipeline->Block();
    const TUint haltId = iFiller->Stop();
    iIdManager->InvalidatePending();
    iPipeline->RemoveAll(haltId);
    iPipeline->Unblock();
    iLock.Signal();
    iPipeline->Quit();
    iFiller->Quit();
}

void PipelineManager::Add(Codec::ContainerBase* aContainer)
{
    iPipeline->AddContainer(aContainer);
}

void PipelineManager::Add(Codec::CodecBase* aCodec)
{
    iPipeline->AddCodec(aCodec);
}

void PipelineManager::Add(Protocol* aProtocol)
{
    iProtocolManager->Add(aProtocol);
}

void PipelineManager::Add(ContentProcessor* aContentProcessor)
{
    iProtocolManager->Add(aContentProcessor);
}

void PipelineManager::Add(IDRMProvider* aProvider)
{
    iProtocolManager->Add(aProvider);
}

void PipelineManager::Add(UriProvider* aUriProvider)
{
    iUriProviders.push_back(aUriProvider);
    iFiller->Add(*aUriProvider);
    if (iModeObserver != nullptr) {
        iModeObserver->NotifyModeAdded(aUriProvider->Mode());
    }
}

void PipelineManager::Start(IVolumeRamper& aVolumeRamper, IVolumeMuterStepped& aVolumeMuter)
{
    iPipeline->Start(aVolumeRamper, aVolumeMuter);
}

void PipelineManager::AddObserver(IPipelineObserver& aObserver)
{
    AutoMutex _(iLockObservers);
    iObservers.push_back(&aObserver);
}

void PipelineManager::RemoveObserver(IPipelineObserver& aObserver)
{
    AutoMutex _(iLockObservers);
    for (TUint i=0; i<iObservers.size(); i++) {
        if (iObservers[i] == &aObserver) {
            iObservers.erase(iObservers.begin()+i);
            break;
        }
    }
}

void PipelineManager::AddObserver(ITrackObserver& aObserver)
{
    iPipeline->AddObserver(aObserver);
}

void PipelineManager::AddObserver(IModeObserver& aObserver)
{
    ASSERT(iModeObserver == nullptr); // multiple observers assumed not required
    iModeObserver = &aObserver;
}

IAsyncTrackObserver& PipelineManager::AsyncTrackObserver() const
{
    return iPipeline->AsyncTrackObserver();
}

IAirplayReporter& PipelineManager::AirplayReporter() const
{
    return iPipeline->AirplayReporter();
}

IAirplayTrackObserver& PipelineManager::AirplayTrackObserver() const
{
    return iPipeline->AirplayTrackObserver();
}

ISpotifyReporter& PipelineManager::SpotifyReporter() const
{
    return iPipeline->SpotifyReporter();
}

ISpotifyTrackObserver& PipelineManager::SpotifyTrackObserver() const
{
    return iPipeline->SpotifyTrackObserver();
}

IClockPuller& PipelineManager::PhaseAdjuster()
{
    return iPipeline->GetPhaseAdjuster();
}

MsgFactory& PipelineManager::Factory()
{
    return iPipeline->Factory();
}

void PipelineManager::Begin(const Brx& aMode, TUint aTrackId)
{
    AutoMutex _(iPublicLock);
    LOG(kPipeline, "PipelineManager::Begin(%.*s, %u)\n", PBUF(aMode), aTrackId);
    iLock.Wait();
    iMode.Replace(aMode);
    iTrackId = aTrackId;
    iLock.Signal();
    iFiller->Play(aMode, aTrackId);
}

void PipelineManager::Play()
{
    AutoMutex _(iPublicLock);
    LOG(kPipeline, "PipelineManager::Play()\n");
    iPipeline->Play();
}

void PipelineManager::PlayAs(const Brx& aMode, const Brx& aCommand)
{
    iFiller->Prepare(aMode, aCommand);
    AutoMutex _(iPublicLock);
    LOG(kPipeline, "PipelineManager::PlayAs(%.*s, %.*s)\n", PBUF(aMode), PBUF(aCommand));
    RemoveAllLocked();
    iFiller->Play(aMode);
    iPipeline->Play();
}

void PipelineManager::Pause()
{
    AutoMutex _(iPublicLock);
    LOG(kPipeline, "PipelineManager::Pause()\n");
    iPipeline->Pause();
}

void PipelineManager::Wait(TUint aFlushId)
{
    AutoMutex _(iPublicLock);
    LOG(kPipeline, "PipelineManager::Wait(%u)\n", aFlushId);
    iPipeline->Wait(aFlushId);
}

void PipelineManager::FlushQuick(TUint aFlushId)
{
    AutoMutex _(iPublicLock);
    LOG(kPipeline, "PipelineManager::FlushQuick(%u)\n", aFlushId);
    iPipeline->FlushQuick(aFlushId);
}

void PipelineManager::Stop()
{
    AutoMutex _(iPublicLock);
    LOG(kPipeline, "PipelineManager::Stop()\n");
    iPipeline->Block();
    const TUint haltId = iFiller->Stop();
    iPipeline->Stop(haltId);
    iPipeline->Unblock();
    iIdManager->InvalidatePending(); /* don't use InvalidateAll - iPipeline->Stop() will
                                        have removed current stream.  InvalidateAll ends
                                        up with Stopper trying to halt (pause) which would
                                        override the attempt to Stop it. */
}

void PipelineManager::StopPrefetch(const Brx& aMode, TUint aTrackId)
{
    AutoMutex _(iPublicLock);
    LOG(kPipeline, "PipelineManager::StopPrefetch(%.*s, %u)\n", PBUF(aMode), aTrackId);
    iPipeline->Block();
    const TUint haltId = iFiller->Stop();
    iIdManager->InvalidatePending();
    iPipeline->RemoveAll(haltId);
    iPipeline->Unblock();
    const TUint trackId = (aTrackId==Track::kIdNone? iFiller->NullTrackId() : aTrackId);
    iPrefetchObserver->SetTrack(trackId);
    iPipeline->Play(); // in case pipeline is paused/stopped, force it to pull until a new track
    iFiller->PlayLater(aMode, trackId);
    try {
        iPrefetchObserver->Wait(5000); /* It's possible that a protocol module will block without
                                          ever delivering content.  Other pipeline operations which
                                          might interrupt it are blocked by iPublicLock so we
                                          timeout after 5s as a workaround */
    }
    catch (Timeout&) {
        Log::Print("Timeout from PipelineManager::StopPrefetch.  trackId=%u, mode=%.*s\n", aTrackId, PBUF(aMode));
    }
}

void PipelineManager::RemoveAll()
{
    AutoMutex _(iPublicLock);
    LOG(kPipeline, "PipelineManager::RemoveAll()\n");
    RemoveAllLocked();
}

void PipelineManager::RemoveAllLocked()
{
    iPipeline->Block();
    const TUint haltId = iFiller->Stop();
    iIdManager->InvalidatePending();
    iPipeline->RemoveAll(haltId);
    iPipeline->Unblock();
}

void PipelineManager::Seek(TUint aStreamId, TUint aSecondsAbsolute)
{
    AutoMutex _(iPublicLock);
    LOG(kPipeline, "PipelineManager::Seek(%u, %u)\n", aStreamId, aSecondsAbsolute);
    iPipeline->Seek(aStreamId, aSecondsAbsolute);
}

void PipelineManager::Next()
{
    AutoMutex _(iPublicLock);
    LOG(kPipeline, "PipelineManager::Next()\n");
    if (iMode.Bytes() == 0) {
        return; // nothing playing or ready to be played so nothing we can advance relative to
    }
    /* Can't quite get away with only calling iPipeline->RemoveCurrentStream()
       This works well when the pipeline is running but doesn't cope with the unusual
       case where a protocol module is stalled before pushing any audio into the pipeline.
       Call to iFiller->Stop() below spots this case and Interrupt()s the blocked protocol. */
    iPipeline->Block();
    const TUint haltId = iFiller->Stop();
    iIdManager->InvalidatePending();
    iPipeline->RemoveAll(haltId);
    iPipeline->Unblock();
    iFiller->Next(iMode);
}

void PipelineManager::Prev()
{
    AutoMutex _(iPublicLock);
    LOG(kPipeline, "PipelineManager::Prev()\n");
    if (iMode.Bytes() == 0) {
        return; // nothing playing or ready to be played so nothing we can advance relative to
    }
    iPipeline->Block();
    const TUint haltId = iFiller->Stop();
    iIdManager->InvalidatePending();
    iPipeline->RemoveAll(haltId);
    iPipeline->Unblock();
    iFiller->Prev(iMode);
}

IPipelineElementUpstream& PipelineManager::InsertElements(IPipelineElementUpstream& aTail)
{
    return iPipeline->InsertElements(aTail);
}

TUint PipelineManager::SenderMinLatencyMs() const
{
    return iPipeline->SenderMinLatencyMs();
}

void PipelineManager::GetThreadPriorityRange(TUint& aMin, TUint& aMax) const
{
    iPipeline->GetThreadPriorityRange(aMin, aMax);
}

void PipelineManager::GetThreadPriorities(TUint& aFiller, TUint& aFlywheelRamper, TUint& aStarvationRamper, TUint& aCodec, TUint& aEvent)
{
    aFiller = iFillerPriority;
    iPipeline->GetThreadPriorities(aFlywheelRamper, aStarvationRamper, aCodec, aEvent);
}

void PipelineManager::GetMaxSupportedSampleRates(TUint& aPcm, TUint& aDsd) const
{
    iPipeline->GetMaxSupportedSampleRates(aPcm, aDsd);
}

Msg* PipelineManager::Pull()
{
    return iPipeline->Pull();
}

void PipelineManager::SetAnimator(IPipelineAnimator& aAnimator)
{
    iPipeline->SetAnimator(aAnimator);
}

void PipelineManager::InvalidateAt(TUint aId)
{
    iIdManager->InvalidateAt(aId);
}

void PipelineManager::InvalidateAfter(TUint aId)
{
    iIdManager->InvalidateAfter(aId);
}

void PipelineManager::InvalidatePending()
{
    iIdManager->InvalidatePending();
}

void PipelineManager::InvalidateAll()
{
    iIdManager->InvalidateAll();
}

void PipelineManager::Mute()
{
    static_cast<IMute*>(iPipeline)->Mute();
}

void PipelineManager::Unmute()
{
    static_cast<IMute*>(iPipeline)->Unmute();
}

void PipelineManager::PostPipelineLatencyChanged()
{
    iPipeline->PostPipelineLatencyChanged();
}

void PipelineManager::SetAttenuation(TUint aAttenuation)
{
    iPipeline->SetAttenuation(aAttenuation);
}

void PipelineManager::DrainAllAudio()
{
    iPipeline->DrainAllAudio();
}

void PipelineManager::StartAt(TUint64 aTime)
{
    iPipeline->StartAt(aTime);
}

void PipelineManager::NotifyPipelineState(EPipelineState aState)
{
    {
        AutoMutex _(iLockObservers);
        for (TUint i = 0; i < iObservers.size(); i++) {
            iObservers[i]->NotifyPipelineState(aState);
        }
    }
    iLock.Wait();
    iPipelineState = aState;
    iLock.Signal();
    if (iPipelineState == EPipelineStopped) {
        iPipelineStoppedSem.Signal();
    }
    else {
        (void)iPipelineStoppedSem.Clear();
    }
}

void PipelineManager::NotifyMode(const Brx& aMode,
                                 const ModeInfo& aInfo,
                                 const ModeTransportControls& aTransportControls)
{
    iLock.Wait();
    iMode.Replace(aMode);
    iLock.Signal();
    AutoMutex _(iLockObservers);
    for (auto it=iObservers.begin(); it!=iObservers.end(); ++it) {
        (*it)->NotifyMode(aMode, aInfo, aTransportControls);
    }
}

void PipelineManager::NotifyTrack(Track& aTrack, TBool aStartOfStream)
{
    iLock.Wait();
    iTrackId = aTrack.Id();
    iLock.Signal();
    AutoMutex _(iLockObservers);
    for (TUint i=0; i<iObservers.size(); i++) {
        iObservers[i]->NotifyTrack(aTrack, aStartOfStream);
    }
}

void PipelineManager::NotifyMetaText(const Brx& aText)
{
    AutoMutex _(iLockObservers);
    for (TUint i=0; i<iObservers.size(); i++) {
        iObservers[i]->NotifyMetaText(aText);
    }
}

void PipelineManager::NotifyTime(TUint aSeconds)
{
    AutoMutex _(iLockObservers);
    for (TUint i=0; i<iObservers.size(); i++) {
        iObservers[i]->NotifyTime(aSeconds);
    }
}

void PipelineManager::NotifyStreamInfo(const DecodedStreamInfo& aStreamInfo)
{
    AutoMutex _(iLockObservers);
    for (TUint i=0; i<iObservers.size(); i++) {
        iObservers[i]->NotifyStreamInfo(aStreamInfo);
    }
}

TUint PipelineManager::SeekRestream(const Brx& aMode, TUint aTrackId)
{
    LOG(kPipeline, "PipelineManager::SeekRestream(%.*s, %u)\n", PBUF(aMode), aTrackId);
    (void)iFiller->Stop();
    iIdManager->InvalidateAll();
    const TUint flushId = iFiller->Flush();
    iFiller->Play(aMode, aTrackId);
    return flushId;
}

TBool PipelineManager::TryGet(IWriter& aWriter, const Brx& aUrl, TUint64 aOffset, TUint aBytes)
{
    return iProtocolManager->TryGet(aWriter, aUrl, aOffset, aBytes);
}


// PipelineManager::PrefetchObserver

PipelineManager::PrefetchObserver::PrefetchObserver()
    : iLock("PFO1")
    , iSem("PFO2", 0)
    , iTrackId(UINT_MAX)
{
}

PipelineManager::PrefetchObserver::~PrefetchObserver()
{
    iSem.Signal();
}

void PipelineManager::PrefetchObserver::SetTrack(TUint aTrackId)
{
    iLock.Wait();
    (void)iSem.Clear();
    iTrackId = aTrackId;
    iLock.Signal();
}

void PipelineManager::PrefetchObserver::Wait(TUint aTimeoutMs)
{
    iSem.Wait(aTimeoutMs);
}

void PipelineManager::PrefetchObserver::NotifyTrackFailed(TUint aTrackId)
{
    CheckTrack(aTrackId);
}

void PipelineManager::PrefetchObserver::NotifyStreamPlayStatus(TUint aTrackId, TUint /*aStreamId*/, EStreamPlay /*aStatus*/)
{
    CheckTrack(aTrackId);
}

void PipelineManager::PrefetchObserver::CheckTrack(TUint aTrackId)
{
    iLock.Wait();
    if (iTrackId != UINT_MAX) {
        LOG(kPipeline, "PipelineManager::PrefetchObserver::CheckTrack expected %u, got %u\n", iTrackId, aTrackId);
    }
    if (aTrackId == iTrackId) {
        iSem.Signal();
        iTrackId = UINT_MAX;
    }
    iLock.Signal();
}

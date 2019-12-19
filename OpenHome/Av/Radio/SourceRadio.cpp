#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Av/Source.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Pipeline/Pipeline.h> // for PipelineStreamNotPausable
#include <OpenHome/Av/Radio/PresetDatabase.h>
#include <OpenHome/Av/Radio/UriProviderRadio.h>
#include <OpenHome/Av/Radio/ProviderRadio.h>
#include <OpenHome/Av/Radio/TuneIn.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Av/Radio/ContentProcessorFactory.h>
#include <OpenHome/Media/UriProviderSingleTrack.h>
#include <OpenHome/Av/SourceFactory.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/PowerManager.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Optional.h>
#include <OpenHome/Debug-ohMediaPlayer.h>
#include <OpenHome/Av/Pins/PodcastPinsITunes.h>
#include <OpenHome/Av/Radio/TuneInPins.h>
#include <OpenHome/Av/Radio/RadioPins.h>
#include <OpenHome/Av/Pins/UrlPins.h>

#include <limits.h>
#include <memory>


using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Configuration;
using namespace OpenHome::Net;
using namespace OpenHome::Media;

// SourceFactory

ISource* SourceFactory::NewRadio(IMediaPlayer& aMediaPlayer)
{ // static
    return new SourceRadio(aMediaPlayer, Brx::Empty());
}

ISource* SourceFactory::NewRadio(IMediaPlayer& aMediaPlayer, const Brx& aTuneInPartnerId)
{ // static
    return new SourceRadio(aMediaPlayer, aTuneInPartnerId);
}

const TChar* SourceFactory::kSourceTypeRadio = "Radio";
const Brn SourceFactory::kSourceNameRadio("Radio");

// SourceRadio

SourceRadio::SourceRadio(IMediaPlayer& aMediaPlayer, const Brx& aTuneInPartnerId)
    : Source(SourceFactory::kSourceNameRadio, SourceFactory::kSourceTypeRadio, aMediaPlayer.Pipeline())
    , iLock("SRAD")
    , iUriProviderPresets(nullptr)
    , iTrack(nullptr)
    , iTrackPosSeconds(0)
    , iStreamId(UINT_MAX)
    , iLive(false)
    , iPresetsUpdated(false)
    , iAutoPlay(false)
{
    MimeTypeList& mimeTypes = aMediaPlayer.MimeTypes();

    iPipeline.Add(ContentProcessorFactory::NewM3u(mimeTypes));
    iPipeline.Add(ContentProcessorFactory::NewM3u(mimeTypes));
    iPipeline.Add(ContentProcessorFactory::NewM3uX());
    iPipeline.Add(ContentProcessorFactory::NewM3uX());
    iPipeline.Add(ContentProcessorFactory::NewPls(mimeTypes));
    iPipeline.Add(ContentProcessorFactory::NewPls(mimeTypes));
    iPipeline.Add(ContentProcessorFactory::NewOpml(mimeTypes));
    iPipeline.Add(ContentProcessorFactory::NewOpml(mimeTypes));
    iPipeline.Add(ContentProcessorFactory::NewAsx());
    iPipeline.Add(ContentProcessorFactory::NewAsx());
    iPipeline.AddObserver(*this);
    iStorePresetNumber = new StoreInt(aMediaPlayer.ReadWriteStore(), aMediaPlayer.PowerManager(),
                                  kPowerPriorityNormal, Brn("Radio.PresetId"),
                                  IPresetDatabaseReader::kPresetIdNone);

    auto& trackFactory = aMediaPlayer.TrackFactory();
    iPresetDatabase = new PresetDatabase(trackFactory);
    iPresetDatabase->AddObserver(*this);

    iUriProviderPresets = new UriProviderRadio(trackFactory, *iPresetDatabase);
    iUriProviderPresets->SetTransportPlay(MakeFunctor(*this, &SourceRadio::Play));
    iUriProviderPresets->SetTransportPause(MakeFunctor(*this, &SourceRadio::Pause));
    iUriProviderPresets->SetTransportStop(MakeFunctor(*this, &SourceRadio::Stop));
    iUriProviderPresets->SetTransportNext(MakeFunctor(*this, &SourceRadio::Next));
    iUriProviderPresets->SetTransportPrev(MakeFunctor(*this, &SourceRadio::Prev));
    aMediaPlayer.Add(iUriProviderPresets);
    iCurrentMode.Set(iUriProviderPresets->Mode());

    iUriProviderSingle = new UriProviderSingleTrack("Radio-Single", false, true, trackFactory);
    iUriProviderSingle->SetTransportPlay(MakeFunctor(*this, &SourceRadio::Play));
    iUriProviderSingle->SetTransportPause(MakeFunctor(*this, &SourceRadio::Pause));
    iUriProviderSingle->SetTransportStop(MakeFunctor(*this, &SourceRadio::Stop));
    aMediaPlayer.Add(iUriProviderSingle);

    iProviderRadio = new ProviderRadio(aMediaPlayer.Device(), *this, *iPresetDatabase);
    mimeTypes.AddUpnpProtocolInfoObserver(MakeFunctorGeneric(*iProviderRadio, &ProviderRadio::NotifyProtocolInfo));
    if (aTuneInPartnerId.Bytes() == 0) {
        iTuneIn = nullptr;
    }
    else {
        iTuneIn = new RadioPresetsTuneIn(aMediaPlayer.Env(),
                                         aTuneInPartnerId,
                                         *iPresetDatabase,
                                         aMediaPlayer.ConfigInitialiser(),
                                         aMediaPlayer.CredentialsManager(),
                                         aMediaPlayer.ThreadPool(),
                                         mimeTypes);
    }

    
    if (aMediaPlayer.PinsInvocable().Ok()) {
        auto podcastPinsITunes = new PodcastPinsLatestEpisodeITunes(aMediaPlayer.Device(), aMediaPlayer.TrackFactory(), aMediaPlayer.CpStack(), aMediaPlayer.ReadWriteStore(), aMediaPlayer.ThreadPool());
        aMediaPlayer.PinsInvocable().Unwrap().Add(podcastPinsITunes);

        if (iTuneIn != nullptr) {
            auto tuneInPins = new TuneInPins(aMediaPlayer.Device(), aMediaPlayer.TrackFactory(), aMediaPlayer.CpStack(), aMediaPlayer.ReadWriteStore(), aMediaPlayer.ThreadPool(), aTuneInPartnerId);
            aMediaPlayer.PinsInvocable().Unwrap().Add(tuneInPins);
            auto radioPins = new RadioPins(aMediaPlayer.Device(), aMediaPlayer.CpStack());
            aMediaPlayer.PinsInvocable().Unwrap().Add(radioPins);
            auto urlPins = new UrlPins(aMediaPlayer.Device(), aMediaPlayer.CpStack(), aMediaPlayer.ThreadPool());
            aMediaPlayer.PinsInvocable().Unwrap().Add(urlPins);
        }
    }
}

SourceRadio::~SourceRadio()
{
    delete iTuneIn;
    delete iPresetDatabase;
    delete iStorePresetNumber;
    delete iProviderRadio;
    if (iTrack != nullptr) {
        iTrack->RemoveRef();
    }
}

void SourceRadio::Activate(TBool aAutoPlay, TBool aPrefetchAllowed)
{
    SourceBase::Activate(aAutoPlay, aPrefetchAllowed);
    if (iTuneIn != nullptr) {
        iTuneIn->Refresh();
    }
    iTrackPosSeconds = 0;
    iActive = true;
    iAutoPlay = aAutoPlay;
    if (aPrefetchAllowed) {
        const TUint trackId = (iTrack==nullptr? Track::kIdNone : iTrack->Id());
        iPipeline.StopPrefetch(iCurrentMode, trackId);
        if (trackId != Track::kIdNone && aAutoPlay) {
            iPipeline.Play();
        }
    }
}

void SourceRadio::Deactivate()
{
    iProviderRadio->SetTransportState(EPipelineStopped);
    iStorePresetNumber->Write();
    Source::Deactivate();
}

TBool SourceRadio::TryActivateNoPrefetch(const Brx& aMode)
{
    if (aMode == iUriProviderPresets->Mode()) {
        iCurrentMode.Set(iUriProviderPresets->Mode());
    }
    else if (aMode == iUriProviderSingle->Mode()) {
        iCurrentMode.Set(iUriProviderSingle->Mode());
    }
    else {
        return false;
    }
    EnsureActiveNoPrefetch();
    return true;
}

void SourceRadio::StandbyEnabled()
{
    Stop();
}

void SourceRadio::PipelineStopped()
{
    // FIXME - could nullptr iPipeline (if we also changed it to be a pointer)
}

TBool SourceRadio::TryFetch(TUint aPresetId, const Brx& aUri)
{
    AutoMutex _(iLock);
    if (aPresetId == IPresetDatabaseReader::kPresetIdNone) {
        return false;
    }
    if (aUri.Bytes() > 0) {
        iPresetUri.Replace(aUri);
        if (!iPresetDatabase->TryGetPresetById(aPresetId, iPresetMetadata)) {
            return false;
        }
    }
    else if (!iPresetDatabase->TryGetPresetById(aPresetId, iPresetUri, iPresetMetadata)) {
        return false;
    }
    iCurrentMode.Set(iUriProviderPresets->Mode());
    iStorePresetNumber->Set(iPresetDatabase->GetPresetNumber(aPresetId));
    iStorePresetNumber->Write();
    iProviderRadio->NotifyPresetInfo(aPresetId, iPresetUri, iPresetMetadata);
    FetchLocked(iPresetUri, iPresetMetadata);
    return true;
}

void SourceRadio::Fetch(const Brx& aUri, const Brx& aMetaData)
{
    AutoMutex _(iLock);
    iCurrentMode.Set(iUriProviderSingle->Mode());
    iStorePresetNumber->Set(IPresetDatabaseReader::kPresetIdNone);
    FetchLocked(aUri, aMetaData);
}

void SourceRadio::FetchLocked(const Brx& aUri, const Brx& aMetaData)
{
    ActivateIfNotActive();
    if (iTrack == nullptr || iTrack->Uri() != aUri) {
        if (iTrack != nullptr) {
            iTrack->RemoveRef();
        }
        if (iCurrentMode == iUriProviderPresets->Mode()) {
            iTrack = iUriProviderPresets->SetTrack(aUri, aMetaData);
        }
        else {
            iTrack = iUriProviderSingle->SetTrack(aUri, aMetaData);
        }
        if (iTrack != nullptr) {
            iPipeline.StopPrefetch(iCurrentMode, iTrack->Id());
        }
    }
}

/*
 * Some control points do not chain calls to SetChannel()/Play() on the provider,
 * which can result in those actions coming in out of order.
 *
 * This causes problems, particularly when no radio station has been pre-fetched,
 * as Play() does nothing due to encountering a nullptr track, followed by a valid
 * track being queued up in the pipeline via Fetch(), which never gets played
 * as the call to Play() has already been issued on a nullptr track.
 */
void SourceRadio::Play()
{
    AutoMutex _(iLock);
    ActivateIfNotActive();
    if (iTrack == nullptr) {
        return;
    }

    /*
     * Fetch() is always called each time a new URI is set. That causes some
     * data to be buffered in the pipeline, which may be stale by the time
     * Play() is called.
     *
     * Therefore, always call RemoveAll(), even if the pipeline has already
     * been initialised with the desired track URI.
     *
     * Pre-fetching and then clearing pipeline may cause the pipeline to report:
     * "Failure to recognise audio format, flushing stream..."
     * which is just a false-positive in this scenario.
     */
    iPipeline.RemoveAll();
    iPipeline.Begin(iCurrentMode, iTrack->Id());
    DoPlay();
}

void SourceRadio::Pause()
{
    if (IsActive()) {
        if (iLive) {
            iPipeline.Stop();
        }
        else {
            try {
                iPipeline.Pause();
            }
            catch (PipelineStreamNotPausable&) {}
        }
    }
}

void SourceRadio::Stop()
{
    if (IsActive()) {
        iPipeline.Stop();
    }
}

void SourceRadio::Next()
{
    NextPrev(true);
}

void SourceRadio::Prev()
{
    NextPrev(false);
}

void SourceRadio::NextPrev(TBool aNext)
{
    const TChar* func = aNext? "Next" : "Prev";
    if (!IsActive()) {
        return;
    }
    AutoMutex _(iLock);
    const TUint presetNum = iStorePresetNumber->Get();
    if (presetNum == IPresetDatabaseReader::kPresetIdNone) {
        LOG(kMedia, "SourceRadio::%s - no preset selected so nothing to move relative to\n", func);
    }
    TUint id = iPresetDatabase->GetPresetId(presetNum);
    const auto track = aNext? iPresetDatabase->NextTrackRef(id) :
                              iPresetDatabase->PrevTrackRef(id);
    if (track == nullptr) {
        LOG(kMedia, "SourceRadio::%s - at end of preset list (and no current support for Repeat mode)\n", func);
        return;
    }
    if (iTrack != nullptr) {
        iTrack->RemoveRef();
    }
    iTrack = track;
    iUriProviderPresets->SetTrack(iTrack);
    iProviderRadio->NotifyPresetInfo(id, iTrack->Uri(), iTrack->MetaData());
    iStorePresetNumber->Set(iPresetDatabase->GetPresetNumber(id));
    iStorePresetNumber->Write();

    iPipeline.RemoveAll();
    iPipeline.Begin(iCurrentMode, iTrack->Id());
    DoPlay();
}

void SourceRadio::SeekAbsolute(TUint aSeconds)
{
    if (IsActive()) {
        (void)iPipeline.Seek(iStreamId, aSeconds);
    }
}

void SourceRadio::SeekRelative(TInt aSeconds)
{
    TUint abs;
    if (aSeconds < 0 && (TUint)(-aSeconds) > iTrackPosSeconds) {
        abs = 0;
    }
    else {
        abs = aSeconds + iTrackPosSeconds;
    }
    SeekAbsolute(abs);
}

void SourceRadio::NotifyPipelineState(EPipelineState aState)
{
    if (IsActive()) {
        iProviderRadio->SetTransportState(aState);
    }
}

void SourceRadio::PresetDatabaseChanged()
{
    AutoMutex _(iLock);
    if (iPresetsUpdated) {
        return;
    }
    iPresetsUpdated = true;
    if (iTrack != nullptr) {
        return;
    }
 
    const TUint presetId = iPresetDatabase->GetPresetId(iStorePresetNumber->Get());
    if (presetId == IPresetDatabaseReader::kPresetIdNone) {
        return;
    }
    if (!iPresetDatabase->TryGetPresetById(presetId, iPresetUri, iPresetMetadata)) {
        iStorePresetNumber->Set(IPresetDatabaseReader::kPresetIdNone);
        return;
    }
    iProviderRadio->NotifyPresetInfo(presetId, iPresetUri, iPresetMetadata);
    if (IsActive() && iAutoPlay) {
        iProviderRadio->NotifyPresetInfo(presetId, iPresetUri, iPresetMetadata);
        FetchLocked(iPresetUri, iPresetMetadata);
        iPipeline.Play();
    }
    else {
        iTrack = iUriProviderPresets->SetTrack(iPresetUri, iPresetMetadata);
    }
}

void SourceRadio::NotifyMode(const Brx& /*aMode*/,
                             const ModeInfo& /*aInfo*/,
                             const ModeTransportControls& /*aTransportControls*/)
{
}

void SourceRadio::NotifyTrack(Track& /*aTrack*/, TBool /*aStartOfStream*/)
{
    if (!IsActive()) {
        return;
    }
}

void SourceRadio::NotifyMetaText(const Brx& /*aText*/)
{
}

void SourceRadio::NotifyTime(TUint aSeconds)
{
    iLock.Wait();
    iTrackPosSeconds = aSeconds;
    iLock.Signal();
}

void SourceRadio::NotifyStreamInfo(const DecodedStreamInfo& aStreamInfo)
{
    iLock.Wait();
    iStreamId = aStreamInfo.StreamId();
    iLive = aStreamInfo.Live();
    iLock.Signal();
}

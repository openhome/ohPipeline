#include <OpenHome/Av/QobuzConnect/MediaControl.h>
#include <OpenHome/Av/QobuzConnect/AudioStream.h>
#include <OpenHome/Types.h>
#include <OpenHome/Functor.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Debug-ohMediaPlayer.h>

using namespace OpenHome;
using namespace OpenHome::Av;

extern "C" {

static void QobuzConnectMediaControl_InitiatePlayback(QbzConnectCore* aCore, QbzInitialPlaybackState aInitialState, void* aUserData)
{
    OpenHome::Av::QobuzConnectMediaControl::InitiatePlaybackCb(aCore, aInitialState, aUserData);
}
static void QobuzConnectMediaControl_PausePlayback(QbzConnectCore* aCore, void* aUserData)
{
    OpenHome::Av::QobuzConnectMediaControl::PausePlaybackCb(aCore, aUserData);
}
static void QobuzConnectMediaControl_ResumePlayback(QbzConnectCore* aCore, void* aUserData)
{
    OpenHome::Av::QobuzConnectMediaControl::ResumePlaybackCb(aCore, aUserData);
}
static void QobuzConnectMediaControl_StopPlayback(QbzConnectCore* aCore, void* aUserData)
{
    OpenHome::Av::QobuzConnectMediaControl::StopPlaybackCb(aCore, aUserData);
}
static void QobuzConnectMediaControl_SeekInProgress(QbzConnectCore* aCore, void* aUserData)
{
    OpenHome::Av::QobuzConnectMediaControl::SeekInProgressCb(aCore, aUserData);
}
static uint64_t QobuzConnectMediaControl_GetPlaybackPosition(QbzConnectCore* aCore, void* aUserData)
{
    return OpenHome::Av::QobuzConnectMediaControl::GetPlaybackPositionCb(aCore, aUserData);
}
static void QobuzConnectMediaControl_PlaybackStateChanged(QbzConnectCore* aCore, QbzPlaybackState aState, void* aUserData)
{
    OpenHome::Av::QobuzConnectMediaControl::PlaybackStateChangedCb(aCore, aState, aUserData);
}
static void QobuzConnectMediaControl_ActiveStateChanged(QbzConnectCore* aCore, bool aActive, void* aUserData)
{
    OpenHome::Av::QobuzConnectMediaControl::ActiveStateChangedCb(aCore, aActive, aUserData);
}
static void QobuzConnectMediaControl_PlaybackVolumeChanged(QbzConnectCore* aCore, uint32_t aVolume, void* aUserData)
{
    OpenHome::Av::QobuzConnectMediaControl::PlaybackVolumeChangedCb(aCore, aVolume, aUserData);
}
static void QobuzConnectMediaControl_PlaybackMuteStateChanged(QbzConnectCore* aCore, bool aMuted, void* aUserData)
{
    OpenHome::Av::QobuzConnectMediaControl::PlaybackMuteStateChangedCb(aCore, aMuted, aUserData);
}

} // extern "C"

QobuzConnectMediaControl::QobuzConnectMediaControl(IThreadPool& aThreadPool, IQobuzConnectPlaybackObserver& aObserver, QobuzConnectAudioStream& aAudioStream)
    : iThreadPool(aThreadPool)
    , iObserver(aObserver)
    , iAudioStream(aAudioStream)
    , iLock("QCM1")
    , iCore(nullptr)
    , iPendingInitialState(QBZ_INITIAL_PLAYBACK_STATE_PLAYING)
    , iPendingActiveState(false)
    , iPendingVolume(0)
    , iPendingMuted(false)
    , iLockPosition("QCM2")
    , iPositionBaseMs(0)
    , iPositionBaseTime(std::chrono::steady_clock::now())
    , iPositionRunning(false)
{
    iHandleInitiate = iThreadPool.CreateHandle(MakeFunctor(*this, &QobuzConnectMediaControl::HandleInitiatePlayback), "QobuzConnectMediaControl-Initiate", ThreadPoolPriority::High);
    iHandlePause = iThreadPool.CreateHandle(MakeFunctor(*this, &QobuzConnectMediaControl::HandlePausePlayback), "QobuzConnectMediaControl-Pause", ThreadPoolPriority::High);
    iHandleResume = iThreadPool.CreateHandle(MakeFunctor(*this, &QobuzConnectMediaControl::HandleResumePlayback), "QobuzConnectMediaControl-Resume", ThreadPoolPriority::High);
    iHandleStop = iThreadPool.CreateHandle(MakeFunctor(*this, &QobuzConnectMediaControl::HandleStopPlayback), "QobuzConnectMediaControl-Stop", ThreadPoolPriority::High);
    iHandleSeek = iThreadPool.CreateHandle(MakeFunctor(*this, &QobuzConnectMediaControl::HandleSeekInProgress), "QobuzConnectMediaControl-Seek", ThreadPoolPriority::High);
    iHandleActiveState = iThreadPool.CreateHandle(MakeFunctor(*this, &QobuzConnectMediaControl::HandleActiveStateChanged), "QobuzConnectMediaControl-Active", ThreadPoolPriority::High);
    iHandleVolume = iThreadPool.CreateHandle(MakeFunctor(*this, &QobuzConnectMediaControl::HandleVolumeChanged), "QobuzConnectMediaControl-Volume", ThreadPoolPriority::High);
    iHandleMute = iThreadPool.CreateHandle(MakeFunctor(*this, &QobuzConnectMediaControl::HandleMuteStateChanged), "QobuzConnectMediaControl-Mute", ThreadPoolPriority::High);
}

QobuzConnectMediaControl::~QobuzConnectMediaControl()
{
    iHandleInitiate->Destroy();
    iHandlePause->Destroy();
    iHandleResume->Destroy();
    iHandleStop->Destroy();
    iHandleSeek->Destroy();
    iHandleActiveState->Destroy();
    iHandleVolume->Destroy();
    iHandleMute->Destroy();
}

QbzMediaDelegate QobuzConnectMediaControl::Delegate()
{
    QbzMediaDelegate delegate = {};
    delegate.user_data = this;
    delegate.initiate_playback_callback = &QobuzConnectMediaControl_InitiatePlayback;
    delegate.pause_playback_callback = &QobuzConnectMediaControl_PausePlayback;
    delegate.resume_playback_callback = &QobuzConnectMediaControl_ResumePlayback;
    delegate.stop_playback_callback = &QobuzConnectMediaControl_StopPlayback;
    delegate.seek_in_progress_callback = &QobuzConnectMediaControl_SeekInProgress;
    delegate.get_playback_position_callback = &QobuzConnectMediaControl_GetPlaybackPosition;
    delegate.playback_volume_changed_callback = &QobuzConnectMediaControl_PlaybackVolumeChanged;
    delegate.playback_mute_state_changed_callback = &QobuzConnectMediaControl_PlaybackMuteStateChanged;
    delegate.playback_state_changed_callback = &QobuzConnectMediaControl_PlaybackStateChanged;
    // maximum_audio_quality_changed_callback / playback_controls_changed_callback /
    // playback_actions_availability_changed_callback are all optional and not implemented in
    // this first pass (loop/shuffle/quality-limit selection from the Controller won't have any
    // on-device effect yet).
    return delegate;
}

QbzRendererStateDelegate QobuzConnectMediaControl::RendererStateDelegate()
{
    QbzRendererStateDelegate delegate;
    delegate.user_data = this;
    delegate.active_state_changed_callback = &QobuzConnectMediaControl_ActiveStateChanged;
    return delegate;
}

void QobuzConnectMediaControl::SetCore(QbzConnectCore* aCore)
{
    AutoMutex _(iLock);
    iCore = aCore;
}

void QobuzConnectMediaControl::NotifyPlaybackInitiated(TUint aSampleRate, TUint aBitDepth, TUint aNumChannels, TBool aStartedPaused)
{
    QbzConnectCore* core;
    {
        AutoMutex _(iLock);
        core = iCore;
    }
    if (core == nullptr) {
        return;
    }
    QbzAudioFormat format;
    format.sample_rate = aSampleRate;
    format.channel_count = aNumChannels;
    format.sample_format = (aBitDepth == 16)
        ? QBZ_AUDIO_SAMPLE_FORMAT_SIGNED_16_BIT_LITTLE_ENDIAN
        : QBZ_AUDIO_SAMPLE_FORMAT_SIGNED_24_BIT_LITTLE_ENDIAN;
    const QbzError error = qbz_connect_notify_playback_initiated(core, format);
    if (error != QBZ_ERROR_OK) {
        LOG(kQobuzConnect, "QobuzConnectMediaControl: notify_playback_initiated failed: %s\n", qbz_error_to_string(error));
    }
    {
        AutoMutex _(iLockPosition);
        iPositionBaseMs = 0; // new stream - position tracking restarts from zero
        iPositionBaseTime = std::chrono::steady_clock::now();
        iPositionRunning = !aStartedPaused;
    }
}

void QobuzConnectMediaControl::NotifyPlaybackPaused()
{
    QbzConnectCore* core;
    {
        AutoMutex _(iLock);
        core = iCore;
    }
    if (core != nullptr) {
        (void)qbz_connect_notify_playback_paused(core);
    }
    ResetPositionBase(false);
}

void QobuzConnectMediaControl::NotifyPlaybackResumed()
{
    QbzConnectCore* core;
    {
        AutoMutex _(iLock);
        core = iCore;
    }
    if (core != nullptr) {
        (void)qbz_connect_notify_playback_resumed(core);
    }
    ResetPositionBase(true);
}

void QobuzConnectMediaControl::NotifyPlaybackStopped()
{
    QbzConnectCore* core;
    {
        AutoMutex _(iLock);
        core = iCore;
    }
    if (core != nullptr) {
        (void)qbz_connect_notify_playback_stopped(core);
    }
    ResetPositionBase(false);
}

void QobuzConnectMediaControl::NotifyPlaybackFinished(TBool aLastTrack)
{
    QbzConnectCore* core;
    {
        AutoMutex _(iLock);
        core = iCore;
    }
    if (core != nullptr) {
        (void)qbz_connect_notify_playback_finished(core, aLastTrack);
    }
    if (aLastTrack) {
        ResetPositionBase(false);
    }
}

void QobuzConnectMediaControl::NotifyPlaybackError()
{
    QbzConnectCore* core;
    {
        AutoMutex _(iLock);
        core = iCore;
    }
    if (core != nullptr) {
        (void)qbz_connect_notify_playback_error(core);
    }
    ResetPositionBase(false);
}

void QobuzConnectMediaControl::NotifySeeked(uint64_t aPositionMs)
{
    // Unlike ResetPositionBase() (which accumulates elapsed time onto the existing base for a
    // play/pause/stop transition), a seek jumps to an arbitrary new position unrelated to
    // whatever the base/elapsed tracking previously held - it must be replaced outright, not
    // accumulated onto. iPositionRunning is left as-is: a seek doesn't itself start or stop
    // playback.
    AutoMutex _(iLockPosition);
    iPositionBaseMs = aPositionMs;
    iPositionBaseTime = std::chrono::steady_clock::now();
}

void QobuzConnectMediaControl::SyncVolume(TUint aVolumePercent)
{
    QbzConnectCore* core;
    {
        AutoMutex _(iLock);
        core = iCore;
    }
    if (core != nullptr) {
        (void)qbz_connect_set_volume(core, aVolumePercent);
    }
}

void QobuzConnectMediaControl::SyncMute(TBool aMuted)
{
    QbzConnectCore* core;
    {
        AutoMutex _(iLock);
        core = iCore;
    }
    if (core != nullptr) {
        (void)qbz_connect_set_mute_state(core, aMuted);
    }
}

void QobuzConnectMediaControl::TryPlay()
{
    QbzConnectCore* core;
    {
        AutoMutex _(iLock);
        core = iCore;
    }
    if (core == nullptr) {
        return;
    }
    // Best-effort: resume covers the common "was paused" case; if playback was fully stopped
    // this returns an error (QBZ_ERROR_UNEXPECTED_REQUEST-ish), in which case fall back to start.
    if (qbz_connect_resume_playback(core) != QBZ_ERROR_OK) {
        (void)qbz_connect_start_playback(core);
    }
}

void QobuzConnectMediaControl::TryPause()
{
    QbzConnectCore* core;
    {
        AutoMutex _(iLock);
        core = iCore;
    }
    if (core != nullptr) {
        (void)qbz_connect_pause_playback(core);
    }
}

void QobuzConnectMediaControl::TryStop()
{
    QbzConnectCore* core;
    {
        AutoMutex _(iLock);
        core = iCore;
    }
    if (core != nullptr) {
        (void)qbz_connect_stop_playback(core);
    }
}

void QobuzConnectMediaControl::InitiatePlaybackCb(QbzConnectCore* /*aCore*/, QbzInitialPlaybackState aInitialState, void* aUserData)
{
    auto* self = reinterpret_cast<QobuzConnectMediaControl*>(aUserData);
    self->iPendingInitialState = aInitialState;
    (void)self->iHandleInitiate->TrySchedule();
}

void QobuzConnectMediaControl::PausePlaybackCb(QbzConnectCore* /*aCore*/, void* aUserData)
{
    (void)reinterpret_cast<QobuzConnectMediaControl*>(aUserData)->iHandlePause->TrySchedule();
}

void QobuzConnectMediaControl::ResumePlaybackCb(QbzConnectCore* /*aCore*/, void* aUserData)
{
    (void)reinterpret_cast<QobuzConnectMediaControl*>(aUserData)->iHandleResume->TrySchedule();
}

void QobuzConnectMediaControl::StopPlaybackCb(QbzConnectCore* /*aCore*/, void* aUserData)
{
    (void)reinterpret_cast<QobuzConnectMediaControl*>(aUserData)->iHandleStop->TrySchedule();
}

void QobuzConnectMediaControl::SeekInProgressCb(QbzConnectCore* /*aCore*/, void* aUserData)
{
    (void)reinterpret_cast<QobuzConnectMediaControl*>(aUserData)->iHandleSeek->TrySchedule();
}

uint64_t QobuzConnectMediaControl::GetPlaybackPositionCb(QbzConnectCore* /*aCore*/, void* aUserData)
{
    return reinterpret_cast<QobuzConnectMediaControl*>(aUserData)->HandleGetPlaybackPosition();
}

void QobuzConnectMediaControl::PlaybackStateChangedCb(QbzConnectCore* /*aCore*/, QbzPlaybackState aState, void* /*aUserData*/)
{
    LOG(kQobuzConnect, "QobuzConnectMediaControl: playback state changed to %s\n", qbz_playback_state_to_string(aState));
}

void QobuzConnectMediaControl::ActiveStateChangedCb(QbzConnectCore* /*aCore*/, bool aActive, void* aUserData)
{
    auto* self = reinterpret_cast<QobuzConnectMediaControl*>(aUserData);
    self->iPendingActiveState = aActive;
    (void)self->iHandleActiveState->TrySchedule();
}

void QobuzConnectMediaControl::PlaybackVolumeChangedCb(QbzConnectCore* /*aCore*/, uint32_t aVolume, void* aUserData)
{
    auto* self = reinterpret_cast<QobuzConnectMediaControl*>(aUserData);
    self->iPendingVolume = aVolume;
    (void)self->iHandleVolume->TrySchedule();
}

void QobuzConnectMediaControl::PlaybackMuteStateChangedCb(QbzConnectCore* /*aCore*/, bool aMuted, void* aUserData)
{
    auto* self = reinterpret_cast<QobuzConnectMediaControl*>(aUserData);
    self->iPendingMuted = aMuted;
    (void)self->iHandleMute->TrySchedule();
}

void QobuzConnectMediaControl::HandleInitiatePlayback()
{
    LOG(kQobuzConnect, "QobuzConnectMediaControl::HandleInitiatePlayback()\n");
    iObserver.QobuzNotifyStreamReady();
    iObserver.QobuzNotifyPlaybackInitiated(iPendingInitialState == QBZ_INITIAL_PLAYBACK_STATE_PAUSED);
}

void QobuzConnectMediaControl::HandlePausePlayback()
{
    LOG(kQobuzConnect, "QobuzConnectMediaControl::HandlePausePlayback()\n");
    iObserver.QobuzNotifyPlaybackPaused();
}

void QobuzConnectMediaControl::HandleResumePlayback()
{
    LOG(kQobuzConnect, "QobuzConnectMediaControl::HandleResumePlayback()\n");
    iObserver.QobuzNotifyPlaybackResumed();
}

void QobuzConnectMediaControl::HandleStopPlayback()
{
    LOG(kQobuzConnect, "QobuzConnectMediaControl::HandleStopPlayback()\n");
    iObserver.QobuzNotifyPlaybackStopped();
}

void QobuzConnectMediaControl::HandleSeekInProgress()
{
    LOG(kQobuzConnect, "QobuzConnectMediaControl::HandleSeekInProgress()\n");
    iAudioStream.FlushForSeek();
    iObserver.QobuzNotifySeekInProgress();
}

void QobuzConnectMediaControl::HandleActiveStateChanged()
{
    LOG(kQobuzConnect, "QobuzConnectMediaControl::HandleActiveStateChanged(%u)\n", iPendingActiveState);
    iObserver.QobuzNotifyActiveStateChanged(iPendingActiveState);
}

void QobuzConnectMediaControl::HandleVolumeChanged()
{
    LOG(kQobuzConnect, "QobuzConnectMediaControl::HandleVolumeChanged(%u)\n", iPendingVolume);
    iObserver.QobuzNotifyVolumeChanged(iPendingVolume);
}

void QobuzConnectMediaControl::HandleMuteStateChanged()
{
    LOG(kQobuzConnect, "QobuzConnectMediaControl::HandleMuteStateChanged(%u)\n", iPendingMuted);
    iObserver.QobuzNotifyMuteStateChanged(iPendingMuted);
}

uint64_t QobuzConnectMediaControl::HandleGetPlaybackPosition()
{
    AutoMutex _(iLockPosition);
    if (!iPositionRunning) {
        return iPositionBaseMs;
    }
    const auto now = std::chrono::steady_clock::now();
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - iPositionBaseTime).count();
    return iPositionBaseMs + (uint64_t)elapsedMs;
}

void QobuzConnectMediaControl::ResetPositionBase(TBool aRunning)
{
    AutoMutex _(iLockPosition);
    if (iPositionRunning) {
        const auto now = std::chrono::steady_clock::now();
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - iPositionBaseTime).count();
        iPositionBaseMs += (uint64_t)elapsedMs;
    }
    iPositionBaseTime = std::chrono::steady_clock::now();
    iPositionRunning = aRunning;
}

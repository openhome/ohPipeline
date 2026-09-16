#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>

#include <qobuz_connect.h>

#include <chrono>

namespace OpenHome {
    class IThreadPool;
    class IThreadPoolHandle;
namespace Av {

class QobuzConnectAudioStream;

/**
 * Notified of QobuzConnectAudioStream-level events that need to reach QobuzConnectMediaControl/
 * SourceQobuzConnect. Separate from IQobuzConnectPlaybackObserver only so that
 * QobuzConnectAudioStream.h (which needs this type for QobuzConnectAudioStream::
 * SetMetadataObserver()) doesn't have to depend on the rest of that interface, or on this header
 * at all - a forward declaration is enough there.
 */
class IQobuzConnectMetadataObserver
{
public:
    virtual ~IQobuzConnectMetadataObserver() {}
    virtual void QobuzNotifyMetadataChanged(const Brx& aTitle, const Brx& aArtist, const Brx& aAlbum, const Brx& aArtworkUri) = 0;
    // The SDK's stream_seeked_callback (QbzAudioStreamSeekedCallback) fired, with the new
    // position - see QobuzConnectMediaControl::NotifySeeked().
    virtual void QobuzNotifyStreamSeeked(uint64_t aPositionMs) = 0;
    // The SDK's stream_finished_callback (QbzAudioStreamFinishedCallback) fired: the active
    // stream has delivered all its audio data (the track reached its natural end, as opposed to
    // being paused/stopped/seeked). See QobuzConnectMediaControl::NotifyPlaybackFinished() - the
    // SDK won't hand over the next track's audio on its own; this must be acknowledged first.
    virtual void QobuzNotifyStreamFinished() = 0;
    // The SDK's stream_started_callback (QbzAudioStreamStartedCallback) fired for a new stream.
    // For a Controller/UI-initiated track change this arrives alongside an explicit
    // initiate_playback_callback (handled via QobuzNotifyStreamReady()/
    // QobuzNotifyPlaybackInitiated()) - but per the SDK README (4.2.2, and
    // qbz_connect_notify_playback_finished's doc comment), when the integration layer itself
    // continues automatically after QobuzNotifyStreamFinished(), no such callback follows: the
    // implementation is responsible for calling qbz_connect_notify_playback_initiated once this
    // fires for the auto-advanced stream. Without that, audio itself keeps playing correctly, but
    // the SDK's own "now playing" state - and hence the Controller's display - never advances
    // past the previous track.
    virtual void QobuzNotifyStreamStarted() = 0;
};

/**
 * Notified (always via IThreadPool - see QobuzConnectMediaControl/QobuzConnectAudioStream's use
 * of RaatPluginAsync-style handle scheduling) when the SDK wants playback state to change.
 * Implemented by SourceQobuzConnect, which owns the actual Pipeline/ProtocolQobuzConnect
 * objects these map onto.
 */
class IQobuzConnectPlaybackObserver : public IQobuzConnectMetadataObserver
{
public:
    virtual ~IQobuzConnectPlaybackObserver() {}
    virtual void QobuzNotifyStreamReady() = 0;    // a new audio stream exists - equivalent to ISourceRaat::NotifySetup
    virtual void QobuzNotifyPlaybackInitiated(TBool aStartPaused) = 0; // equivalent to ISourceRaat::NotifyStart
    virtual void QobuzNotifyPlaybackPaused() = 0;
    virtual void QobuzNotifyPlaybackResumed() = 0;
    virtual void QobuzNotifyPlaybackStopped() = 0; // equivalent to ISourceRaat::NotifyStop
    virtual void QobuzNotifySeekInProgress() = 0;
    // Renderer became (or stopped being) the Controller's selected playback target - i.e. this
    // is what should drive actually switching DS onto the Qobuz Connect source, the way
    // selecting an AirPlay/Spotify Connect/Roon target does on other sources. Without this,
    // "Connect" wouldn't actually connect - the user would have to manually select the source
    // on the device first, defeating the point.
    virtual void QobuzNotifyActiveStateChanged(TBool aActive) = 0;
    // Controller (phone app) requested a new absolute volume/mute state - see
    // QobuzConnectMediaControl::SyncVolume/SyncMute for the opposite direction (DS's own volume
    // changing for some other reason, e.g. IR remote/front panel, needs reporting back to the SDK
    // so the Controller's own slider stays in sync).
    virtual void QobuzNotifyVolumeChanged(TUint aVolumePercent) = 0; // 0-100, per QbzMediaPlaybackVolumeChangedCallback
    virtual void QobuzNotifyMuteStateChanged(TBool aMuted) = 0;
};

/**
 * Implements the SDK's Media delegate. QBZ_VOLUME_CAPABILITY_ABSOLUTE_VOLUME is advertised (see
 * App.cpp), so playback_volume_changed_callback/playback_mute_state_changed_callback are wired up
 * below - incoming volume/mute requests from the Controller are forwarded to
 * IQobuzConnectPlaybackObserver (implemented by SourceQobuzConnect, which maps the SDK's 0-100
 * scale onto DS's own VolumeManager range), and SyncVolume/SyncMute push the opposite direction
 * (DS's current volume/mute, however it changed) back into the SDK so the Controller's displayed
 * state doesn't go stale.
 *
 * get_playback_position_callback runs synchronously on the SDK's own uv-loop thread and must
 * return quickly (it's a direct query, not an async request like the others) - position is
 * tracked here as a simple base value + elapsed wall-clock time since the last play/pause/stop
 * transition (or seek - see NotifySeeked()), recomputed on read under iLock. This is an
 * approximation - it doesn't account for actual DAC/pipeline output latency - but is corrected
 * to the exact value the SDK reports after a seek.
 */
class QobuzConnectMediaControl
{
public:
    QobuzConnectMediaControl(IThreadPool& aThreadPool, IQobuzConnectPlaybackObserver& aObserver, QobuzConnectAudioStream& aAudioStream);
    ~QobuzConnectMediaControl();
public:
    QbzMediaDelegate Delegate();
    QbzRendererStateDelegate RendererStateDelegate();
    void SetCore(QbzConnectCore* aCore);
public: // acks back to the SDK - called by SourceQobuzConnect once it has actually made the
        // corresponding Pipeline state change (from the same ThreadPool-scheduled callback that
        // triggered it, i.e. not directly from the SDK's own uv-loop thread)
    void NotifyPlaybackInitiated(TUint aSampleRate, TUint aBitDepth, TUint aNumChannels, TBool aStartedPaused);
    void NotifyPlaybackPaused();
    void NotifyPlaybackResumed();
    void NotifyPlaybackStopped();
    void NotifyPlaybackFinished(TBool aLastTrack);
    void NotifyPlaybackError();
public:
    // Corrects the position tracked for GetPlaybackPositionCb() to the exact value the SDK
    // reports post-seek (QbzAudioStreamSeekedCallback), rather than leaving it wherever the
    // pre-seek base+elapsed-time tracking left off - see SourceQobuzConnect::
    // QobuzNotifyStreamSeeked().
    void NotifySeeked(uint64_t aPositionMs);
public:
    // Push DS's current volume/mute (however it changed - Controller request, IR remote, front
    // panel...) back into the SDK, so the Controller's own displayed volume stays in sync. Safe to
    // call at any time, including before the SDK's core exists (silently dropped, matching the
    // Notify* methods above).
    void SyncVolume(TUint aVolumePercent); // 0-100
    void SyncMute(TBool aMuted);
public:
    // Wired up to DS's own UI transport controls (see UriProviderQobuzConnect in
    // SourceQobuzConnect.cpp), mirroring how UriProviderRaat's SetTransportPlay/Pause/Stop wire
    // to IRaatTransport - the SDK explicitly supports the integration layer driving these (see
    // qbz_connect_start_playback's doc comment: "typical use case is after the integration layer
    // calls qbz_connect_stop_playback and later wants to resume it").
    void TryPlay();
    void TryPause();
    void TryStop();
public:
    // Called by the extern "C" trampoline functions in MediaControl.cpp (which aren't members or
    // friends of this class, so these must be public, not private).
    static void InitiatePlaybackCb(QbzConnectCore* aCore, QbzInitialPlaybackState aInitialState, void* aUserData);
    static void PausePlaybackCb(QbzConnectCore* aCore, void* aUserData);
    static void ResumePlaybackCb(QbzConnectCore* aCore, void* aUserData);
    static void StopPlaybackCb(QbzConnectCore* aCore, void* aUserData);
    static void SeekInProgressCb(QbzConnectCore* aCore, void* aUserData);
    static uint64_t GetPlaybackPositionCb(QbzConnectCore* aCore, void* aUserData);
    static void PlaybackStateChangedCb(QbzConnectCore* aCore, QbzPlaybackState aState, void* aUserData);
    static void ActiveStateChangedCb(QbzConnectCore* aCore, bool aActive, void* aUserData);
    static void PlaybackVolumeChangedCb(QbzConnectCore* aCore, uint32_t aVolume, void* aUserData);
    static void PlaybackMuteStateChangedCb(QbzConnectCore* aCore, bool aMuted, void* aUserData);
private:
    void HandleInitiatePlayback();
    void HandlePausePlayback();
    void HandleResumePlayback();
    void HandleStopPlayback();
    void HandleSeekInProgress();
    void HandleActiveStateChanged();
    void HandleVolumeChanged();
    void HandleMuteStateChanged();
    uint64_t HandleGetPlaybackPosition();
    void ResetPositionBase(TBool aRunning);
private:
    IThreadPool& iThreadPool;
    IQobuzConnectPlaybackObserver& iObserver;
    QobuzConnectAudioStream& iAudioStream;
    Mutex iLock;
    QbzConnectCore* iCore;
    IThreadPoolHandle* iHandleInitiate;
    IThreadPoolHandle* iHandlePause;
    IThreadPoolHandle* iHandleResume;
    IThreadPoolHandle* iHandleStop;
    IThreadPoolHandle* iHandleSeek;
    IThreadPoolHandle* iHandleActiveState;
    IThreadPoolHandle* iHandleVolume;
    IThreadPoolHandle* iHandleMute;
    QbzInitialPlaybackState iPendingInitialState;
    TBool iPendingActiveState;
    TUint iPendingVolume;
    TBool iPendingMuted;
    // Position tracking - see class comment.
    Mutex iLockPosition;
    uint64_t iPositionBaseMs;
    std::chrono::steady_clock::time_point iPositionBaseTime;
    TBool iPositionRunning;
};

}
}

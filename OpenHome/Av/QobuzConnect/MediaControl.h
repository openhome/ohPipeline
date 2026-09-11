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
 * Notified (always via IThreadPool - see QobuzConnectMediaControl/QobuzConnectAudioStream's use
 * of RaatPluginAsync-style handle scheduling) when the SDK wants playback state to change.
 * Implemented by SourceQobuzConnect, which owns the actual Pipeline/ProtocolQobuzConnect
 * objects these map onto.
 */
class IQobuzConnectPlaybackObserver
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
};

/**
 * Implements the SDK's Media delegate. Playback volume/mute callbacks are deliberately left
 * unset (nullptr) in the delegate struct - they're only mandatory when QbzDeviceInfo advertises
 * QBZ_VOLUME_CAPABILITY_ABSOLUTE_VOLUME, which SourceQobuzConnect doesn't (v1 scope decision:
 * wiring Qobuz Connect's volume control into DS's VolumeManager is a separate, not-yet-started
 * piece of work).
 *
 * get_playback_position_callback runs synchronously on the SDK's own uv-loop thread and must
 * return quickly (it's a direct query, not an async request like the others) - position is
 * tracked here as a simple base value + elapsed wall-clock time since the last play/pause/stop
 * transition, recomputed on read under iLock. This is an approximation (it doesn't account for
 * actual DAC/pipeline output latency, and isn't corrected against the exact position reported by
 * QbzAudioStreamSeekedCallback after a seek - AudioStream doesn't currently thread that value
 * through here) - reasonable for a first pass, worth tightening up later.
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
private:
    void HandleInitiatePlayback();
    void HandlePausePlayback();
    void HandleResumePlayback();
    void HandleStopPlayback();
    void HandleSeekInProgress();
    void HandleActiveStateChanged();
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
    QbzInitialPlaybackState iPendingInitialState;
    TBool iPendingActiveState;
    // Position tracking - see class comment.
    Mutex iLockPosition;
    uint64_t iPositionBaseMs;
    std::chrono::steady_clock::time_point iPositionBaseTime;
    TBool iPositionRunning;
};

}
}

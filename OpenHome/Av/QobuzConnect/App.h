#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Av/QobuzConnect/AudioStream.h>
#include <OpenHome/Av/QobuzConnect/MediaControl.h>

#include <uv.h>
#include <qobuz_connect.h>

namespace OpenHome {
    class Environment;
namespace Net {
    class IMdnsProvider;
}
namespace Media {
    class TrackFactory;
}
namespace Av {

    class IMediaPlayer;
    class QobuzConnectAdvertising;
    class QobuzConnectLocalConfigServer;
    class ProtocolQobuzConnect;

/**
 * Owns Qobuz Connect's own libuv event loop and dedicated thread, and the QbzConnectCore
 * instance itself. Mirrors RaatApp (OpenHome/Av/Raat/App.h) structurally - one thread, created
 * in Start(), whose body (QobuzThread()) blocks running the event loop until told to quit - but
 * differs in one important way: RAAT's RAAT__device_run() is opaque and drives RAAT's own
 * (renamed raat_uv_*) loop internally, whereas Qobuz Connect hands *us* a uv_loop_t* to create
 * and drive ourselves (qbz_connect_create() takes it as a parameter). This class therefore owns
 * a real libuv event loop directly - via the *unrenamed*, genuinely separate copy of libuv
 * bundled with the Qobuz Connect SDK (see ds/wscript's QOBUZ Connect section for why this is a
 * distinct library from RAAT's own raat_uv_*-renamed copy, and why the two must never be mixed
 * in the same translation unit - this file and everything it includes only ever sees Qobuz
 * Connect's own <uv.h>, never RAAT's).
 *
 * Clean shutdown uses a uv_async_t, the same pattern the SDK's own sample_console_app uses
 * (see dependencies/<platform>/qobuz_connect/sample_console_app/sample_app.c): qbz_connect_free()
 * and uv_loop_close() must happen on the loop's own thread, so Stop() (called from ~QobuzConnectApp,
 * i.e. from some other thread) just calls uv_async_send() to wake the loop and run AsyncQuit()
 * there, which does the actual teardown and calls uv_stop().
 */
class QobuzConnectApp
{
public:
    QobuzConnectApp(
        IMediaPlayer& aMediaPlayer,
        Net::IMdnsProvider& aMdnsProvider,
        IQobuzConnectPlaybackObserver& aPlaybackObserver,
        const Brx& aAppId,
        const Brx& aAppSecret,
        const Brx& aDeviceName,
        const Brx& aManufacturer,
        const Brx& aModel,
        const Brx& aSerialNumber,
        const Brx& aUniqueDeviceId,
        QbzDeviceType aDeviceType,
        QbzAudioQuality aMaxAudioQuality);
    ~QobuzConnectApp();
public:
    void Start();
    IQobuzConnectAudioReader& Reader();
    QobuzConnectMediaControl& MediaControl();
public:
    void QobuzThread(); // uv thread entrypoint - public only so the extern "C" trampoline can call it
private:
    static void AsyncQuitCb(uv_async_t* aHandle);
    void AsyncQuit();
    // Diagnostic only (see report on the playback-activation stall): logs periodically for as
    // long as the loop is running, and polls qbz_connect_get_renderer_active_state() each tick.
    // Answers two questions a plain log line can't: is the whole SDK thread wedged (heartbeat
    // stops) or just the renderer-activation state machine (heartbeat keeps going); and does the
    // SDK's internal active state ever actually flip even though active_state_changed_callback
    // never fires.
    static void HeartbeatCb(uv_timer_t* aHandle);
    void Heartbeat();
private:
    IMediaPlayer& iMediaPlayer;
    uv_thread_t iThread;
    uv_loop_t iLoop;
    uv_async_t iAsyncQuit;
    uv_timer_t iHeartbeat;
    QbzConnectCore* iCore;
    QobuzConnectAudioStream* iAudioStream;
    QobuzConnectMediaControl* iMediaControl;
    QobuzConnectAdvertising* iAdvertising;
    QobuzConnectLocalConfigServer* iLocalConfigServer;
    Bws<64> iAppId;
    Bws<64> iAppSecret;
    Bws<64> iDeviceName;
    Bws<64> iManufacturer;
    Bws<64> iModel;
    Bws<64> iSerialNumber;
    Bws<64> iUniqueDeviceId;
    QbzDeviceType iDeviceType;
    QbzAudioQuality iMaxAudioQuality;
    TBool iStarted;
};

}
}

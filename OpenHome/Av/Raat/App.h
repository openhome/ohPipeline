#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>

#include <uv.h>
#include <raat_device.h>
#include <raat_info.h>

// RAAT's bundled libuv is rebuilt with uv_thread_* renamed to raat_uv_thread_* only on platforms
// where Qobuz Connect is also compiled in (QOBUZ_CONNECT_ENABLED - see ds/wscript's QOBUZ Connect
// section for the full history), to avoid colliding with Qobuz Connect's own, unrenamed bundled
// libuv on those platforms only. Elsewhere - anywhere Qobuz Connect isn't compiled in, so there's
// no collision to avoid - RAAT's libuv is the standard, unrenamed build, so the plain uv_thread_*
// names must be used instead. Confirmed as a hard platform-portability break otherwise (Windows-x86
// release build: 'raat_uv_thread_create'/'raat_uv_thread_t' not found).
#ifdef QOBUZ_CONNECT_ENABLED
typedef raat_uv_thread_t OhRaatUvThreadT;
#define OhRaatUvThreadCreate raat_uv_thread_create
#define OhRaatUvThreadJoin raat_uv_thread_join
#else
typedef uv_thread_t OhRaatUvThreadT;
#define OhRaatUvThreadCreate uv_thread_create
#define OhRaatUvThreadJoin uv_thread_join
#endif

namespace OpenHome {
    class Environment;
    class Timer;
namespace Media {
    class IAudioTime;
    class IPullableClock;
}
namespace Av {

    class IMediaPlayer;
    class ISourceRaat;
    class ISourceRaatStandbyControl;
    class IRaatReader;
    class IRaatTime;
    class IRaatSignalPathObservable;
    class RaatOutput;
    class RaatVolume;
    class RaatSourceSelection;
    class RaatTransport;
    class IRaatTransport;
    
class RaatApp
{
public:
    RaatApp(
        Environment& aEnv,
        IMediaPlayer& aMediaPlayer,
        ISourceRaat& aSourceRaat,
        Media::IAudioTime& aAudioTime,
        Media::IPullableClock& aPullableClock,
        IRaatSignalPathObservable& aSignalPathObservable,
        const Brx& aSerialNumber,
        const Brx& aSoftwareVersion,
        const Brx& aConfigUrl);
    ~RaatApp();
public:
    void Start();
    IRaatReader& Reader();
    IRaatTransport& Transport();
public:
    void RaatThread();
private:
    void StartPlugins();
private:
    IMediaPlayer& iMediaPlayer;
    OhRaatUvThreadT iThread;
    Timer* iTimer;
    RAAT__Device* iDevice;
    RAAT__Info* iInfo;
    RaatOutput* iOutput;
    RaatVolume* iVolume;
    RaatSourceSelection* iSourceSelection;
    RaatTransport* iTransport;
    Bwh iSerialNumber;
    Bwh iSoftwareVersion;
    Bwh iConfigUrl;
    TBool iStarted;
};

}
}
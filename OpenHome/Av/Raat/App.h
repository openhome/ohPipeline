#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>

#include <uv.h>
#include <raat_device.h>
#include <raat_info.h>

// RAAT's bundled libuv has uv_thread_* renamed to raat_uv_thread_* unconditionally, on every
// platform it publishes for - not just where Qobuz Connect is also compiled in - to avoid
// colliding with Qobuz Connect's own, unrenamed bundled libuv wherever the two are linked
// together. The rename is applied to raat's vendored external/uv source itself with no
// platform guard, so every published raat package uses the renamed names, even on platforms
// that never compile in Qobuz Connect at all (confirmed via a Windows-x86 release build:
// 'uv_thread_create'/'uv_thread_t' not found there either).
typedef raat_uv_thread_t OhRaatUvThreadT;
#define OhRaatUvThreadCreate raat_uv_thread_create
#define OhRaatUvThreadJoin raat_uv_thread_join

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
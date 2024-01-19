#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>
#include <Linn/Diagnostic.h>

#include <uv.h>
#include <raat_device.h> 
#include <raat_info.h> 

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
        const Brx& aConfigUrl,
        Linn::DiagnosticManager& aDiagnosticManager);
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
    uv_thread_t iThread;
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
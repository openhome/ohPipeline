#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>

#include <raat_device.h> 
#include <raat_info.h> 

namespace OpenHome {
    class Environment;
    class Timer;
namespace Av {

    class IMediaPlayer;
    class ISourceRaat;
    class IRaatReader;
    class IRaatTime;
    class IRaatSignalPathObservable;
    class RaatOutput;
    class RaatVolume;
    class RaatSourceSelection;
    class RaatTransport;
    
class RaatApp
{
public:
    RaatApp(
        Environment& aEnv,
        IMediaPlayer& aMediaPlayer,
        ISourceRaat& aSourceRaat,
        IRaatTime& aRaatTime,
        IRaatSignalPathObservable& aSignalPathObservable,
        const Brx& aSerialNumber,
        const Brx& aSoftwareVersion);
    ~RaatApp();
    IRaatReader& Reader();
private:
    void RaatThread();
    void FriendlyNameChanged(const Brx& aName);
    void StartPlugins();
private:
    IMediaPlayer& iMediaPlayer;
    ThreadFunctor* iThread;
    Timer* iTimer;
    RAAT__Device* iDevice;
    RAAT__Info* iInfo;
    RaatOutput* iOutput;
    RaatVolume* iVolume;
    RaatSourceSelection* iSourceSelection;
    RaatTransport* iTransport;
    TUint iFriendlyNameId;
    Bwh iSerialNumber;
    Bwh iSoftwareVersion;
};

}
}
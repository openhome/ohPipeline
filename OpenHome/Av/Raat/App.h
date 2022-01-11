#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>

#include <raat_device.h> 
#include <raat_info.h> 

namespace OpenHome {
    class Environment;
namespace Av {

    class IMediaPlayer;
    class ISourceRaat;
    class IRaatReader;
    class RaatOutput;
    class RaatVolume;
    class RaatSourceSelection;
    
class RaatApp
{
public:
    RaatApp(
        Environment& aEnv,
        IMediaPlayer& aMediaPlayer,
        ISourceRaat& aSourceRaat,
        const Brx& aSerialNumber,
        const Brx& aSoftwareVersion);
    ~RaatApp();
    IRaatReader& Reader();
private:
    void RaatThread();
    void FriendlyNameChanged(const Brx& aName);
private:
    IMediaPlayer& iMediaPlayer;
    ThreadFunctor* iThread;
    RAAT__Device* iDevice;
    RAAT__Info* iInfo;
    RaatOutput* iOutput;
    RaatVolume* iVolume;
    RaatSourceSelection* iSourceSelection;
    TUint iFriendlyNameId;
    Bwh iSerialNumber;
    Bwh iSoftwareVersion;
};

}
}
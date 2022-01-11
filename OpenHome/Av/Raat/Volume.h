#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Av/VolumeManager.h>
#include <OpenHome/Media/MuteManager.h>

#include <rc_status.h>
#include <raat_plugin_volume.h>

namespace OpenHome {
    class IThreadPoolHandle;
    namespace  Configuration {
        class ConfigNum;
    }
namespace Av {

class RaatVolume;

typedef struct {
    RAAT__VolumePlugin iPlugin; // must be first member
    RaatVolume* iSelf;
} RaatVolumePluginExt;

class IMediaPlayer;

class RaatVolume : private IVolumeObserver, private Media::IMuteObserver
{
public:
    RaatVolume(IMediaPlayer& aMediaPlayer);
    ~RaatVolume();
    RAAT__VolumePlugin* Plugin();
    void AddStateListener(RAAT__VolumeStateCallback aCb, void *aCbUserdata);
    void RemoveStateListener(RAAT__VolumeStateCallback aCb, void *aCbUserdata);
    void GetState(RAAT__VolumeState *aState);
    RC__Status SetVolume(TUint aVolume);
    void SetMute(TBool aValue);
    void ToggleMute();
private: // from IVolumeObserver
    void VolumeChanged(const IVolumeValue& aVolume) override;
private: // from Media::IMuteObserver
    void MuteChanged(TBool aValue) override;
private:
    void LimitChanged(Configuration::ConfigNum::KvpNum& aKvp);
    void NotifyChange();
private:
    RaatVolumePluginExt iPluginExt;
    RAAT__VolumeStateListeners iListeners;
    IVolumeManager& iVolumeManager;
    IThreadPoolHandle* iHandleNotify;
    std::atomic<TUint> iVolume;
    std::atomic<TUint> iVolumeLimit;
    std::atomic<TBool> iMute;
    Configuration::ConfigNum& iConfigLimit;
    TUint iSubscriberIdLimit;
};

}
}

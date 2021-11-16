#include <OpenHome/Av/Raat/Volume.h>
#include <OpenHome/Types.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Av/VolumeManager.h>
#include <OpenHome/Media/MuteManager.h>
#include <OpenHome/Configuration/ConfigManager.h>

#include <raat_plugin_volume.h>
#include <rc_allocator.h>
#include <rc_status.h>


static inline OpenHome::Av::RaatVolume* Volume(void *self)
{
    auto ext = reinterpret_cast<OpenHome::Av::RaatVolumePluginExt*>(self);
    return ext->iSelf;
}

extern "C"
static RC__Status Raat_Volume_Get_Info(void * /*self*/, json_t **out_info)
{
    *out_info = nullptr;
    return RC__STATUS_SUCCESS;
}

extern "C"
RC__Status Raat_Volume_Add_State_Listener(void *self, RAAT__VolumeStateCallback cb, void *cb_userdata)
{
    Volume(self)->AddStateListener(cb, cb_userdata);
    return RC__STATUS_SUCCESS;
}

extern "C"
RC__Status Raat_Volume_Remove_State_Listener(void *self, RAAT__VolumeStateCallback cb, void *cb_userdata)
{
    Volume(self)->RemoveStateListener(cb, cb_userdata);
    return RC__STATUS_SUCCESS;
}

extern "C"
RC__Status Raat_Volume_Get_State(void *self, RAAT__VolumeState *out_state)
{
    Volume(self)->GetState(out_state);
    return RC__STATUS_SUCCESS;
}

extern "C"
RC__Status Raat_Volume_Set_Volume(void *self, double volume_value)
{
    Volume(self)->SetVolume(volume_value);
    return RC__STATUS_SUCCESS;
}

extern "C"
RC__Status Raat_Volume_Increment_Volume(void * /*self*/, RAAT__VolumeIncrement /*increment*/)
{
    return RC__STATUS_NOT_IMPLEMENTED;
}

extern "C"
RC__Status Raat_Volume_Set_Mute(void *self, bool mute_value)
{
    Volume(self)->SetMute(mute_value);
    return RC__STATUS_SUCCESS;
}

extern "C"
RC__Status Raat_Volume_Toggle_Mute(void *self)
{
    Volume(self)->ToggleMute();
    return RC__STATUS_SUCCESS;
}


using namespace OpenHome;
using namespace OpenHome::Av;

// RaatVolume

RaatVolume::RaatVolume(IMediaPlayer& aMediaPlayer)
    : iVolumeManager(aMediaPlayer.VolumeManager())
    , iConfigLimit(aMediaPlayer.ConfigManager().GetNum(VolumeConfig::kKeyLimit))
{
    auto ret = RAAT__volume_state_listeners_init(&iListeners, RC__allocator_malloc());
    ASSERT(ret == RC__STATUS_SUCCESS);

    (void)memset(&iPluginExt, 0, sizeof iPluginExt);
    iPluginExt.iPlugin.get_info = Raat_Volume_Get_Info;
    iPluginExt.iPlugin.add_state_listener = Raat_Volume_Add_State_Listener;
    iPluginExt.iPlugin.remove_state_listener = Raat_Volume_Remove_State_Listener;
    iPluginExt.iPlugin.get_state = Raat_Volume_Get_State;
    iPluginExt.iPlugin.set_volume = Raat_Volume_Set_Volume;
    iPluginExt.iPlugin.increment_volume = Raat_Volume_Increment_Volume;
    iPluginExt.iPlugin.set_mute = Raat_Volume_Set_Mute;
    iPluginExt.iPlugin.toggle_mute = Raat_Volume_Toggle_Mute;
    iPluginExt.iSelf = this;

    iHandleNotify = aMediaPlayer.ThreadPool().CreateHandle(
        MakeFunctor(*this, &RaatVolume::NotifyChange),
        "RaatVolume",
        ThreadPoolPriority::Medium);

    iVolumeManager.AddVolumeObserver(*this);
    iVolumeManager.AddMuteObserver(*this);
    iSubscriberIdLimit = iConfigLimit.Subscribe(MakeFunctorConfigNum(*this, &RaatVolume::LimitChanged));
}

RaatVolume::~RaatVolume()
{
    iConfigLimit.Unsubscribe(iSubscriberIdLimit);
    iHandleNotify->Destroy();
    RAAT__volume_state_listeners_destroy(&iListeners);
}

RAAT__VolumePlugin* RaatVolume::Plugin()
{
    return (RAAT__VolumePlugin*)&iPluginExt;
}

void RaatVolume::AddStateListener(RAAT__VolumeStateCallback aCb, void *aCbUserdata)
{
    (void)RAAT__volume_state_listeners_add(&iListeners, aCb, aCbUserdata);
}

void RaatVolume::RemoveStateListener(RAAT__VolumeStateCallback aCb, void *aCbUserdata)
{
    (void)RAAT__volume_state_listeners_remove(&iListeners, aCb, aCbUserdata);
}

void RaatVolume::GetState(RAAT__VolumeState *aState)
{
    (void)memset(aState, 0, sizeof *aState);
    aState->volume_type = RAAT__VOLUME_TYPE_NUMBER;
    aState->min_volume = 0.0;
    aState->max_volume = iVolumeLimit.load();
    aState->volume_value = iVolume.load();
    aState->mute_value = iMute.load();
    aState->volume_step = 1.0;
    aState->db_min_volume = 0.0;
    aState->db_max_volume = 0.0;
}

RC__Status RaatVolume::SetVolume(TUint  aVolume)
{
    RC__Status ret = RC__STATUS_SUCCESS;
    try {
        iVolumeManager.SetVolume(aVolume);
    }
    catch (VolumeNotSupported&) {
        ret = RAAT__VOLUME_PLUGIN_STATUS_VOLUME_NOT_SUPPORTED;
    }
    catch (VolumeOutOfRange&) {
        ret = RAAT__VOLUME_PLUGIN_STATUS_VOLUME_NOT_SUPPORTED;
    }
    return ret;
}

void RaatVolume::SetMute(TBool aValue)
{
    if (aValue) {
        iVolumeManager.Mute();
    }
    else {
        iVolumeManager.Unmute();
    }
}

void RaatVolume::ToggleMute()
{
    if (iMute.load()) {
        iVolumeManager.Unmute();
    }
    else {
        iVolumeManager.Mute();
    }
}

void RaatVolume::VolumeChanged(const IVolumeValue& aVolume)
{
    iVolume.store(aVolume.VolumeUser());
    (void)iHandleNotify->TrySchedule();
}

void RaatVolume::MuteChanged(TBool aValue)
{
    iMute.store(aValue);
    (void)iHandleNotify->TrySchedule();
}

void RaatVolume::LimitChanged(Configuration::ConfigNum::KvpNum& aKvp)
{
    iVolumeLimit.store(aKvp.Value());
    (void)iHandleNotify->TrySchedule();
}

void RaatVolume::NotifyChange()
{
    RAAT__VolumeState state;
    GetState(&state);
    (void)RAAT__volume_state_listeners_invoke(&iListeners, &state);
}

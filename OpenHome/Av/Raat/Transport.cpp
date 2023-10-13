#include <OpenHome/Av/Raat/Transport.h>
#include <OpenHome/Av/Raat/Plugin.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Media/PipelineManager.h>

#include <atomic>

#include <rc_status.h>
#include <raat_plugin_transport.h>
#include <jansson.h>


static inline OpenHome::Av::RaatTransport* Transport(void *self)
{
    auto ext = reinterpret_cast<OpenHome::Av::RaatTransportPluginExt*>(self);
    return ext->iSelf;
}

extern "C" {

RC__Status Raat_RaatTransport_Get_Info(void * /*self*/, json_t **out_info)
{
    *out_info = nullptr;
    return RC__STATUS_SUCCESS;
}

RC__Status Raat_RaatTransport_Add_Control_Listener(void *self, RAAT__TransportControlCallback cb, void *cb_userdata)
{
    Transport(self)->AddControlListener(cb, cb_userdata);
    return RC__STATUS_SUCCESS;
}

RC__Status Raat_RaatTransport_Remove_Control_Listener(void *self, RAAT__TransportControlCallback cb, void *cb_userdata)
{
    Transport(self)->RemoveControlListener(cb, cb_userdata);
    return RC__STATUS_SUCCESS;
}

RC__Status Raat_RaatTransport_Update_Status(void *self, json_t *status)
{
    Transport(self)->UpdateStatus(status);
    return RC__STATUS_SUCCESS;
}

RC__Status Raat_RaatTransport_Update_Artwork(void *self, const char *mime_type, void *data, size_t data_len)
{
    Transport(self)->UpdateArtwork(mime_type, data, data_len);
    return RC__STATUS_SUCCESS;
}

}


using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;

// RaatTransportStatusParser

const Brn RaatTransportStatusParser::kLoopDisabled("disabled");
const Brn RaatTransportStatusParser::kLoopEnabled("loop");
const Brn RaatTransportStatusParser::kLoopOneEnabled("loopone");
const Brn RaatTransportStatusParser::kStatePlaying("playing");
const Brn RaatTransportStatusParser::kStateLoading("loading");
const Brn RaatTransportStatusParser::kStatePaused("paused");
const Brn RaatTransportStatusParser::kStateStopped("stopped");

const std::map<Brn, RaatTrackInfo::EState, BufferCmp> RaatTransportStatusParser::kTrackStateMap = {
    { kStatePlaying, RaatTrackInfo::EState::ePlaying },
    { kStateLoading, RaatTrackInfo::EState::eLoading },
    { kStatePaused, RaatTrackInfo::EState::ePaused },
    { kStateStopped, RaatTrackInfo::EState::eStopped }
};

const std::map<Brn, RaatTransportInfo::ERepeatMode, BufferCmp> RaatTransportStatusParser::kRepeatModeMap = {
    { kLoopDisabled, RaatTransportInfo::ERepeatMode::eOff },
    { kLoopEnabled, RaatTransportInfo::ERepeatMode::eRepeat },
    { kLoopOneEnabled, RaatTransportInfo::ERepeatMode::eRepeatOne }
};

void RaatTransportStatusParser::Parse(
    json_t*             aJson,
    RaatTransportInfo&  aTransportInfo,
    RaatTrackInfo&      aTrackInfo)
{
    /*
        { 
            "loop":    "disabled" | "loop" | "loopone",
            "shuffle": true | false,
            "state":   "playing" | "loading" | "paused" | "stopped",
            "seek":    seek position | null,

            "is_previous_allowed": true | false,        NOTE: is_*_allowed were introduced in Roon 1.3
            "is_next_allowed":     true | false,        NOTE: is_*_allowed were introduced in Roon 1.3
            "is_play_allowed":     true | false,        NOTE: is_*_allowed were introduced in Roon 1.3
            "is_pause_allowed":    true | false,        NOTE: is_*_allowed were introduced in Roon 1.3
            "is_seek_allowed":     true | false,        NOTE: is_*_allowed were introduced in Roon 1.3

            "now_playing": {                                            NOTE: this field is be omitted if nothing is playing
                "one_line":      "text for single line displays",

                "two_line_title":    "title for two line displays",
                "two_line_subtitle": "subtitle for two line displays"

                "three_line_title":       "title for three line displays" | null,        NOTE three_line_* were introduced in Roon 1.2
                "three_line_subtitle":    "subtitle for three line displays" | null,
                "three_line_subsubtitle": "subsubtitle for three line displays" | null,

                "length":        length | null,

                "title":         NOTE: THIS IS DEPRECATED. DO NOT USE. YOU WILL FAIL CERTIFICATION.
                "album":         NOTE: THIS IS DEPRECATED. DO NOT USE. YOU WILL FAIL CERTIFICATION.
                "channel":       NOTE: THIS IS DEPRECATED. DO NOT USE. YOU WILL FAIL CERTIFICATION.
                "artist":        NOTE: THIS IS DEPRECATED. DO NOT USE. YOU WILL FAIL CERTIFICATION.
                "composer":      NOTE: THIS IS DEPRECATED. DO NOT USE. YOU WILL FAIL CERTIFICATION.
            }
            "stream_format": {                                          NOTE: this field is optional. please behave gracefully if it is not present. 
                "sample_type":     "dsd" | "pcm",                      NOTE: This information is for display purposes only. Do not allow it to influence audio playback.
                "sample_rate":     44100 | 48000 | ...,
                "bits_per_sample": 1 | 16 | 24 | 32,
                "channels":        1, 2, ...
            }
        }
    */

    // State
    Brn loop = Brn(ValueString(aJson, "loop"));
    const TBool shuffle = ValueBool(aJson, "shuffle");
    Brn state = Brn(ValueString(aJson, "state"));
    const TUint positionSecs = ValueUint(aJson, "seek");

    // Capabilities
    const TBool prevAllowed = ValueBool(aJson, "is_previous_allowed");
    const TBool nextAllowed = ValueBool(aJson, "is_next_allowed");
    const TBool pauseAllowed = ValueBool(aJson, "is_pause_allowed");
    const TBool seekAllowed = ValueBool(aJson, "is_seek_allowed");

    // Metadata
    auto* metadata = json_object_get(aJson, "now_playing");
    Brn title = Brn(ValueString(metadata, "three_line_title"));
    Brn subtitle = Brn(ValueString(metadata, "three_line_subtitle"));
    Brn subsubtitle = Brn(ValueString(metadata, "three_line_subsubtitle"));
    TUint durationSecs = ValueUint(metadata, "length");

    aTransportInfo.SetPrevSupported(prevAllowed);
    aTransportInfo.SetNextSupported(nextAllowed);
    aTransportInfo.SetPauseSupported(pauseAllowed);
    aTransportInfo.SetSeekSupported(seekAllowed);
    aTransportInfo.SetShuffle(shuffle);
    auto itRepeat = kRepeatModeMap.find(loop);
    if (itRepeat == kRepeatModeMap.end()) {
        THROW(RaatTransportStatusParserError);
    }
    aTransportInfo.SetRepeat(itRepeat->second);

    aTrackInfo.SetTitle(title);
    aTrackInfo.SetSubtitle(subtitle);
    aTrackInfo.SetSubSubtitle(subsubtitle);
    aTrackInfo.SetDurationSecs(durationSecs);
    aTrackInfo.SetPositionSecs(positionSecs);
    auto itState = kTrackStateMap.find(state);
    if (itState == kTrackStateMap.end()) {
        THROW(RaatTransportStatusParserError);
    }
    aTrackInfo.SetState(itState->second);
}

const TChar* RaatTransportStatusParser::ValueString(json_t* aObject, const TChar* aKey)
{
    json_t* kvp = json_object_get(aObject, aKey);
    if (kvp == nullptr) {
        return "";
    }
    const TChar* val = json_string_value(kvp);
    if (val == nullptr) {
        return "";
    }
    return val;
}

TBool RaatTransportStatusParser::ValueBool(json_t* aObject, const TChar* aKey)
{
    json_t* kvp = json_object_get(aObject, aKey);
    if (kvp == nullptr) {
        return false;
    }
    return json_is_true(kvp);
}

TUint RaatTransportStatusParser::ValueUint(json_t* aObject, const TChar* aKey)
{
    json_t* kvp = json_object_get(aObject, aKey);
    if (kvp == nullptr) {
        return 0;
    }
    return (TUint)json_integer_value(kvp);
}

// RaatRepeatRandomAdapter

RaatRepeatRandomAdapter::RaatRepeatRandomAdapter(
    ITransportRepeatRandom& aTransportRepeatRandom,
    IRaatRepeatRandomInvoker& aRaatRepeatRandom)

    : iTransportRepeatRandom(aTransportRepeatRandom)
    , iRaatRepeatRandom(aRaatRepeatRandom)
    , iLinnRepeatEnabled(false)
    , iRaatRepeatMode(RaatTransportInfo::ERepeatMode::eOff)
    , iRandomEnabled(false)
    , iActive(false)
    , iLock("RRPT")
{
    iTransportRepeatRandom.AddObserver(*this, "RaatTransport");
}

RaatRepeatRandomAdapter::~RaatRepeatRandomAdapter()
{
    iTransportRepeatRandom.RemoveObserver(*this);
}

void RaatRepeatRandomAdapter::SetActive(TBool aActive)
{
    {
        AutoMutex _(iLock);
        iActive = aActive;
    }
    if (aActive) {
        iTransportRepeatRandom.SetRepeat(iLinnRepeatEnabled);
        iTransportRepeatRandom.SetRandom(iRandomEnabled);
    }
}

void RaatRepeatRandomAdapter::RaatRepeatChanged(RaatTransportInfo::ERepeatMode aMode)
{
    TBool active = false;
    TBool changed = false;
    TBool repeatEnabled = false;
    TBool isSynced = false;
    {
        AutoMutex _(iLock);
        active = iActive;
        repeatEnabled = (aMode != RaatTransportInfo::ERepeatMode::eOff);
        if (!iLinnRepeatChangePending) {
            if (iLinnRepeatEnabled != repeatEnabled) {
                iLinnRepeatEnabled = repeatEnabled;
                changed = true;
            }
            iRaatRepeatMode = aMode;
        }
        if (iRaatRepeatMode == aMode) {
            iLinnRepeatChangePending = false;
            isSynced = true;
        }
    }

    if(!active) {
        return;
    }
    if (changed) {
        iTransportRepeatRandom.SetRepeat(repeatEnabled);
    }
    if (!isSynced) {
        iRaatRepeatRandom.ToggleRepeat();
    }
}

void RaatRepeatRandomAdapter::RaatRandomChanged(TBool aRandom)
{
    if (DoSetRandom(aRandom)) {
        iTransportRepeatRandom.SetRandom(aRandom);
    }
}

void RaatRepeatRandomAdapter::TransportRepeatChanged(TBool aRepeat)
{
    {
        AutoMutex _(iLock);
        if (!iActive) {
            return;
        }
        if (aRepeat == iLinnRepeatEnabled) {
            return;
        }
        iLinnRepeatEnabled = aRepeat;
        iRaatRepeatMode = iLinnRepeatEnabled ? RaatTransportInfo::ERepeatMode::eRepeat : RaatTransportInfo::ERepeatMode::eOff;
        iLinnRepeatChangePending = true;
    }
    iRaatRepeatRandom.ToggleRepeat();
}

void RaatRepeatRandomAdapter::TransportRandomChanged(TBool aRandom)
{
    if (DoSetRandom(aRandom)) {
        iRaatRepeatRandom.ToggleRandom();
    }
}

TBool RaatRepeatRandomAdapter::DoSetRandom(TBool aRandom)
{
    AutoMutex _(iLock);
    if (aRandom == iRandomEnabled) {
        return false;
    }

    iRandomEnabled = aRandom;
    return iActive;
}


// RaatTransport

RaatTransport::RaatTransport(IMediaPlayer& aMediaPlayer)
    : iRaatRepeatRandomAdapter(
        aMediaPlayer.TransportRepeatRandom(),
        *this)
    , iArtworkServer(aMediaPlayer.Env())
    , iMetadataHandler(
        aMediaPlayer.Pipeline().AsyncTrackObserver(),
        *(aMediaPlayer.Env().InfoAggregator()),
        iArtworkServer)
    , iState(RaatTrackInfo::EState::eUndefined)
    , iLockStatus("RTTR")
{
    auto ret = RAAT__transport_control_listeners_init(&iListeners, RC__allocator_malloc());
    ASSERT(ret == RC__STATUS_SUCCESS);

    (void)memset(&iPluginExt, 0, sizeof iPluginExt);
    iPluginExt.iPlugin.get_info = Raat_RaatTransport_Get_Info;
    iPluginExt.iPlugin.add_control_listener = Raat_RaatTransport_Add_Control_Listener;
    iPluginExt.iPlugin.remove_control_listener = Raat_RaatTransport_Remove_Control_Listener;
    iPluginExt.iPlugin.update_status = Raat_RaatTransport_Update_Status;
    iPluginExt.iPlugin.update_artwork = Raat_RaatTransport_Update_Artwork;
    iPluginExt.iSelf = this;
}

RaatTransport::~RaatTransport()
{
    RAAT__transport_control_listeners_destroy(&iListeners);
}

RAAT__TransportPlugin* RaatTransport::Plugin()
{
    return (RAAT__TransportPlugin*)&iPluginExt;
}

void RaatTransport::AddControlListener(RAAT__TransportControlCallback aCb, void *aCbUserdata)
{
    RAAT__transport_control_listeners_add(&iListeners, aCb, aCbUserdata);
}

void RaatTransport::RemoveControlListener(RAAT__TransportControlCallback aCb, void *aCbUserdata)
{
    RAAT__transport_control_listeners_remove(&iListeners, aCb, aCbUserdata);
}

void RaatTransport::UpdateStatus(json_t *aStatus)
{
    TBool randomChanged = false;
    TBool repeatChanged = false;
    {
        AutoMutex _(iLockStatus);

        RaatTransportInfo transportInfo;
        RaatTrackInfo trackInfo;
        RaatTransportStatusParser::Parse(aStatus, transportInfo, trackInfo);

        randomChanged = (iState == RaatTrackInfo::EState::eUndefined || (iTransportInfo.Shuffle() != transportInfo.Shuffle()));
        repeatChanged = (iState == RaatTrackInfo::EState::eUndefined || (iTransportInfo.RepeatMode() != transportInfo.RepeatMode()));

        iTransportInfo.Set(transportInfo);
        iMetadataHandler.TrackInfoChanged(trackInfo);
        if (iState != trackInfo.GetState()) {
            iState = trackInfo.GetState();
        }
    }

    if (randomChanged) {
        iRaatRepeatRandomAdapter.RaatRandomChanged(iTransportInfo.Shuffle());
    }
    else if (repeatChanged) {
        iRaatRepeatRandomAdapter.RaatRepeatChanged(iTransportInfo.RepeatMode());
    }
}

void RaatTransport::UpdateArtwork(const char *mime_type, void *data, size_t data_len)
{
    if (data == nullptr) {
        iArtworkServer.ClearArtwork();
        return;
    }

    Brn artwork((TByte*)data, (TUint)data_len);
    iArtworkServer.SetArtwork(artwork, Brn(mime_type));
}

void RaatTransport::DoReportState(const TChar* aState)
{
    json_t* ctrl = json_object();
    json_object_set_new(ctrl, "button", json_string(aState));
    RAAT__transport_control_listeners_invoke(&iListeners, ctrl);
}

void RaatTransport::Play()
{
    DoReportState("play");
}

void RaatTransport::TryPause()
{
    AutoMutex _(iLockStatus);
    if (iTransportInfo.PauseSupported()) {
        DoReportState("pause");
    }
    else {
        Stop();
    }
}

void RaatTransport::Stop()
{
    DoReportState("stop");
}

void RaatTransport::TryMoveNext()
{
    AutoMutex _(iLockStatus);
    if (iTransportInfo.NextSupported()) {
        DoReportState("next");
    }
}

void RaatTransport::TryMovePrev()
{
    AutoMutex _(iLockStatus);
    if (iTransportInfo.PrevSupported()) {
        DoReportState("previous");
    }
}

void RaatTransport::RaatSourceActivated()
{
    iRaatRepeatRandomAdapter.SetActive(true);
}

void RaatTransport::RaatSourceDeactivated()
{
    iRaatRepeatRandomAdapter.SetActive(false);
    DoReportState("pause");
}

void RaatTransport::ToggleRepeat()
{
    DoReportState("toggleloop");
}

void RaatTransport::ToggleRandom()
{
    DoReportState("toggleshuffle");
}

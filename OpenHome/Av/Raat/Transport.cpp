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

// RaatTransportRepeatAdapter

RaatTransportRepeatAdapter::RaatTransportRepeatAdapter(
    ITransportRepeatRandom& aTransportRepeatRandom,
    IRaatRepeatToggler&     aRepeatToggler)

    : iTransportRepeatRandom(aTransportRepeatRandom)
    , iRepeatToggler(aRepeatToggler)
    , iLinnRepeat(false)
    , iRaatRepeat(RaatTransportInfo::ERepeatMode::eOff)
    , iLock("RRPT")
{
}

void RaatTransportRepeatAdapter::RaatRepeatChanged(RaatTransportInfo::ERepeatMode aMode)
{
    TBool changed = false;
    TBool repeatEnabled = false;
    TBool isSynced = false;
    {
        AutoMutex _(iLock);
        repeatEnabled = (aMode != RaatTransportInfo::ERepeatMode::eOff);
        if (!iLinnRepeatChangePending) {
            if (iLinnRepeat != repeatEnabled) {
                iLinnRepeat = repeatEnabled;
                changed = true;
            }
            iRaatRepeat = aMode;
        }
        if (iRaatRepeat == aMode) {
            if (iLinnRepeatChangePending) {
                iLinnRepeatChangePending = false;
            }
            isSynced = true;
        }
    }

    if (changed) {
        iTransportRepeatRandom.SetRepeat(repeatEnabled);
    }
    if (!isSynced) {
        iRepeatToggler.ToggleRepeat();
    }
}

void RaatTransportRepeatAdapter::LinnRepeatChanged(TBool aRepeat)
{
    {
        AutoMutex _(iLock);
        iLinnRepeat = aRepeat;
        iRaatRepeat = iLinnRepeat ? RaatTransportInfo::ERepeatMode::eRepeat : RaatTransportInfo::ERepeatMode::eOff;
        iLinnRepeatChangePending = true;
    }
    iRepeatToggler.ToggleRepeat();
}

TBool RaatTransportRepeatAdapter::RepeatEnabled() const
{
    AutoMutex _(iLock);
    return iLinnRepeat;
}


// RaatTransport

RaatTransport::RaatTransport(IMediaPlayer& aMediaPlayer, IRaatTransportStateObserver& aStateObserver)
    : iTransportRepeatRandom(aMediaPlayer.TransportRepeatRandom())
    , iRepeatAdapter(iTransportRepeatRandom, *this)
    , iStateObserver(aStateObserver)
    , iMetadataHandler(
        aMediaPlayer.Pipeline().AsyncTrackReporter(),
        *(aMediaPlayer.Env().InfoAggregator()))
    , iActive(false)
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
    iPluginExt.iSelf = this;

    iTransportRepeatRandom.AddObserver(*this, "RaatTransport");
}

RaatTransport::~RaatTransport()
{
    iTransportRepeatRandom.RemoveObserver(*this);
    RAAT__transport_control_listeners_destroy(&iListeners);
}

RAAT__TransportPlugin* RaatTransport::Plugin()
{
    return (RAAT__TransportPlugin*)&iPluginExt;
}

void RaatTransport::AddControlListener(RAAT__TransportControlCallback aCb, void *aCbUserdata)
{
    Log::Print("RaatTransport::AddControlListener(cb, %p)\n", aCbUserdata);
    RAAT__transport_control_listeners_add(&iListeners, aCb, aCbUserdata);
}

void RaatTransport::RemoveControlListener(RAAT__TransportControlCallback aCb, void *aCbUserdata)
{
    RAAT__transport_control_listeners_remove(&iListeners, aCb, aCbUserdata);
}

void RaatTransport::UpdateStatus(json_t *aStatus)
{
    TBool stateChanged = false;
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
            stateChanged = true;
        }
    }

    if (stateChanged) {
        iStateObserver.TransportStateChanged(iState);
    }
    if (randomChanged) {
        iTransportRepeatRandom.SetRandom(iTransportInfo.Shuffle());
    }
    if (repeatChanged) {
        iRepeatAdapter.RaatRepeatChanged(iTransportInfo.RepeatMode());
    }
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

TBool RaatTransport::CanPause()
{
    AutoMutex _(iLockStatus);
    if (!iTransportInfo.PauseSupported()) {
        return false;
    }
    DoReportState("pause");
    return true;
}

void RaatTransport::Stop()
{
    DoReportState("stop");
}

TBool RaatTransport::CanMoveNext()
{
    AutoMutex _(iLockStatus);
    if (!iTransportInfo.NextSupported()) {
        return false;
    }
    DoReportState("next");
    return true;
}

TBool RaatTransport::CanMovePrev()
{
    AutoMutex _(iLockStatus);
    if (!iTransportInfo.PrevSupported()) {
        return false;
    }
    DoReportState("previous");
    return true;
}

void RaatTransport::RaatSourceActivated()
{
    AutoMutex _(iLockStatus);
    iActive = true;
}

void RaatTransport::RaatSourceDeactivated()
{
    AutoMutex _(iLockStatus);
    iActive = false;
    DoReportState("stop");
}

void RaatTransport::ToggleRepeat()
{
    AutoMutex _(iLockStatus);
    DoReportState("toggleloop");
}

void RaatTransport::TransportRepeatChanged(TBool aRepeat)
{
    TBool changed = false;
    {
        AutoMutex _(iLockStatus);
        if (iActive && iRepeatAdapter.RepeatEnabled() != aRepeat) {
            changed = true;
        }
    }
    if (changed) {
        iRepeatAdapter.LinnRepeatChanged(aRepeat);
    }
}

void RaatTransport::TransportRandomChanged(TBool aRandom)
{
    AutoMutex _(iLockStatus);
    if (iActive && iTransportInfo.Shuffle() != aRandom) {
        DoReportState("toggleshuffle");
    }
}

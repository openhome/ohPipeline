#include <OpenHome/Av/Raat/Transport.h>
#include <OpenHome/Av/Raat/Plugin.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Av/MediaPlayer.h>

#include <atomic>

#include <rc_status.h>
#include <raat_plugin_transport.h>
#include <jansson.h>


static inline OpenHome::Av::RaatTransport* Transport(void *self)
{
    auto ext = reinterpret_cast<OpenHome::Av::RaatTransportPluginExt*>(self);
    return ext->iSelf;
}

extern "C"
RC__Status Raat_RaatTransport_Get_Info(void * /*self*/, json_t **out_info)
{
    *out_info = nullptr;
    return RC__STATUS_SUCCESS;
}

extern "C"
RC__Status Raat_RaatTransport_Add_Control_Listener(void *self, RAAT__TransportControlCallback cb, void *cb_userdata)
{
    Transport(self)->AddControlListener(cb, cb_userdata);
    return RC__STATUS_SUCCESS;
}

extern "C"
RC__Status Raat_RaatTransport_Remove_Control_Listener(void *self, RAAT__TransportControlCallback cb, void *cb_userdata)
{
    Transport(self)->RemoveControlListener(cb, cb_userdata);
    return RC__STATUS_SUCCESS;
}

extern "C"
RC__Status Raat_RaatTransport_Update_Status(void *self, json_t *status)
{
    Transport(self)->UpdateStatus(status);
    return RC__STATUS_SUCCESS;
}


using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;


RaatTransport::RaatTransport(IMediaPlayer& aMediaPlayer, IRaatMetadataObserver& aMetadataObserver)
    : iTransportRepeatRandom(aMediaPlayer.TransportRepeatRandom())
    , iMetadataObserver(aMetadataObserver)
    , iActive(false)
    , iStarted(false)
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
    /*
        {
            "now_playing": {
                "length": 231,
                "composer": "Larry Mullen, Jr. / Adam Clayton / Bono / The Edge",
                "title": "One",
                "two_line_subtitle": "Johnny Cash",
                "album": "American III: Solitary Man",
                "two_line_title": "One",
                "three_line_subsubtitle": "American III: Solitary Man",
                "three_line_title": "One",
                "three_line_subtitle": "Johnny Cash",
                "artist": "Johnny Cash",
                "one_line": "One - Johnny Cash"
            },
            "is_next_allowed": true,
            "shuffle": false,
            "state": "paused",
            "seek": 36,
            "is_previous_allowed": true,
            "is_seek_allowed": true,
            "is_play_allowed": true,
            "is_pause_allowed": false,
            "loop": "disabled"
        }
    */
//    Log::Print("RaatTransport::UpdateStatus - %s\n\n", json_dumps(aStatus, 0)); // FIXME - leaks
    iTrackCapabilities.SetPauseSupported(ValueBool(aStatus, "is_next_allowed"));
    iTrackCapabilities.SetNextSupported(ValueBool(aStatus, "is_next_allowed"));
    iTrackCapabilities.SetPrevSupported(ValueBool(aStatus, "is_previous_allowed"));
    iTrackCapabilities.SetSeekSupported(ValueBool(aStatus, "is_seek_allowed"));
    const TBool shuffle = ValueBool(aStatus, "shuffle");
    TBool report = (!iStarted || iTrackCapabilities.Shuffle() != shuffle);
    iTrackCapabilities.SetShuffle(shuffle);
    if (report) {
        iTransportRepeatRandom.SetRandom(shuffle);
    }
    static const Brn kRepeatOff("disabled");
    Brn loopCurrent = Brn(ValueString(aStatus, "loop"));
    const TBool repeat = loopCurrent == kRepeatOff;
    report = (!iStarted || iTrackCapabilities.Repeat() != repeat);
    iTrackCapabilities.SetRepeat(repeat);
    if (report) {
        iTransportRepeatRandom.SetRepeat(shuffle);
    }
    const TUint posSeconds = ValueUint(aStatus, "seek");
    iStarted = true;

    json_t* nowPlaying = json_object_get(aStatus, "now_playing");
    const char* title = ValueString(nowPlaying, "two_line_title");
    const char* subtitle = ValueString(nowPlaying, "two_line_subtitle");
    Brn titleBuf(title);
    Brn subtitleBuf(subtitle);
    const TUint durationSeconds = ValueUint(nowPlaying, "length");
//    Log::Print("RaatTransport::UpdateStatus - %s, %s, %u\n", title, subtitle, posSeconds);
    iMetadataObserver.MetadataChanged(titleBuf, subtitleBuf, posSeconds, durationSeconds);
}

const TChar* RaatTransport::ValueString(json_t* aObject, const TChar* aKey)
{ // static
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

TBool RaatTransport::ValueBool(json_t* aObject, const TChar* aKey)
{ // static
    json_t* kvp = json_object_get(aObject, aKey);
    if (kvp == nullptr) {
        return false;
    }
    return json_is_true(kvp);
}

TUint RaatTransport::ValueUint(json_t* aObject, const TChar* aKey)
{ // static
    json_t* kvp = json_object_get(aObject, aKey);
    if (kvp == nullptr) {
        return 0;
    }
    return (TUint)json_integer_value(kvp);
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
    if (!iTrackCapabilities.PauseSupported()) {
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
    if (!iTrackCapabilities.NextSupported()) {
        return false;
    }
    DoReportState("next");
    return true;
}

TBool RaatTransport::CanMovePrev()
{
    if (!iTrackCapabilities.PrevSupported()) {
        return false;
    }
    DoReportState("previous");
    return true;
}

void RaatTransport::RaatSourceActivated()
{
    iActive.store(true);
}

void RaatTransport::RaatSourceDectivated()
{
    iActive.store(false);
}

void RaatTransport::TransportRepeatChanged(TBool aRepeat)
{
    if (iActive.load() && iTrackCapabilities.Repeat() != aRepeat) {
        DoReportState("toggleloop");
    }
}

void RaatTransport::TransportRandomChanged(TBool aRandom)
{
    if (iActive.load() && iTrackCapabilities.Shuffle() != aRandom) {
        DoReportState("toggleshuffle");
    }
}

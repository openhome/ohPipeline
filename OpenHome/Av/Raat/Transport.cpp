#include <OpenHome/Av/Raat/Transport.h>
#include <OpenHome/Av/Raat/Plugin.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Functor.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Av/OhMetadata.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Media/PipelineObserver.h>

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


RaatTransport::RaatTransport(IMediaPlayer& aMediaPlayer)
    : RaatPluginAsync(aMediaPlayer.ThreadPool())
{
    auto ret = RAAT__transport_control_listeners_init(&iListeners, RC__allocator_malloc());
    ASSERT(ret == RC__STATUS_SUCCESS);

    (void)memset(&iPluginExt, 0, sizeof iPluginExt);
    iPluginExt.iPlugin.get_info = Raat_RaatTransport_Get_Info;
    iPluginExt.iPlugin.add_control_listener = Raat_RaatTransport_Add_Control_Listener;
    iPluginExt.iPlugin.remove_control_listener = Raat_RaatTransport_Remove_Control_Listener;
    iPluginExt.iPlugin.update_status = Raat_RaatTransport_Update_Status;
    iPluginExt.iSelf = this;

    aMediaPlayer.Pipeline().AddObserver(*this);
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
    TryReportState();
}

void RaatTransport::RemoveControlListener(RAAT__TransportControlCallback aCb, void *aCbUserdata)
{
    RAAT__transport_control_listeners_remove(&iListeners, aCb, aCbUserdata);
}

void RaatTransport::UpdateStatus(json_t *aStatus)
{
    iDidlLite.SetBytes(0);
    WriterBuffer w(iDidlLite);
    WriterDIDLLite writer(Brn(""), DIDLLite::kItemTypeAudioItem, w);
    json_t* nowPlaying = json_object_get(aStatus, "now_playing");
    const char* title = json_string_value(json_object_get(nowPlaying, "two_line_title"));
    const char* subTitle = json_string_value(json_object_get(nowPlaying, "two_line_subtitle"));
    writer.WriteTitle(Brn(title));
    writer.WriteArtist(Brn(subTitle));
    writer.WriteEnd();
    Log::Print("RaatTransport::UpdateStatus - %.*s\n", PBUF(iDidlLite));

// FIXME - pass to output module / protocol
}

void RaatTransport::ReportState()
{
    // FIXME - report TransportState to Roon
    const char* buttonType = nullptr;
    {
        AutoMutex _(iLock);
        switch (iTransportState)
        {
        case EPipelinePlaying:
            buttonType = "play";
            break;
        case EPipelinePaused:
        case EPipelineStopped:
        case EPipelineWaiting:
            buttonType = "pause";
            break;
        case EPipelineBuffering:
            // don't set buttonType => no update to Roon transport controls
            break;
        }
    }
    if (buttonType != nullptr) {
        json_t* ctrl = json_object();
        json_object_set_new(ctrl, "button", json_string(buttonType));
        RAAT__transport_control_listeners_invoke(&iListeners, ctrl);
    }
}

void RaatTransport::NotifyPipelineState(EPipelineState aState)
{
    {
        AutoMutex _(iLock);
        iTransportState = aState;
    }
    TryReportState();
}

void RaatTransport::NotifyMode(
    const Brx& /*aMode*/,
    const ModeInfo& /*aInfo*/,
    const ModeTransportControls& /*aTransportControls*/)
{
}

void RaatTransport::NotifyTrack(Track& /*aTrack*/, TBool /*aStartOfStream*/)
{
}

void RaatTransport::NotifyMetaText(const Brx& /*aText*/)
{
}

void RaatTransport::NotifyTime(TUint /*aSeconds*/)
{
}

void RaatTransport::NotifyStreamInfo(const DecodedStreamInfo& /*aStreamInfo*/)
{
}

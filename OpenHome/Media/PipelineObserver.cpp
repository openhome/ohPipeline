#include <OpenHome/Media/PipelineObserver.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Private/Printer.h>
#include <map>

using namespace OpenHome;
using namespace OpenHome::Media;

// TransportState

const TChar* TransportState::FromPipelineState(EPipelineState aState)
{ // static
    const TChar* state;
    switch (aState)
    {
    case EPipelinePlaying:
        state = "Playing";
        break;
    case EPipelinePaused:
        state = "Paused";
        break;
    case EPipelineStopped:
        state = "Stopped";
        break;
    case EPipelineBuffering:
        state = "Buffering";
        break;
    case EPipelineWaiting:
        state = "Waiting";
        break;
    default:
        ASSERTS();
        state = "";
        break;
    }
    return state;
}

    
// NullPipelineObserver

void NullPipelineObserver::NotifyPipelineState(EPipelineState /*aState*/)
{
}

void NullPipelineObserver::NotifyMode(const Brx& /*aMode*/,
                                      const ModeInfo& /*aInfo*/,
                                      const ModeTransportControls& /*aTransportControls*/)
{
}

void NullPipelineObserver::NotifyTrack(Track& /*aTrack*/, TBool /*aStartOfStream*/)
{
}

void NullPipelineObserver::NotifyMetaText(const Brx& /*aText*/)
{
}

void NullPipelineObserver::NotifyTime(TUint /*aSeconds*/)
{
}


void NullPipelineObserver::NotifyStreamInfo(const DecodedStreamInfo& /*aStreamInfo*/)
{
}


// LoggingPipelineObserver

LoggingPipelineObserver::LoggingPipelineObserver()
    : iEnable(true)
    , iDurationSeconds(0) // NotifyTime may be called before NotifyStreamInfo during startup
{
}

void LoggingPipelineObserver::Enable(TBool aEnable)
{
    iEnable = aEnable;
}

void LoggingPipelineObserver::NotifyPipelineState(EPipelineState aState)
{
    if (!iEnable) {
        return;
    }
    const char* state = "";
    switch (aState)
    {
    case EPipelinePlaying:
        state = "playing";
        break;
    case EPipelinePaused:
        state = "paused";
        break;
    case EPipelineStopped:
        state = "stopped";
        break;
    case EPipelineBuffering:
        state = "buffering";
        break;
    case EPipelineWaiting:
        state = "waiting";
        break;
    default:
        ASSERTS();
    }
    Log::Print("Pipeline state change: %s\n", state);
}

void LoggingPipelineObserver::NotifyMode(const Brx& aMode,
                                         const ModeInfo& aInfo,
                                         const ModeTransportControls& /*aTransportControls*/)
{
    if (!iEnable) {
        return;
    }
    Log::Print("Pipeline report property: MODE {mode=%.*s; latencyMode=%u; supportsNext=%u; supportsPrev=%u}\n",
               PBUF(aMode), aInfo.LatencyMode(), aInfo.SupportsNext(), aInfo.SupportsPrev());
}

void LoggingPipelineObserver::NotifyTrack(Track& aTrack, TBool aStartOfStream)
{
    if (!iEnable) {
        return;
    }
    Log::Print("Pipeline report property: TRACK {uri=%.*s; trackId=%u; startOfStream=%u}\n",
               PBUF(aTrack.Uri()), aTrack.Id(), aStartOfStream);
}

void LoggingPipelineObserver::NotifyMetaText(const Brx& aText)
{
    if (!iEnable) {
        return;
    }
    Log::Print("Pipeline report property: METATEXT {%.*s}\n", PBUF(aText));
}

void LoggingPipelineObserver::NotifyTime(TUint aSeconds)
{
    if (!iEnable) {
        return;
    }
    Log::Print("Pipeline report property: TIME {secs=%u; duration=%u}\n", aSeconds, iDurationSeconds);
}

void LoggingPipelineObserver::NotifyStreamInfo(const DecodedStreamInfo& aStreamInfo)
{
    if (!iEnable) {
        return;
    }
    iDurationSeconds = (TUint)(aStreamInfo.TrackLength() / Jiffies::kPerSecond);
    Log::Print("Pipeline report property: FORMAT {bitRate=%u; bitDepth=%u, sampleRate=%u, numChannels=%u, codec=%.*s; trackLength=%llx, lossless=%u, channelConfig=%s}\n",
               aStreamInfo.BitRate(), aStreamInfo.BitDepth(), aStreamInfo.SampleRate(), aStreamInfo.NumChannels(),
               PBUF(aStreamInfo.CodecName()), aStreamInfo.TrackLength(), aStreamInfo.Lossless(), aStreamInfo.Profile().ToString());
}


// NullPipelineObservable

void NullPipelineObservable::AddObserver(IPipelineObserver& /*aObserver*/)
{
}

void NullPipelineObservable::RemoveObserver(IPipelineObserver& /*aObserver*/)
{
}

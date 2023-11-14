#include <OpenHome/Media/Pipeline/Logger.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Pipeline/Msg.h>

using namespace OpenHome;
using namespace OpenHome::Media;

#undef LOG_METADATA /* Track metadata and stream metatext are huge.  They tend to
                       drown out all other logging and reporting them slows pipeline
                       progress to a crawl. */

//  Logger

Logger::Logger(IPipelineElementUpstream& aUpstreamElement, const TChar* aId)
    : iUpstreamElement(&aUpstreamElement)
    , iDownstreamElement(nullptr)
    , iId(aId)
    , iEnabled(false)
    , iFilter(EMsgNone)
    , iShutdownSem("PDSD", 0)
    , iJiffiesPcm(0)
    , iJiffiesDsd(0)
    , iJiffiesSilence(0)
    , iJiffiesPlayable(0)
{
}

Logger::Logger(const TChar* aId, IPipelineElementDownstream& aDownstreamElement)
    : iUpstreamElement(nullptr)
    , iDownstreamElement(&aDownstreamElement)
    , iId(aId)
    , iEnabled(false)
    , iFilter(EMsgNone)
    , iShutdownSem("PDSD", 0)
    , iJiffiesPcm(0)
    , iJiffiesDsd(0)
    , iJiffiesSilence(0)
    , iJiffiesPlayable(0)
{
}

Logger::~Logger()
{
    if (iEnabled) {
        iShutdownSem.Wait();
    }
}

void Logger::SetEnabled(TBool aEnabled)
{
    iEnabled = aEnabled;
}

void Logger::SetFilter(TUint aMsgTypes)
{
    iFilter = aMsgTypes;
}

Msg* Logger::Pull()
{
    Msg* msg = iUpstreamElement->Pull();
    if (iEnabled && msg != nullptr) {
        (void)msg->Process(*this);
    }
    return msg;
}

void Logger::Push(Msg* aMsg)
{
    if (iEnabled) {
        (void)aMsg->Process(*this);
    }
    iDownstreamElement->Push(aMsg);
}

inline TBool Logger::IsEnabled(EMsgType aType) const
{
    if (iEnabled && (iFilter & aType) == aType) {
        return true;
    }
    return false;
}

inline TUint64 JiffiesToMs(TUint64 aJiffies)
{
    return aJiffies / Jiffies::kPerMs;
}

void Logger::LogAudio()
{
    Log::Print("Logger (%s): pcm=%llu (%llums), dsd=%llu(%llums), silence=%llu (%llums), playable=%llu (%llums)\n",
        iId, iJiffiesPcm, JiffiesToMs(iJiffiesPcm),
        iJiffiesDsd, JiffiesToMs(iJiffiesDsd),
        iJiffiesSilence, JiffiesToMs(iJiffiesSilence),
        iJiffiesPlayable, JiffiesToMs(iJiffiesPlayable));
}

Msg* Logger::ProcessMsg(MsgMode* aMsg)
{
    if (IsEnabled(EMsgMode)) {
        iBuf.SetBytes(0);
        iBuf.AppendPrintf("Pipeline (%s): mode {mode:", iId);
        iBuf.Append(aMsg->Mode());
        const ModeInfo& info = aMsg->Info();
        iBuf.AppendPrintf(", latencyMode: %u, supportsNext: %u, supportsPrev: %u}\n",
                          info.LatencyMode(), info.SupportsNext(), info.SupportsPrev());
        Log::Print(iBuf);
    }
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgTrack* aMsg)
{
    if (IsEnabled(EMsgTrack)) {
        iBuf.SetBytes(0);
        iBuf.AppendPrintf("Pipeline (%s): track {uri:", iId);
        iBuf.Append(aMsg->Track().Uri());
        iBuf.Append(", metaData: ");
#ifdef LOG_METADATA
        iBuf.Append(aMsg->Track().MetaData());
#else
        iBuf.Append("(omitted)");
#endif
        iBuf.AppendPrintf(", id: %u, startOfStream: %u}\n", aMsg->Track().Id(), aMsg->StartOfStream());
        Log::Print(iBuf);
    }
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgDrain* aMsg)
{
    if (IsEnabled(EMsgDrain)) {
        Log::Print("Pipeline (%s): drain %u\n", iId, aMsg->Id());
    }
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgDelay* aMsg)
{
    if (IsEnabled(EMsgDelay)) {
        const TUint remaining = aMsg->RemainingJiffies();
        const TUint total = aMsg->TotalJiffies();
        Log::Print("Pipeline (%s): remaining {%ums (%u jiffies)}, total {%ums (%u jiffies)}\n",
                   iId, Jiffies::ToMs(remaining), remaining, Jiffies::ToMs(total), total);
    }
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgEncodedStream* aMsg)
{
    if (IsEnabled(EMsgEncodedStream)) {
        iBuf.SetBytes(0);
        iBuf.AppendPrintf("Pipeline (%s): encodedStream {", iId);
        iBuf.Append(aMsg->Uri());
        iBuf.Append(", metaText: ");
#ifdef LOG_METADATA
        iBuf.Append(aMsg->MetaText());
#else
        iBuf.Append("(omitted)");
#endif
        iBuf.AppendPrintf(" , totalBytes: %llu, streamId: %u, seekable: %u, live: %u}\n",
                          aMsg->TotalBytes(), aMsg->StreamId(), aMsg->Seekable(), aMsg->Live());
        Log::Print(iBuf);
    }
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgStreamSegment* aMsg)
{
    if (IsEnabled(EMsgStreamSegment)) {
        Log::Print("Pipeline (%s): streamSegment {%.*s}\n", iId, PBUF(aMsg->Id()));
    }
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgAudioEncoded* aMsg)
{
    if (IsEnabled(EMsgAudioEncoded)) {
        Log::Print("Pipeline (%s): audioEncoded {bytes: %u}\n", iId, aMsg->Bytes());
    }
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgMetaText* aMsg)
{
    if (IsEnabled(EMsgMetaText)) {
        iBuf.SetBytes(0);
        iBuf.AppendPrintf("Pipeline (%s): metaText {", iId);
        iBuf.Append(aMsg->MetaText());
        iBuf.AppendPrintf("}\n");
        Log::Print(iBuf);
    }
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgStreamInterrupted* aMsg)
{
    if (IsEnabled(EMsgStreamInterrupted)) {
        Log::Print("Pipeline (%s): changeInput\n", iId);
    }
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgHalt* aMsg)
{
    if (IsEnabled(EMsgHalt)) {
        Log::Print("Pipeline (%s): halt { id: %u }\n", iId, aMsg->Id());
    }
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgFlush* aMsg)
{
    if (IsEnabled(EMsgFlush)) {
        Log::Print("Pipeline (%s): flush { id: %u }\n", iId, aMsg->Id());
    }
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgWait* aMsg)
{
    if (IsEnabled(EMsgWait)) {
        Log::Print("Pipeline (%s): wait\n", iId);
    }
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgDecodedStream* aMsg)
{
    if (IsEnabled(EMsgDecodedStream)) {
        const DecodedStreamInfo& stream = aMsg->StreamInfo();
        iBuf.SetBytes(0);
        iBuf.AppendPrintf("Pipeline (%s): decodedStream {streamId: %u, bitRate: %u, bitDepth: %u, sampleRate: %u, codec: ",
                           iId, stream.StreamId(), stream.BitRate(), stream.BitDepth(), stream.SampleRate());
        iBuf.Append(stream.CodecName());
        iBuf.AppendPrintf(", trackLength: %llu, sampleStart: %llu, lossless: %u, seekable: %u, live: %u}\n",
                          stream.TrackLength(), stream.SampleStart(), stream.Lossless(), stream.Seekable(), stream.Live());
        Log::Print(iBuf);
    }
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgBitRate* aMsg)
{
    if (IsEnabled(EMsgBitRate)) {
        Log::Print("Pipeline (%s): bitRate {%u}\n", iId, aMsg->BitRate());
    }
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgAudioPcm* aMsg)
{
    if (aMsg->HasBufferObserver()) {
        iJiffiesPcm += aMsg->Jiffies();
    }
    if (IsEnabled(EMsgAudioPcm) ||
        (IsEnabled(EMsgAudioRamped) && aMsg->Ramp().IsEnabled())) {
        LogAudioDecoded(*aMsg, "audioPcm");
    }
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgAudioDsd* aMsg)
{
    if (aMsg->HasBufferObserver()) {
        iJiffiesDsd += aMsg->Jiffies();
    }
    if (IsEnabled(EMsgAudioDsd) ||
        (IsEnabled(EMsgAudioRamped) && aMsg->Ramp().IsEnabled())) {
        LogAudioDecoded(*aMsg, "audioDsd");
    }
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgSilence* aMsg)
{
    if (aMsg->HasBufferObserver()) {
        iJiffiesSilence += aMsg->Jiffies();
    }
    if (IsEnabled(EMsgSilence) ||
        (IsEnabled(EMsgAudioRamped) && aMsg->Ramp().IsEnabled())) {
        iBuf.SetBytes(0);
        iBuf.AppendPrintf("Pipeline (%s): silence {jiffies: %u", iId, aMsg->Jiffies());
        LogRamp(aMsg->Ramp());
        iBuf.Append("}\n");
        Log::Print(iBuf);
    }
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgPlayable* aMsg)
{
    if (aMsg->HasBufferObserver()) {
        iJiffiesPlayable += aMsg->Jiffies();
    }
    if (IsEnabled(EMsgPlayable) ||
        (IsEnabled(EMsgAudioRamped) && aMsg->Ramp().IsEnabled())) {
        iBuf.SetBytes(0);
        iBuf.AppendPrintf("Pipeline (%s): playable {bytes: %u", iId, aMsg->Bytes());
        LogRamp(aMsg->Ramp());
        iBuf.Append("}\n");
        Log::Print(iBuf);
    }
    return aMsg;
}

Msg* Logger::ProcessMsg(MsgQuit* aMsg)
{
    if (IsEnabled(EMsgQuit)) {
        Log::Print("Pipeline (%s): quit\n", iId);
    }
    iShutdownSem.Signal();
    return aMsg;
}

void Logger::LogAudioDecoded(MsgAudioDecoded& aAudio, const TChar* aType)
{
    iBuf.SetBytes(0);
    iBuf.AppendPrintf("Pipeline (%s): %s {track offset: %llu, jiffies: %u", iId, aType, aAudio.TrackOffset(), aAudio.Jiffies());
    LogRamp(aAudio.Ramp());
    iBuf.Append("}\n");
    Log::Print(iBuf);
}

void Logger::LogRamp(const Media::Ramp& aRamp)
{
    if (aRamp.IsEnabled()) {
        iBuf.AppendPrintf(", ramp: [%08x..%08x]", aRamp.Start(), aRamp.End());
    }
}

#include <OpenHome/Types.h>
#include <OpenHome/Media/Pipeline/SpotifyReporter.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/ThreadPool.h>

#include <limits>

using namespace OpenHome;
using namespace OpenHome::Media;


// SpotifyDidlLiteWriter

SpotifyDidlLiteWriter::SpotifyDidlLiteWriter(const Brx& aUri, const ISpotifyMetadata& aMetadata)
    : iUri(aUri)
    , iMetadata(aMetadata)
{
}

void SpotifyDidlLiteWriter::Write(IWriter& aWriter, TUint aBitDepth, TUint aChannels, TUint aSampleRate) const
{
    WriterAscii writer(aWriter);
    writer.Write(Brn("<DIDL-Lite xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "));
    writer.Write(Brn("xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\" "));
    writer.Write(Brn("xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\">"));
    writer.Write(Brn("<item id=\"0\" parentID=\"0\" restricted=\"True\">"));

    writer.Write(Brn("<dc:title>"));
    Converter::ToXmlEscaped(writer, iMetadata.Track());
    writer.Write(Brn("</dc:title>"));

    writer.Write(Brn("<dc:creator>"));
    Converter::ToXmlEscaped(writer, iMetadata.Artist());
    writer.Write(Brn("</dc:creator>"));

    writer.Write(Brn("<upnp:artist role='AlbumArtist'>"));
    Converter::ToXmlEscaped(writer, iMetadata.Artist());
    writer.Write(Brn("</upnp:artist>"));

    writer.Write(Brn("<upnp:album>"));
    Converter::ToXmlEscaped(writer, iMetadata.Album());
    writer.Write(Brn("</upnp:album>"));

    writer.Write(Brn("<upnp:albumArtURI>"));
    Converter::ToXmlEscaped(writer, iMetadata.AlbumCoverUrl());
    writer.Write(Brn("</upnp:albumArtURI>"));

    WriteRes(writer, aBitDepth, aChannels, aSampleRate);

    writer.Write(Brn("<upnp:class>object.item.audioItem.musicTrack</upnp:class></item></DIDL-Lite>"));
}

void SpotifyDidlLiteWriter::SetDurationString(Bwx& aBuf) const
{
    // H+:MM:SS[.F0/F1]
    const TUint msPerSecond = 1000;
    const TUint msPerMinute = msPerSecond*60;
    const TUint msPerHour = msPerMinute*60;

    TUint timeRemaining = iMetadata.DurationMs();
    const TUint hours = iMetadata.DurationMs()/msPerHour;
    timeRemaining -= hours*msPerHour;

    const TUint minutes = timeRemaining/msPerMinute;
    timeRemaining -= minutes*msPerMinute;

    const TUint seconds = timeRemaining/msPerSecond;
    timeRemaining -= seconds*msPerSecond;

    const TUint milliseconds = timeRemaining;

    ASSERT(hours <= 99);
    if (hours < 10) {
        aBuf.Append('0');
    }
    Ascii::AppendDec(aBuf, hours);
    aBuf.Append(':');

    ASSERT(minutes <= 59);
    if (minutes < 10) {
        aBuf.Append('0');
    }
    Ascii::AppendDec(aBuf, minutes);
    aBuf.Append(':');

    ASSERT(seconds <= 60);
    if (seconds < 10) {
        aBuf.Append('0');
    }
    Ascii::AppendDec(aBuf, seconds);

    if (milliseconds > 0) {
        aBuf.Append('.');
        Ascii::AppendDec(aBuf, milliseconds);
        aBuf.Append('/');
        Ascii::AppendDec(aBuf, msPerSecond);
    }
}

void SpotifyDidlLiteWriter::WriteRes(IWriter& aWriter, TUint aBitDepth, TUint aChannels, TUint aSampleRate) const
{
    WriterAscii writer(aWriter);

    Bws<kMaxDurationBytes> duration;
    SetDurationString(duration);
    writer.Write(Brn("<res"));
    writer.Write(Brn(" duration=\""));
    writer.Write(duration);
    writer.Write(Brn("\""));

    writer.Write(Brn(" protocolInfo=\""));
    writer.Write(Brn("spotify:*:audio/L16:*"));
    writer.Write(Brn("\""));

    WriteOptionalAttributes(writer, aBitDepth, aChannels, aSampleRate);

    writer.Write(Brn(">"));
    writer.Write(iUri);
    writer.Write(Brn("</res>"));
}

void SpotifyDidlLiteWriter::WriteOptionalAttributes(IWriter& aWriter, TUint aBitDepth, TUint aChannels, TUint aSampleRate) const
{
    WriterAscii writer(aWriter);

    if (aBitDepth != 0) {
        writer.Write(Brn(" bitsPerSample=\""));
        writer.WriteUint(aBitDepth);
        writer.Write(Brn("\""));
    }

    if (aSampleRate != 0) {
        writer.Write(Brn(" sampleFrequency=\""));
        writer.WriteUint(aSampleRate);
        writer.Write(Brn("\""));
    }

    if (aChannels != 0) {
        writer.Write(Brn(" nrAudioChannels=\""));
        writer.WriteUint(aChannels);
        writer.Write(Brn("\""));
    }

    if (aBitDepth != 0 && aChannels != 0 && aSampleRate != 0) {
        const TUint byteDepth = aBitDepth/8;
        const TUint bytesPerSec = byteDepth*aSampleRate*aChannels;
        const TUint bytesPerMs = bytesPerSec / 1000;
        const TUint totalBytes = iMetadata.DurationMs() * bytesPerMs;
        writer.Write(Brn(" size=\""));
        writer.WriteUint(totalBytes);
        writer.Write(Brn("\""));
    }
}


// StartOffset

StartOffset::StartOffset()
    : iOffsetMs(0)
{
}

void StartOffset::SetMs(TUint aOffsetMs)
{
    iOffsetMs = aOffsetMs;
}

TUint64 StartOffset::OffsetSample(TUint aSampleRate) const
{
    return (static_cast<TUint64>(iOffsetMs)*aSampleRate)/1000;
}

TUint StartOffset::OffsetMs() const
{
    return iOffsetMs;
}

TUint StartOffset::AbsoluteDiff(TUint aOffsetMs) const
{
    TUint offsetDiff = 0;
    if (iOffsetMs >= aOffsetMs) {
        offsetDiff = iOffsetMs - aOffsetMs;
    }
    else {
        offsetDiff = aOffsetMs - iOffsetMs;
    }
    return offsetDiff;
}


// SpotifyReporter

const TUint SpotifyReporter::kSupportedMsgTypes =   eMode
                                                  | eTrack
                                                  | eDrain
                                                  | eDelay
                                                  | eMetatext
                                                  | eStreamInterrupted
                                                  | eHalt
                                                  | eFlush
                                                  | eWait
                                                  | eDecodedStream
                                                  | eBitRate
                                                  | eAudioPcm
                                                  | eAudioDsd
                                                  | eSilence
                                                  | eQuit;

const TUint SpotifyReporter::kTrackOffsetChangeThresholdMs;
const Brn SpotifyReporter::kInterceptMode("Spotify");

SpotifyReporter::SpotifyReporter(
    IPipelineElementUpstream& aUpstreamElement,
    MsgFactory& aMsgFactory,
    TrackFactory& aTrackFactory,
    IThreadPool& aThreadPool,
    IInfoAggregator& aInfoAggregator
)
    : PipelineElement(kSupportedMsgTypes)
    , iUpstreamElement(aUpstreamElement)
    , iMsgFactory(aMsgFactory)
    , iTrackFactory(aTrackFactory)
    , iTrackDurationMs(0)
    , iMetadata(nullptr)
    , iMsgDecodedStreamPending(false)
    , iDecodedStream(nullptr)
    , iSubSamples(0)
    , iSubSamplesTrack(0)
    , iStreamId(kStreamIdInvalid)
    , iTrackDurationMsDecodedStream(0)
    , iInterceptMode(false)
    , iPipelineTrackSeen(false)
    , iGeneratedTrackPending(false)
    , iPendingFlushId(MsgFlush::kIdInvalid)
    , iPlaybackStartPending(false)
    , iPlaybackContinuePending(false)
    , iLock("SARL")
{
}

SpotifyReporter::~SpotifyReporter()
{
    AutoMutex _(iLock);
    if (iMetadata != nullptr) {
        iMetadata->RemoveReference();
    }
    if (iDecodedStream != nullptr) {
        iDecodedStream->RemoveRef();
    }
}

Msg* SpotifyReporter::Pull()
{
    Msg* msg = nullptr;
    while (msg == nullptr) {
        if (!iInterceptMode) {
            msg = iUpstreamElement.Pull();
            msg = msg->Process(*this);

            if (iInterceptMode) {
                // Mode changed. Need to set up some variables that are
                // accessed from different threads, so need to acquire iLock.
                AutoMutex amx(iLock);
                iMsgDecodedStreamPending = true;
                iSubSamples = 0;
                iSubSamplesTrack = 0;
                iStreamId = kStreamIdInvalid;
                iTrackDurationMsDecodedStream = 0;
            }
        }
        else {
            {
                /*
                 * iLock needs to be held for a subset of the checks below, and
                 * in certain msg->Process() calls.
                 *
                 * However, cannot hold iLock during a Pull() call to the
                 * upstream element, as it blocks when pipeline is not playing
                 * anything.
                 *
                 * So, must acquire iLock to perform some checking before
                 * decided whether to pull a message, release iLock while
                 * pulling a message from upstream, then re-acquire the iLock
                 * when processing the message.
                 *
                 * So, iLock will be acquired (at most) 2 times when on Spotify
                 * mode. However, iLock will never be acquired when not on a
                 * Spotify mode.
                 */
                AutoMutex _(iLock);

                // Don't output any generated MsgTrack or modified MsgDecodedStream
                // unless in Spotify mode, and seen a MsgTrack and MsgDecodedStream
                // arrive via pipeline.
                if (iPipelineTrackSeen && iDecodedStream != nullptr) {
                    // If new metadata is available, generate a new MsgTrack
                    // with that metadata.
                    if (iGeneratedTrackPending) {
                        iGeneratedTrackPending = false;
                        const DecodedStreamInfo& info = iDecodedStream->StreamInfo(); // iDecodedStream is checked in outer if block, so is not nullptr.
                        const TUint bitDepth = info.BitDepth();
                        const TUint channels = info.NumChannels();
                        const TUint sampleRate = info.SampleRate();

                        // Metadata should be available in most cases. However, don't delay track message if it isn't.
                        BwsTrackMetaData metadata;
                        if (iMetadata != nullptr) {
                            WriterBuffer writerBuffer(metadata);
                            SpotifyDidlLiteWriter metadataWriter(iTrackUri, iMetadata->Metadata());
                            metadataWriter.Write(writerBuffer, bitDepth, channels, sampleRate);
                            // Keep metadata cached here, in case pipeline restarts
                            // (e.g., source has switched away from Spotify and
                            // back again) but Spotify is still on same track, so
                            // hasn't evented out new metadata.
                        }

                        Track* track = iTrackFactory.CreateTrack(iTrackUri, metadata);
                        const TBool startOfStream = false;  // Report false as don't want downstream elements to re-enter any stream detection mode.
                        auto trackMsg = iMsgFactory.CreateMsgTrack(*track, startOfStream);
                        track->RemoveRef();
                        return trackMsg;
                    }
                    else if (iMsgDecodedStreamPending) {
                        // iDecodedStream is checked in outer if block, so is not nullptr.
                        iMsgDecodedStreamPending = false;
                        auto streamMsg = CreateMsgDecodedStreamLocked();
                        UpdateDecodedStream(*streamMsg);
                        return iDecodedStream;
                    }
                }
            }

            /*
             * Calling Pull() on upstream element may block for a long time,
             * e.g., when pipeline is not playing anything.
             *
             * If lock was held during that time, it would cause the pipeline
             * to lock up if a component to the left of the pipeline tried to
             * call SubSamples(), TrackChanged() or NotifySeek().
             */
            msg = iUpstreamElement.Pull();

            {
                /*
                 * Re-acquire iLock, as certain ProcessMsg() calls will alter
                 * protected members when Spotify mode is active.
                 */
                AutoMutex _(iLock);
                msg = msg->Process(*this);
            }
        }
    }
    return msg;
}

void SpotifyReporter::AddSpotifyPlaybackObserver(ISpotifyPlaybackObserver& aObserver)
{
    AutoMutex amx(iLock);
    iPlaybackObservers.push_back(aObserver);
}

TUint64 SpotifyReporter::SubSamples() const
{
    AutoMutex amx(iLock);
    return iSubSamples;
}

void SpotifyReporter::GetPlaybackPosMs(TUint& aStreamId, TUint& aPos)
{
    AutoMutex amx(iLock);
    aStreamId = iStreamId;
    aPos = GetPlaybackPosMsLocked();
}

void SpotifyReporter::Flush(TUint aFlushId)
{
    AutoMutex amx(iLock);
    iPendingFlushId = aFlushId;
    iPlaybackContinuePending = true; // Notify observers on seeing subsequent audio that playback has continued (e.g., if this flush followed a seek). This will be overridden if a new stream starts (e.g., if this flush followed a next/prev call).
}

void SpotifyReporter::MetadataChanged(Media::ISpotifyMetadataAllocated* aMetadata)
{
    AutoMutex amx(iLock);
    // If there is already pending metadata, it's now invalid.
    if (iMetadata != nullptr) {
        iMetadata->RemoveReference();
        iMetadata = nullptr;
    }
    iMetadata = aMetadata;  // aMetadata may be nullptr.
    if (iMetadata != nullptr) {
        iTrackDurationMs = iMetadata->Metadata().DurationMs();
    }
    iGeneratedTrackPending = true; // Pick up new metadata.
    iMsgDecodedStreamPending = true;

    // If this metadata is being delivered as part of a track change, any start offset (be it zero or non-zero) will be updated via call to ::TrackOffsetChanged(). ::TrackOffsetChanged() will also be called if a seek occurred.

    // If this metadata arrives mid-track (i.e., because retrieval of the new metadata has been delayed, or the metadata has actually changed mid-track) the start sample for the new MsgDecodedStream should already be (roughly) correct without any extra book-keeping, as long as calls to ::TrackPosition() are being made, which update iStartOffset to avoid any playback time sync issues.
}

void SpotifyReporter::TrackOffsetChanged(TUint aOffsetMs)
{
    AutoMutex _(iLock);
    // Must output new MsgDecodedStream to update start offset.
    iMsgDecodedStreamPending = true;
    iStartOffset.SetMs(aOffsetMs);
}

void SpotifyReporter::TrackPosition(TUint aPositionMs)
{
    AutoMutex _(iLock);
    const TUint offsetDiffAbs = iStartOffset.AbsoluteDiff(aPositionMs);
    if (offsetDiffAbs > kTrackOffsetChangeThresholdMs) {
        // Must output new MsgDecodedStream to update start offset.
        iMsgDecodedStreamPending = true;
    }
    iStartOffset.SetMs(aPositionMs);
}

//void SpotifyReporter::FlushTrackState()
//{
//    AutoMutex _(iLock);
//    iTrackUri.SetBytes(0);
//    if (iMetadata != nullptr) {
//        iMetadata->Destroy();
//        iMetadata = nullptr;
//    }
//    iStartOffset.SetMs(0);
//    iTrackDurationMs = 0;
//}

Msg* SpotifyReporter::ProcessMsg(MsgMode* aMsg)
{
    if (aMsg->Mode() == kInterceptMode) {

        // If iInterceptMode is already true, this must have been called with
        // lock held, so can safely reset internal members that require locking.
        if (iInterceptMode) {
            iMsgDecodedStreamPending = true;
            iSubSamples = 0;
            iSubSamplesTrack = 0;
            iTrackDurationMsDecodedStream = 0;
            iStreamId = kStreamIdInvalid;
        }

        iInterceptMode = true;

        ClearDecodedStream();
        iPipelineTrackSeen = false;
    }
    else {
        iInterceptMode = false;
    }

    return aMsg;
}

Msg* SpotifyReporter::ProcessMsg(MsgTrack* aMsg)
{
    if (!iInterceptMode) {
        return aMsg;
    }
    // iLock already held in ::Pull() method.
    iTrackUri.Replace(aMsg->Track().Uri()); // Cache URI for reuse in out-of-band MsgTracks.

    Parser p(iTrackUri);
    (void)p.Next(':');
    const auto streamIdBuf = p.Remaining();

    try {
        const auto streamId = Ascii::Uint(streamIdBuf);
        // iStreamId == kStreamIdInvalid immediately after seeing Spotify MsgMode, so won't report playback finished on first MsgTrack seen after Spotify MsgMode.
        if (iStreamId != kStreamIdInvalid) {
            const auto pos = GetPlaybackPosMsLocked();

            // iLock already held.
            for (auto& o : iPlaybackObservers) {
                o.get().NotifyPlaybackFinishedNaturally(iStreamId, pos); // Notify for previous valid stream ID.
            }
        }
        iStreamId = streamId;
    }
    catch (AsciiError&) {
        LOG_ERROR(kPipeline, "SpotifyReporter::ProcessMsg(MsgTrack*) Unable to parse stream ID from URI: %.*s\n", PBUF(iTrackUri));
    }

    iPipelineTrackSeen = true; // Only matters when in iInterceptMode. Ensures in-band MsgTrack is output before any are generated from out-of-band notifications.
    iGeneratedTrackPending = true;
    iPlaybackStartPending = true; // Spotify stream ID has almost certainly changed on every call to this.

    return aMsg;
}

Msg* SpotifyReporter::ProcessMsg(MsgDecodedStream* aMsg)
{
    if (!iInterceptMode) {
        return aMsg;
    }
    const DecodedStreamInfo& info = aMsg->StreamInfo();
    ASSERT(info.SampleRate() != 0);     // This is used as a divisor. Don't want a divide-by-zero error.
    ASSERT(info.NumChannels() != 0);

    // Clear any previous cached MsgDecodedStream and cache the one received.
    UpdateDecodedStream(*aMsg);

    aMsg->RemoveRef();  // UpdateDecodedStream() adds its own reference.
    iMsgDecodedStreamPending = true;    // Set flag so that a MsgDecodedStream with updated attributes is output in place of this.
    // Don't attempt to notify observers that stream started following this - that is handled while processing MsgTrack. Pipeline codecs may output multiple MsgDecodedStreams that do not correlate with Spotify streams.

    // Get start sample and update iSubSamplesTrack to reflect it, for track-based subsample tracking.
    // Do not do this for iSubSamples which tracks continuous PCM streams, as track offsets are tracked in a different way.
    const auto sampleStart = info.SampleStart();
    const auto subSampleStart = sampleStart * info.NumChannels();
    iSubSamplesTrack = subSampleStart;

    const auto samplesTotal = info.TrackLength() / Jiffies::PerSample(info.SampleRate());
    const auto msTotal = static_cast<TUint>((samplesTotal * 1000) / info.SampleRate());
    iTrackDurationMsDecodedStream = msTotal;

    // iLock already held.
    for (auto& o : iPlaybackObservers) {
        o.get().NotifyTrackLength(iStreamId, iTrackDurationMsDecodedStream);
    }

    return nullptr;
}

Msg* SpotifyReporter::ProcessMsg(MsgAudioPcm* aMsg)
{
    if (!iInterceptMode) {
        return aMsg;
    }

    if (iPlaybackStartPending) {
        // Start of audio from this stream (whether before or after a flush).
        iPlaybackStartPending = false;
        iPlaybackContinuePending = false; // We should never output audio for a track before iPlaybackStartPending flag is set, so ignore any seeks that happened prior to it.

        // iLock already held.
        for (auto& o : iPlaybackObservers) {
            o.get().NotifyPlaybackStarted(iStreamId);
        }
    }
    if (iPlaybackContinuePending) {
        // Audio other than the very start of stream after flush.
        iPlaybackContinuePending = false;

        // iLock already held.
        for (auto& o : iPlaybackObservers) {
            o.get().NotifyPlaybackContinued(iStreamId);
        }
    }

    ASSERT(iDecodedStream != nullptr);  // Can't receive audio until MsgDecodedStream seen.
    const DecodedStreamInfo& info = iDecodedStream->StreamInfo();
    TUint samples = aMsg->Jiffies()/Jiffies::PerSample(info.SampleRate());

    if (iPendingFlushId == MsgFlush::kIdInvalid) {
        // iLock held in ::Pull() method to protect iSubSamples.
        TUint64 subSamplesPrev = iSubSamples;
        iSubSamples += samples*info.NumChannels();
        ASSERT(iSubSamples >= subSamplesPrev); // Overflow not handled.

        TUint64 subSamplesTrackPrev = iSubSamplesTrack;
        iSubSamplesTrack += samples*info.NumChannels();
        ASSERT(iSubSamplesTrack >= subSamplesTrackPrev); // Overflow not handled.
    }
    return aMsg;
}

Msg* SpotifyReporter::ProcessMsg(MsgFlush* aMsg)
{
    if (!iInterceptMode) {
        return aMsg;
    }

    // iLock already held in ::Pull() method.
    if (aMsg->Id() >= iPendingFlushId) {
        iPendingFlushId = MsgFlush::kIdInvalid;
    }
    return aMsg;
}

void SpotifyReporter::ClearDecodedStream()
{
    if (iDecodedStream != nullptr) {
        iDecodedStream->RemoveRef();
        iDecodedStream = nullptr;
    }
}

void SpotifyReporter::UpdateDecodedStream(MsgDecodedStream& aMsg)
{
    ClearDecodedStream();
    iDecodedStream = &aMsg;
    iDecodedStream->AddRef();
}

TUint64 SpotifyReporter::TrackLengthJiffiesLocked() const
{
    ASSERT(iDecodedStream != nullptr);
    const DecodedStreamInfo& info = iDecodedStream->StreamInfo();
    const TUint64 trackLengthJiffies = (static_cast<TUint64>(iTrackDurationMs)*info.SampleRate()*Jiffies::PerSample(info.SampleRate()))/1000;
    return trackLengthJiffies;
}

MsgDecodedStream* SpotifyReporter::CreateMsgDecodedStreamLocked() const
{
    ASSERT(iDecodedStream != nullptr);
    const DecodedStreamInfo& info = iDecodedStream->StreamInfo();
    // Due to out-of-band track notification from Spotify, audio for current track was probably pushed into pipeline before track offset/duration was known, so use updated values here.
    const TUint64 trackLengthJiffies = TrackLengthJiffiesLocked();
    const TUint64 startOffset = iStartOffset.OffsetSample(info.SampleRate());
    MsgDecodedStream* msg =
        iMsgFactory.CreateMsgDecodedStream(info.StreamId(), info.BitRate(), info.BitDepth(),
                                           info.SampleRate(), info.NumChannels(), info.CodecName(),
                                           trackLengthJiffies, startOffset,
                                           info.Lossless(), info.Seekable(), info.Live(), info.AnalogBypass(),
                                           info.Format(), info.Multiroom(), info.Profile(), info.StreamHandler(),
                                           info.Ramp());
    return msg;
}

TUint SpotifyReporter::GetPlaybackPosMsLocked() const
{
    // Reports playback position for track-based (non-PCM) streams.
    if (iDecodedStream != nullptr) {
        const auto& info = iDecodedStream->StreamInfo();
        const auto samples = iSubSamplesTrack / info.NumChannels();
        const auto samplesScaled = samples * 1000;
        const auto ms = static_cast<TUint>(samplesScaled / info.SampleRate());
        // Log::Print("SpotifyReporter::GetPlaybackPosMsLocked iStreamId: %u, ms: %u (%u:%02u)\n", iStreamId, ms, (ms / 1000) / 60, (ms / 1000) % 60);
        return ms;
    }
    return 0;
}

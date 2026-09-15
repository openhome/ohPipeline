#include <OpenHome/Av/QobuzConnect/AudioStream.h>
#include <OpenHome/Av/QobuzConnect/MediaControl.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Debug-ohMediaPlayer.h>

#include <cstring>
#include <vector>

using namespace OpenHome;
using namespace OpenHome::Av;

// QobuzConnectStreamFormat

QobuzConnectStreamFormat::QobuzConnectStreamFormat()
    : iSampleRate(0)
    , iNumChannels(0)
    , iBitDepth(0)
    , iSdkFormat(QBZ_AUDIO_SAMPLE_FORMAT_SIGNED_16_BIT_LITTLE_ENDIAN)
    , iLock("QCSF")
{
}

void QobuzConnectStreamFormat::Set(const QbzAudioFormat& aFormat)
{
    AutoMutex _(iLock);
    iSampleRate = aFormat.sample_rate;
    iNumChannels = aFormat.channel_count;
    iSdkFormat = aFormat.sample_format;
    iBitDepth = (aFormat.sample_format == QBZ_AUDIO_SAMPLE_FORMAT_SIGNED_16_BIT_LITTLE_ENDIAN) ? 16 : 32;
}

TUint QobuzConnectStreamFormat::SampleRate() const
{
    AutoMutex _(iLock);
    return iSampleRate;
}

TUint QobuzConnectStreamFormat::NumChannels() const
{
    AutoMutex _(iLock);
    return iNumChannels;
}

TUint QobuzConnectStreamFormat::BitDepth() const
{
    AutoMutex _(iLock);
    return iBitDepth;
}

QbzAudioSampleFormat QobuzConnectStreamFormat::SdkFormat() const
{
    AutoMutex _(iLock);
    return iSdkFormat;
}


// QobuzConnectAudioStream

extern "C" {

static void QobuzConnectAudioStream_StreamStarted(QbzConnectCore* aCore, QbzAudioStreamId aStreamId, QbzAudioStreamProperties aProperties, uint64_t aInitialPositionMs, void* aUserData)
{
    OpenHome::Av::QobuzConnectAudioStream::StreamStartedCb(aCore, aStreamId, aProperties, aInitialPositionMs, aUserData);
}

static size_t QobuzConnectAudioStream_StreamData(QbzConnectCore* aCore, QbzAudioStreamId aStreamId, const uint8_t* aData, size_t aSize, void* aUserData)
{
    return OpenHome::Av::QobuzConnectAudioStream::StreamDataCb(aCore, aStreamId, aData, aSize, aUserData);
}

static void QobuzConnectAudioStream_StreamMetadata(QbzConnectCore* aCore, QbzAudioStreamId aStreamId, const QbzAudioMetadata* aMetadata, void* aUserData)
{
    OpenHome::Av::QobuzConnectAudioStream::StreamMetadataCb(aCore, aStreamId, aMetadata, aUserData);
}

static void QobuzConnectAudioStream_StreamFinished(QbzConnectCore* aCore, QbzAudioStreamId aStreamId, void* aUserData)
{
    OpenHome::Av::QobuzConnectAudioStream::StreamFinishedCb(aCore, aStreamId, aUserData);
}

static void QobuzConnectAudioStream_StreamSeeked(QbzConnectCore* aCore, QbzAudioStreamId aStreamId, uint64_t aPositionMs, void* aUserData)
{
    OpenHome::Av::QobuzConnectAudioStream::StreamSeekedCb(aCore, aStreamId, aPositionMs, aUserData);
}

static void QobuzConnectAudioStream_StreamDispose(QbzConnectCore* aCore, QbzAudioStreamId aStreamId, void* aUserData)
{
    OpenHome::Av::QobuzConnectAudioStream::StreamDisposeCb(aCore, aStreamId, aUserData);
}

} // extern "C"

QobuzConnectAudioStream::QobuzConnectAudioStream()
    : iCore(nullptr)
    , iMetadataObserver(nullptr)
    , iLock("QCAS")
    , iSemDataAvailable("QCAD", 0)
    , iBufferedBytes(0)
    , iActiveStreamId(0)
    , iResumeStreamId(0)
    , iReadingForStreamId(0)
    , iActiveStreamFinished(false)
    , iInterrupted(false)
    , iPendingCount(0)
{
}

QobuzConnectAudioStream::~QobuzConnectAudioStream()
{
    AutoMutex _(iLock);
    while (!iChunks.empty()) {
        delete iChunks.front();
        iChunks.pop_front();
    }
}

QbzAudioStreamDelegate QobuzConnectAudioStream::Delegate()
{
    QbzAudioStreamDelegate delegate;
    delegate.user_data = this;
    delegate.stream_started_callback = &QobuzConnectAudioStream_StreamStarted;
    delegate.stream_data_callback = &QobuzConnectAudioStream_StreamData;
    delegate.stream_metadata_callback = &QobuzConnectAudioStream_StreamMetadata;
    delegate.stream_finished_callback = &QobuzConnectAudioStream_StreamFinished;
    delegate.stream_seeked_callback = &QobuzConnectAudioStream_StreamSeeked;
    delegate.stream_dispose_callback = &QobuzConnectAudioStream_StreamDispose;
    return delegate;
}

void QobuzConnectAudioStream::SetCore(QbzConnectCore* aCore)
{
    AutoMutex _(iLock);
    iCore = aCore;
}

void QobuzConnectAudioStream::SetMetadataObserver(IQobuzConnectMetadataObserver& aObserver)
{
    AutoMutex _(iLock);
    iMetadataObserver = &aObserver;
}

const QobuzConnectStreamFormat& QobuzConnectAudioStream::StreamFormat()
{
    return iStreamFormat;
}

void QobuzConnectAudioStream::NotifyReading()
{
    AutoMutex _(iLock);
    iReadingForStreamId = iActiveStreamId;
}

void QobuzConnectAudioStream::Read(IQobuzConnectAudioWriter& aWriter)
{
    iSemDataAvailable.Wait();

    Bwh* chunk = nullptr;
    TBool stopped;
    {
        AutoMutex _(iLock);
        // iActiveStreamId != iReadingForStreamId (the stream this run of the read loop was told,
        // via NotifyReading(), it's reading for) is checked directly here rather than via a "did
        // something change since I started waiting" flag or a "is there any active stream at
        // all" check, because two things can happen between a dispose and this Read() call
        // actually running: HandleStreamData signals iSemDataAvailable once per chunk, so several
        // signals for the disposed stream's last few chunks can already be queued up (a
        // "changed since MY start" flag would only ever catch the first of the Read() calls those
        // wake, since each later call recaptures its baseline fresh, after the change already
        // happened); and the SDK's stream_started_callback for the NEXT stream can already have
        // run by the time any of those calls execute, moving iActiveStreamId straight from the
        // disposed id to a new nonzero one without ever visibly sitting at 0. Comparing against
        // the snapshot taken once when this read loop began catches both: any active-stream
        // change at all - to 0, or straight to a different id - no longer matches what this
        // particular run of the loop was told to read for.
        stopped = iInterrupted || (iActiveStreamId != iReadingForStreamId);
        if (!stopped) {
            if (!iChunks.empty()) {
                chunk = iChunks.front();
                iChunks.pop_front();
                iBufferedBytes -= chunk->Bytes();
            }
            else if (iActiveStreamFinished) {
                stopped = true;
            }
        }
    }

    if (stopped) {
        LOG(kQobuzConnect, "QobuzConnectAudioStream::Read: stream ended (interrupted/finished/disposed)\n");
        THROW(QobuzConnectAudioStreamStopped);
    }
    if (chunk == nullptr) {
        LOG(kQobuzConnect, "QobuzConnectAudioStream::Read: spurious wake, no chunk\n");
        return; // spurious wake (e.g. two Read() calls raced on the same signal) - caller loops back in
    }

    // TEMP diagnostic - see HandleStreamData above.
    LOG(kQobuzConnect, "QobuzConnectAudioStream::Read: delivering %u bytes to pipeline\n", chunk->Bytes());
    aWriter.Write(*chunk);
    delete chunk;

    QbzConnectCore* coreToResume = nullptr;
    QbzAudioStreamId resumeStreamId = 0;
    {
        AutoMutex _(iLock);
        if (iResumeStreamId != 0 && iBufferedBytes < kMaxBufferBytes) {
            coreToResume = iCore;
            resumeStreamId = iResumeStreamId;
            iResumeStreamId = 0;
        }
    }
    if (coreToResume != nullptr) {
        (void)qbz_connect_resume_audio_delivery(coreToResume, resumeStreamId);
    }
}

void QobuzConnectAudioStream::Interrupt()
{
    AutoMutex _(iLock);
    iInterrupted = true;
    iSemDataAvailable.Signal();
}

void QobuzConnectAudioStream::FlushForSeek()
{
    QbzConnectCore* coreToResume = nullptr;
    QbzAudioStreamId resumeStreamId = 0;
    {
        AutoMutex _(iLock);
        while (!iChunks.empty()) {
            delete iChunks.front();
            iChunks.pop_front();
        }
        iBufferedBytes = 0;
        iPendingCount = 0; // don't splice a leftover pre-seek partial frame onto post-seek bytes
        if (iResumeStreamId != 0) {
            coreToResume = iCore;
            resumeStreamId = iResumeStreamId;
            iResumeStreamId = 0;
        }
    }
    if (coreToResume != nullptr) {
        // We just freed up the whole buffer - if the stream had been paused due to backpressure,
        // tell it to carry on rather than leaving it stalled until (if ever) another Read() runs.
        (void)qbz_connect_resume_audio_delivery(coreToResume, resumeStreamId);
    }
}

void QobuzConnectAudioStream::StreamStartedCb(QbzConnectCore* /*aCore*/, QbzAudioStreamId aStreamId, QbzAudioStreamProperties aProperties, uint64_t /*aInitialPositionMs*/, void* aUserData)
{
    reinterpret_cast<QobuzConnectAudioStream*>(aUserData)->HandleStreamStarted(aStreamId, aProperties);
}

size_t QobuzConnectAudioStream::StreamDataCb(QbzConnectCore* /*aCore*/, QbzAudioStreamId aStreamId, const uint8_t* aData, size_t aSize, void* aUserData)
{
    return reinterpret_cast<QobuzConnectAudioStream*>(aUserData)->HandleStreamData(aStreamId, aData, aSize);
}

void QobuzConnectAudioStream::StreamMetadataCb(QbzConnectCore* /*aCore*/, QbzAudioStreamId /*aStreamId*/, const QbzAudioMetadata* aMetadata, void* aUserData)
{
    reinterpret_cast<QobuzConnectAudioStream*>(aUserData)->HandleStreamMetadata(aMetadata);
}

void QobuzConnectAudioStream::StreamFinishedCb(QbzConnectCore* /*aCore*/, QbzAudioStreamId aStreamId, void* aUserData)
{
    reinterpret_cast<QobuzConnectAudioStream*>(aUserData)->HandleStreamFinished(aStreamId);
}

void QobuzConnectAudioStream::StreamSeekedCb(QbzConnectCore* /*aCore*/, QbzAudioStreamId aStreamId, uint64_t aPositionMs, void* aUserData)
{
    reinterpret_cast<QobuzConnectAudioStream*>(aUserData)->HandleStreamSeeked(aStreamId, aPositionMs);
}

void QobuzConnectAudioStream::StreamDisposeCb(QbzConnectCore* /*aCore*/, QbzAudioStreamId aStreamId, void* aUserData)
{
    reinterpret_cast<QobuzConnectAudioStream*>(aUserData)->HandleStreamDispose(aStreamId);
}

void QobuzConnectAudioStream::HandleStreamStarted(QbzAudioStreamId aStreamId, const QbzAudioStreamProperties& aProperties)
{
    LOG(kQobuzConnect, "QobuzConnectAudioStream::HandleStreamStarted(%llu)\n", (unsigned long long)aStreamId);
    AutoMutex _(iLock);
    if (iActiveStreamId != 0 && iActiveStreamId != aStreamId) {
        // A genuinely concurrent stream (gapless/cross-fade, not supported in this v1) would
        // arrive while the old stream is still being read normally. But if the old stream was
        // abandoned locally (Interrupt() was called on it, e.g. Protocol::Interrupt() from a
        // Pipeline-level teardown) rather than actually finishing/being disposed by the SDK,
        // iActiveStreamId would otherwise stay stuck on the old id forever - the SDK has no way
        // to know we gave up on it, so it may never send stream_dispose_callback for it, and
        // every future stream's data would be silently discarded from here on. Treat this case
        // as the SDK having moved on to a new stream, not a real concurrent one.
        if (!iInterrupted) {
            LOG(kQobuzConnect, "QobuzConnectAudioStream: ignoring concurrent stream %llu (active=%llu, gapless not supported)\n", (unsigned long long)aStreamId, (unsigned long long)iActiveStreamId);
            return;
        }
        LOG(kQobuzConnect, "QobuzConnectAudioStream: replacing abandoned stream %llu with %llu (old stream was locally interrupted, never disposed)\n", (unsigned long long)iActiveStreamId, (unsigned long long)aStreamId);
        while (!iChunks.empty()) {
            delete iChunks.front();
            iChunks.pop_front();
        }
        iBufferedBytes = 0;
        iResumeStreamId = 0;
    }
    iActiveStreamId = aStreamId;
    iActiveStreamFinished = false;
    iInterrupted = false;
    iPendingCount = 0; // a new stream's byte sequence never continues an old one's partial 24-in-32 frame
    iStreamFormat.Set(aProperties.format);
}

size_t QobuzConnectAudioStream::HandleStreamData(QbzAudioStreamId aStreamId, const uint8_t* aData, size_t aSize)
{
    AutoMutex _(iLock);
    // TEMP diagnostic: this callback previously had no logging at all - added while chasing a
    // "Qobuz app shows playing, but no audio out" report, to confirm whether the SDK is calling
    // in with real PCM at all.
    LOG(kQobuzConnect, "QobuzConnectAudioStream::HandleStreamData(%llu, %u bytes) active=%llu\n",
        (unsigned long long)aStreamId, (unsigned)aSize, (unsigned long long)iActiveStreamId);
    if (aStreamId != iActiveStreamId) {
        // Not the stream we're currently reading (see HandleStreamStarted) - accept+discard so
        // the SDK doesn't stall waiting for us to consume it.
        LOG(kQobuzConnect, "QobuzConnectAudioStream::HandleStreamData: discarding - not the active stream\n");
        return aSize;
    }
    const TUint spaceAvailable = (iBufferedBytes < kMaxBufferBytes) ? (kMaxBufferBytes - iBufferedBytes) : 0;
    const size_t consumed = (aSize < (size_t)spaceAvailable) ? aSize : (size_t)spaceAvailable;
    if (consumed > 0) {
        if (iStreamFormat.BitDepth() == 32) {
            AppendRepacked24In32Locked(aData, (TUint)consumed);
        }
        else {
            iChunks.push_back(new Bwh(aData, (TUint)consumed));
            iBufferedBytes += (TUint)consumed;
            iSemDataAvailable.Signal();
        }
    }
    if (consumed < aSize) {
        iResumeStreamId = aStreamId;
    }
    return consumed;
}

void QobuzConnectAudioStream::AppendRepacked24In32Locked(const uint8_t* aData, TUint aSize)
{
    // The SDK packs 24-bit samples low-justified in each 4-byte little-endian container - value
    // in bytes 0-2, byte 3 always zero (see SDK README section 4.4) - but this Pipeline's
    // BitDepth()==32 handling (DecodedAudio::CopyToBigEndian32 and downstream code that reads
    // the top bytes of a 32-bit sample as its significant magnitude, e.g. RampApplicator) expects
    // a high-justified layout: byte 0 always zero, value in bytes 1-3. Without repacking here,
    // hi-res Qobuz Connect audio is silently attenuated by roughly 48dB - correct format/gain/
    // pipeline state throughout, but no audible sound. The SDK delivers arbitrary byte slices,
    // not sample-aligned, so up to 3 leftover bytes from one call are carried over in
    // iPendingBytes and combined with the next.
    const TUint total = iPendingCount + aSize;
    std::vector<TByte> combined(total);
    if (iPendingCount > 0) {
        memcpy(combined.data(), iPendingBytes, iPendingCount);
    }
    memcpy(combined.data() + iPendingCount, aData, aSize);

    const TUint frames = total / 4;
    const TUint outBytes = frames * 4;
    if (outBytes > 0) {
        std::vector<TByte> repacked(outBytes);
        for (TUint f = 0; f < frames; f++) {
            const TByte* s = &combined[f * 4];
            repacked[f * 4 + 0] = 0;
            repacked[f * 4 + 1] = s[0];
            repacked[f * 4 + 2] = s[1];
            repacked[f * 4 + 3] = s[2];
            // s[3] (the SDK's always-zero padding byte) is discarded.
        }
        iChunks.push_back(new Bwh(repacked.data(), outBytes));
        iBufferedBytes += outBytes;
        iSemDataAvailable.Signal();
    }

    iPendingCount = total - outBytes;
    if (iPendingCount > 0) {
        memcpy(iPendingBytes, &combined[outBytes], iPendingCount);
    }
}

void QobuzConnectAudioStream::HandleStreamMetadata(const QbzAudioMetadata* aMetadata)
{
    if (aMetadata == nullptr) {
        return;
    }
    LOG(kQobuzConnect, "QobuzConnectAudioStream::HandleStreamMetadata title=%s\n",
        (aMetadata->title != nullptr) ? aMetadata->title : "");

    IQobuzConnectMetadataObserver* observer;
    {
        AutoMutex _(iLock);
        observer = iMetadataObserver;
    }
    if (observer == nullptr) {
        return;
    }
    // Brn() only ever aliases the given char*'s existing storage (the SDK's own metadata struct,
    // valid only for the duration of this callback) - safe here since the observer chain
    // (SourceQobuzConnect::QobuzNotifyMetadataChanged -> QobuzConnectMetadataHandler::
    // MetadataChanged) copies whatever it wants to keep before returning.
    observer->QobuzNotifyMetadataChanged(
        (aMetadata->title != nullptr) ? Brn(aMetadata->title) : Brx::Empty(),
        (aMetadata->artist != nullptr) ? Brn(aMetadata->artist) : Brx::Empty(),
        (aMetadata->album != nullptr) ? Brn(aMetadata->album) : Brx::Empty(),
        (aMetadata->album_art_url != nullptr) ? Brn(aMetadata->album_art_url) : Brx::Empty());
}

void QobuzConnectAudioStream::HandleStreamFinished(QbzAudioStreamId aStreamId)
{
    LOG(kQobuzConnect, "QobuzConnectAudioStream::HandleStreamFinished(%llu)\n", (unsigned long long)aStreamId);
    AutoMutex _(iLock);
    if (aStreamId != iActiveStreamId) {
        return;
    }
    iActiveStreamFinished = true;
    iSemDataAvailable.Signal(); // wake Read() in case it's blocked waiting for more data that will never come
}

void QobuzConnectAudioStream::HandleStreamSeeked(QbzAudioStreamId aStreamId, uint64_t aPositionMs)
{
    LOG(kQobuzConnect, "QobuzConnectAudioStream::HandleStreamSeeked(%llu, %llu)\n", (unsigned long long)aStreamId, (unsigned long long)aPositionMs);
    IQobuzConnectMetadataObserver* observer;
    {
        AutoMutex _(iLock);
        if (aStreamId != iActiveStreamId) {
            return;
        }
        // Any buffered audio predates the seek (QbzMediaSeekInProgressCallback is expected to
        // have already flushed it - see QobuzConnectMediaControl) - reset finished/interrupted so
        // fresh post-seek data can flow through Read() again.
        iActiveStreamFinished = false;
        iInterrupted = false;
        iPendingCount = 0; // don't splice a leftover pre-seek partial frame onto post-seek bytes
        observer = iMetadataObserver;
    }
    if (observer != nullptr) {
        // Without this, QobuzConnectMediaControl's position tracking (base + elapsed wall-clock
        // time) never learns a seek happened at all - qbz_connect_get_playback_position keeps
        // reporting the pre-seek position, so the Controller's own seek bar looked like the seek
        // had no effect even though audio itself resumed correctly from the new position.
        observer->QobuzNotifyStreamSeeked(aPositionMs);
    }
}

void QobuzConnectAudioStream::HandleStreamDispose(QbzAudioStreamId aStreamId)
{
    LOG(kQobuzConnect, "QobuzConnectAudioStream::HandleStreamDispose(%llu)\n", (unsigned long long)aStreamId);
    AutoMutex _(iLock);
    if (aStreamId != iActiveStreamId) {
        return;
    }
    while (!iChunks.empty()) {
        delete iChunks.front();
        iChunks.pop_front();
    }
    iBufferedBytes = 0;
    iResumeStreamId = 0;
    iActiveStreamId = 0;
    // Disposal ends this stream just as definitively as a natural finish - e.g. skipping a track
    // disposes the old stream directly, without stream_finished_callback ever firing for it. A
    // Read() blocked waiting for more of THIS stream's data must be woken and told to stop, the
    // same way HandleStreamFinished() already does - otherwise it stays blocked forever, and
    // ProtocolQobuzConnect::Stream()'s outer loop never gets a chance to unwind and re-announce
    // OutputStream()/format for whatever comes next. Without this, once the next stream's data
    // does arrive, Read() silently delivers it through the SAME still-open call as if nothing
    // happened, leaving the Pipeline's own transport state (and any flush a pending Stop queued)
    // stuck - matching the "audio works until you skip a track" report.
    //
    // Setting iActiveStreamFinished here isn't actually what makes this reliable, since the
    // SDK's own stream_started_callback for the NEXT stream can (and does, in practice - this
    // was confirmed on hardware) run HandleStreamStarted() before a still-blocked Read() call
    // gets scheduled to notice this dispose, and HandleStreamStarted() unconditionally resets
    // iActiveStreamFinished for the new stream. What actually makes Read() stop reliably is its
    // own direct check of iActiveStreamId==0 (set just above) rather than a flag that a later
    // event can clear first - see Read()'s comment for why a "changed since I started" signal
    // (an earlier version of this fix used iEpoch for that) isn't enough either: several
    // HandleStreamData-driven signals for this stream's last few chunks can already be queued
    // up, and only the first Read() call they wake would ever see a freshly-changed value.
    iActiveStreamFinished = true;
    iSemDataAvailable.Signal();
}

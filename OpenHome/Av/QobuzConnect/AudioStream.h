#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Thread.h>

#include <qobuz_connect.h>

#include <chrono>
#include <deque>

EXCEPTION(QobuzConnectAudioStreamStopped)

namespace OpenHome {
namespace Av {

class IQobuzConnectMetadataObserver; // see MediaControl.h - only a reference to this is needed here

class IQobuzConnectAudioWriter
{
public:
    virtual ~IQobuzConnectAudioWriter() {}
    virtual void Write(const Brx& aData) = 0;
};

// Reports the format exactly as the SDK delivers it on the wire, unmodified: 16-bit samples
// are 2 tightly-packed bytes each; "24-bit" samples are actually delivered as 4-byte little
// endian containers with the top byte zeroed (see SDK README section 4.4), so BitDepth()
// reports 32 for that case, not 24 - this is deliberate. Repacking down to a tightly-packed
// 3-byte/sample form would need to buffer partial samples split across chunk boundaries
// (audio chunks from the SDK are arbitrary byte slices, not sample-aligned), which this first
// pass avoids: declaring the true 32-bit-per-sample wire layout to the Pipeline keeps framing
// trivial (frame size = channels * bitDepth/8) at the cost of the bottom 8 bits always being
// zero. Worth revisiting if the Pipeline/DAC path doesn't like 32-bit PCM in practice.
class QobuzConnectStreamFormat
{
public:
    QobuzConnectStreamFormat();
public:
    void Set(const QbzAudioFormat& aFormat);
public:
    TUint SampleRate() const;
    TUint NumChannels() const;
    TUint BitDepth() const;
    QbzAudioSampleFormat SdkFormat() const;
private:
    TUint iSampleRate;
    TUint iNumChannels;
    TUint iBitDepth;
    QbzAudioSampleFormat iSdkFormat;
    mutable Mutex iLock;
};

/**
 * Interface the Pipeline-facing Protocol implementation pulls audio from.
 *
 * Mirrors IRaatReader (OpenHome/Av/Raat/Output.h), but internally bridges Qobuz Connect's
 * push-based delegate callback (QbzAudioStreamDataCallback, invoked from the SDK's own
 * libuv thread, must return quickly) to this pull-based blocking Read(), since the
 * Pipeline's protocol/filler thread expects to block waiting for data, the same way
 * ProtocolRaat blocks on IRaatReader::Read().
 */
class IQobuzConnectAudioReader
{
public:
    virtual ~IQobuzConnectAudioReader() {}
    virtual const QobuzConnectStreamFormat& StreamFormat() = 0;
    // Where the active stream's audio actually begins (QbzAudioStreamStartedCallback's
    // aInitialPositionMs, ms) - normally 0, but the SDK can start a stream partway through a
    // track (e.g. resuming a session left mid-track) - see SourceQobuzConnect::
    // QobuzNotifyPlaybackInitiated(), which feeds this into QobuzConnectMediaControl's position
    // tracking so the Controller's displayed position matches where audio actually starts.
    virtual uint64_t InitialPositionMs() = 0;
    // Total duration of the active stream, in ms (QbzAudioStreamProperties.duration) - see
    // ProtocolQobuzConnect::OutputStream(), which converts this into the byte count the Pipeline
    // expects for a raw PCM stream's declared length, so DS's own duration/time-remaining/
    // progress-bar reporting (e.g. in the Linn App) has something to work from - without this,
    // elapsed time displays fine (it doesn't need a duration), but time remaining and the
    // progress bar have nothing to divide against.
    virtual uint64_t DurationMs() = 0;
    // Must be called once, right before a fresh run of Read() calls begins (i.e. right before
    // ProtocolQobuzConnect::Stream() enters its inner read loop) - captures which SDK stream is
    // active right now so that Read() can detect, no matter how the active stream subsequently
    // changes (disposed, or disposed-and-immediately-replaced by a new one before any Read() call
    // gets scheduled to notice), that it should stop rather than silently keep delivering data
    // for a stream this particular run of the read loop was never told about.
    virtual void NotifyReading() = 0;
    virtual void Read(IQobuzConnectAudioWriter& aWriter) = 0; // blocks until data is available; throws QobuzConnectAudioStreamStopped once the active stream has finished+drained, or on Interrupt()
    virtual void Interrupt() = 0;
};

/**
 * Implements the SDK's AudioStream delegate, and bridges it to IQobuzConnectAudioReader.
 *
 * Simplification (v1): only ever feeds the Pipeline one stream's audio at a time - true gapless
 * crossfade (the SDK supports multiple concurrent streams for this - lowest stream ID is
 * "active") isn't implemented. A second stream_started_callback arriving while one is already
 * active (the SDK's own gapless-preload behaviour) is held as a pending stream rather than acted
 * on immediately, and adopted once the first is disposed - see HandleStreamStarted()/
 * HandleStreamDispose()'s comments.
 */
class QobuzConnectAudioStream : public IQobuzConnectAudioReader
{
private:
    // Bounds how much undecoded audio we'll buffer before applying backpressure (returning
    // less than the full size from the data callback, per the SDK's documented contract). Sized
    // generously (comfortably a couple of seconds even at the SDK's highest supported quality -
    // 384kHz/24-bit/2ch, ~3MB/s once repacked to 32-bit containers - see SDK README 4.4) rather
    // than tightly, since this is the only cushion available to absorb a startup network/decode
    // hiccup before Read()'s own real-time pacing (kPacingLookaheadMs) has had a chance to build
    // one up of its own - too small a cushion here showed up on hardware as a Pipeline buffering/
    // dropout a second or so into an otherwise-fine track start.
    static const TUint kMaxBufferBytes = 2 * 1024 * 1024;
    // How far ahead of real playback time Read() is allowed to hand audio to the Pipeline - see
    // Read()'s comment. A jitter cushion, not a hard cap - just needs to comfortably absorb
    // normal network/decode timing variance without reintroducing a large gap between the SDK's
    // "stream finished" notion and when that stream's audio is actually audible.
    static const TUint kPacingLookaheadMs = 1500;
public:
    QobuzConnectAudioStream();
    ~QobuzConnectAudioStream();
public:
    QbzAudioStreamDelegate Delegate();
    void SetCore(QbzConnectCore* aCore);
    // Optional - if never called, metadata callbacks are simply logged and dropped (matching
    // this class's pre-existing behaviour). Called once by QobuzConnectApp at construction.
    void SetMetadataObserver(IQobuzConnectMetadataObserver& aObserver);
public: // from IQobuzConnectAudioReader
    const QobuzConnectStreamFormat& StreamFormat() override;
    uint64_t InitialPositionMs() override;
    uint64_t DurationMs() override;
    void NotifyReading() override;
    void Read(IQobuzConnectAudioWriter& aWriter) override;
    void Interrupt() override;
public:
    // Discards any buffered (pre-seek) audio. Called by QobuzConnectMediaControl when the SDK
    // signals a seek is in progress (QbzMediaSeekInProgressCallback), before the new
    // post-seek data starts arriving via HandleStreamData.
    void FlushForSeek();
public:
    // Called by the extern "C" trampoline functions in AudioStream.cpp (which aren't members or
    // friends of this class, so these must be public, not private).
    static void StreamStartedCb(QbzConnectCore* aCore, QbzAudioStreamId aStreamId, QbzAudioStreamProperties aProperties, uint64_t aInitialPositionMs, void* aUserData);
    static size_t StreamDataCb(QbzConnectCore* aCore, QbzAudioStreamId aStreamId, const uint8_t* aData, size_t aSize, void* aUserData);
    static void StreamMetadataCb(QbzConnectCore* aCore, QbzAudioStreamId aStreamId, const QbzAudioMetadata* aMetadata, void* aUserData);
    static void StreamFinishedCb(QbzConnectCore* aCore, QbzAudioStreamId aStreamId, void* aUserData);
    static void StreamSeekedCb(QbzConnectCore* aCore, QbzAudioStreamId aStreamId, uint64_t aPositionMs, void* aUserData);
    static void StreamDisposeCb(QbzConnectCore* aCore, QbzAudioStreamId aStreamId, void* aUserData);
private:
    void HandleStreamStarted(QbzAudioStreamId aStreamId, const QbzAudioStreamProperties& aProperties, uint64_t aInitialPositionMs);
    size_t HandleStreamData(QbzAudioStreamId aStreamId, const uint8_t* aData, size_t aSize);
    void HandleStreamMetadata(const QbzAudioMetadata* aMetadata);
    void HandleStreamFinished(QbzAudioStreamId aStreamId);
    void HandleStreamSeeked(QbzAudioStreamId aStreamId, uint64_t aPositionMs);
    void HandleStreamDispose(QbzAudioStreamId aStreamId);
    void AppendRepacked24In32Locked(const uint8_t* aData, TUint aSize);
private:
    QbzConnectCore* iCore;
    QobuzConnectStreamFormat iStreamFormat;
    IQobuzConnectMetadataObserver* iMetadataObserver; // not owned; nullptr until SetMetadataObserver() is called
    Mutex iLock;
    Semaphore iSemDataAvailable;
    std::deque<Bwh*> iChunks;
    TUint iBufferedBytes;
    QbzAudioStreamId iActiveStreamId; // 0 == none active
    uint64_t iActiveStreamInitialPositionMs; // valid iff iActiveStreamId != 0 - see InitialPositionMs()
    uint64_t iActiveStreamDurationMs; // valid iff iActiveStreamId != 0 - see DurationMs()
    QbzAudioStreamId iResumeStreamId; // non-zero once backpressure has been applied and resume is owed
    // Non-zero once a stream_started_callback arrives for a stream while another is still active
    // (the SDK's gapless-preload mechanism - see HandleStreamStarted()'s comment). Held here
    // rather than adopted immediately, and promoted to iActiveStreamId once the current stream is
    // disposed - see HandleStreamDispose().
    QbzAudioStreamId iPendingStreamId;
    QbzAudioFormat iPendingStreamFormat; // valid iff iPendingStreamId != 0
    uint64_t iPendingStreamInitialPositionMs; // valid iff iPendingStreamId != 0
    uint64_t iPendingStreamDurationMs; // valid iff iPendingStreamId != 0
    QbzAudioStreamId iReadingForStreamId; // snapshot of iActiveStreamId taken by NotifyReading() - see that method's doc comment
    TBool iActiveStreamFinished; // active stream has delivered all its data - Read() returns once the buffer drains
    TBool iInterrupted;
    // Real-time pacing state for the active stream - see Read()'s comment. Reset whenever the
    // active stream (re)starts delivering data from a known point in time: a fresh stream
    // (HandleStreamStarted) or immediately after a seek completes (HandleStreamSeeked).
    std::chrono::steady_clock::time_point iReadStartTime;
    double iDeliveredMs; // audio-time handed to the Pipeline so far, relative to iReadStartTime
    TByte iPendingBytes[3]; // leftover bytes from the tail of a 24-in-32 frame split across HandleStreamData calls
    TUint iPendingCount; // 0-3, how many of iPendingBytes are valid
};

}
}

#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Thread.h>

#include <qobuz_connect.h>

#include <deque>

EXCEPTION(QobuzConnectAudioStreamStopped)

namespace OpenHome {
namespace Av {

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
 * Simplification (v1): only ever tracks one active stream at a time. Qobuz Connect's SDK
 * supports multiple concurrent streams for gapless/cross-fade transitions (lowest stream ID
 * is "active"), which isn't implemented here - a second stream_started_callback arriving
 * while one is already active is logged and its data ignored until the first is disposed.
 */
class QobuzConnectAudioStream : public IQobuzConnectAudioReader
{
private:
    // Bounds how much undecoded audio we'll buffer before applying backpressure (returning
    // less than the full size from the data callback, per the SDK's documented contract).
    static const TUint kMaxBufferBytes = 256 * 1024;
public:
    QobuzConnectAudioStream();
    ~QobuzConnectAudioStream();
public:
    QbzAudioStreamDelegate Delegate();
    void SetCore(QbzConnectCore* aCore);
public: // from IQobuzConnectAudioReader
    const QobuzConnectStreamFormat& StreamFormat() override;
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
    void HandleStreamStarted(QbzAudioStreamId aStreamId, const QbzAudioStreamProperties& aProperties);
    size_t HandleStreamData(QbzAudioStreamId aStreamId, const uint8_t* aData, size_t aSize);
    void HandleStreamFinished(QbzAudioStreamId aStreamId);
    void HandleStreamSeeked(QbzAudioStreamId aStreamId);
    void HandleStreamDispose(QbzAudioStreamId aStreamId);
    void AppendRepacked24In32Locked(const uint8_t* aData, TUint aSize);
private:
    QbzConnectCore* iCore;
    QobuzConnectStreamFormat iStreamFormat;
    Mutex iLock;
    Semaphore iSemDataAvailable;
    std::deque<Bwh*> iChunks;
    TUint iBufferedBytes;
    QbzAudioStreamId iActiveStreamId; // 0 == none active
    QbzAudioStreamId iResumeStreamId; // non-zero once backpressure has been applied and resume is owed
    QbzAudioStreamId iReadingForStreamId; // snapshot of iActiveStreamId taken by NotifyReading() - see that method's doc comment
    TBool iActiveStreamFinished; // active stream has delivered all its data - Read() returns once the buffer drains
    TBool iInterrupted;
    TByte iPendingBytes[3]; // leftover bytes from the tail of a 24-in-32 frame split across HandleStreamData calls
    TUint iPendingCount; // 0-3, how many of iPendingBytes are valid
};

}
}

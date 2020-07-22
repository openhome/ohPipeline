#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Media/Supply.h>
#include <OpenHome/SocketHttp.h>

#include <algorithm>

EXCEPTION(UriLoaderError);

EXCEPTION(HlsPlaylistInvalid);
EXCEPTION(HlsPlaylistUnsupported);
EXCEPTION(HlsNoMoreSegments);
EXCEPTION(HlsEndOfStream);

EXCEPTION(HlsPlaylistProviderError);
EXCEPTION(HlsSegmentUriError);
EXCEPTION(HlsSegmentError);

namespace OpenHome {
    class ITimer;
    class ITimerFactory;
namespace Media {

class IHlsPlaylistProvider
{
public:
    /*
     * Blocks until playlist is available.
     *
     * Throws HlsPlaylistProviderError if manifest unavailable (or Interrupt() is called while in this call).
     */
    virtual IReader& Reload() = 0;
    virtual const Uri& GetUri() const = 0;
    virtual void InterruptPlaylistProvider(TBool aInterrupt) = 0;
    virtual ~IHlsPlaylistProvider() {};
};

class ISegmentUriProvider
{
public:
    /*
     * Returns duration of segment in ms.
     *
     * THROWS HlsSegmentUriError, HlsEndOfStream.
     */
    virtual TUint NextSegmentUri(Uri& aUri) = 0;
    virtual void InterruptSegmentUriProvider(TBool aInterrupt) = 0;
    virtual ~ISegmentUriProvider() {}
};

class ISegmentProvider
{
public:
    /*
     * Blocks until segment is available.
     *
     * Throws HlsSegmentError if segment unavailable (or Interrupt() is called while in this call).
     *
     * IReader becomes invalid following upon next call to NextSegment() or Interrupt().
     */
    virtual IReader& NextSegment() = 0;
    virtual void InterruptSegmentProvider(TBool aInterrupt) = 0;
    virtual ~ISegmentProvider() {}
};

/*
 * Class that optionally proxies an underlying IReader.
 *
 * If an underlying IReader has not been set, this class will consume calls to ReadFlush() and ReadInterrupt(). If Read() is called when an underlying IReader has not been set, a ReaderError will be thrown.
 *
 * While in a Read() call, the only other method that it is valid to call (from another thread) is ReadInterrupt(). Any other call from any other thread will result in undefined behaviour.
 */
class ReaderProxy : public IReader
{
public:
    ReaderProxy();
    TBool IsReaderSet() const;
    void SetReader(IReader& aReader);
    void Clear();
public: // from IReader
    Brn Read(TUint aBytes) override;
    void ReadFlush() override;
    void ReadInterrupt() override;
private:
    IReader* iReader;
    mutable Mutex iLock;
};

class ReaderLoggerTime : public IReader
{
public:
    ReaderLoggerTime(const TChar* aId, IReader& aReader, TUint aNormalReadLimitMs);
public: // from IReader
    Brn Read(TUint aBytes) override;
    void ReadFlush() override;
    void ReadInterrupt() override;
private:
    const TChar* iId;
    IReader& iReader;
    const TUint iNormalReadLimitMs;
};

class ReaderLogger : public IReader
{
public:
    ReaderLogger(const TChar* aId, IReader& aReader);
    void SetEnabled(TBool aEnabled);
public: // from IReader
    Brn Read(TUint aBytes);
    void ReadFlush();
    void ReadInterrupt();
private:
    const TChar* iId;
    IReader& iReader;
    TBool iEnabled;
};

class UriLoader
{
public:
    UriLoader(Environment& aEnv, SslContext& aSsl, const Brx& aUserAgent, ITimerFactory& aTimerFactory, TUint aRetryInterval);
    ~UriLoader();
    IReader& Load(const Uri& aUri); // THROWS UriLoaderError
    void Reset();
    void Interrupt(TBool aInterrupt);
private:
    SocketHttp iSocket;
    const TUint iRetryInterval;
    std::atomic<TBool> iInterrupted;
    Semaphore iSemRetry;
    ITimer* iTimerRetry;
};

/*
 * Only safe to call Interrupt() from another thread when inside any other method of this class.
 */
class PlaylistProvider : public IHlsPlaylistProvider
{
private:
    static const TUint kConnectRetryIntervalMs = 1 * 1000;
public:
    PlaylistProvider(Environment& aEnv, SslContext& aSsl, const Brx& aUserAgent, ITimerFactory& aTimerFactory);
    void SetUri(const Uri& aUri);
    void Reset();
public: // from IHlsPlaylistProvider
    IReader& Reload() override;
    const Uri& GetUri() const override;
    void InterruptPlaylistProvider(TBool aInterrupt) override;
private:
    UriLoader iLoader;
    Uri iUri;
};

class SegmentProvider : public ISegmentProvider
{
private:
    static const TUint kConnectRetryIntervalMs = 1 * 1000;
public:
    SegmentProvider(Environment& aEnv, SslContext& aSsl, const Brx& aUserAgent, ITimerFactory& aTimerFactory, ISegmentUriProvider& aProvider);
    void Reset();
public: // from ISegmentProvider
    IReader& NextSegment() override;
    void InterruptSegmentProvider(TBool aInterrupt) override;
private:
    UriLoader iLoader;
    ISegmentUriProvider& iProvider;
};

class SegmentDescriptor
{
public:
    SegmentDescriptor(TUint64 aIndex, const Brx& aUri, TUint aDurationMs);
    SegmentDescriptor(const SegmentDescriptor& aDescriptor);
    TUint64 Index() const;
    /*
     * This is the URI contained within the playlist.
     *
     * It is up to a client of this class to determine whether the URI is absolute or relative and perform appropriate concatenation with the playlist URI, if required.
     */
    const Brx& SegmentUri() const;
    /*
     * When attempting to retrieve a given segment, this method should be used to be guaranteed that an absolute URI will be returned.
     *
     * THROWS UriError.
     */
    void AbsoluteUri(const Uri& aBaseUri, Uri& aUriOut) const;
    TUint DurationMs() const;
private:
    const TUint64 iIndex;
    const Brh iUri;
    const TUint iDurationMs;
};

class HlsPlaylistParser
{
private:
    static const TUint kMaxLineBytes = 2048;
    static const TUint kMillisecondsPerSecond = 1000;
public:
    HlsPlaylistParser();
    /*
     * Set stream to read from and perform some initial parsing.
     *
     * THROWS HlsNoMoreSegments if end of stream reached during initial parsing, HlsPlaylistInvalid if unable to parse playlist, HlsPlaylistUnsupported if unsupported version.
     */
    void Parse(IReader& aReader);
    void Reset();
    TUint TargetDurationMs() const;
    TBool StreamEnded() const;
    /*
     * Attempts to retrieve next segment from playlist.
     *
     * Up to caller of this to check that index of the segment descriptor is the same as the index of the previous segment descriptor if continuity in segments is desired.
     *
     * THROWS HlsNoMoreSegments if end of stream reached during parsing, HlsPlaylistInvalid if unable to parse playlist, HlsEndOfStream if end of stream identify reached.
     */
    SegmentDescriptor GetNextSegmentUri();
    void Interrupt(TBool aInterrupt);
private:
    void PreProcess();
    void ReadNextLine();
private:
    ReaderProxy iReaderProxy;
    ReaderLogger iReaderLogger;
    ReaderUntilS<kMaxLineBytes> iReaderUntil;
    TUint iTargetDurationMs;
    TUint64 iSequenceNo;
    TBool iEndList;
    TBool iEndOfStreamReached;
    Brn iNextLine;
    TBool iUnsupported;
    TBool iInvalid;
};

class IHlsReloadTimer
{
public:
    /*
     * Start timer ticking.
     */
    virtual void Restart() = 0;
    /*
     * Block until timer has ticked for aWaitMs since last Restart() call.
     */
    virtual void Wait(TUint aWaitMs) = 0;
    virtual void InterruptReloadTimer() = 0;
    virtual ~IHlsReloadTimer() {}
};

class HlsReloadTimer : public IHlsReloadTimer
{
public:
    HlsReloadTimer(Environment& aEnv, ITimerFactory& aTimerFactory);
    ~HlsReloadTimer();
public: // from IHlsReloadTimer
    void Restart() override;
    void Wait(TUint aWaitMs) override;
    void InterruptReloadTimer() override;
private:
    void TimerFired();
private:
    OsContext& iCtx;
    TUint iResetTimeMs;
    Semaphore iSem;
    ITimer* iTimer;
};

class HlsM3uReader : public ISegmentUriProvider
{
public:
    static const TUint64 kSeqNumFirstInPlaylist = 0;
private:
    static const TUint kMaxLineBytes = 2048;
public:
    HlsM3uReader(IHlsPlaylistProvider& aProvider, IHlsReloadTimer& aReloadTimer);
    ~HlsM3uReader();
    TBool StreamEnded() const;
    TBool Error() const;
    void Interrupt(TBool aInterrupt);
    void Reset();
    /*
     * Only valid to call this between Reset() and NextSegmentUri() calls. Calls at any other time will result in undefined behaviour.
     *
     * aPreferredStartSegment value of 0 means segments returned will be from first segment in playlist.
     *
     * If aPreferredStartSegment is lower that first segment sequence number encountered in playlist, will return first segment from playlist onwards and disregard aPreferredStartSegment.
     */
    void SetStartSegment(TUint64 aPreferredStartSegment);
    TUint64 LastSegment() const;
public: // from ISegmentUriProvider
    TUint NextSegmentUri(Uri& aUri) override;
    void InterruptSegmentUriProvider(TBool aInterrupt) override;
private:
    void ReloadVariantPlaylist();
private:
    IHlsPlaylistProvider& iProvider;
    IHlsReloadTimer& iReloadTimer;
    HlsPlaylistParser iParser;
    TUint64 iLastSegment;
    TUint64 iPreferredStartSegment;
    TBool iNewSegmentEncountered;
    std::atomic<TBool> iInterrupted;
    TBool iError;
};

/*
 * Class that presents many segments as a continuous stream through an IReader interface.
 *
 * It is possible to infer state changes and errors that this class encounters from the return value of Read() (a buffer of size 0 indicates end-of-stream) and ReaderError being thrown by Read().
 *
 * However, it is known that certain users of this class (ContentAudio) do not correctly handle the end-of-stream state and will continue to read until this throws a ReaderError. Therefore, the Error() getter exists to check whether this has indeed encountered an error, or has terminated under normal conditions, when ContentAudio returns with value EProtocolStreamErrorRecoverable.
 */
class SegmentStreamer : public IReader
{
public:
    SegmentStreamer(ISegmentProvider& aProvider);
    TBool Error() const;
    void Reset();
    void Interrupt(TBool aInterrupt);
public: // from IReader
    Brn Read(TUint aBytes) override;
    void ReadFlush() override;
    void ReadInterrupt() override;
private:
    ISegmentProvider& iProvider;
    ReaderProxy iReader;
    TBool iInterrupted;
    TBool iError;
    TBool iStreamEnded;
    Mutex iLock;
};

} // namespace Media
} // namespace OpenHome

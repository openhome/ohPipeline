#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Media/Supply.h>

#include <algorithm>

EXCEPTION(HttpSocketUriError);
EXCEPTION(HttpSocketMethodInvalid);
EXCEPTION(HttpSocketConnectionError);
EXCEPTION(HttpSocketRequestError);
EXCEPTION(HttpSocketResponseError);
EXCEPTION(HttpSocketError);

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

// FIXME - the changes in this class (parsing and reporting of the presence of "keep-alive" option) should really be rolled into ohNet's HttpHeaderConnection class.
class HttpHeaderConnection : public HttpHeader
{
public:
    static const Brn kConnectionClose;
    static const Brn kConnectionKeepAlive;
    static const Brn kConnectionUpgrade;
public:
    TBool Close() const;
    TBool KeepAlive() const;
    TBool Upgrade() const;
private:
    virtual TBool Recognise(const Brx& aHeader);
    virtual void Process(const Brx& aValue);
private:
    TBool iClose;
    TBool iKeepAlive;
    TBool iUpgrade;
};

/*
 * Helper class to make sending HTTP requests to and reading HTTP responses from socket simpler.
 *
 * Safe to re-use opened socket to send/receive multiple request/response pairs over a single connection (i.e., a persistent connection, where Close() is not called after a send/receive pair, and it only called when the underlying connection is no longer required).
 *
 * Due to the way underlying HTTP helper classes (notably ReaderHttpResponse) are implemented, this class has to provide some level of buffering on the input (i.e., response) side.
 *
 * Chunked responses are transparently handled.
 *
 * As it is necessary, due to implementation reasons, that input buffering is handled by this class (through constructor params), output buffering is also handled by this class (through constructor params) for the sake of completeness.
 *
 * Optionally follows redirects (only for GET requests).
 */
class HttpSocket : private IReader
{
private:
    static const TUint kDefaultHttpPort = 80;
    static const TUint kDefaultReadBufferBytes = 1024;
    static const TUint kDefaultWriteBufferBytes = 1024;
    static const TUint kDefaultConnectTimeoutMs = 5 * 1000;
    static const TUint kDefaultResponseTimeoutMs = 60 * 1000;
    static const TUint kDefaultReceiveTimeoutMs = 10 * 1000;

    static const Brn kSchemeHttp;
private:
    class ReaderUntilDynamic : public ReaderUntil
    {
    public:
        ReaderUntilDynamic(TUint aMaxBytes, IReader& aReader);
    private: // from ReaderUntil
        TByte* Ptr() override;
    private:
        Bwh iBuf;
    };
    class Swd : public Swx
    {
    public:
        Swd(TUint aMaxBytes, IWriter& aWriter);
    private: // from Swx
        TByte* Ptr() override;
    private:
        Bwh iBuf;
    };
public:
    HttpSocket( Environment& aEnv,
                const Brx& aUserAgent,
                TUint aReadBufferBytes = kDefaultReadBufferBytes,
                TUint aWriteBufferBytes = kDefaultWriteBufferBytes,
                TUint aConnectTimeoutMs = kDefaultConnectTimeoutMs,
                TUint aResponseTimeoutMs = kDefaultResponseTimeoutMs,
                TUint aReceiveTimeoutMs = kDefaultReceiveTimeoutMs,
                TBool aFollowRedirects = true);
    ~HttpSocket();
public:
    /*
     * Set a new URI, which can subsequently be connected to.
     *
     * This call invalidates any previous state of this socket, including any IReaders or IWriters that were in use.
     *
     * Multiple URIs can be connected to, in turn, using the same socket without calling Disconnect()/Connect() for each request (this class will attempt to use HTTP 1.1 persistent connections for new URIs, where applicable).
     */
    void SetUri(const Uri& aUri);
    const Brn GetRequestMethod() const;
    /*
     * Default request method is GET.
     *
     * Throws HttpSocketMethodInvalid.
     */
    void SetRequestMethod(const Brx& aMethod);
    /*
     * Connect to URI.
     *
     * Throws HttpSocketConnectionError.
     *
     * Other methods that rely on this being called, such as GetResponseCode(), GetInputStream(), etc., will implicitly call this.
     *
     * Upon successful connection, response length (if known) can be identified using GetContentLength() call.
     */
    void Connect();
    /*
     * Explicitly close any underlying resources that are in use.
     *
     * Do not do this if the intention is to keep using this socket for persistent HTTP connections.
     */
    void Disconnect();

    IReader& GetInputStream();
    IWriter& GetOutputStream();
    TInt GetResponseCode();     // Returns -1 if unknown code.
    TInt GetContentLength();    // Returns -1 if unknown (e.g., chunked data).
    void Interrupt(TBool aInterrupt);
private: // from IReader
    Brn Read(TUint aBytes) override;
    void ReadFlush() override;
    void ReadInterrupt() override;
private:
    /*
     * Throws HttpSocketRequestError if error occurred writing request.
     */
    void WriteRequest(const Uri& aUri, Brx& aMethod);
    /*
     * Throws HttpSocketResponseError if error occurred reading response.
     */
    TUint ReadResponse();
    void SendRequestHeaders();
    void ProcessResponse();
private:
    Environment& iEnv;
    Bwh iUserAgent;
    const TUint iConnectTimeoutMs;
    const TUint iResponseTimeoutMs;
    const TUint iReceiveTimeoutMs;
    const TBool iFollowRedirects;
    SocketTcpClient iTcpClient;
    HttpHeaderConnection iHeaderConnection;
    HttpHeaderContentLength iHeaderContentLength;
    HttpHeaderLocation iHeaderLocation;
    HttpHeaderTransferEncoding iHeaderTransferEncoding;
    Srd iReadBuffer;
    ReaderUntilDynamic iReaderUntil;
    ReaderHttpResponse iReaderResponse;
    Swd iWriteBuffer;
    WriterHttpRequest iWriterRequest;
    ReaderHttpChunked iDechunker;
    TBool iConnected;
    TBool iRequestHeadersSent;
    TBool iResponseReceived;
    TInt iCode;             // TInt to carry symbolic value of -1.
    TInt iContentLength;    // TInt to carry symbolic value of -1.
    TInt iBytesRemaining;
    Brn iMethod;
    Uri iUri;
    Endpoint iEndpoint;
    TBool iPersistConnection;
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
    ReaderProxy(const TChar* aId);
    TBool IsReaderSet() const;
    void SetReader(IReader& aReader);
    void Clear();
public: // from IReader
    Brn Read(TUint aBytes) override;
    void ReadFlush() override;
    void ReadInterrupt() override;
private:
    const TChar* iId;
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
    UriLoader(Environment& aEnv, const Brx& aUserAgent, ITimerFactory& aTimerFactory, TUint aRetryInterval);
    ~UriLoader();
    IReader& Load(const Uri& aUri); // THROWS UriLoaderError
    void Reset();
    void Interrupt(TBool aInterrupt);
private:
    HttpSocket iSocket;
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
    PlaylistProvider(Environment& aEnv, const Brx& aUserAgent, ITimerFactory& aTimerFactory);
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
    SegmentProvider(Environment& aEnv, const Brx& aUserAgent, ITimerFactory& aTimerFactory, ISegmentUriProvider& aProvider);
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
    // Attempt to parse up to version 3 (EXTINF with floating point values). However, don't actually support EXT-X-KEY tag (i.e., encrypted stream), which is a requirement of version 1.
    static const TUint kMaxM3uVersion = 3;
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
    IReader* iReader;
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

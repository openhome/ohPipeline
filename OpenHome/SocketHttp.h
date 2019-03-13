#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/SocketSsl.h>

EXCEPTION(SocketHttpUriError);
EXCEPTION(SocketHttpMethodInvalid);
EXCEPTION(SocketHttpConnectionError);
EXCEPTION(SocketHttpRequestError);
EXCEPTION(SocketHttpResponseError);
EXCEPTION(SocketHttpError);

namespace OpenHome {

class SocketHttpHeaderConnection : public HttpHeader
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

class RequestHeader
{
public:
    RequestHeader(const OpenHome::Brx& aField, const OpenHome::Brx& aValue);
    RequestHeader(const RequestHeader& aHeader);
    const OpenHome::Brx& Field() const;
    const OpenHome::Brx& Value() const;
    void Set(const OpenHome::Brx& aValue);
private:
    const OpenHome::Brh iField;
    OpenHome::Bwh iValue;
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
class SocketHttp : private IReader
{
private:
    static const TUint kDefaultHttpPort = 80;
    static const TUint kDefaultHttpsPort = 443;
    static const TUint kDefaultReadBufferBytes = 1024;
    static const TUint kDefaultWriteBufferBytes = 1024;
    static const TUint kDefaultConnectTimeoutMs = 5 * 1000;
    static const TUint kDefaultResponseTimeoutMs = 60 * 1000;

    static const Brn kSchemeHttp;
    static const Brn kSchemeHttps;
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
public:
    SocketHttp( Environment& aEnv,
                const Brx& aUserAgent,
                TUint aReadBufferBytes = kDefaultReadBufferBytes,
                TUint aWriteBufferBytes = kDefaultWriteBufferBytes,
                TUint aConnectTimeoutMs = kDefaultConnectTimeoutMs,
                TUint aResponseTimeoutMs = kDefaultResponseTimeoutMs,
                TBool aFollowRedirects = true);
    ~SocketHttp();
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
     * Throws SocketHttpMethodInvalid if method not supported; SocketHttpError if socket already connected.
     */
    void SetRequestMethod(const Brx& aMethod);
    /*
     * If this is called a "transfer-encoding: chunked" header will be sent upon connection.
     * GetOutputStream() should be used to retrieve a stream for writing the data.
     *
     * This will override any previous SetRequestContentLength() call.
     *
     * Throws SocketHttpError if already connected.
     */
    void SetRequestChunked();
    /*
     * If this is called a "content-length: <aContentLength>" header will be sent upon connection.
     * GetOutputStream() should be used to retrieve a stream for writing the data.
     *
     * This will override any previous SetRequestChunked() call.
     *
     * Throws SocketHttpError if already connected.
     */
    void SetRequestContentLength(TUint64 aContentLength);
    /*
     * Set any custom request headers to be sent up with requests.
     *
     * Throws SocketHttpError if already connected.
     */
    void SetRequestHeader(const OpenHome::Brx& aField, const OpenHome::Brx& aValue);
    /*
     * Connect to URI.
     *
     * Throws SocketHttpConnectionError.
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
    /*
     * Reset state of socket to defaults (e.g., clears any request-related settings such as request method, custom request headers, chunking).
     * Does not clear URI.
     */
    void Reset();

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
     * Throws SocketHttpRequestError if error occurred writing request.
     */
    void WriteRequest();
    /*
     * Throws SocketHttpResponseError if error occurred reading response.
     */
    TUint ReadResponse();
    void SendRequestHeaders();
    void ProcessResponse();
    void ResetResponseState();
private:
    Bwh iUserAgent;
    const TUint iConnectTimeoutMs;
    const TUint iResponseTimeoutMs;
    const TBool iFollowRedirects;
    SocketSsl iSocket;
    SocketHttpHeaderConnection iHeaderConnection;
    HttpHeaderContentLength iHeaderContentLength;
    HttpHeaderLocation iHeaderLocation;
    HttpHeaderTransferEncoding iHeaderTransferEncoding;
    Srd iReadBuffer;
    ReaderUntilDynamic iReaderUntil;
    ReaderHttpResponse iReaderResponse;
    WriterHttpChunked iWriterChunked;
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

    TBool iRequestChunked;
    TBool iRequestContentLengthSet;
    TUint64 iRequestContentLength;
    std::vector<RequestHeader> iRequestHeaders;
};

} // namespace OpenHome

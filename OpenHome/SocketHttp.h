#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Uri.h>

EXCEPTION(HttpSocketUriError);
EXCEPTION(HttpSocketMethodInvalid);
EXCEPTION(HttpSocketConnectionError);
EXCEPTION(HttpSocketRequestError);
EXCEPTION(HttpSocketResponseError);
EXCEPTION(HttpSocketError);

namespace OpenHome {

class HttpSocketHeaderConnection : public HttpHeader
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
    HttpSocketHeaderConnection iHeaderConnection;
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

} // namespace OpenHome

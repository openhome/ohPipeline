#include <OpenHome/Types.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/SocketHttp.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Debug.h>

using namespace OpenHome;


// SocketHttpHeaderConnection

const Brn SocketHttpHeaderConnection::kConnectionClose("close");
const Brn SocketHttpHeaderConnection::kConnectionKeepAlive("keep-alive");
const Brn SocketHttpHeaderConnection::kConnectionUpgrade("upgrade");

TBool SocketHttpHeaderConnection::Close() const
{
    return (Received() ? iClose : false);
}

TBool SocketHttpHeaderConnection::KeepAlive() const
{
    return (Received() ? iKeepAlive : false);
}

TBool SocketHttpHeaderConnection::Upgrade() const
{
    return (Received() ? iUpgrade : false);
}

TBool SocketHttpHeaderConnection::Recognise(const Brx& aHeader)
{
    return Ascii::CaseInsensitiveEquals(aHeader, Http::kHeaderConnection);
}

void SocketHttpHeaderConnection::Process(const Brx& aValue)
{
    iClose = false;
    iKeepAlive = false;
    iUpgrade = false;
    if (Ascii::CaseInsensitiveEquals(aValue, kConnectionClose)) {
        iClose = true;
        SetReceived();
    }
    else if (Ascii::CaseInsensitiveEquals(aValue, kConnectionKeepAlive)) {
        iKeepAlive = true;
        SetReceived();
    }
    else if (Ascii::CaseInsensitiveEquals(aValue, kConnectionUpgrade)) {
        iUpgrade = true;
        SetReceived();
    }
}


// RequestHeader

RequestHeader::RequestHeader(const Brx& aField, const Brx& aValue)
    : iField(aField)
    , iValue(aValue)
{
}

RequestHeader::RequestHeader(const RequestHeader& aHeader)
    : iField(Brn(aHeader.iField))
    , iValue(aHeader.iValue)
{
}

const Brx& RequestHeader::Field() const
{
    return iField;
}

const Brx& RequestHeader::Value() const
{
    return iValue;
}

void RequestHeader::Set(const Brx& aValue)
{
    if (aValue.Bytes() > iValue.MaxBytes()) {
        iValue.Grow(aValue.Bytes());
    }
    iValue.Replace(aValue);
}


// SocketHttp::ReaderUntilDynamic

SocketHttp::ReaderUntilDynamic::ReaderUntilDynamic(TUint aMaxBytes, IReader& aReader)    : ReaderUntil(aMaxBytes, aReader)
    , iBuf(aMaxBytes)
{
}

TByte* SocketHttp::ReaderUntilDynamic::Ptr()
{
    return const_cast<TByte*>(iBuf.Ptr());
}


// SocketHttp::Swd

SocketHttp::Swd::Swd(TUint aMaxBytes, IWriter& aWriter)
    : Swx(aMaxBytes, aWriter)
    , iBuf(aMaxBytes)
{
}

TByte* SocketHttp::Swd::Ptr()
{
    return const_cast<TByte*>(iBuf.Ptr());
}


// SocketHttp

const TUint SocketHttp::kDefaultHttpPort;
const TUint SocketHttp::kDefaultHttpsPort;
const TUint SocketHttp::kDefaultReadBufferBytes;
const TUint SocketHttp::kDefaultWriteBufferBytes;
const TUint SocketHttp::kDefaultConnectTimeoutMs;
const TUint SocketHttp::kDefaultResponseTimeoutMs;

const Brn SocketHttp::kSchemeHttp("http");
const Brn SocketHttp::kSchemeHttps("https");

SocketHttp::SocketHttp(Environment& aEnv, const Brx& aUserAgent, TUint aReadBufferBytes, TUint aWriteBufferBytes, TUint aConnectTimeoutMs, TUint aResponseTimeoutMs, TBool aFollowRedirects)
    : iEnv(aEnv)
    , iUserAgent(aUserAgent)
    , iConnectTimeoutMs(aConnectTimeoutMs)
    , iResponseTimeoutMs(aResponseTimeoutMs)
    , iFollowRedirects(aFollowRedirects)
    , iSocket(aEnv, aReadBufferBytes)
    , iReadBuffer(aReadBufferBytes, iSocket)
    , iReaderUntil(aReadBufferBytes, iReadBuffer)
    , iReaderResponse(aEnv, iReaderUntil)
    , iWriteBuffer(aWriteBufferBytes, iSocket)
    , iWriterRequest(iWriteBuffer)
    , iWriterChunked(iWriteBuffer)
    , iDechunker(iReaderUntil)
    , iConnected(false)
    , iRequestHeadersSent(false)
    , iResponseReceived(false)
    , iCode(-1)
    , iContentLength(-1)
    , iBytesRemaining(-1)
    , iMethod(Http::kMethodGet)
    , iPersistConnection(true)
    , iRequestChunked(false)
    , iRequestContentLengthSet(false)
    , iRequestContentLength(0)
{
    iReaderResponse.AddHeader(iHeaderConnection);
    iReaderResponse.AddHeader(iHeaderContentLength);
    iReaderResponse.AddHeader(iHeaderLocation);
    iReaderResponse.AddHeader(iHeaderTransferEncoding);

    //iSocket.LogVerbose(true);
}

SocketHttp::~SocketHttp()
{
    Disconnect();
}

void SocketHttp::SetUri(const Uri& aUri)
{
    // Check if new endpoint is same as current endpoint. If so, possible to re-use connection.

    if (aUri.Scheme() != kSchemeHttp
            && aUri.Scheme() != kSchemeHttps) {
        THROW(SocketHttpUriError);
    }

    TBool baseUrlChanged = true;
    if (iConnected) {
        if (aUri.Scheme() == iUri.Scheme()
                && aUri.Host() == iUri.Host()
                && aUri.Port() == iUri.Port()) {
            baseUrlChanged = false;
        }
    }
    LOG(kHttp, "SocketHttp::SetUri baseUrlChanged: %u\n\tiUri: %.*s\n\taUri: %.*s\n", baseUrlChanged, PBUF(iUri.AbsoluteUri()), PBUF(aUri.AbsoluteUri()));

    try {
        if (baseUrlChanged) {
            // Get port.
            TInt port = aUri.Port();
            if (port == Uri::kPortNotSpecified) {
                if (aUri.Scheme() == kSchemeHttps) {
                    port = kDefaultHttpsPort;
                }
                else {
                    port = kDefaultHttpPort;
                }
            }

            // Set up socket for HTTP or HTTPS.
            if (aUri.Scheme() == kSchemeHttps) {
                iSocket.SetSecure(true);
            }
            else {
                iSocket.SetSecure(false);
            }

            // Endpoint constructor may throw NetworkError if unable to resolve host.
            const Endpoint ep(port, aUri.Host());
            // New base URL is not the same as current base URL.
            // New connection required.
            Disconnect();
            iEndpoint.Replace(ep);
        }

        LOG(kHttp, "SocketHttp::SetUri iPersistConnection: %u\n", iPersistConnection);
        if (!iPersistConnection) {
            // Previous response required that this connection not be re-used.
            // Call Disconnect() here in case, for some unknown reason, previous client of this SocketHttp didn't read until end of stream and trigger Disconnect() in the Read() method, or in case there was some error in stream length, or for any other reason.
            Disconnect();
        }

        // Set iUri.
        try {
            iUri.Replace(aUri.AbsoluteUri());
        }
        catch (const UriError&) {
            THROW(SocketHttpUriError);
        }

        ResetResponseState();
    }
    catch (const NetworkError&) {
        LOG(kHttp, "SocketHttp::SetUri error setting address and port\n");
        THROW(SocketHttpUriError);
    }
}

const Brn SocketHttp::GetRequestMethod() const
{
    return iMethod;
}

void SocketHttp::SetRequestMethod(const Brx& aMethod)
{
    if (iConnected) {
        THROW(SocketHttpError);
    }
    // Invalid operation to set this following a call to Connect().
    if (aMethod == Http::kMethodGet) {
        iMethod.Set(Http::kMethodGet);
    }
    else if (aMethod == Http::kMethodPost) {
        iMethod.Set(Http::kMethodPost);
    }
    else {
        THROW(SocketHttpMethodInvalid);
    }
}

void SocketHttp::SetRequestChunked()
{
    if (iConnected) {
        THROW(SocketHttpError);
    }

    iRequestChunked = true;
    iRequestContentLengthSet = false;
    iRequestContentLength = 0;
    iWriterChunked.SetChunked(true);
}

void SocketHttp::SetRequestContentLength(TUint64 aContentLength)
{
    if (iConnected) {
        THROW(SocketHttpError);
    }

    iRequestChunked = false;
    iRequestContentLengthSet = true;
    iRequestContentLength = aContentLength;
    iWriterChunked.SetChunked(false);
}

void SocketHttp::SetRequestHeader(const Brx& aField, const Brx& aValue)
{
    if (iConnected) {
        THROW(SocketHttpError);
    }

    for (auto& h : iRequestHeaders) {
        if (h.Field() == aField) {
            h.Set(aValue);
            return;
        }
    }

    iRequestHeaders.push_back(RequestHeader(aField, aValue));
}

void SocketHttp::Connect()
{
    // Underlying socket may already be open and connected if this new connection is part of an HTTP persistent connection.

    if (!iConnected) {
        try {
            LOG(kHttp, "SocketHttp::Connect connecting...\n");
            iSocket.Connect(iEndpoint, iConnectTimeoutMs);
        }
        catch (const NetworkTimeout&) {
            iSocket.Close();
            LOG(kHttp, "<SocketHttp::Connect caught NetworkTimeout\n");
            THROW(SocketHttpConnectionError);
        }
        catch (const NetworkError&) {
            iSocket.Close();
            LOG(kHttp, "<SocketHttp::Connect caught NetworkError\n");
            THROW(SocketHttpConnectionError);
        }

        iConnected = true;
        LOG(kHttp, "<SocketHttp::Connect\n");
    }
}

void SocketHttp::Disconnect()
{
    LOG(kHttp, "SocketHttp::Disconnect\n");
    ResetResponseState();
    if (iConnected) {
        iSocket.Close();
        iConnected = false;
    }
}

void SocketHttp::Reset()
{
    // This does not disconnect socket. It's feasible that a client may be calling this to, e.g., clear custom request headers, before sending a new request to the same URI, so can make use of persistent connections in that scenario by not disconnecting.
    // So, safe to do this while iConnected is true.

    iRequestChunked = false;
    iRequestContentLengthSet = false;
    iRequestContentLength = 0;
    iRequestHeaders.clear();
    iMethod.Set(Http::kMethodGet);
}

IReader& SocketHttp::GetInputStream()
{
    Connect();
    SendRequestHeaders();
    ProcessResponse();
    return *this;
}

IWriter& SocketHttp::GetOutputStream()
{
    Connect();
    SendRequestHeaders();
    // If request has been specified as a POST request, caller should use IWriter returned from here to write POST data.
    return iWriterChunked;
}

TInt SocketHttp::GetResponseCode()
{
    Connect();
    SendRequestHeaders();
    ProcessResponse();
    return iCode;
}

TInt SocketHttp::GetContentLength()
{
    Connect();
    SendRequestHeaders();
    ProcessResponse();
    return iContentLength;
}

void SocketHttp::Interrupt(TBool aInterrupt)
{
    iSocket.Interrupt(aInterrupt);
}

Brn SocketHttp::Read(TUint aBytes)
{
    if (!iConnected || !iResponseReceived) {
        THROW(ReaderError);
    }

    TUint bytes = aBytes;
    if (iContentLength != -1) {
        // This stream has a content length, so need to keep track of bytes read to support correct IReader behaviour.
        if (iBytesRemaining < 0) {
            // Trying to read beyond end of stream.
            THROW(ReaderError);
        }
        if (iBytesRemaining == 0) {
            // End-of-stream signifier.
            iBytesRemaining = -1;

            if (!iPersistConnection) {
                // We're done reading from this stream and cannot re-use connection. Close connection and free up underlying resources.
                Disconnect();
            }

            return Brx::Empty();
        }
        if (iBytesRemaining > 0) {
            const TUint bytesRemaining = iBytesRemaining;
            if (bytesRemaining < bytes) {
                bytes = bytesRemaining;
            }
        }
    }

    ;
    try {
        Brn buf = iDechunker.Read(bytes);
        // If reading from a stream of known length, update bytes remaining.
        if (iBytesRemaining > 0) {
            iBytesRemaining -= buf.Bytes();
        }

        // Dechunker returns a buffer of 0 bytes in length when end-of-stream reached (conforming to IReader interface).
        if (buf.Bytes() == 0) {
            if (!iPersistConnection) {
                // We're done reading from this stream and cannot re-use connection. Close connection and free up underlying resources.
                Disconnect();
            }
        }

        return buf;
    }
    catch (const ReaderError&) {
        // Break in stream. Close connection.
        Disconnect();
        throw;
    }
}

void SocketHttp::ReadFlush()
{
    iDechunker.ReadFlush();
}

void SocketHttp::ReadInterrupt()
{
    iDechunker.ReadInterrupt();
}

void SocketHttp::WriteRequest(const Uri& aUri, Brx& aMethod)
{
    LOG(kHttp, ">SocketHttp::WriteRequest aUri: %.*s, aMethod: %.*s\n", PBUF(aUri.AbsoluteUri()), PBUF(aMethod));
    try {
        iWriterRequest.WriteMethod(aMethod, aUri.PathAndQuery(), Http::eHttp11);

        TInt port = aUri.Port();
        if (port == Uri::kPortNotSpecified) {
            port = kDefaultHttpPort;
        }
        Http::WriteHeaderHostAndPort(iWriterRequest, aUri.Host(), port);

        if (iRequestChunked) {
            iWriterRequest.WriteHeader(Http::kHeaderTransferEncoding, Http::kTransferEncodingChunked);
        }
        if (iRequestContentLengthSet) {
            auto& writer = iWriterRequest.WriteHeaderField(Http::kHeaderContentLength);
            writer.WriteUint64(iRequestContentLength);
        }

        if (iUserAgent.Bytes() > 0) {
            iWriterRequest.WriteHeader(Http::kHeaderUserAgent, iUserAgent);
        }

        for (const auto& h : iRequestHeaders) {
            iWriterRequest.WriteHeader(h.Field(), h.Value());
        }

        iWriterRequest.WriteFlush();
    }
    catch(const WriterError&) {
        LOG(kHttp, "<SocketHttp::WriteRequest caught WriterError\n");
        THROW(SocketHttpRequestError);
    }
}

TUint SocketHttp::ReadResponse()
{
    try {
        iReaderResponse.Read(iResponseTimeoutMs);
    }
    catch(HttpError&) {
        LOG(kHttp, "SocketHttp::ReadResponse caught HttpError\n");
        THROW(SocketHttpResponseError);
    }
    catch(ReaderError&) {
        LOG(kHttp, "SocketHttp::ReadResponse caught ReaderError\n");
        THROW(SocketHttpResponseError);
    }

    const auto code = iReaderResponse.Status().Code();
    LOG(kHttp, "SocketHttp::ReadResponse code %u\n", code);
    return code;
}

void SocketHttp::SendRequestHeaders()
{
    if (!iRequestHeadersSent) {
        // Send request headers.
        try {
            WriteRequest(iUri, iMethod);
            iRequestHeadersSent = true;
        }
        catch (const SocketHttpRequestError&) {
            Disconnect();
            THROW(SocketHttpConnectionError);
        }
    }

    // If this is a GET request, the request is now complete.
    // If this is a POST request, caller should call GetOutputStream() and write request body.
    // Following either of the above, GetInputStream(), GetResponseCode(), etc., can be called to handle responses to the request.
}

void SocketHttp::ProcessResponse()
{
    if (!iResponseReceived) {
        try {
            for (;;) { // loop until we don't get a redirection response (i.e. normally don't loop at all!)
                const TUint code = ReadResponse();

                // Check for redirection
                if (code >= HttpStatus::kRedirectionCodes && code < HttpStatus::kClientErrorCodes) {
                    // Only redirect if client has set this socket to follow redirects, and this is a GET request.
                    if (iFollowRedirects && iMethod == Http::kMethodGet) {
                        if (!iHeaderLocation.Received()) {
                            LOG(kHttp, "<SocketHttp::ProcessResponse expected redirection but did not receive a location field. code: %d\n", code);
                            THROW(SocketHttpError);
                        }

                        try {
                            Uri uri(iHeaderLocation.Location());
                            SetUri(uri);    // This will handle any disconnect if redirect is to a different endpoint.
                            Connect();
                            SendRequestHeaders();
                        }
                        catch (const UriError&) {
                            LOG(kHttp, "<SocketHttp::ProcessResponse caught UriError\n");
                            THROW(SocketHttpError);
                        }
                        continue;
                    }
                }
                else if (code >= HttpStatus::kClientErrorCodes) {
                    LOG(kHttp, "<SocketHttp::ProcessResponse received error code: %u\n", code);
                }

                // Not a redirect so is final response; set response state and return.
                if (code != 0) {
                    if (iHeaderTransferEncoding.IsChunked()) {
                        iDechunker.SetChunked(true);
                        iContentLength = -1;
                        iBytesRemaining = -1;
                    }
                    else {
                        iContentLength = iHeaderContentLength.ContentLength();
                        iBytesRemaining = iContentLength;
                    }
                    iResponseReceived = true;
                    iCode = code;

                    // See https://tools.ietf.org/html/rfc7230#section-6.3 for persistence evaluation logic.
                    iPersistConnection = true;
                    if (iHeaderConnection.Close()) {
                        iPersistConnection = false;
                    }
                    else if (iReaderResponse.Version() == Http::EVersion::eHttp11) {
                        iPersistConnection = true;
                    }
                    else if (iReaderResponse.Version() == Http::EVersion::eHttp10 && iHeaderConnection.KeepAlive()) {
                        iPersistConnection = true;
                    }
                    else {
                        iPersistConnection = false;
                    }

                    return;
                }
            }
        }
        catch (const SocketHttpRequestError&) {
            LOG(kHttp, "<SocketHttp::ProcessResponse caught SocketHttpRequestError\n");
            Disconnect();
            THROW(SocketHttpError);
        }
        catch (const SocketHttpResponseError&) {
            LOG(kHttp, "<SocketHttp::ProcessResponse caught SocketHttpResponseError\n");
            Disconnect();
            THROW(SocketHttpError);
        }
    }
}

void SocketHttp::ResetResponseState()
{
    // Does not clear iConnected value. Only done by Disconnect().
    if (iConnected) {
        iReaderResponse.Flush();
        iDechunker.ReadFlush();

        try {
            iWriterRequest.WriteFlush();
            iWriteBuffer.WriteFlush();
        }
        catch (const WriterError&) {
            // Nothing to do.
        }
    }

    iDechunker.SetChunked(false);
    iRequestHeadersSent = false;
    iResponseReceived = false;
    iCode = -1;
    iContentLength = -1;
    iBytesRemaining = -1;

    // Persistence is per-connection; not a global client-settable state of this socket.
    iPersistConnection = true;
}

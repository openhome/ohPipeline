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

const Brn SocketHttp::kSchemeHttp("http");

SocketHttp::SocketHttp(Environment& aEnv, const Brx& aUserAgent, TUint aReadBufferBytes, TUint aWriteBufferBytes, TUint aConnectTimeoutMs, TUint aResponseTimeoutMs, TUint aReceiveTimeoutMs, TBool aFollowRedirects)
    : iEnv(aEnv)
    , iUserAgent(aUserAgent)
    , iConnectTimeoutMs(aConnectTimeoutMs)
    , iResponseTimeoutMs(aResponseTimeoutMs)
    , iReceiveTimeoutMs(aReceiveTimeoutMs)
    , iFollowRedirects(aFollowRedirects)
    , iReadBuffer(aReadBufferBytes, iTcpClient)
    , iReaderUntil(aReadBufferBytes, iReadBuffer)
    , iReaderResponse(aEnv, iReaderUntil)
    , iWriteBuffer(aWriteBufferBytes, iTcpClient)
    , iWriterRequest(iWriteBuffer)
    , iDechunker(iReaderUntil)
    , iConnected(false)
    , iRequestHeadersSent(false)
    , iResponseReceived(false)
    , iCode(-1)
    , iContentLength(-1)
    , iBytesRemaining(-1)
    , iMethod(Http::kMethodGet)
    , iPersistConnection(true)
{
    iReaderResponse.AddHeader(iHeaderConnection);
    iReaderResponse.AddHeader(iHeaderContentLength);
    iReaderResponse.AddHeader(iHeaderLocation);
    iReaderResponse.AddHeader(iHeaderTransferEncoding);
}

SocketHttp::~SocketHttp()
{
    Disconnect();
}

void SocketHttp::SetUri(const Uri& aUri)
{
    // Check if new endpoint is same as current endpoint. If so, possible to re-use connection.

    if (aUri.Scheme() != kSchemeHttp) {
        THROW(SocketHttpUriError);
    }

    TBool baseUrlChanged = false;
    if (aUri.Scheme() != iUri.Scheme()
            || aUri.Host() != iUri.Host()
            || aUri.Port() != iUri.Port()) {
        baseUrlChanged = true;
    }
    LOG(kHttp, "SocketHttp::SetUri baseUrlChanged: %u\n\tiUri: %.*s\n\taUri: %.*s\n", baseUrlChanged, PBUF(iUri.AbsoluteUri()), PBUF(aUri.AbsoluteUri()));

    TInt port = aUri.Port();
    if (port == Uri::kPortNotSpecified) {
        port = kDefaultHttpPort;
    }

    try {
        if (baseUrlChanged) {
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

        // Set iUri here, as disconnect may have cleared it.
        try {
            iUri.Replace(aUri.AbsoluteUri());
        }
        catch (const UriError&) {
            THROW(SocketHttpUriError);
        }

        iReaderResponse.Flush();
        iDechunker.ReadFlush();
        iDechunker.SetChunked(false);
        iRequestHeadersSent = false;
        iResponseReceived = false;
        iCode = -1;
        iContentLength = -1;
        iBytesRemaining = -1;
        iPersistConnection = true;
        try {
            //iWriterRequest.WriteFlush();
            //iWriteBuffer.WriteFlush();
        }
        catch (const WriterError&) {
            // Nothing to do.
        }
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
    // FIXME - should maybe throw exception if already connected

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

void SocketHttp::Connect()
{
    // Underlying socket may already be open and connected if this new connection is part of an HTTP persistent connection.

    if (!iConnected) {
        try {
            LOG(kHttp, "SocketHttp::Connect connecting...\n");
            iTcpClient.Open(iEnv);
            iTcpClient.Connect(iEndpoint, iConnectTimeoutMs);
        }
        catch (const NetworkTimeout&) {
            iTcpClient.Close();
            LOG(kHttp, "<SocketHttp::Connect caught NetworkTimeout\n");
            THROW(SocketHttpConnectionError);
        }
        catch (const NetworkError&) {
            iTcpClient.Close();
            LOG(kHttp, "<SocketHttp::Connect caught NetworkError\n");
            THROW(SocketHttpConnectionError);
        }

        // Not all implementations support a receive timeout.
        // So, consume exceptions from such implementations.
        try {
            iTcpClient.SetRecvTimeout(iReceiveTimeoutMs);
        }
        catch (NetworkError&) {
            LOG(kHttp, "SocketHttp::Connect Unable to set recv timeout of %u ms\n", iReceiveTimeoutMs);
        }
        iConnected = true;
        LOG(kHttp, "<HttpReader::Connect\n");
    }
}

void SocketHttp::Disconnect()
{
    LOG(kHttp, "HttpReader::Disconnect\n");
    if (iConnected) {
        iReaderResponse.Flush();
        iDechunker.ReadFlush();
        try {
            // iWriterRequest.WriteFlush();
            // iWriteBuffer.WriteFlush();
        }
        catch (const WriterError&) {
            // Nothing to do.
            LOG(kHttp, "SocketHttp::Disconnect caught WriterError\n");
        }
        iTcpClient.Close();
    }
    iDechunker.SetChunked(false);
    iConnected = false;
    iRequestHeadersSent = false;
    iResponseReceived = false;
    iCode = -1;
    iContentLength = -1;
    iBytesRemaining = -1;
    iUri.Clear();
    iPersistConnection = true;

    try {
        iEndpoint.SetAddress(0);
        iEndpoint.SetPort(0);
    }
    catch (const NetworkError&) {
    }
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
    return iWriteBuffer;
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
    iTcpClient.Interrupt(aInterrupt);
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

        if (iUserAgent.Bytes() > 0) {
            iWriterRequest.WriteHeader(Http::kHeaderUserAgent, iUserAgent);
        }
        iWriterRequest.WriteFlush();
    }
    catch(const WriterError&) {
        LOG(kHttp, "<HttpReader::WriteRequest caught WriterError\n");
        THROW(SocketHttpRequestError);
    }
}

TUint SocketHttp::ReadResponse()
{
    try {
        iReaderResponse.Read(iResponseTimeoutMs);
    }
    catch(HttpError&) {
        LOG(kHttp, "HttpReader::ReadResponse caught HttpError\n");
        THROW(SocketHttpResponseError);
    }
    catch(ReaderError&) {
        LOG(kHttp, "HttpReader::ReadResponse caught ReaderError\n");
        THROW(SocketHttpResponseError);
    }

    const auto code = iReaderResponse.Status().Code();
    LOG(kHttp, "HttpReader::ReadResponse code %u\n", code);
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
            Disconnect();   // FIXME - correct, or up to caller to do?
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
                    if (iFollowRedirects && iMethod == Http::kMethodGet) {
                        if (!iHeaderLocation.Received()) {
                            LOG(kHttp, "<HttpReader::ProcessResponse expected redirection but did not receive a location field. code: %d\n", code);
                            THROW(SocketHttpError);
                        }

                        try {
                            Uri uri(iHeaderLocation.Location());



                            // URI may be redirecting to a different endpoint, so close connection before following redirect.
                            // NOTE: This makes no attempt to accomodate HTTP->HTTPS redirects or vice-versa.
                            Disconnect();



                            // FIXME - disconnect clears EVERY bit of state about this socket, including following redirects. Almost certainly unintended.
                            // Need to do SetUri() and then the following:
                            // Connect();
                            // SendRequestHeaders();




                            // FIXME - if this is done without Disconnect(), means can only redirect to same host.

                            WriteRequest(uri, iMethod);
                        }
                        catch (const UriError&) {
                            LOG(kHttp, "<HttpReader::ProcessResponse caught UriError\n");
                            THROW(SocketHttpError);
                        }
                        continue;
                    }
                    // Not following redirects.
                }
                else if (code >= HttpStatus::kClientErrorCodes) {
                    LOG(kHttp, "<HttpReader::ProcessResponse received error code: %u\n", code);
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
            LOG(kHttp, "<HttpReader::ProcessResponse caught SocketHttpRequestError\n");
            Disconnect();   // FIXME - correct, or up to caller to do?
            THROW(SocketHttpError);
        }
        catch (const SocketHttpResponseError&) {
            LOG(kHttp, "<HttpReader::ProcessResponse caught SocketHttpResponseError\n");
            Disconnect();   // FIXME - correct, or up to caller to do?
            THROW(SocketHttpError);
        }
    }
}

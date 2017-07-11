#include <OpenHome/Web/SimpleHttpServer.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/NetworkAdapterList.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Net/Private/Ssdp.h>
#include <OpenHome/Private/Env.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Net/Private/DviDevice.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Net/Private/XmlParser.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Net/Core/OhNet.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Net/Private/Error.h>
#include <OpenHome/OsWrapper.h>
#include <OpenHome/MimeTypes.h>
#include <OpenHome/Web/WebAppFramework.h> // for MimeUtils - should be in ohNet (MimeTypes.h)

#include <stdlib.h>
#include <functional>
#include <limits>

using namespace OpenHome;
using namespace OpenHome::Web;
using namespace OpenHome::Net;


// SimpleHttpSession

SimpleHttpSession::SimpleHttpSession(DvStack& aDvStack, TIpAddress aInterface, TUint aPort, Web::IResourceManager& aResourceManager)
    : iDvStack(aDvStack)
    , iInterface(aInterface)
    , iPort(aPort)
    , iResourceManager(aResourceManager)
    , iStarted(false)
    , iShutdownSem("DSUS", 1)
{
    iReadBuffer = new Srs<1024>(*this);
    iReaderUntil = new ReaderUntilS<4096>(*iReadBuffer);
    iReaderRequest = new ReaderHttpRequest(aDvStack.Env(), *iReaderUntil);
    iDechunker = new ReaderHttpChunked(*iReaderUntil);
    iWriterChunked = new WriterHttpChunked(*this);
    iWriterBuffer = new Sws<kMaxResponseBytes>(*iWriterChunked);
    iWriterResponse = new WriterHttpResponse(*iWriterBuffer);

    iReaderRequest->AddMethod(Http::kMethodGet);
    iReaderRequest->AddMethod(Http::kMethodPost);
    iReaderRequest->AddMethod(Http::kMethodHead);

    iReaderRequest->AddHeader(iHeaderHost);
    iReaderRequest->AddHeader(iHeaderContentLength);
    iReaderRequest->AddHeader(iHeaderTransferEncoding);
    iReaderRequest->AddHeader(iHeaderConnection);
    iReaderRequest->AddHeader(iHeaderExpect);
    iReaderRequest->AddHeader(iHeaderSid);
    iReaderRequest->AddHeader(iHeaderTimeout);
    iReaderRequest->AddHeader(iHeaderNt);
    iReaderRequest->AddHeader(iHeaderUserAgent);
}

SimpleHttpSession::~SimpleHttpSession()
{
    Interrupt(true);
    iShutdownSem.Wait();
    delete iWriterResponse;
    delete iWriterBuffer;
    delete iWriterChunked;
    delete iDechunker;
    delete iReaderRequest;
    delete iReaderUntil;
    delete iReadBuffer;
}

void SimpleHttpSession::StartSession()
{
    iStarted = true;
}

void SimpleHttpSession::Run()
{
    iShutdownSem.Wait();
    iErrorStatus = &HttpStatus::kOk;
    iReaderRequest->Flush();
    iWriterChunked->SetChunked(false);
    iInvocationService = NULL;
    iResourceWriterHeadersOnly = false;
    iSoapRequest.SetBytes(0);
    iDechunker->SetChunked(false);
    iDechunker->ReadFlush();
    iResponseStarted = false;
    iResponseEnded = false;
    Brn method;
    Brn reqUri;
    // check headers
    try {
        try {
            iReaderRequest->Read(kReadTimeoutMs);
        }
        catch (HttpError&) {
            Error(HttpStatus::kBadRequest);
        }
        if (iReaderRequest->MethodNotAllowed()) {
            Error(HttpStatus::kMethodNotAllowed);
        }
        method.Set(iReaderRequest->Method());
        iReaderRequest->UnescapeUri();

        reqUri.Set(iReaderRequest->Uri());
        LOG(kDvDevice, "Method: %.*s, uri: %.*s\n", PBUF(method), PBUF(reqUri));

        if (method == Http::kMethodGet) {
            Get();
        }
        else if (method == Http::kMethodHead) {
            iResourceWriterHeadersOnly = true;
            Get();
        }
        else if (method == Http::kMethodPost) {
            Post();
        }
    }
    catch (HttpError&) {
        LOG2(kDvDevice, kDvEvent, "HttpError handling %.*s for %.*s\n", PBUF(method), PBUF(reqUri));
        if (iErrorStatus == &HttpStatus::kOk) {
            iErrorStatus = &HttpStatus::kBadRequest;
        }
    }
    catch (ReaderError&) {
        LOG2(kDvDevice, kDvEvent, "ReaderError handling %.*s for %.*s\n", PBUF(method), PBUF(reqUri));
        if (iErrorStatus == &HttpStatus::kOk) {
            iErrorStatus = &HttpStatus::kBadRequest;
        }
    }
    catch (WriterError&) {
        LOG2(kDvDevice, kDvEvent, "WriterError handling %.*s for %.*s\n", PBUF(method), PBUF(reqUri));
    }
    catch (ResourceInvalid&) {
        LOG2(kDvDevice, kDvEvent, "ResourceInvalid handling %.*s for %.*s\n", PBUF(method), PBUF(reqUri));
    }
    try {
        if (!iResponseStarted) {
            if (iErrorStatus == &HttpStatus::kOk) {
                iErrorStatus = &HttpStatus::kNotFound;
            }
            iWriterResponse->WriteStatus(*iErrorStatus, Http::eHttp11);
            Http::WriteHeaderConnectionClose(*iWriterResponse);
            iWriterResponse->WriteFlush();
        }
        else if (!iResponseEnded) {
            iWriterResponse->WriteFlush();
        }
    }
    catch (WriterError&) {
        LOG2(kDvDevice, kDvEvent, "WriterError(2) handling %.*s for %.*s\n", PBUF(method), PBUF(reqUri));
    }
    iShutdownSem.Signal();
}

void SimpleHttpSession::Error(const HttpStatus& aStatus)
{
    iErrorStatus = &aStatus;
    THROW(HttpError);
}

void SimpleHttpSession::Get()
{
    auto reqVersion = iReaderRequest->Version();
    if (reqVersion == Http::eHttp11) {
        if (!iHeaderHost.Received()) {
            Error(HttpStatus::kBadRequest);
        }
    }

    // Try access requested resource.
    const Brx& uri = iReaderRequest->Uri();
    IResourceHandler* resourceHandler = CreateResourceHandler(uri);    // throws ResourceInvalid

    try {
        Brn mimeType = MimeUtils::MimeTypeFromUri(uri);
        LOG(kHttp, "HttpSession::Get URI: %.*s  Content-Type: %.*s\n", PBUF(uri), PBUF(mimeType));

        // Write response headers.
        iResponseStarted = true;
        iWriterResponse->WriteStatus(HttpStatus::kOk, reqVersion);
        IWriterAscii& writer = iWriterResponse->WriteHeaderField(Http::kHeaderContentType);
        writer.Write(mimeType);
        //writer.Write(Brn("; charset=\"utf-8\""));
        writer.WriteFlush();
        iWriterResponse->WriteHeader(Http::kHeaderConnection, Http::kConnectionClose);
        const TUint len = resourceHandler->Bytes();
        ASSERT(len > 0);    // Resource handler reporting incorrect byte count or corrupt resource.
        Http::WriteHeaderContentLength(*iWriterResponse, len);
        iWriterResponse->WriteFlush();

        // Write content.
        resourceHandler->Write(*iWriterBuffer);
        iWriterBuffer->WriteFlush(); // FIXME - move into iResourceWriter.Write()?
        resourceHandler->Destroy();
        iResponseEnded = true;
    }
    catch (Exception&) {
        // If ANY exception occurs need to free up resources.
        resourceHandler->Destroy();
        throw;
    }
}

void SimpleHttpSession::Post()
{
    Error(HttpStatus::kBadRequest);
}

IResourceHandler* SimpleHttpSession::CreateResourceHandler(const Brx& aResource)
{
    Parser p(aResource);
    p.Next('/');    // skip leading '/'
    Brn prefix = p.Next('/');
    Brn tail = p.Next('?'); // Read up to query string (if any).

    if (prefix != SimpleHttpServer::kResourcePrefix) {
        THROW(ResourceInvalid);
    }

    return iResourceManager.CreateResourceHandler(tail);
}

void SimpleHttpSession::WriteServerHeader(IWriterHttpHeader& aWriter)
{
    IWriterAscii& stream = aWriter.WriteHeaderField(Brn("SERVER"));
    TUint major, minor;
    Brn osName = Os::GetPlatformNameAndVersion(iDvStack.Env().OsCtx(), major, minor);
    stream.Write(osName);
    stream.Write('/');
    stream.WriteUint(major);
    stream.Write('.');
    stream.WriteUint(minor);
    stream.Write(Brn(" UPnP/1.1 ohNet/"));
    iDvStack.Env().GetVersion(major, minor);
    stream.WriteUint(major);
    stream.Write('.');
    stream.WriteUint(minor);
    stream.WriteFlush();
}

void SimpleHttpSession::WriteResourceBegin(TUint aTotalBytes, const TChar* aMimeType)
{
    if (iHeaderExpect.Continue()) {
        iWriterResponse->WriteStatus(HttpStatus::kContinue, Http::eHttp11);
        iWriterResponse->WriteFlush();
    }
    iWriterResponse->WriteStatus(HttpStatus::kOk, Http::eHttp11);
    if (aTotalBytes > 0) {
        Http::WriteHeaderContentLength(*iWriterResponse, aTotalBytes);
    }
    else {
        if (iReaderRequest->Version() == Http::eHttp11) { 
            iWriterResponse->WriteHeader(Http::kHeaderTransferEncoding, Http::kTransferEncodingChunked);
        }
    }
    if (aMimeType != NULL) {
        IWriterAscii& writer = iWriterResponse->WriteHeaderField(Http::kHeaderContentType);
        writer.Write(Brn(aMimeType));
        writer.Write(Brn("; charset=\"utf-8\""));
        writer.WriteFlush();
    }
    Http::WriteHeaderConnectionClose(*iWriterResponse);
    iWriterResponse->WriteFlush();
    if (aTotalBytes == 0) {
        if (iReaderRequest->Version() == Http::eHttp11) { 
            iWriterChunked->SetChunked(true);
        }
    }
    iResponseStarted = true;
}

void SimpleHttpSession::WriteResource(const TByte* aData, TUint aBytes)
{
    if (iResourceWriterHeadersOnly) {
        return;
    }
    Brn buf(aData, aBytes);
    iWriterBuffer->Write(buf);
}

void SimpleHttpSession::WriteResourceEnd()
{
    iResponseEnded = true;
    iWriterBuffer->WriteFlush();
}


// SimpleHttpServer

const Brn SimpleHttpServer::kResourcePrefix("SimpleHttpServer");

SimpleHttpServer::SimpleHttpServer(DvStack& aDvStack, Web::IResourceManager& aResourceManager, TUint aPort)
    : DviServer(aDvStack)
    , iDvStack(aDvStack)
    , iEnv(aDvStack.Env())
    , iServer(nullptr)
    , iPort(aPort)
    , iResourceManager(aResourceManager)
    , iStarted(false)
{
    Initialise();

    Functor functor = MakeFunctor(*this, &SimpleHttpServer::CurrentAdapterChanged);
    NetworkAdapterList& nifList = iEnv.NetworkAdapterList();
    iAdapterListenerId = nifList.AddCurrentChangeListener(functor, "SimpleHttpServer", false);

    CurrentAdapterChanged();
}

SimpleHttpServer::~SimpleHttpServer()
{
    Deinitialise();

    delete iServer;

    NetworkAdapterList& nifList = iEnv.NetworkAdapterList();
    nifList.RemoveCurrentChangeListener(iAdapterListenerId);
}

void SimpleHttpServer::Start()
{
    //AutoMutex amx(iMutex);
    ASSERT(!iStarted);
    iStarted = true;
    for (SimpleHttpSession& s : iSessions) {
        s.StartSession();
    }
}

void SimpleHttpServer::CurrentAdapterChanged()
{
    NetworkAdapterList& nifList = iEnv.NetworkAdapterList();
    AutoNetworkAdapterRef ref(iEnv, "SimpleHttpServer::CurrentAdapterChanged");
    NetworkAdapter* current = ref.Adapter();

    // Get current subnet, otherwise choose first from a list
    if (current == nullptr) {
        std::vector<NetworkAdapter*>* subnetList = nifList.CreateSubnetList();
        if (subnetList->size() > 0) {
            current = (*subnetList)[0];
        }
        NetworkAdapterList::DestroySubnetList(subnetList);
    }

    //AutoMutex amx(iMutex);
    if (current != nullptr) {
        delete iServer;
        iSessions.clear();
        iServer = CreateServer(*current);
        AddSessions(*current);
        if (iStarted) {
            for (SimpleHttpSession& s : iSessions) {
                s.StartSession();
            }
        }
        Endpoint ep(0, current->Address());
        TByte octets[4];
        ep.GetAddressOctets(octets);
        Log::Print("SimpleHttpServer: http://%d.%d.%d.%d:%d/%.*s\n", octets[0], octets[1], octets[2], octets[3], iServer->Port(), PBUF(kResourcePrefix));
    }
}

void SimpleHttpServer::AddSessions(const NetworkAdapter& aNif)
{
    for (TUint i=0; i<kServerThreads; i++) {
        Bws<20> name("SimpleHttpSession");
        Ascii::AppendDec(name, i+1);
        auto* session = new SimpleHttpSession(iDvStack, aNif.Address(), iServer->Port(), iResourceManager);
        iSessions.push_back(*session);
        iServer->Add(name.PtrZ(), session);
    }
}

SocketTcpServer* SimpleHttpServer::CreateServer(const NetworkAdapter& aNif)
{
    SocketTcpServer* server = new SocketTcpServer(iDvStack.Env(), "SimpleHttpServer", iPort, aNif.Address());
    return server;
}

void SimpleHttpServer::NotifyServerDeleted(TIpAddress aInterface)
{
}

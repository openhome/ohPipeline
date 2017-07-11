#ifndef HEADER_HTTP_SERVER
#define HEADER_HTTP_SERVER

#include <OpenHome/Types.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Private/DviDevice.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Net/Private/DviService.h>
#include <OpenHome/Net/Private/Subscription.h>
#include <OpenHome/Net/Private/Service.h>
#include <OpenHome/Net/Private/DviServer.h>
#include <OpenHome/Net/Private/DviSubscription.h>
#include <OpenHome/Web/ResourceHandler.h>

#include <vector>
#include <map>
#include <functional>

namespace OpenHome {
    class NetworkAdapter;

using namespace OpenHome::Net;

namespace Web {

class SimpleHttpSession : public SocketTcpSession, private Net::IResourceWriter
{
public:
    SimpleHttpSession(DvStack& aDvStack, TIpAddress aInterface, TUint aPort, IResourceManager& aResourceManager);
    ~SimpleHttpSession();
    void StartSession();
private:
    void Run();
    void Error(const HttpStatus& aStatus);
    void Get();
    void Post();
    IResourceHandler* CreateResourceHandler(const Brx& aResource);
    void WriteServerHeader(IWriterHttpHeader& aWriter);
    void InvocationReportErrorNoThrow(TUint aCode, const Brx& aDescription);
private: // IResourceWriter
    void WriteResourceBegin(TUint aTotalBytes, const TChar* aMimeType);
    void WriteResource(const TByte* aData, TUint aBytes);
    void WriteResourceEnd();
private:
    static const TUint kMaxRequestBytes = 64*1024;
    static const TUint kMaxResponseBytes = 4*1024;
    static const TUint kReadTimeoutMs = 5 * 1000;
    static const TUint kMaxRequestPathBytes = 256;
private:
    DvStack& iDvStack;
    TIpAddress iInterface;
    TUint iPort;
    IResourceManager& iResourceManager;
    TBool iStarted;
    Srx* iReadBuffer;
    ReaderUntil* iReaderUntil;
    ReaderHttpRequest* iReaderRequest;
    ReaderHttpChunked* iDechunker;
    WriterHttpChunked* iWriterChunked;
    Sws<kMaxResponseBytes>* iWriterBuffer;
    WriterHttpResponse* iWriterResponse;
    HttpHeaderHost iHeaderHost;
    HttpHeaderContentLength iHeaderContentLength;
    HttpHeaderTransferEncoding iHeaderTransferEncoding;
    HttpHeaderConnection iHeaderConnection;
    HttpHeaderExpect iHeaderExpect;
    HeaderSid iHeaderSid;
    HeaderTimeout iHeaderTimeout;
    HeaderNt iHeaderNt;
    HttpHeaderUserAgent iHeaderUserAgent;
    const HttpStatus* iErrorStatus;
    TBool iResponseStarted;
    TBool iResponseEnded;
    Bws<kMaxRequestBytes> iSoapRequest;
    Bws<kMaxRequestPathBytes> iMappedRequestUri;
    DviDevice* iInvocationDevice;
    DviService* iInvocationService;
    mutable Bws<128> iResourceUriPrefix;
    TBool iResourceWriterHeadersOnly;
    Semaphore iShutdownSem;
};


class SimpleHttpServer : public DviServer
{
public:
    static const TUint kServerThreads = 1;
    static const OpenHome::Brn kResourcePrefix;
public:
    SimpleHttpServer(DvStack& aDvStack, IResourceManager& iResourceManager, TUint aPort = 0);
    ~SimpleHttpServer();
    void Start();
protected: // from DviServer
    SocketTcpServer* CreateServer(const NetworkAdapter& aNif);
    void NotifyServerDeleted(TIpAddress aInterface);
private:
    void CurrentAdapterChanged();
    void AddSessions(const NetworkAdapter& aNif);
private:
    DvStack& iDvStack;
    Environment& iEnv;
    SocketTcpServer* iServer;
    TUint iPort;
    IResourceManager& iResourceManager;
    TBool iStarted;
    TUint iAdapterListenerId;
    std::vector<std::reference_wrapper<SimpleHttpSession>> iSessions;
};

} // namespace Web
} // namespace OpenHome

#endif // HEADER_HTTP_SERVER

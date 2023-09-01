#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Env.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/Http.h>

#include <memory>

EXCEPTION(RaatArtworkNotAvailable);
EXCEPTION(RaatArtworkTypeUnsupported);

namespace OpenHome {
    class NetworkAdapter;
namespace Av {

class IRaatArtworkServerObserver
{
public:
    virtual void ArtworkChanged(const Brx& aArtworkUri) = 0;
    virtual ~IRaatArtworkServerObserver() {}
};

class IRaatArtworkServer
{
public:
    virtual void SetArtwork(const Brx& aData, const Brx& aType) = 0;
    virtual void ClearArtwork() = 0;
    virtual void AddObserver(IRaatArtworkServerObserver& aObserver) = 0;
    virtual void RemoveObserver(IRaatArtworkServerObserver& aObserver) = 0;
    virtual ~IRaatArtworkServer() {}
};

class IRaatArtworkResource
{
public:
    virtual const Brx& Path() const = 0;
    virtual const Brx& Data() const = 0;
    virtual TUint Size() const = 0;
    virtual ~IRaatArtworkResource() {}
};

class IRaatArtworkProvider
{
public:
    virtual const IRaatArtworkResource& GetArtworkResource() = 0;
    virtual ~IRaatArtworkProvider() {}
};

class RaatArtworkResource : public IRaatArtworkResource
{
public:
    RaatArtworkResource(const Brx& aPath, const Brx& aData)
        : iPath(aPath)
        , iData(aData)
        , iSize(aData.Bytes())
        {}
public: // from IRaatArtworkResource
    const Brx& Path() const override { return iPath; }
    const Brx& Data() const override { return iData; }
    TUint Size() const override { return iSize; }
private:
    Bwh iPath;
    Bwh iData;
    TUint iSize;
};

class RaatArtworkHttpServer
    : public IRaatArtworkServer
    , public IRaatArtworkProvider
{
private:
    static const TChar* kAdapterCookie;
    static const Brn kResourcePrefix;
    static const std::map<Brn, Brn, BufferCmp> kMimeTypeFileExtensionMap;
public:
    RaatArtworkHttpServer(Environment& aEnv);
    ~RaatArtworkHttpServer();
public: // from IRaatArtworkServer
    void SetArtwork(const Brx& aData, const Brx& aType) override;
    void ClearArtwork() override;
    void AddObserver(IRaatArtworkServerObserver& aObserver) override;
    void RemoveObserver(IRaatArtworkServerObserver& aObserver) override;
private: // from IRaatArtworkProvider
    const IRaatArtworkResource& GetArtworkResource() override;
private:
    void CurrentAdapterChanged();
    void CreateResourcePath(const Brx& aType, Bwx& aPath);
    void NotifyObservers(const Brx& aUri);
private:
    Environment& iEnv;
    TUint iAdapterListenerId;
    NetworkAdapter* iAdapter;
    TUint iCount;
    Mutex iLock;

    Bws<64> iBaseUri;

    std::unique_ptr<SocketTcpServer> iServer;
    std::unique_ptr<RaatArtworkResource> iResource;
    std::vector<std::reference_wrapper<IRaatArtworkServerObserver>> iObservers;
};

class RaatArtworkHttpSession : public SocketTcpSession
{
public:
    RaatArtworkHttpSession(Environment& aEnv, IRaatArtworkProvider& aArtworkProvider);
    ~RaatArtworkHttpSession();
private: // from SocketTcpSession
    void Run() override;
private:
    Environment& iEnv;
    IRaatArtworkProvider& iArtworkProvider;
    Srx* iReadBuffer;
    ReaderUntil* iReaderUntil;
    ReaderHttpRequest* iReaderRequest;
    Swx* iWriterBuffer;
    WriterHttpResponse* iWriterResponse;
};

}
}
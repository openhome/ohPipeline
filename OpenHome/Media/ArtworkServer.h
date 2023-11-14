#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Env.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/Http.h>

#include <memory>

EXCEPTION(ArtworkNotAvailable);
EXCEPTION(ArtworkTypeUnsupported);

namespace OpenHome {
    class NetworkAdapter;
namespace Media {

class IArtworkServerObserver
{
public:
    virtual void ArtworkChanged(const Brx& aArtworkUri) = 0;
    virtual ~IArtworkServerObserver() {}
};

class IArtworkServer
{
public:
    virtual void SetArtwork(const Brx& aData, const Brx& aType) = 0;
    virtual void ClearArtwork() = 0;
    virtual void AddObserver(IArtworkServerObserver& aObserver) = 0;
    virtual void RemoveObserver(IArtworkServerObserver& aObserver) = 0;
    virtual ~IArtworkServer() {}
};

class IArtworkResource
{
public:
    virtual const Brx& Path() const = 0;
    virtual const Brx& Data() const = 0;
    virtual TUint Size() const = 0;
    virtual ~IArtworkResource() {}
};

class IArtworkProvider
{
public:
    virtual const IArtworkResource& GetArtworkResource() = 0;
    virtual ~IArtworkProvider() {}
};

class ArtworkResource : public IArtworkResource
{
public:
    ArtworkResource(const Brx& aPath, const Brx& aData)
        : iPath(aPath)
        , iData(aData)
        , iSize(aData.Bytes())
        {}
public: // from IArtworkResource
    const Brx& Path() const override { return iPath; }
    const Brx& Data() const override { return iData; }
    TUint Size() const override { return iSize; }
private:
    Bwh iPath;
    Bwh iData;
    TUint iSize;
};

class ArtworkHttpServer
    : public IArtworkServer
    , public IArtworkProvider
{
private:
    static const TChar* kAdapterCookie;
    static const Brn kResourcePrefix;
    static const std::map<Brn, Brn, BufferCmp> kMimeTypeFileExtensionMap;
public:
    ArtworkHttpServer(Environment& aEnv);
    ~ArtworkHttpServer();
public: // from IArtworkServer
    void SetArtwork(const Brx& aData, const Brx& aType) override;
    void ClearArtwork() override;
    void AddObserver(IArtworkServerObserver& aObserver) override;
    void RemoveObserver(IArtworkServerObserver& aObserver) override;
private: // from IArtworkProvider
    const IArtworkResource& GetArtworkResource() override;
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
    Mutex iLockObservers;

    Bws<64> iBaseUri;

    std::unique_ptr<SocketTcpServer> iServer;
    std::unique_ptr<ArtworkResource> iResource;
    std::vector<std::reference_wrapper<IArtworkServerObserver>> iObservers;
};

class ArtworkHttpSession : public SocketTcpSession
{
public:
    ArtworkHttpSession(Environment& aEnv, IArtworkProvider& aArtworkProvider);
    ~ArtworkHttpSession();
private: // from SocketTcpSession
    void Run() override;
private:
    IArtworkProvider& iArtworkProvider;
    Srx* iReadBuffer;
    ReaderUntil* iReaderUntil;
    ReaderHttpRequest* iReaderRequest;
    Swx* iWriterBuffer;
    WriterHttpResponse* iWriterResponse;
};

}
}
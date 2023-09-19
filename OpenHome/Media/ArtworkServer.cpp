#include <OpenHome/Media/ArtworkServer.h>
#include <OpenHome/Private/NetworkAdapterList.h>
#include <OpenHome/Private/Ascii.h>

#include <OpenHome/Private/Debug.h>

using namespace OpenHome;
using namespace OpenHome::Media;

// ArtworkServer

const TChar* ArtworkHttpServer::kAdapterCookie("ArtworkServer");
const Brn ArtworkHttpServer::kResourcePrefix("/artwork-");
const std::map<Brn, Brn, BufferCmp> ArtworkHttpServer::kMimeTypeFileExtensionMap = {
    { Brn("image/jpeg"), Brn(".jpeg") },
    { Brn("image/bmp"), Brn(".bmp") },
    { Brn("image/png"), Brn(".png") }
};

ArtworkHttpServer::ArtworkHttpServer(Environment& aEnv)
    : iEnv(aEnv)
    , iAdapterListenerId(0)
    , iAdapter(nullptr)
    , iCount(0)
    , iLock("ARTS")
    , iLockObservers("ARTO")
{
    NetworkAdapterList& nifList = iEnv.NetworkAdapterList();
    Functor functor = MakeFunctor(*this, &ArtworkHttpServer::CurrentAdapterChanged);
    iAdapterListenerId = nifList.AddCurrentChangeListener(functor, "ArtworkServer", true);
    CurrentAdapterChanged();
}

ArtworkHttpServer::~ArtworkHttpServer()
{
    NetworkAdapterList& nifList = iEnv.NetworkAdapterList();
    nifList.RemoveCurrentChangeListener(iAdapterListenerId);
    if (iAdapter != nullptr) {
        iAdapter->RemoveRef(kAdapterCookie);
    }
}

void ArtworkHttpServer::SetArtwork(const Brx& aData, const Brx& aType)
{
    Bws<128> uri;
    {
        AutoMutex _(iLock);
        Bws<32> path;
        CreateResourcePath(aType, path);

        uri.Append(iBaseUri);
        uri.Append(path);

        iResource.reset(new ArtworkResource(path, aData));
    }
    NotifyObservers(uri);
}

void ArtworkHttpServer::ClearArtwork()
{
    {
        AutoMutex _(iLock);
        iResource.reset();
    }
    NotifyObservers(Brx::Empty());
}


void ArtworkHttpServer::AddObserver(IArtworkServerObserver& aObserver)
{
    AutoMutex _(iLockObservers);
    iObservers.push_back(aObserver);
}

void ArtworkHttpServer::RemoveObserver(IArtworkServerObserver& aObserver)
{
    AutoMutex _(iLockObservers);
    for (auto it = iObservers.begin(); it != iObservers.end(); ++it) {
        auto& o = (*it).get();
        if (&o == &aObserver) {
            iObservers.erase(it);
            return;
        }
    }
}

const IArtworkResource& ArtworkHttpServer::GetArtworkResource()
{
    AutoMutex _(iLock);
    if (iResource == nullptr) {
        THROW(ArtworkNotAvailable);
    }
    return *iResource;
}

void ArtworkHttpServer::CurrentAdapterChanged()
{
    NetworkAdapterList& nifList = iEnv.NetworkAdapterList();
    NetworkAdapter* current = iEnv.NetworkAdapterList().CurrentAdapter(kAdapterCookie).Ptr();

    // If no current adapter, choose first (if any) from subnet list.
    if (current == nullptr) {
        auto* subnetList = nifList.CreateSubnetList();
        if (subnetList->size() > 0) {
            current = (*subnetList)[0];
            current->AddRef(kAdapterCookie);
        }
        NetworkAdapterList::DestroySubnetList(subnetList);
    }

    // Update our current adapter
    AutoMutex _(iLock);
    if (iAdapter != current) {
        if (iAdapter != nullptr) {
            iAdapter->RemoveRef(kAdapterCookie);
        }
        iAdapter = current;  // Ref already added to current above; ref passes to iCurrentAdapter (if current != nullptr).
    }
    else {
        if (current != nullptr) {
            current->RemoveRef(kAdapterCookie); // current is not different from iCurrentAdapter. Remove reference from current.
            current = nullptr;
        }
    }

    if (iAdapter != nullptr) {
        iServer.reset(new SocketTcpServer(iEnv, "ArtworkServer", 0, iAdapter->Address()));
        iServer->Add("ArtworkSession", new ArtworkHttpSession(iEnv, *this));

        Bws<64> uri;
        uri.Append("http://");
        Endpoint ep(iServer->Port(), iServer->Interface());
        ep.AppendEndpoint(uri);
        iBaseUri.Replace(uri);
    }
}

void ArtworkHttpServer::CreateResourcePath(const Brx& aType, Bwx& aPath)
{
    aPath.Append(kResourcePrefix);
    Ascii::AppendDec(aPath, iCount++);
    auto it = kMimeTypeFileExtensionMap.find(Brn(aType));
    if (it == kMimeTypeFileExtensionMap.end()) {
        Log::Print("ArtworkHttpServer::SetArtwork(), MIME type not supported\n");
        THROW(ArtworkTypeUnsupported);
    }
    aPath.Append(it->second);
}

void ArtworkHttpServer::NotifyObservers(const Brx& aUri)
{
    for (auto observer : iObservers) {
        observer.get().ArtworkChanged(aUri);
    }
}


// ArtworkHttpSession

ArtworkHttpSession::ArtworkHttpSession(Environment& aEnv, IArtworkProvider& aArtworkProvider)
    : iEnv(aEnv)
    , iArtworkProvider(aArtworkProvider)
{
    iReadBuffer = new Srs<1024>(*this);
    iReaderUntil = new ReaderUntilS<4096>(*iReadBuffer);
    iReaderRequest = new ReaderHttpRequest(aEnv, *iReaderUntil);
    iWriterBuffer = new Sws<8192>(*this);
    iWriterResponse = new WriterHttpResponse(*iWriterBuffer);

    iReaderRequest->AddMethod(Http::kMethodGet);
}

ArtworkHttpSession::~ArtworkHttpSession()
{
    Interrupt(true);
    delete iWriterResponse;
    delete iWriterBuffer;
    delete iReaderRequest;
    delete iReaderUntil;
    delete iReadBuffer;
}

void ArtworkHttpSession::Run()
{
    const HttpStatus* status = &HttpStatus::kOk;
    try {
        try {
            iReaderRequest->Read();
        }
        catch (HttpError&) {
            status = &HttpStatus::kBadRequest;
            THROW(HttpError);
        }
        catch (ReaderError&) {
            status = &HttpStatus::kBadRequest;
            THROW(HttpError);
        }
        if (iReaderRequest->MethodNotAllowed()) {
            status = &HttpStatus::kMethodNotAllowed;
            THROW(HttpError);
        }

        try {
            const auto& resource = iArtworkProvider.GetArtworkResource();
            if (iReaderRequest->Uri() != resource.Path()) {
                status = &HttpStatus::kNotFound;
                THROW(HttpError);
            }

            try {
                iWriterResponse->WriteStatus(*status, Http::eHttp11);
                Http::WriteHeaderContentLength(*iWriterResponse, resource.Size());
                Http::WriteHeaderConnectionClose(*iWriterResponse);
                iWriterResponse->WriteFlush();
                iWriterBuffer->Write(resource.Data());
                iWriterBuffer->WriteFlush();
            }
            catch (WriterError&) {}
        }
        catch (ArtworkNotAvailable&) {
            status = &HttpStatus::kNotFound;
            THROW(HttpError);
        }
    }
    catch (HttpError&) {
        try {
            iWriterResponse->WriteStatus(*status, Http::eHttp11);
            Http::WriteHeaderConnectionClose(*iWriterResponse);
            iWriterResponse->WriteFlush();
        }
        catch (Exception&) {}
    }
}


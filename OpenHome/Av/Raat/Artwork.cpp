#include <OpenHome/Av/Raat/Artwork.h>
#include <OpenHome/Private/NetworkAdapterList.h>
#include <OpenHome/Private/Ascii.h>

#include <OpenHome/Private/Debug.h>

using namespace OpenHome;
using namespace OpenHome::Av;

// RaatArtworkServer

const TChar* RaatArtworkHttpServer::kAdapterCookie("RaatArtworkServer");
const Brn RaatArtworkHttpServer::kResourcePrefix("/artwork-");
const std::map<Brn, Brn, BufferCmp> RaatArtworkHttpServer::kMimeTypeFileExtensionMap = {
    { Brn("image/jpeg"), Brn(".jpeg") },
    { Brn("image/bmp"), Brn(".bmp") },
    { Brn("image/png"), Brn(".png") }
};

RaatArtworkHttpServer::RaatArtworkHttpServer(Environment& aEnv)
    : iEnv(aEnv)
    , iAdapterListenerId(0)
    , iAdapter(nullptr)
    , iCount(0)
    , iLock("RART")
{
    NetworkAdapterList& nifList = iEnv.NetworkAdapterList();
    Functor functor = MakeFunctor(*this, &RaatArtworkHttpServer::CurrentAdapterChanged);
    iAdapterListenerId = nifList.AddCurrentChangeListener(functor, "RaatArtworkServer", true);
    CurrentAdapterChanged();
}

RaatArtworkHttpServer::~RaatArtworkHttpServer()
{
    NetworkAdapterList& nifList = iEnv.NetworkAdapterList();
    nifList.RemoveCurrentChangeListener(iAdapterListenerId);
    if (iAdapter != nullptr) {
        iAdapter->RemoveRef(kAdapterCookie);
    }
}

void RaatArtworkHttpServer::SetArtwork(const Brx& aData, const Brx& aType)
{
    Bws<128> uri;
    {
        AutoMutex _(iLock);
        Bws<32> path;
        CreateResourcePath(aType, path);

        uri.Append(iBaseUri);
        uri.Append(path);

        iResource.reset(new RaatArtworkResource(path, aData));
    }
    NotifyObservers(uri);
}

void RaatArtworkHttpServer::ClearArtwork()
{
    {
        AutoMutex _(iLock);
        iResource.reset();
    }
    NotifyObservers(Brx::Empty());
}


void RaatArtworkHttpServer::AddObserver(IRaatArtworkServerObserver& aObserver)
{
    iObservers.push_back(aObserver);
}

void RaatArtworkHttpServer::RemoveObserver(IRaatArtworkServerObserver& aObserver)
{

}

const IRaatArtworkResource& RaatArtworkHttpServer::GetArtworkResource()
{
    AutoMutex _(iLock);
    if (iResource == nullptr) {
        THROW(RaatArtworkNotAvailable);
    }
    return *iResource;
}

void RaatArtworkHttpServer::CurrentAdapterChanged()
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
        iServer->Add("ArtworkSession", new RaatArtworkHttpSession(iEnv, *this));

        Bws<64> uri;
        uri.Append("http://");
        Endpoint ep(iServer->Port(), iServer->Interface());
        ep.AppendEndpoint(uri);
        iBaseUri.Replace(uri);
    }
}

void RaatArtworkHttpServer::CreateResourcePath(const Brx& aType, Bwx& aPath)
{
    aPath.Append(kResourcePrefix);
    Ascii::AppendDec(aPath, iCount++);
    auto it = kMimeTypeFileExtensionMap.find(Brn(aType));
    if (it == kMimeTypeFileExtensionMap.end()) {
        Log::Print("RaatArtworkHttpServer::SetArtwork(), MIME type not supported\n");
        THROW(RaatArtworkTypeUnsupported);
    }
    aPath.Append(it->second);
}

void RaatArtworkHttpServer::NotifyObservers(const Brx& aUri)
{
    for (auto observer : iObservers) {
        observer.get().ArtworkChanged(aUri);
    }
}


// RaatArtworkHttpSession

RaatArtworkHttpSession::RaatArtworkHttpSession(Environment& aEnv, IRaatArtworkProvider& aArtworkProvider)
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

RaatArtworkHttpSession::~RaatArtworkHttpSession()
{
    Interrupt(true);
    delete iWriterResponse;
    delete iWriterBuffer;
    delete iReaderRequest;
    delete iReaderUntil;
    delete iReadBuffer;
}

void RaatArtworkHttpSession::Run()
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
        catch (RaatArtworkNotAvailable&) {
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


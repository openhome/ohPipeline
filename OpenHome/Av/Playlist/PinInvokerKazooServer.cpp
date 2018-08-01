#include <OpenHome/Av/Playlist/PinInvokerKazooServer.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Json.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Av/Pins/Pins.h>
#include <OpenHome/Av/OhMetadata.h>
#include <OpenHome/Av/Playlist/TrackDatabase.h>
#include <OpenHome/Net/Core/CpDevice.h>
#include <OpenHome/Net/Core/CpDeviceUpnp.h>
#include <OpenHome/Net/Core/FunctorCpDevice.h>
#include <OpenHome/Net/Core/CpDeviceDv.h>
#include <Generated/CpAvOpenhomeOrgPlaylist1.h>
#include <OpenHome/Net/Private/XmlParser.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Timer.h>
#include <OpenHome/Private/Uri.h>

#include <map>
#include <vector>
#include <utility>

using namespace OpenHome;
using namespace OpenHome::Net;
using namespace OpenHome::Av;

// DeviceListKazooServer

const Brn DeviceListKazooServer::kDomainUpnp("upnp.org");
const Brn DeviceListKazooServer::kServiceContentDirectory("ContentDirectory");

DeviceListKazooServer::DeviceListKazooServer(Environment& aEnv, CpStack& aCpStack)
    : iEnv(aEnv)
    , iLock("DLKS")
    , iSemAdded("DLKS", 0)
    , iCancelled(false)
{
    auto added = MakeFunctorCpDevice(*this, &DeviceListKazooServer::DeviceAdded);
    auto removed = MakeFunctorCpDevice(*this, &DeviceListKazooServer::DeviceRemoved);
    iDeviceList = new CpDeviceListUpnpServiceType(aCpStack, kDomainUpnp, kServiceContentDirectory,
                                                  1, added, removed);
}

DeviceListKazooServer::~DeviceListKazooServer()
{
    delete iDeviceList;
    for (auto kvp : iMap) {
        kvp.second->RemoveRef();
    }
}

void DeviceListKazooServer::GetPropertyServerUri(const Brx& aUdn, Bwx& aPsUri, TUint aTimeoutMs)
{
    auto timeout = Time::Now(iEnv) + aTimeoutMs;
    iLock.Wait();
    iSemAdded.Clear();
    Brn udn(aUdn);
    auto it = iMap.find(udn);
    iLock.Signal();
    while (it == iMap.end()) {
        auto waitTime = timeout - Time::Now(iEnv);
        if (waitTime == 0 || waitTime > timeout) {
            break;
        }
        iDeviceList->Refresh();
        iSemAdded.Wait(waitTime);
        {
            AutoMutex _(iLock);
            if (iCancelled) {
                THROW(PropertyServerNotFound);
            }
            it = iMap.find(udn);
        }
    }
    if (it == iMap.end()) {
        THROW(PropertyServerNotFound);
    }
    Brh xml;
    if (!it->second->GetAttribute("Upnp.DeviceXml", xml)) {
        THROW(PropertyServerNotFound);
    }
    try {
        /* Note that the following would not work against all UPnP devices.
           The Media Endpoint API is complex and lightly documented so we assume that no-one
           but Linn will ever implement it ...and that Linn's implementation will continue to
           use ohNet, which formats its device XML in predictable ways.*/
        Brn root = XmlParserBasic::Find("root", xml);
        Brn device = XmlParserBasic::Find("device", root);
        Brn presUrl = XmlParserBasic::Find("presentationURL", root);
        Brn psRoot = XmlParserBasic::Find("X_PATH", device);

        iUri.Replace(presUrl);
        aPsUri.ReplaceThrow(iUri.Scheme());
        aPsUri.AppendThrow("://");
        aPsUri.AppendThrow(iUri.Host());
        aPsUri.Append(':');
        Ascii::AppendDec(aPsUri, iUri.Port());
        aPsUri.AppendThrow(psRoot);
    }
    catch (XmlError&) {
        THROW(PropertyServerNotFound);
    }
}

void DeviceListKazooServer::Cancel()
{
    AutoMutex _(iLock);
    iCancelled = true;
    iSemAdded.Signal();
}

void DeviceListKazooServer::DeviceAdded(CpDevice& aDevice)
{
    AutoMutex _(iLock);
    aDevice.AddRef();
    Brn udn(aDevice.Udn());
    iMap.insert(std::pair<Brn, CpDevice*>(udn, &aDevice));
    iSemAdded.Signal();
}

void DeviceListKazooServer::DeviceRemoved(CpDevice& aDevice)
{
    AutoMutex _(iLock);
    Brn udn(aDevice.Udn());
    auto it = iMap.find(udn);
    if (it != iMap.end()) {
        it->second->RemoveRef();
        iMap.erase(it);
    }
}


//PinInvokerKazooServer

const TChar* PinInvokerKazooServer::kMode = "openhome.me";
const Brn PinInvokerKazooServer::kModeBuf(kMode);
const Brn PinInvokerKazooServer::kDomainUpnp("upnp.org");
const Brn PinInvokerKazooServer::kServiceContentDirectory("ContentDirectory");
const TUint PinInvokerKazooServer::kConnectTimeoutMs = 3000;
const Brn PinInvokerKazooServer::kHostAlbum("album");
const Brn PinInvokerKazooServer::kHostArtist("artist");
const Brn PinInvokerKazooServer::kHostContainer("container");
const Brn PinInvokerKazooServer::kHostGenre("genre");

PinInvokerKazooServer::PinInvokerKazooServer(Environment& aEnv,
                                             CpStack& aCpStack,
                                             DvDevice& aDevice,
                                             IThreadPool& aThreadPool)
    : iEnv(aEnv)
    , iCpStack(aCpStack)
    , iReaderBuf(iSocket)
    , iReaderUntil1(iReaderBuf)
    , iWriterBuf(iSocket)
    , iWriterRequest(iWriterBuf)
    , iReaderResponse(aEnv, iReaderUntil1)
    , iDechunker(iReaderUntil1)
    , iReaderUntil2(iDechunker)
    , iResponseBody(4 * 1024)
    , iSemDeviceFound("PiKS", 0)
    , iShuffle(false)
{
    iReaderResponse.AddHeader(iHeaderContentLength);
    iReaderResponse.AddHeader(iHeaderTransferEncoding);

    iThreadPoolHandle = aThreadPool.CreateHandle(MakeFunctor(*this, &PinInvokerKazooServer::ReadFromServer),
                                                 "PinInvokerKazooServer", ThreadPoolPriority::Medium);
    iDeviceList = new DeviceListKazooServer(aEnv, aCpStack);;
    iCpDeviceSelf = CpDeviceDv::New(aCpStack, aDevice);
    iProxyPlaylist = new CpProxyAvOpenhomeOrgPlaylist1(*iCpDeviceSelf);
}

PinInvokerKazooServer::~PinInvokerKazooServer()
{
    delete iDeviceList;
    iThreadPoolHandle->Destroy();
    delete iProxyPlaylist;
    iCpDeviceSelf->RemoveRef();
}

void PinInvokerKazooServer::Invoke(const IPin& aPin)
{
    if (aPin.Mode() != Brn(kMode)) {
        return;
    }

    iPinUri.Replace(aPin.Uri());
    iShuffle = aPin.Shuffle();
    Brn query(iPinUri.Query());
    query.Set(query.Split(1)); // queries being with '?' - we'd rather just deal with the query body
    Parser parser(query);
    iQueryKvps.clear();
    TBool complete = false;
    do {
        Brn key = parser.Next('=');
        Brn val = parser.Next('&');
        if (val.Bytes() == 0) {
            val.Set(parser.Remaining());
            complete = true;
        }
        iQueryKvps.push_back(std::pair<Brn, Brn>(key, val));

    } while (!complete);

    iUdn.ReplaceThrow(FromQuery("udn"));
    Bws<128> psUri;
    iDeviceList->GetPropertyServerUri(iUdn, psUri, 5000);
    iEndpointUri.Replace(psUri);
    (void)iThreadPoolHandle->TrySchedule();
}

const TChar* PinInvokerKazooServer::Mode() const
{
    return kMode;
}

Brn PinInvokerKazooServer::FromQuery(const TChar* aKey) const
{
    Brn key(aKey);
    Brn val;
    for (const auto& kvp : iQueryKvps) {
        if (kvp.first == key) {
            val.Set(kvp.second);
            break;
        }
    }
    if (val.Bytes() == 0) {
        Log::Print("PinInvokerKazooServer - no %s in query - %.*s\n", aKey, PBUF(iPinUri.Query()));
        THROW(PinUriError);
    }
    return val;
}

void PinInvokerKazooServer::ReadFromServer()
{
    iSocket.Open(iEnv);
    AutoSocketReader _(iSocket, iReaderUntil2);
    Endpoint ep(iEndpointUri.Port(), iEndpointUri.Host());
    iSocket.Connect(ep, kConnectTimeoutMs);
    //iSocket.LogVerbose(true);

    Bws<128> mePathBase;
    {   // get path for media endpoint
        WriteRequestReadResponse(iEndpointUri.PathAndQuery(), true);
        JsonParser parser;
        parser.Parse(iResponseBody.Buffer());
        JsonParser parser2;
        Brn endpoints = parser.String("me");
        parser2.Parse(endpoints);
        Brn endpoint = parser2.String(FromQuery("me"));
        JsonParser parser3;
        parser3.Parse(endpoint);
        Brn path = parser3.String("Path");
        mePathBase.AppendThrow(path);
        Log::Print("path is %.*s\n", PBUF(path));
    }

    Bws<64> sessionId;
    Bws<256> reqPath(mePathBase);
    {   // create session
        reqPath.AppendThrow("/create");
        WriteRequestReadResponse(reqPath, true);
        Parser p(iResponseBody.Buffer());
        (void)p.Next('\"');
        sessionId.AppendThrow(p.Next('\"'));
    }

    iProxyPlaylist->SyncDeleteAll();
    iProxyPlaylist->SyncSetShuffle(iShuffle);
    TUint lastTrackId = ITrackDatabase::kTrackIdNone;
    TUint playlistCapacity;
    iProxyPlaylist->SyncTracksMax(playlistCapacity);
    iPlaying = false;

    const Brx& host = iPinUri.Host();
    if (host == kHostAlbum) {
        Brn albumId(FromQuery("browse"));
        (void)AddAlbum(mePathBase, sessionId, albumId, lastTrackId, playlistCapacity);
    }
    else if (host == kHostContainer) {
        Brn tag(FromQuery("list"));
        const TUint total = List(mePathBase, sessionId, tag);
        for (TUint i = 0; i < total; i++) {
            Read(mePathBase, sessionId, i, 1);
            AddTrack(lastTrackId);
        }
    }
    else if (host == kHostArtist) {
        Brn artistId(FromQuery("browse"));
        const TUint total = Browse(mePathBase, sessionId, artistId);

        // read albums, one at a time
        for (TUint i = 0; i < total && playlistCapacity > 0; i++) {
            ReadIdAddAlbum(mePathBase, sessionId, artistId, i, lastTrackId, playlistCapacity);
        }
    }
    else if (host == kHostGenre) {
        Brn genreId(FromQuery("browse"));
        const TUint total = Browse(mePathBase, sessionId, genreId);
        // we expect to sometimes find more tracks than fit in a playlist
        // insert all tracks from a randomly selected album as a very coarse
        // way of randomising selected content
        const TUint startIndex = iEnv.Random(total);
        for (TUint i = startIndex; i < total && playlistCapacity > 0; i++) {
            ReadIdAddAlbum(mePathBase, sessionId, genreId, i, lastTrackId, playlistCapacity);
        }
        for (TUint i = 0; i < startIndex && playlistCapacity > 0; i++) {
            ReadIdAddAlbum(mePathBase, sessionId, genreId, i, lastTrackId, playlistCapacity);
        }
    }
    else {
        Log::Print("PinInvokerKazooServer - unhandled path in %.*s\n", PBUF(iPinUri.Query()));
    }

    // destroy session
    reqPath.ReplaceThrow(mePathBase);
    reqPath.AppendThrow("/destroy?session=");
    reqPath.AppendThrow(sessionId);
    WriteRequestReadResponse(reqPath, false);
}

void PinInvokerKazooServer::WriteRequestReadResponse(const Brx& aPathAndQuery, TBool aKeepAlive)
{
    iWriterRequest.WriteMethod(Http::kMethodGet, aPathAndQuery, Http::eHttp11);
    Http::WriteHeaderHostAndPort(iWriterRequest, iEndpointUri.Host(), iEndpointUri.Port());
    if (!aKeepAlive) {
        Http::WriteHeaderConnectionClose(iWriterRequest);
    }
    iWriterRequest.WriteFlush();
    iReaderResponse.Read();
    const TUint code = iReaderResponse.Status().Code();
    iDechunker.SetChunked(iHeaderTransferEncoding.IsChunked());
    iResponseBody.Reset();

    if (code != 200) {
        // FIXME - log error
        THROW(HttpError);
    }

    if (iDechunker.IsChunked()) {
        for (;;) {
            Brn buf = iReaderUntil2.Read(kReadBufBytes);
            if (buf.Bytes() == 0) {
                break;
            }
            iResponseBody.Write(buf);
        }
    }
    else {
        TUint count = iHeaderContentLength.ContentLength();
        while (count > 0) {
            Brn buf = iReaderUntil2.Read(kReadBufBytes);
            iResponseBody.Write(buf);
            count -= buf.Bytes();
        }
    }
}

TUint PinInvokerKazooServer::ParseTotal()
{
    JsonParser parser;
    parser.Parse(iResponseBody.Buffer());
    return (TUint)parser.Num("Total");
}

TUint PinInvokerKazooServer::Browse(const Brx& aMePath, const Brx& aSessionId, const Brx& aId)
{
    Bws<256> pathAndQuery(aMePath);
    pathAndQuery.AppendThrow("/browse?session=");
    pathAndQuery.AppendThrow(aSessionId);
    pathAndQuery.AppendThrow("&id=");
    pathAndQuery.AppendThrow(aId);
    WriteRequestReadResponse(pathAndQuery, true);
    return ParseTotal();
}

TUint PinInvokerKazooServer::List(const Brx& aMePath, const Brx& aSessionId, const Brx& aTag)
{
    Bws<256> pathAndQuery(aMePath);
    pathAndQuery.AppendThrow("/list?session=");
    pathAndQuery.AppendThrow(aSessionId);
    pathAndQuery.AppendThrow("&tag=");
    pathAndQuery.AppendThrow(aTag);
    WriteRequestReadResponse(pathAndQuery, true);
    return ParseTotal();
}

void PinInvokerKazooServer::Read(const Brx& aMePath, const Brx& aSessionId, TUint aIndex, TUint aCount)
{
    Bws<256> pathAndQuery(aMePath);
    pathAndQuery.AppendThrow("/read?session=");
    pathAndQuery.AppendThrow(aSessionId);
    pathAndQuery.AppendThrow("&index=");
    Ascii::AppendDec(pathAndQuery, aIndex);
    pathAndQuery.AppendThrow("&count=");
    Ascii::AppendDec(pathAndQuery, aCount);
    WriteRequestReadResponse(pathAndQuery, true);
}

void PinInvokerKazooServer::ReadIdAddAlbum(const Brx& aMePath, const Brx& aSessionId, const Brx& aContainerId,
                                           TUint aIndex, TUint& aInsertAfterId, TUint& aPlaylistCapacity)
{
    if (aIndex > 0) {
        (void)Browse(aMePath, aSessionId, aContainerId);
    }
    Read(aMePath, aSessionId, aIndex, 1);

    auto parserArray = JsonParserArray::Create(iResponseBody.Buffer());
    JsonParser parserObj;
    parserObj.Parse(parserArray.NextObject());
    Brn albumId = parserObj.String("Id");
    (void)AddAlbum(aMePath, aSessionId, albumId, aInsertAfterId, aPlaylistCapacity);
}

void PinInvokerKazooServer::AddAlbum(const Brx& aMePath, const Brx& aSessionId, const Brx& aId,
                                     TUint& aInsertAfterId, TUint& aPlaylistCapacity)
{
    const TUint total = Browse(aMePath, aSessionId, aId);
    // read tracks, one at a time
    for (TUint i = 0; i < total && aPlaylistCapacity > 0; i++) {
        Read(aMePath, aSessionId, i, 1);
        AddTrack(aInsertAfterId);
        aPlaylistCapacity--;
    }
}

void PinInvokerKazooServer::AddTrack(TUint& aInsertAfterId)
{
    iTrackUri.Replace(Brx::Empty());
    iTrackMetadata.Replace(Brx::Empty());
    OpenHomeMetadataBuf metadata;
    auto parserArray1 = JsonParserArray::Create(iResponseBody.Buffer());
    JsonParser parserObj;
    parserObj.Parse(parserArray1.NextObject());
    auto parserArray2 = JsonParserArray::Create(parserObj.String("Metadata"));
    try {
        for (;;) {
            auto parserArray3 = JsonParserArray::Create(parserArray2.NextArray());
            auto keyNum = Ascii::Uint(parserArray3.NextString());
            Brn key(OhMetadataKey(keyNum));
            if (key.Bytes() == 0) {
                continue;
            }
            try {
                for (;;) {
                    Brn val = parserArray3.NextStringEscaped();
                    metadata.push_back(std::pair<Brn, Brn>(key, val));
                }
            }
            catch (JsonArrayEnumerationComplete&) {}
        }
    }
    catch (JsonArrayEnumerationComplete&) {}
    metadata.push_back(std::pair<Brn, Brn>(Brn("type"), Brn("object.item.audioItem.musicTrack")));

    OhMetadata::ToUriDidlLite(metadata, iTrackUri, iTrackMetadata);
    iProxyPlaylist->SyncInsert(aInsertAfterId, iTrackUri, iTrackMetadata, aInsertAfterId);
    if (!iPlaying) {
        iProxyPlaylist->SyncPlay();
        iPlaying = true;
    }
}

const TChar* PinInvokerKazooServer::OhMetadataKey(TUint aKsTag)
{ // static
    // see https://github.com/linn/UI/blob/master/ohOs/src/ohOs.App.V1/Tag.cs
    // for list of integer tags supported by Kazoo Server, plus hints for their use

    switch (aKsTag)
    {
    case 101:
        return "description";
    case 102:
        return "channels";
    case 103:
        return "bitDepth";
    case 104:
        return "sampleRate";
    case 105:
        return "bitRate";
    case 106:
        return "duration";
    case 107: // codec
        return "";
    case 108:
        return "artist";
    case 109: // bpm
        return "";
    case 110:
        return "composer";
    case 111:
        return "conductor";
    case 112:
        return "disc";
    case 114:
        return "genre";
    case 115: // grouping
        return "";
    case 116: // lyrics
        return "";
    case 118:
        return "title";
    case 119:
        return "track";
    case 120:
        return "tracks";
    case 121:
        return "year";
    case 122:
        return "albumArtwork";
    case 123:
        return "uri";
    case 124: // weight
        return "";
    case 125: // availability
        return "";
    case 126: // favourited
        return "";
    case 201:
        return "albumTitle";
    case 202:
        return "albumArtist";
    case 203:
        return "albumArtwork";
    default: // Many more tags skipped. Its not clear whether any are required
        return "";
    }
}

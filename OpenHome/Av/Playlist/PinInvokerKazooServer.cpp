#include <OpenHome/Av/Playlist/PinInvokerKazooServer.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Json.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Av/Pins/Pins.h>
#include <OpenHome/Av/OhMetadata.h>
#include <OpenHome/Av/Playlist/DeviceListMediaServer.h>
#include <OpenHome/Av/Playlist/TrackDatabase.h>
#include <OpenHome/Net/Core/CpDeviceDv.h>
#include <Generated/CpAvOpenhomeOrgPlaylist1.h>
#include <OpenHome/Net/Private/XmlParser.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Timer.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Media/Debug.h>

#include <vector>
#include <utility>

using namespace OpenHome;
using namespace OpenHome::Net;
using namespace OpenHome::Av;


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
const Brn PinInvokerKazooServer::kHostPlaylist("playlist");
const Brn PinInvokerKazooServer::kResponseTracks("tracks");
const Brn PinInvokerKazooServer::kResponseAlbums("albums");

PinInvokerKazooServer::PinInvokerKazooServer(Environment& aEnv,
                                             CpStack& aCpStack,
                                             DvDevice& aDevice,
                                             IThreadPool& aThreadPool,
                                             DeviceListMediaServer& aDeviceList)
    : iEnv(aEnv)
    , iDeviceList(aDeviceList)
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
    iCpDeviceSelf = CpDeviceDv::New(aCpStack, aDevice);
    iProxyPlaylist = new CpProxyAvOpenhomeOrgPlaylist1(*iCpDeviceSelf);
}

PinInvokerKazooServer::~PinInvokerKazooServer()
{
    iThreadPoolHandle->Destroy();
    delete iProxyPlaylist;
    iCpDeviceSelf->RemoveRef();
}

void PinInvokerKazooServer::BeginInvoke(const IPin& aPin, Functor aCompleted)
{
    if (aPin.Mode() != Brn(kMode)) {
        return;
    }

    AutoPinComplete completion(aCompleted);
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

    Brn udn = FromQuery("udn");
    Bws<128> psUri;
    iDeviceList.GetPropertyServerUri(udn, psUri, 5000);
    iEndpointUri.Replace(psUri);
    iSocket.Interrupt(false);
    iSocket.Open(iEnv);
    completion.Cancel();
    iCompleted = aCompleted;
    (void)iThreadPoolHandle->TrySchedule();
}

void PinInvokerKazooServer::Cancel()
{
    iSocket.Interrupt(true);
}

const TChar* PinInvokerKazooServer::Mode() const
{
    return kMode;
}

TBool PinInvokerKazooServer::SupportsVersion(TUint version) const
{
    return version >= kMinSupportedVersion && version <= kMaxSupportedVersion;
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
        LOG_ERROR(kPipeline, "PinInvokerKazooServer - no %s in query - %.*s\n", aKey, PBUF(iPinUri.Query()));
        THROW(PinUriError);
    }
    return val;
}

void PinInvokerKazooServer::ReadFromServer()
{
    AutoFunctor _(iCompleted);
    AutoSocketReader __(iSocket, iReaderUntil2);
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
        Brn resp(FromQuery("response"));
        if (resp == kResponseTracks) {
            for (TUint i = 0; i < total; i++) {
                Read(mePathBase, sessionId, i, 1);
                AddTrack(lastTrackId);
            }
        }
        else if (resp == kResponseAlbums) {
            // We may find more tracks than fit in a playlist. Insert all tracks from a
            // randomly selected album as a very coarse way of randomising selected content.
            const TUint startIndex = iEnv.Random(total);
            TBool repositionCursor = false;
            for (TUint i = startIndex; i < total && playlistCapacity > 0; i++) {
                ListReadIdAddAlbum(mePathBase, sessionId, tag, i, lastTrackId, playlistCapacity, repositionCursor);
            }
            for (TUint i = 0; i < startIndex && playlistCapacity > 0; i++) {
                ListReadIdAddAlbum(mePathBase, sessionId, tag, i, lastTrackId, playlistCapacity, repositionCursor);
            }
        }
        else {
            LOG_ERROR(kPipeline, "PinInvokerKazooServer - unknown response type in %.*s\n", PBUF(iPinUri.Query()));
        }
    }
    else if (host == kHostArtist) {
        Brn artistId(FromQuery("browse"));
        const TUint total = Browse(mePathBase, sessionId, artistId);

        // read albums, one at a time
        TBool repositionCursor = false;
        for (TUint i = 0; i < total && playlistCapacity > 0; i++) {
            BrowseReadIdAddAlbum(mePathBase, sessionId, artistId, i, lastTrackId, playlistCapacity, repositionCursor);
        }
    }
    else if (host == kHostGenre) {
        Brn genreId(FromQuery("browse"));
        const TUint total = Browse(mePathBase, sessionId, genreId);
        // we expect to sometimes find more tracks than fit in a playlist
        // insert all tracks from a randomly selected album as a very coarse
        // way of randomising selected content
        const TUint startIndex = iEnv.Random(total);
        TBool repositionCursor = false;
        for (TUint i = startIndex; i < total && playlistCapacity > 0; i++) {
            BrowseReadIdAddAlbum(mePathBase, sessionId, genreId, i, lastTrackId, playlistCapacity, repositionCursor);
        }
        for (TUint i = 0; i < startIndex && playlistCapacity > 0; i++) {
            BrowseReadIdAddAlbum(mePathBase, sessionId, genreId, i, lastTrackId, playlistCapacity, repositionCursor);
        }
    }
    else if (host == kHostPlaylist) {
        Brn id(FromQuery("browse"));
        (void)AddAlbum(mePathBase, sessionId, id, lastTrackId, playlistCapacity); // not actually an album, but KS doesn't have a better term for a container of tracks...
    }
    else {
        LOG_ERROR(kPipeline, "PinInvokerKazooServer - unhandled path in %.*s\n", PBUF(iPinUri.Query()));
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
        LOG_ERROR(kPipeline, "PinInvokerKazooServer::WriteRequestReadResponse http error %d from query %.*s\n",
                             code, PBUF(aPathAndQuery));
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

void PinInvokerKazooServer::ReadIdAddAlbum(const Brx& aMePath, const Brx& aSessionId,
                                           TUint aIndex, TUint& aInsertAfterId, TUint& aPlaylistCapacity)
{
    Read(aMePath, aSessionId, aIndex, 1);

    auto parserArray = JsonParserArray::Create(iResponseBody.Buffer());
    JsonParser parserObj;
    parserObj.Parse(parserArray.NextObject());
    Brn albumId = parserObj.String("Id");
    (void)AddAlbum(aMePath, aSessionId, albumId, aInsertAfterId, aPlaylistCapacity);
}

void PinInvokerKazooServer::BrowseReadIdAddAlbum(const Brx& aMePath, const Brx& aSessionId, const Brx& aContainerId,
                                                 TUint aIndex, TUint& aInsertAfterId, TUint& aPlaylistCapacity,
                                                 TBool& aRepositionCursor)
{
    if (aRepositionCursor) {
        (void)Browse(aMePath, aSessionId, aContainerId);
    }
    else {
        aRepositionCursor = true;
    }
    ReadIdAddAlbum(aMePath, aSessionId, aIndex, aInsertAfterId, aPlaylistCapacity);
}

void PinInvokerKazooServer::ListReadIdAddAlbum(const Brx& aMePath, const Brx& aSessionId, const Brx& aTag,
                                               TUint aIndex, TUint& aInsertAfterId, TUint& aPlaylistCapacity,
                                               TBool& aRepositionCursor)
{
    if (aRepositionCursor) {
        (void)List(aMePath, aSessionId, aTag);
    }
    else {
        aRepositionCursor = true;
    }
    ReadIdAddAlbum(aMePath, aSessionId, aIndex, aInsertAfterId, aPlaylistCapacity);
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
    try {
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
                        Brn val = parserArray3.NextStringEscaped(Json::Encoding::Utf16);
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
    catch (AssertionFailed&) {
        throw;
    }
    catch (Exception& ex) {
        LOG_ERROR(kPipeline, "PinInvokerKazooServer::AddTrack exception - %s from %s:%d - processing %.*s\n",
                             ex.Message(), ex.File(), ex.Line(), PBUF(iResponseBody.Buffer()));
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

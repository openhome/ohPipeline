#include <OpenHome/Av/Qobuz/Qobuz.h>
#include <OpenHome/Av/Qobuz/QobuzMetadata.h>
#include <OpenHome/Av/Qobuz/QobuzPins.h>
#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Types.h>
#include <OpenHome/SocketSsl.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Av/Utils/FormUrl.h>
#include <OpenHome/Json.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Net/Core/CpDeviceDv.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Net;
using namespace OpenHome::Configuration;

// Pin mode
static const TChar* kPinModeQobuz = "qobuz";
static const Brn    kBufPinModeQobuz = Brn(kPinModeQobuz);

// Pin types
static const TChar* kPinTypeArtist = "artist";
static const TChar* kPinTypeAlbum = "album";
static const TChar* kPinTypePlaylist = "playlist";
static const TChar* kPinTypeTrack = "track";
static const TChar* kPinTypeContainer = "container";

// Pin params
static const TChar* kPinKeyId = "id";
static const TChar* kPinKeyTrackId = "trackId";
static const TChar* kPinKeyPath = "path";
static const TChar* kPinKeyResponseType = "response";

// Pin response types
static const TChar* kPinResponseTracks = "tracks";
static const TChar* kPinResponseAlbums = "albums";
static const TChar* kPinResponsePlaylists = "playlists";

QobuzPins::QobuzPins(Qobuz& aQobuz, 
                     Environment& aEnv,
                     DvDeviceStandard& aDevice,
                     Media::TrackFactory& aTrackFactory, 
                     CpStack& aCpStack, 
                     IThreadPool& aThreadPool)
    : iLock("QPIN")
    , iQobuz(aQobuz)
    , iJsonResponse(kJsonResponseChunks)
    , iQobuzMetadata(aTrackFactory)
    , iPin(iPinIdProvider)
    , iEnv(aEnv)
    , iInterrupted(false)
{
    CpDeviceDv* cpDevice = CpDeviceDv::New(aCpStack, aDevice);
    iCpPlaylist = new CpProxyAvOpenhomeOrgPlaylist1(*cpDevice);
    cpDevice->RemoveRef(); // iProxy will have claimed a reference to the device so no need for us to hang onto another
    iThreadPoolHandle = aThreadPool.CreateHandle(MakeFunctor(*this, &QobuzPins::Invoke),
                                                 "QobuzPins", ThreadPoolPriority::Medium);
}

QobuzPins::~QobuzPins()
{
    iThreadPoolHandle->Destroy();
    delete iCpPlaylist;
}

void QobuzPins::BeginInvoke(const IPin& aPin, Functor aCompleted)
{
    if (aPin.Mode() != kBufPinModeQobuz) {
        return;
    }
    AutoPinComplete completion(aCompleted);
    iInterrupted.store(false);
    iQobuz.Interrupt(false);
    iQobuz.Login(iToken);
    (void)iPin.TryUpdate(aPin.Mode(), aPin.Type(), aPin.Uri(), aPin.Title(),
                         aPin.Description(), aPin.ArtworkUri(), aPin.Shuffle());
    completion.Cancel();
    iCompleted = aCompleted;
    (void)iThreadPoolHandle->TrySchedule();
}

void QobuzPins::Cancel()
{
    iInterrupted.store(true);
    iQobuz.Interrupt(true);
}

const TChar* QobuzPins::Mode() const
{
    return kPinModeQobuz;
}

void QobuzPins::Invoke()
{
    AutoFunctor _(iCompleted);
    iCpPlaylist->SyncTracksMax(iMaxPlaylistTracks);
    TBool res = false;
    try {
        PinUri pinUri(iPin);
        Brn val;
        if (Brn(pinUri.Type()) == Brn(kPinTypeTrack)) {
            if (pinUri.TryGetValue(kPinKeyTrackId, val)) {
                res = LoadByStringQuery(val, QobuzMetadata::eTrack, iPin.Shuffle());
            }
            else {
                THROW(PinUriMissingRequiredParameter);
            }
        }
        else if (Brn(pinUri.Type()) == Brn(kPinTypeContainer) ||
                 Brn(pinUri.Type()) == Brn(kPinTypePlaylist) ||
                 Brn(pinUri.Type()) == Brn(kPinTypeArtist) ||
                 Brn(pinUri.Type()) == Brn(kPinTypeAlbum)) {
            if (pinUri.TryGetValue(kPinKeyPath, val)) {
                res = LoadByPath(val, pinUri, iPin.Shuffle());
            }
            else if (pinUri.TryGetValue(kPinKeyId, val)) {
                // test only - load by string query as if done by a control point
                res = LoadByStringQuery(val, QobuzMetadata::StringToIdType(pinUri.Type()), iPin.Shuffle());
            }
            else {
                THROW(PinUriMissingRequiredParameter);
            }
        }
        else {
            LOG_ERROR(kPipeline, "QobuzPins::Invoke - unsupported type - %.*s\n", PBUF(iPin.Type()));
            THROW(PinTypeNotSupported);
        }
    }
    catch (PinUriMissingRequiredParameter&) {
        LOG_ERROR(kPipeline, "QobuzPins::Invoke - missing parameter in %.*s\n", PBUF(iPin.Uri()));
        throw;
    }

    if (!res) {
        THROW(PinInvokeError);
    }
}

TBool QobuzPins::LoadByPath(const Brx& aPath, const PinUri& aPinUri, TBool aShuffle)
{
    TBool res = false;
    Brn response(Brx::Empty());
    aPinUri.TryGetValue(kPinKeyResponseType, response);
    if (response == Brn(kPinResponseTracks)) {
        res = LoadTracks(aPath, aShuffle);
    }
    else if (response == Brn(kPinResponseAlbums)) {
        res = LoadContainers(aPath, QobuzMetadata::eAlbum, aShuffle);
    }
    else if (response == Brn(kPinResponsePlaylists)) {
        res = LoadContainers(aPath, QobuzMetadata::ePlaylist, aShuffle);
    }
    else {
        THROW(PinUriMissingRequiredParameter);
    }
    return res;
}

TBool QobuzPins::LoadByStringQuery(const Brx& aQuery, QobuzMetadata::EIdType aIdType, TBool aShuffle)
{
    AutoMutex _(iLock);
    TUint lastId = 0;
    JsonParser parser;
    InitPlaylist(aShuffle);
    Bwh inputBuf(64);
    TUint tracksFound = 0;

    try {
        if (aQuery.Bytes() == 0) {
            return false;
        }
        // track/artist/album/playlist search string to id
        else if (!IsValidId(aQuery, aIdType)) {
            iJsonResponse.Reset();
            TBool success = iQobuz.TryGetId(iJsonResponse, aQuery, aIdType); // send request to Qobuz
            if (!success) {
                return false;
            }
            inputBuf.ReplaceThrow(QobuzMetadata::FirstIdFromJson(iJsonResponse.Buffer(), aIdType)); // parse response from Qobuz
            if (inputBuf.Bytes() == 0) {
                return false;
            }
        }
        else {
            inputBuf.ReplaceThrow(aQuery);
        }

        try {
            lastId = LoadTracksById(inputBuf, aIdType, lastId, tracksFound);
        }
        catch (PinNothingToPlay&) {
        }
    }   
    catch (Exception& ex) {
        LOG_ERROR(kPipeline, "%s in QobuzPins::LoadByStringQuery\n", ex.Message());
        return false;
    }

    if (tracksFound == 0) {
        THROW(PinNothingToPlay);
    }

    return lastId;
}

TBool QobuzPins::LoadTracks(const Brx& aPath, TBool aShuffle)
{
    AutoMutex _(iLock);
    TUint lastId = 0;
    InitPlaylist(aShuffle);
    TUint tracksFound = 0;

    try {
        if (aPath.Bytes() == 0) {
            return false;
        }
        try {
            lastId = LoadTracksById(aPath, QobuzMetadata::eNone, lastId, tracksFound);
        }
        catch (PinNothingToPlay&) {
        }
    }   
    catch (Exception& ex) {
        LOG_ERROR(kMedia, "%s in QobuzPins::LoadTracks\n", ex.Message());
        return false;
    }

    if (tracksFound == 0) {
        THROW(PinNothingToPlay);
    }

    return lastId;
}

TBool QobuzPins::LoadContainers(const Brx& aPath, QobuzMetadata::EIdType aIdType, TBool aShuffle)
{
    AutoMutex _(iLock);
    JsonParser parser;
    InitPlaylist(aShuffle);
    TUint lastId = 0;
    TUint tracksFound = 0;
    TUint containersFound = 0;
    Bwh containerIds[kItemLimitPerRequest];
    for (TUint i = 0; i < kItemLimitPerRequest; i++) {
        containerIds[i].Grow(20);
    }

    TUint start, end;
    TUint total = GetTotalItems(parser, aPath, QobuzMetadata::eNone, true, start, end); // aIdType relevant to tracks, not containers
    TUint offset = start;

    do {
        try {
            iJsonResponse.Reset();
            TBool success = iQobuz.TryGetIdsByRequest(iJsonResponse, aPath, kItemLimitPerRequest, offset); // send request to Qobuz
            if (!success) {
                return false;
            }
            UpdateOffset(total, end, true, offset);
            
            parser.Reset();
            parser.Parse(iJsonResponse.Buffer());
            TUint idCount = 0;
            if (parser.HasKey(Brn("albums"))) { 
                parser.Parse(parser.String(Brn("albums")));
            }
            else if (parser.HasKey(Brn("playlists"))) { 
                parser.Parse(parser.String(Brn("playlists")));
            }
            auto parserItems = JsonParserArray::Create(parser.String(Brn("items")));
            JsonParser parserItem;
            try {
                for (TUint i = 0; i < kItemLimitPerRequest; i++) {
                    parserItem.Parse(parserItems.NextObject());
                    containerIds[i].ReplaceThrow(parserItem.String(Brn("id"))); // parse response from Qobuz
                    idCount++;
                    if (containerIds[i].Bytes() == 0) {
                        return false;
                    }
                }
            }
            catch (JsonArrayEnumerationComplete&) {}
            for (TUint j = 0; j < idCount; j++) {
                try {
                    lastId = LoadTracksById(containerIds[j], aIdType, lastId, tracksFound);
                }
                catch (PinNothingToPlay&) {
                }
                containersFound++;
                if ( (tracksFound >= iMaxPlaylistTracks) || (containersFound >= total) ) {
                    return true;
                }
            }
        }   
        catch (Exception& ex) {
            LOG_ERROR(kPipeline, "%s in QobuzPins::LoadContainers\n", ex.Message());
            return false;
        }
    } while (offset != end);

    if (tracksFound == 0) {
        THROW(PinNothingToPlay);
    }

    return true;
}

TUint QobuzPins::LoadTracksById(const Brx& aId, QobuzMetadata::EIdType aIdType, TUint aPlaylistId, TUint& aCount)
{
    if (iInterrupted.load()) {
        LOG(kMedia, "QobuzPins::LoadTracksById - interrupted\n");
        THROW(PinInterrupted);
    }

    TUint newId = 0;
    TUint currId = aPlaylistId;
    TBool initPlay = (aPlaylistId == 0);
    TBool isPlayable = false;
    JsonParser parser;
    Media::Track* track = nullptr;

    TUint start, end;
    TUint total = GetTotalItems(parser, aId, aIdType, false, start, end);
    TUint offset = start;

    // id to list of tracks
    LOG(kMedia, "QobuzPins::LoadTracksById: %.*s\n", PBUF(aId));
    do {
        try {
            iJsonResponse.Reset();
            TBool success = false;
            auto connection = aCount < iMaxPlaylistTracks - 1 ? Qobuz::Connection::KeepAlive : Qobuz::Connection::Close;
            if (aIdType == QobuzMetadata::eNone) {
                success = iQobuz.TryGetIdsByRequest(iJsonResponse, aId, kItemLimitPerRequest, offset, connection);
            }
            else {
                success = iQobuz.TryGetTracksById(iJsonResponse, aId, aIdType, kItemLimitPerRequest, offset, connection);
            }
            if (!success) {
                THROW(PinNothingToPlay);
            }
            UpdateOffset(total, end, false, offset);

            parser.Reset();
            parser.Parse(iJsonResponse.Buffer());
            try {
                if (parser.HasKey(Brn("tracks"))) { 
                    parser.Parse(parser.String(Brn("tracks")));
                }
                if (parser.HasKey("items")) {
                    auto parserItems = JsonParserArray::Create(parser.String(Brn("items")));
                    for (;;) {
                        track = iQobuzMetadata.TrackFromJson(iJsonResponse.Buffer(), parserItems.NextObject(), aIdType);
                        if (track != nullptr) {
                            aCount++;
                            iCpPlaylist->SyncInsert(currId, (*track).Uri(), (*track).MetaData(), newId);
                            track->RemoveRef();
                            track = nullptr;
                            currId = newId;
                            isPlayable = true;
                            if (aCount >= iMaxPlaylistTracks) {
                                offset = end; // force exit as we could be part way through a group of tracks
                                break;
                            }
                        }
                    }
                }
                else {
                    track = iQobuzMetadata.TrackFromJson(iJsonResponse.Buffer(), iJsonResponse.Buffer(), aIdType);
                    if (track != nullptr) {
                        aCount++;
                        iCpPlaylist->SyncInsert(currId, (*track).Uri(), (*track).MetaData(), newId);
                        track->RemoveRef();
                        track = nullptr;
                        currId = newId;
                        isPlayable = true;
                    }
                }
            }
            catch (JsonArrayEnumerationComplete&) {}
            
            if (initPlay && isPlayable) {
                initPlay = false;
                iCpPlaylist->SyncPlay();
            }
        }
        catch (Exception& ex) {
            LOG_ERROR(kPipeline, "%s in QobuzPins::LoadTracksById \n", ex.Message());
            if (track != nullptr) {
                track->RemoveRef();
                track = nullptr;
            }
            throw;
        }
    } while (offset != end);

    if (!isPlayable) {
        THROW(PinNothingToPlay);
    }

    return currId;
}

TUint QobuzPins::GetTotalItems(JsonParser& aParser, const Brx& aId, QobuzMetadata::EIdType aIdType, TBool aIsContainer, TUint& aStartIndex, TUint& aEndIndex)
{
    TUint total = 0;
    try {
        iJsonResponse.Reset();
        TBool success = false;
        if (aIdType == QobuzMetadata::eNone) {
            success = iQobuz.TryGetIdsByRequest(iJsonResponse, aId, 1, 0);
        }
        else {
            success = iQobuz.TryGetTracksById(iJsonResponse, aId, aIdType, 1, 0);
        }
        if (success) {
            aParser.Reset();
            aParser.Parse(iJsonResponse.Buffer());

            if (aParser.HasKey(Brn("albums"))) { 
                aParser.Parse(aParser.String(Brn("albums")));
            }
            else if (aParser.HasKey(Brn("playlists"))) { 
                aParser.Parse(aParser.String(Brn("playlists")));
            }
            else if (aParser.HasKey(Brn("tracks"))) { 
                aParser.Parse(aParser.String(Brn("tracks")));
            }

            if (aParser.HasKey(Brn("items"))) {
                total = aParser.Num(Brn("total"));
            }
            else {
                total = 1;
            }
        }
    }
    catch (Exception&) {
    }

    if (total == 0) {
        THROW(PinNothingToPlay);
    }

    // determine order for retrieving items
    aStartIndex = 0;
    aEndIndex = total;

    if (aIsContainer) {
        aStartIndex = iEnv.Random(total);
        if (aStartIndex > 0) {
            aEndIndex = aStartIndex;
        }
    }
    else {
        if (total > iMaxPlaylistTracks) {
            aStartIndex = iEnv.Random(total);
            if (iMaxPlaylistTracks > (total - aStartIndex)) {
                aEndIndex = iMaxPlaylistTracks - (total - aStartIndex); 
            }
            else {
                aEndIndex = iMaxPlaylistTracks + aStartIndex;
            }
        }
    }
    aEndIndex--; // 0 indexed
    
    return total;
}

void QobuzPins::UpdateOffset(TUint aTotalItems, TUint aEndIndex, TBool aIsContainer, TUint& aOffset)
{
    aOffset += kItemLimitPerRequest;
    TBool wrap = (aOffset >= aTotalItems);
    if (!aIsContainer) {
        // track responses are only randomised if the track count is > MAX (1000)
        // container responses are always randomised as they are based on total containers, not total tracks
        wrap = wrap && (aTotalItems > iMaxPlaylistTracks);
    }
    if (wrap) {
        aOffset = 0; // wrap around - only relevant to randomised case
    }
    else if (aOffset > aEndIndex) {
        if (!aIsContainer) {
            aOffset = aEndIndex; // as there can be a wrap around, this is required to exit
        }
    }
}

TBool QobuzPins::IsValidId(const Brx& aRequest, QobuzMetadata::EIdType /*aIdType*/) {
    if (aRequest == QobuzMetadata::kIdTypeUserSpecific) {
        return true; // no additional user input
    }

    for (TUint i = 0; i<aRequest.Bytes(); i++) {
        if (!Ascii::IsDigit(aRequest[i])) {
            return false;
        }
    }
    return true;
}

void QobuzPins::InitPlaylist(TBool aShuffle)
{
    iCpPlaylist->SyncDeleteAll();
    iCpPlaylist->SyncSetShuffle(aShuffle);
}
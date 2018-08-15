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
static const TChar* kPinTypeSmart = "smart";
static const TChar* kPinTypeTrack = "track";
static const TChar* kPinTypeContainer = "container";

// Pin params
static const TChar* kPinKeyId = "id";
static const TChar* kPinKeyTrackId = "trackId";
static const TChar* kPinKeyPath = "path";
static const TChar* kPinKeyResponseType = "response";
static const TChar* kPinKeySmartType = "smartType";
static const TChar* kPinKeyGenre = "genre";

// Pin response types
static const TChar* kPinResponseTracks = "tracks";
static const TChar* kPinResponseAlbums = "albums";

// Pin smart types
static const TChar* kSmartTypeAwardWinning = "awards";
static const TChar* kSmartTypeBestSellers = "bestsellers";
static const TChar* kSmartTypeCollection = "collection";
static const TChar* kSmartTypeFavorites = "fav";
static const TChar* kSmartTypeMostFeatured = "mostfeatured";
static const TChar* kSmartTypeMostStreamed = "moststreamed";
static const TChar* kSmartTypeNew = "new";
static const TChar* kSmartTypePurchased = "purchased";
static const TChar* kSmartTypeRecommended = "recommended";
static const TChar* kSmartTypeSavedPlaylist = "savedplaylist";

QobuzPins::QobuzPins(Qobuz& aQobuz, 
                     DvDeviceStandard& aDevice,
                     Media::TrackFactory& aTrackFactory, 
                     CpStack& aCpStack, 
                     IThreadPool& aThreadPool)
    : iLock("QPIN")
    , iQobuz(aQobuz)
    , iJsonResponse(kJsonResponseChunks)
    , iTrackFactory(aTrackFactory)
    , iPin(iPinIdProvider)
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
    iQobuz.Login(iToken);
    (void)iPin.TryUpdate(aPin.Mode(), aPin.Type(), aPin.Uri(), aPin.Title(),
                         aPin.Description(), aPin.ArtworkUri(), aPin.Shuffle());
    completion.Cancel();
    iCompleted = aCompleted;
    (void)iThreadPoolHandle->TrySchedule();
}

void QobuzPins::Invoke()
{
    AutoFunctor _(iCompleted);
    TBool res = false;
    try {
        PinUri pinUri(iPin);
        Brn id;
        if (Brn(pinUri.Type()) == Brn(kPinTypeArtist)) {
            if (pinUri.TryGetValue(kPinKeyId, id)) {
                res = LoadTracksByArtist(id, iPin.Shuffle());
            }
            else if (pinUri.TryGetValue(kPinKeyPath, id)) {
                res = LoadByPath(id, pinUri, iPin.Shuffle());
            }
            else {
                THROW(PinUriMissingRequiredParameter);
            }
        }
        else if (Brn(pinUri.Type()) == Brn(kPinTypeAlbum)) {
            if (pinUri.TryGetValue(kPinKeyId, id)) {
                res = LoadTracksByAlbum(id, iPin.Shuffle());
            }
            else if (pinUri.TryGetValue(kPinKeyPath, id)) {
                res = LoadByPath(id, pinUri, iPin.Shuffle());
            }
            else {
                THROW(PinUriMissingRequiredParameter);
            }
        }
        else if (Brn(pinUri.Type()) == Brn(kPinTypeTrack)) {
            if (pinUri.TryGetValue(kPinKeyTrackId, id)) {
                res = LoadTracksByTrack(id, iPin.Shuffle());
            }
            else {
                THROW(PinUriMissingRequiredParameter);
            }
        }
        else if (Brn(pinUri.Type()) == Brn(kPinTypePlaylist)) {
            if (pinUri.TryGetValue(kPinKeyId, id)) {
                res = LoadTracksByPlaylist(id, iPin.Shuffle());
            }
            else if (pinUri.TryGetValue(kPinKeyPath, id)) {
                res = LoadByPath(id, pinUri, iPin.Shuffle());
            }
            else {
                THROW(PinUriMissingRequiredParameter);
            }
        }
        else if (Brn(pinUri.Type()) == Brn(kPinTypeContainer)) {
            if (pinUri.TryGetValue(kPinKeyPath, id)) {
                res = LoadByPath(id, pinUri, iPin.Shuffle());
            }
            else {
                THROW(PinUriMissingRequiredParameter);
            }
        }
        else if (Brn(pinUri.Type()) == Brn(kPinTypeSmart)) {
            Brn smartType;
            if (!pinUri.TryGetValue(kPinKeySmartType, smartType)) {
                THROW(PinUriMissingRequiredParameter);
            }
            Brn genre(QobuzMetadata::kGenreNone);
            pinUri.TryGetValue(kPinKeyGenre, genre);

            if (smartType == Brn(kSmartTypeNew)) { res = LoadTracksByNew(genre, iPin.Shuffle()); }
            else if (smartType == Brn(kSmartTypeRecommended)) { res = LoadTracksByRecommended(genre, iPin.Shuffle()); }
            else if (smartType == Brn(kSmartTypeMostStreamed)) { res = LoadTracksByMostStreamed(genre, iPin.Shuffle()); }
            else if (smartType == Brn(kSmartTypeBestSellers)) { res = LoadTracksByBestSellers(genre, iPin.Shuffle()); }
            else if (smartType == Brn(kSmartTypeAwardWinning)) { res = LoadTracksByAwardWinning(genre, iPin.Shuffle()); }
            else if (smartType == Brn(kSmartTypeMostFeatured)) { res = LoadTracksByMostFeatured(genre, iPin.Shuffle()); }
            else if (smartType == Brn(kSmartTypeFavorites)) { res = LoadTracksByFavorites(iPin.Shuffle()); }
            else if (smartType == Brn(kSmartTypePurchased)) { res = LoadTracksByPurchased(iPin.Shuffle()); }
            else if (smartType == Brn(kSmartTypeCollection)) { res = LoadTracksByCollection(iPin.Shuffle()); }
            else if (smartType == Brn(kSmartTypeSavedPlaylist)) { res = LoadTracksBySavedPlaylist(iPin.Shuffle()); }
            else {
                LOG_ERROR(kPipeline, "QobuzPins::Invoke - unsupported smart type in %.*s\n", PBUF(iPin.Uri()));
                THROW(PinSmartTypeNotSupported);
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

void QobuzPins::Cancel()
{
    iQobuz.Interrupt(true);
}

const TChar* QobuzPins::Mode() const
{
    return kPinModeQobuz;
}

TBool QobuzPins::LoadByPath(const Brx& aPath, const PinUri& aPinUri, TBool aShuffle)
{
    TBool res = false;
    Brn response(Brx::Empty());
    aPinUri.TryGetValue(kPinKeyResponseType, response);
    if (response == Brn(kPinResponseTracks)) {
        res = LoadTracksByPath(aPath, aShuffle);
    }
    else if (response == Brn(kPinResponseAlbums)) {
        res = LoadAlbumsByPath(aPath, aShuffle);
    }
    else {
        THROW(PinUriMissingRequiredParameter);
    }
    return res;
}

TBool QobuzPins::LoadTracksByArtist(const Brx& aArtist, TBool aShuffle)
{
    return LoadTracksByQuery(aArtist, QobuzMetadata::eArtist, aShuffle);
}

TBool QobuzPins::LoadTracksByAlbum(const Brx& aAlbum, TBool aShuffle)
{
    return LoadTracksByQuery(aAlbum, QobuzMetadata::eAlbum, aShuffle);
}

TBool QobuzPins::LoadTracksByTrack(const Brx& aTrack, TBool aShuffle)
{
    return QobuzPins::LoadTracksByQuery(aTrack, QobuzMetadata::eTrack, aShuffle);
}

TBool QobuzPins::LoadTracksByPlaylist(const Brx& aPlaylist, TBool aShuffle)
{
    return QobuzPins::LoadTracksByQuery(aPlaylist, QobuzMetadata::ePlaylist, aShuffle);
}

TBool QobuzPins::LoadTracksBySavedPlaylist(TBool aShuffle)
{
    return QobuzPins::LoadTracksByQuery(QobuzMetadata::kIdTypeUserSpecific, QobuzMetadata::eSavedPlaylist, aShuffle);
}

TBool QobuzPins::LoadTracksByFavorites(TBool aShuffle)
{
    return QobuzPins::LoadTracksByQuery(QobuzMetadata::kIdTypeUserSpecific, QobuzMetadata::eFavorites, aShuffle);
}

TBool QobuzPins::LoadTracksByPurchased(TBool aShuffle)
{
    return QobuzPins::LoadTracksByQuery(QobuzMetadata::kIdTypeUserSpecific, QobuzMetadata::ePurchased, aShuffle);
}

TBool QobuzPins::LoadTracksByCollection(TBool aShuffle)
{
    return QobuzPins::LoadTracksByQuery(QobuzMetadata::kIdTypeUserSpecific, QobuzMetadata::eCollection, aShuffle);
}

TBool QobuzPins::LoadTracksByNew(const Brx& aGenre, TBool aShuffle)
{
    return LoadTracksBySmartType(aGenre, QobuzMetadata::eSmartNew, aShuffle);
} 

TBool QobuzPins::LoadTracksByRecommended(const Brx& aGenre, TBool aShuffle)
{
    return LoadTracksBySmartType(aGenre, QobuzMetadata::eSmartRecommended, aShuffle);
}

TBool QobuzPins::LoadTracksByMostStreamed(const Brx& aGenre, TBool aShuffle)
{
    return LoadTracksBySmartType(aGenre, QobuzMetadata::eSmartMostStreamed, aShuffle);
} 

TBool QobuzPins::LoadTracksByBestSellers(const Brx& aGenre, TBool aShuffle)
{
    return LoadTracksBySmartType(aGenre, QobuzMetadata::eSmartBestSellers, aShuffle);
} 

TBool QobuzPins::LoadTracksByAwardWinning(const Brx& aGenre, TBool aShuffle)
{
    return LoadTracksBySmartType(aGenre, QobuzMetadata::eSmartAwardWinning, aShuffle);
} 

TBool QobuzPins::LoadTracksByMostFeatured(const Brx& aGenre, TBool aShuffle)
{
    return LoadTracksBySmartType(aGenre, QobuzMetadata::eSmartMostFeatured, aShuffle);
} 

TBool QobuzPins::LoadTracksBySmartType(const Brx& aGenre, QobuzMetadata::EIdType aType, TBool aShuffle)
{
    AutoMutex _(iLock);
    JsonParser parser;
    InitPlaylist(aShuffle);
    Bwh inputBuf(64);
    Bwh albumIds[kMaxAlbums];
    TBool tracksFound = false;

    try {
        if (aGenre.Bytes() == 0) {
            inputBuf.ReplaceThrow(QobuzMetadata::kGenreNone); // genre is optional
        }
        // genre search string to id
        else if (!IsValidGenreId(aGenre)) {
            iJsonResponse.Reset();
            TBool success = iQobuz.TryGetGenreList(iJsonResponse); // send request to Qobuz
            if (!success) {
                return false;
            }
            inputBuf.ReplaceThrow(QobuzMetadata::GenreIdFromJson(iJsonResponse.Buffer(), aGenre)); // parse response from Qobuz
            if (inputBuf.Bytes() == 0) {
                return false;
            }
        }
        else {
            inputBuf.ReplaceThrow(aGenre);
        }

        // request tracks from smart types (returned as list of albums - place an arbitrary limit on the number of albums to return for now)
        iJsonResponse.Reset();
        TBool success = iQobuz.TryGetIds(iJsonResponse, inputBuf, aType, kMaxAlbums); // send request to Qobuz
        if (!success) {
            return false;
        }
        
        // response is list of albums (filtered by genre), so need to loop through albums
        parser.Reset();
        parser.Parse(iJsonResponse.Buffer());
        TUint idCount = 0;
        if (parser.HasKey(Brn("albums"))) { 
            parser.Parse(parser.String(Brn("albums")));
        }
        if (parser.HasKey(Brn("items"))) {
            TUint albums = parser.Num(Brn("total"));
            if (albums != 0) {
                auto parserItems = JsonParserArray::Create(parser.String(Brn("items")));
                JsonParser parserItem;
                try {
                    for (TUint i = 0; i < kMaxAlbums; i++) {
                        parserItem.Parse(parserItems.NextObject());
                        albumIds[i].Grow(20);
                        albumIds[i].ReplaceThrow(parserItem.String(Brn("id"))); // parse response from Qobuz
                        idCount++;
                        if (albumIds[i].Bytes() == 0) {
                            return false;
                        }
                    }
                }
                catch (JsonArrayEnumerationComplete&) {}
                TUint lastId = 0;
                for (TUint j = 0; j < idCount; j++) {
                    try {
                        lastId = LoadTracksById(albumIds[j], aType, lastId);
                        tracksFound = true;
                    }
                    catch (PinNothingToPlay&) {
                    }
                } 
            } 
        }
    }   
    catch (Exception& ex) {
        LOG_ERROR(kPipeline, "%s in QobuzPins::LoadTracksBySmartType\n", ex.Message());
        return false;
    }

    if (!tracksFound) {
        THROW(PinNothingToPlay);
    }

    return true;
}

TBool QobuzPins::LoadTracksByQuery(const Brx& aQuery, QobuzMetadata::EIdType aType, TBool aShuffle)
{
    AutoMutex _(iLock);
    TUint lastId = 0;
    JsonParser parser;
    InitPlaylist(aShuffle);
    Bwh inputBuf(64);
    TBool tracksFound = false;

    try {
        if (aQuery.Bytes() == 0) {
            return false;
        }
        // track/artist/album/playlist search string to id
        else if (!IsValidId(aQuery, aType)) {
            iJsonResponse.Reset();
            TBool success = iQobuz.TryGetId(iJsonResponse, aQuery, aType); // send request to Qobuz
            if (!success) {
                return false;
            }
            inputBuf.ReplaceThrow(QobuzMetadata::FirstIdFromJson(iJsonResponse.Buffer(), aType)); // parse response from Qobuz
            if (inputBuf.Bytes() == 0) {
                return false;
            }
        }
        else {
            inputBuf.ReplaceThrow(aQuery);
        }

        try {
            lastId = LoadTracksById(inputBuf, aType, lastId);
            tracksFound = true;
        }
        catch (PinNothingToPlay&) {
        }

        if (aType == QobuzMetadata::ePurchased || aType == QobuzMetadata::eCollection) {
            // should not be required but collection endpoint not working correctly (only returns albums, no tracks)
            try {
                lastId = LoadTracksById(inputBuf, QobuzMetadata::ePurchasedTracks, lastId);
                tracksFound = true;
            }
            catch (PinNothingToPlay&) {
            }
        }

    }   
    catch (Exception& ex) {
        LOG_ERROR(kPipeline, "%s in QobuzPins::LoadTracksByQuery\n", ex.Message());
        return false;
    }

    if (!tracksFound) {
        THROW(PinNothingToPlay);
    }

    return lastId;
}

TBool QobuzPins::LoadTracksByPath(const Brx& aPath, TBool aShuffle)
{
    AutoMutex _(iLock);
    TUint lastId = 0;
    InitPlaylist(aShuffle);
    TBool tracksFound = false;

    try {
        if (aPath.Bytes() == 0) {
            return false;
        }
        try {
            lastId = LoadTracksById(aPath, QobuzMetadata::ePath, lastId);
            tracksFound = true;
        }
        catch (PinNothingToPlay&) {
        }
    }   
    catch (Exception& ex) {
        LOG_ERROR(kMedia, "%s in QobuzPins::LoadTracksByPath\n", ex.Message());
        return false;
    }

    if (!tracksFound) {
        THROW(PinNothingToPlay);
    }

    return lastId;
}

TBool QobuzPins::LoadAlbumsByPath(const Brx& aPath, TBool aShuffle)
{
    AutoMutex _(iLock);
    JsonParser parser;
    InitPlaylist(aShuffle);
    Bwh albumIds[kMaxAlbums];
    TUint lastId = 0;
    TBool tracksFound = false;

    try {
        // request favorite albums (returned as list - place an arbitrary limit on the number of albums to return for now)
        iJsonResponse.Reset();
        TBool success = iQobuz.TryGetIdsByRequest(iJsonResponse, aPath, kMaxAlbums); // send request to Qobuz
        if (!success) {
            return false;
        }
        
        // response is list of albums, so need to loop through albums
        parser.Reset();
        parser.Parse(iJsonResponse.Buffer());
        TUint idCount = 0;
        if (parser.HasKey(Brn("albums"))) { 
            parser.Parse(parser.String(Brn("albums")));
        }
        if (parser.HasKey(Brn("items"))) {
            TUint albums = parser.Num(Brn("total"));
            if (albums != 0) {
                auto parserItems = JsonParserArray::Create(parser.String(Brn("items")));
                JsonParser parserItem;
                try {
                    for (TUint i = 0; i < kMaxAlbums; i++) {
                        parserItem.Parse(parserItems.NextObject());
                        albumIds[i].Grow(20);
                        albumIds[i].ReplaceThrow(parserItem.String(Brn("id"))); // parse response from Qobuz
                        idCount++;
                        if (albumIds[i].Bytes() == 0) {
                            return false;
                        }
                    }
                }
                catch (JsonArrayEnumerationComplete&) {}
                for (TUint j = 0; j < idCount; j++) {
                    try {
                        lastId = LoadTracksById(albumIds[j], QobuzMetadata::eAlbum, lastId);
                        tracksFound = true;
                    }
                    catch (PinNothingToPlay&) {
                    }
                } 
            } 
        }
    }   
    catch (Exception& ex) {
        LOG_ERROR(kPipeline, "%s in QobuzPins::LoadTracksByFavorites\n", ex.Message());
        return false;
    }

    if (!tracksFound) {
        THROW(PinNothingToPlay);
    }

    return true;
}

TUint QobuzPins::LoadTracksById(const Brx& aId, QobuzMetadata::EIdType aType, TUint aPlaylistId)
{
    QobuzMetadata qm(iTrackFactory);
    TUint offset = 0;
    TUint total;
    iCpPlaylist->SyncTracksMax(total);
    TUint newId = 0;
    TUint currId = aPlaylistId;
    TBool initPlay = (aPlaylistId == 0);
    TBool isPlayable = false;
    JsonParser parser;

    // id to list of tracks
    LOG(kMedia, "QobuzPins::LoadTracksById: %.*s\n", PBUF(aId));
    while (offset < total) {
        try {
            iJsonResponse.Reset();
            TBool success = false;
            if (aType == QobuzMetadata::ePath) {
                success = iQobuz.TryGetTracksByRequest(iJsonResponse, aId, kTrackLimitPerRequest, offset);
            }
            else {
                success = iQobuz.TryGetTracksById(iJsonResponse, aId, aType, kTrackLimitPerRequest, offset);
            }
            if (!success) {
                return false;
            }
            offset += kTrackLimitPerRequest;

            parser.Reset();
            parser.Parse(iJsonResponse.Buffer());
            if (parser.HasKey(Brn("tracks"))) { 
                parser.Parse(parser.String(Brn("tracks")));
            }
            if (parser.HasKey(Brn("items"))) { 
                TUint tracks = parser.Num(Brn("total"));
                if (tracks == 0) {
                    break;
                }
                if (tracks < total) {
                    total = tracks;
                }
                auto parserItems = JsonParserArray::Create(parser.String(Brn("items")));

                try {
                    for (;;) {
                        auto* track = qm.TrackFromJson(iJsonResponse.Buffer(), parserItems.NextObject(), aType);
                        if (track != nullptr) {
                            iCpPlaylist->SyncInsert(currId, (*track).Uri(), (*track).MetaData(), newId);
                            track->RemoveRef();
                            currId = newId;
                            isPlayable = true;
                        }
                    }
                }
                catch (JsonArrayEnumerationComplete&) {}
            }
            else {
                total = 1;
                auto* track = qm.TrackFromJson(iJsonResponse.Buffer(), iJsonResponse.Buffer(), aType);
                if (track != nullptr) {
                    iCpPlaylist->SyncInsert(currId, (*track).Uri(), (*track).MetaData(), newId);
                    track->RemoveRef();
                    currId = newId;
                    isPlayable = true;
                }
            }
            if (initPlay && isPlayable) {
                initPlay = false;
                iCpPlaylist->SyncPlay();
            }
        }
        catch (Exception& ex) {
            LOG_ERROR(kPipeline, "%s in QobuzPins::LoadTracksById \n", ex.Message());
            return false;
        }
        
    }

    if (!isPlayable) {
        THROW(PinNothingToPlay);
    }

    return currId;
}

TBool QobuzPins::IsValidId(const Brx& aRequest, QobuzMetadata::EIdType /*aType*/) {
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

TBool QobuzPins::IsValidGenreId(const Brx& aRequest) {
    for (TUint i = 0; i<aRequest.Bytes(); i++) {
        if (!Ascii::IsDigit(aRequest[i]) && aRequest[i] != ',') {
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
#include <OpenHome/Av/Tidal/Tidal.h>
#include <OpenHome/Av/Tidal/TidalMetadata.h>
#include <OpenHome/Av/Tidal/TidalPins.h>
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
#include <OpenHome/Net/Core/CpDeviceDv.h>
#include <OpenHome/Private/Parser.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Net;
using namespace OpenHome::Configuration;

// Pin mode
static const TChar* kPinModeTidal = "tidal";

// Pin types
static const TChar* kPinTypeArtist = "artist";
static const TChar* kPinTypeAlbum = "album";
static const TChar* kPinTypeGenre = "genre";
static const TChar* kPinTypeMood = "mood";
static const TChar* kPinTypePlaylist = "playlist";
static const TChar* kPinTypeSmart = "smart";
static const TChar* kPinTypeTrack = "track";

// Pin params
static const TChar* kPinKeyId = "id";
static const TChar* kPinKeyTrackId = "trackId";
static const TChar* kPinKeyPath = "path";
static const TChar* kPinKeyResponseType = "response";
static const TChar* kPinKeySmartType = "smartType";

// Pin response types
static const TChar* kPinResponseTracks = "tracks";
static const TChar* kPinResponseAlbums = "albums";

// Pin smart types
static const TChar* kSmartTypeDiscovery = "discovery";
static const TChar* kSmartTypeExclusive = "exclusive";
static const TChar* kSmartTypeFavorites = "fav";
static const TChar* kSmartTypeNew = "new";
static const TChar* kSmartTypeRecommended = "recommended";
static const TChar* kSmartTypeRising = "rising";
static const TChar* kSmartTypeSavedPlaylist = "savedplaylist";
static const TChar* kSmartTypeTop20 = "top20";

// Potential Validation
// valid genre strings: https://api.tidal.com/v1/genres?countryCode={{countryCode}}
// valid mood strings: https://api.tidal.com/v1/moods?countryCode={{countryCode}}
// valid featured strings: https://api.tidal.com/v1/featured?countryCode={{countryCode}}
// would need to call THROW(TidalRequestInvalid); on fail
// rising and discovery have no validation

TidalPins::TidalPins(Tidal& aTidal, DvDeviceStandard& aDevice, Media::TrackFactory& aTrackFactory, CpStack& aCpStack, TUint aMaxTracks)
    : iLock("TPIN")
    , iTidal(aTidal)
    , iJsonResponse(kJsonResponseChunks)
    , iTrackFactory(aTrackFactory)
    , iCpStack(aCpStack)
    , iMaxTracks(aMaxTracks)
{
    CpDeviceDv* cpDevice = CpDeviceDv::New(iCpStack, aDevice);
    iCpPlaylist = new CpProxyAvOpenhomeOrgPlaylist1(*cpDevice);
    cpDevice->RemoveRef(); // iProxy will have claimed a reference to the device so no need for us to hang onto another
}

TidalPins::~TidalPins()
{
    delete iCpPlaylist;
}

void TidalPins::BeginInvoke(const IPin& aPin, Functor aCompleted)
{
    AutoFunctor _(aCompleted);
    PinUri pin(aPin);
    TBool res = false;
    if (Brn(pin.Mode()) == Brn(kPinModeTidal)) {
        Bwh token(128);
        iTidal.Login(token);
        Brn id;
        if (Brn(pin.Type()) == Brn(kPinTypeArtist)) { 
            if (pin.TryGetValue(kPinKeyId, id)) {
                res = LoadTracksByArtist(id, aPin.Shuffle());
            }
            else if (pin.TryGetValue(kPinKeyPath, id)) {
                Brn response(Brx::Empty());
                pin.TryGetValue(kPinKeyResponseType, response);
                if (response == Brn(kPinResponseTracks)) {
                    res = LoadTracksByPath(id, aPin.Shuffle());
                }
                else if (response == Brn(kPinResponseAlbums)) {
                    res = LoadAlbumsByPath(id, aPin.Shuffle());
                }
                else {
                    THROW(PinMissingRequiredParameter);
                }
            }
            else {
                THROW(PinMissingRequiredParameter);
            }
        }
        else if (Brn(pin.Type()) == Brn(kPinTypeAlbum)) { 
            if (pin.TryGetValue(kPinKeyId, id)) {
                res = LoadTracksByAlbum(id, aPin.Shuffle());
            }
            else if (pin.TryGetValue(kPinKeyPath, id)) {
                Brn response(Brx::Empty());
                pin.TryGetValue(kPinKeyResponseType, response);
                if (response == Brn(kPinResponseTracks)) {
                    res = LoadTracksByPath(id, aPin.Shuffle());
                }
                else if (response == Brn(kPinResponseAlbums)) {
                    res = LoadAlbumsByPath(id, aPin.Shuffle());
                }
                else {
                    THROW(PinMissingRequiredParameter);
                }
            }
            else {
                THROW(PinMissingRequiredParameter);
            }
        }
        else if (Brn(pin.Type()) == Brn(kPinTypeTrack)) {
            if (pin.TryGetValue(kPinKeyTrackId, id)) {
                res = LoadTracksByTrack(id, aPin.Shuffle());
            }
            else {
                THROW(PinMissingRequiredParameter);
            }
        }
        else if (Brn(pin.Type()) == Brn(kPinTypePlaylist)) {
            if (pin.TryGetValue(kPinKeyId, id)) {
                res = LoadTracksByPlaylist(id, aPin.Shuffle());
            }
            else if (pin.TryGetValue(kPinKeyPath, id)) {
                Brn response(Brx::Empty());
                pin.TryGetValue(kPinKeyResponseType, response);
                if (response == Brn(kPinResponseTracks)) {
                    res = LoadTracksByPath(id, aPin.Shuffle());
                }
                else if (response == Brn(kPinResponseAlbums)) {
                    res = LoadAlbumsByPath(id, aPin.Shuffle());
                }
                else {
                    THROW(PinMissingRequiredParameter);
                }
            }
            else {
                THROW(PinMissingRequiredParameter);
            }
        }
        else if (Brn(pin.Type()) == Brn(kPinTypeGenre)) {
            if (pin.TryGetValue(kPinKeyId, id)) {
                res = LoadTracksByGenre(id, aPin.Shuffle());
            }
            else if (pin.TryGetValue(kPinKeyPath, id)) {
                Brn response(Brx::Empty());
                pin.TryGetValue(kPinKeyResponseType, response);
                if (response == Brn(kPinResponseTracks)) {
                    res = LoadTracksByPath(id, aPin.Shuffle());
                }
                else if (response == Brn(kPinResponseAlbums)) {
                    res = LoadAlbumsByPath(id, aPin.Shuffle());
                }
                else {
                    THROW(PinMissingRequiredParameter);
                }
            }
            else {
                THROW(PinMissingRequiredParameter);
            }
        }
        else if (Brn(pin.Type()) == Brn(kPinTypeMood)) {
            if (pin.TryGetValue(kPinKeyId, id)) {
                res = LoadTracksByMood(id, aPin.Shuffle());
            }
            else {
                THROW(PinMissingRequiredParameter);
            }
        }
        else if (Brn(pin.Type()) == Brn(kPinTypeSmart)) {
            Brn smartType;
            if (!pin.TryGetValue(kPinKeySmartType, smartType)) {
                THROW(PinMissingRequiredParameter);
            }

            if (smartType == Brn(kSmartTypeDiscovery)) { res = LoadTracksByDiscovery(aPin.Shuffle()); }
            else if (smartType == Brn(kSmartTypeExclusive)) { res = LoadTracksByExclusive(aPin.Shuffle()); }
            else if (smartType == Brn(kSmartTypeFavorites)) { res = LoadTracksByFavorites(aPin.Shuffle()); }
            else if (smartType == Brn(kSmartTypeNew)) { res = LoadTracksByNew(aPin.Shuffle()); }
            else if (smartType == Brn(kSmartTypeRecommended)) { res = LoadTracksByRecommended(aPin.Shuffle()); }
            else if (smartType == Brn(kSmartTypeRising)) { res = LoadTracksByRising(aPin.Shuffle()); }
            else if (smartType == Brn(kSmartTypeSavedPlaylist)) { res = LoadTracksBySavedPlaylist(aPin.Shuffle()); }
            else if (smartType == Brn(kSmartTypeTop20)) { res = LoadTracksByTop20(aPin.Shuffle()); }
            else {
                THROW(PinSmartTypeNotSupported);
            }
        }
        else {
            THROW(PinTypeNotSupported);
        }
        if (!res) {
            THROW(PinInvokeError);
        }
    }
}

void TidalPins::Cancel()
{
}

const TChar* TidalPins::Mode() const
{
    return kPinModeTidal;
}

TBool TidalPins::LoadTracksByArtist(const Brx& aArtist, TBool aShuffle)
{
    return LoadTracksByQuery(aArtist, TidalMetadata::eArtist, aShuffle);
}

TBool TidalPins::LoadTracksByAlbum(const Brx& aAlbum, TBool aShuffle)
{
    return LoadTracksByQuery(aAlbum, TidalMetadata::eAlbum, aShuffle);
}

TBool TidalPins::LoadTracksByTrack(const Brx& aTrack, TBool aShuffle)
{
    return TidalPins::LoadTracksByQuery(aTrack, TidalMetadata::eTrack, aShuffle);
}

TBool TidalPins::LoadTracksByPlaylist(const Brx& aPlaylist, TBool aShuffle)
{
    return TidalPins::LoadTracksByQuery(aPlaylist, TidalMetadata::ePlaylist, aShuffle);
}

TBool TidalPins::LoadTracksBySavedPlaylist(TBool aShuffle)
{
    return TidalPins::LoadTracksByMultiplePlaylists(TidalMetadata::eSavedPlaylist, aShuffle);
}

TBool TidalPins::LoadTracksByGenre(const Brx& aGenre, TBool aShuffle)
{
    return TidalPins::LoadTracksByQuery(aGenre, TidalMetadata::eGenre, aShuffle);
}

TBool TidalPins::LoadTracksByMood(const Brx& aMood, TBool aShuffle)
{
    return TidalPins::LoadTracksByMultiplePlaylists(aMood, TidalMetadata::eMood, aShuffle);
}

TBool TidalPins::LoadTracksByNew(TBool aShuffle)
{
    return TidalPins::LoadTracksBySmartType(TidalMetadata::eSmartNew, aShuffle);
} 

TBool TidalPins::LoadTracksByRecommended(TBool aShuffle)
{
    return TidalPins::LoadTracksBySmartType(TidalMetadata::eSmartRecommended, aShuffle);
} 

TBool TidalPins::LoadTracksByTop20(TBool aShuffle)
{
    return TidalPins::LoadTracksBySmartType(TidalMetadata::eSmartTop20, aShuffle);
} 

TBool TidalPins::LoadTracksByExclusive(TBool aShuffle)
{
    return TidalPins::LoadTracksByMultiplePlaylists(TidalMetadata::eSmartExclusive, aShuffle);
} 

TBool TidalPins::LoadTracksByRising(TBool aShuffle)
{
    return TidalPins::LoadTracksBySmartType(TidalMetadata::eSmartRising, aShuffle);
} 

TBool TidalPins::LoadTracksByDiscovery(TBool aShuffle)
{
    return TidalPins::LoadTracksBySmartType(TidalMetadata::eSmartDiscovery, aShuffle);
} 

TBool TidalPins::LoadTracksBySmartType(TidalMetadata::EIdType aType, TBool aShuffle)
{
    return LoadTracksByQuery(TidalMetadata::kIdTypeSmart, aType, aShuffle);
}

TBool TidalPins::LoadTracksByMultiplePlaylists(TidalMetadata::EIdType aType, TBool aShuffle)
{
    return LoadTracksByMultiplePlaylists(Brx::Empty(), aType, aShuffle);
}

TBool TidalPins::LoadTracksByFavorites(TBool aShuffle)
{
    AutoMutex _(iLock);
    JsonParser parser;
    InitPlaylist(aShuffle);
    Bwh albumIds[kMaxAlbums];
    TUint lastId = 0;
    TBool tracksFound = false;

    try {
        // load favorite tracks
        try {
            lastId = LoadTracksById(TidalMetadata::kIdTypeUserSpecific, TidalMetadata::eFavorites, lastId);
            tracksFound = true;
        }
        catch (PinNothingToPlay&) {
        }
        // request favorite albums (returned as list - place an arbitrary limit on the number of albums to return for now)
        iJsonResponse.Reset();
        TBool success = iTidal.TryGetIds(iJsonResponse, TidalMetadata::kIdTypeUserSpecific, TidalMetadata::eFavorites, kMaxAlbums); // send request to Tidal
        if (!success) {
            return false;
        }
        
        // response is list of albums, so need to loop through albums
        parser.Reset();
        parser.Parse(iJsonResponse.Buffer());
        TUint idCount = 0;
        if (parser.HasKey(Brn("totalNumberOfItems"))) {
            TUint albums = parser.Num(Brn("totalNumberOfItems"));
            if (albums != 0) {
                auto parserItems = JsonParserArray::Create(parser.String(Brn("items")));
                JsonParser parserItem;
                try {
                    for (TUint i = 0; i < kMaxAlbums; i++) {
                        parserItem.Parse(parserItems.NextObject());

                        JsonParser parserAlbum;
                        parserAlbum.Parse(parserItem.String(Brn("item")));

                        albumIds[i].Grow(20);
                        albumIds[i].ReplaceThrow(parserAlbum.String(Brn("id"))); // parse response from Tidal
                        idCount++;
                        if (albumIds[i].Bytes() == 0) {
                            return false;
                        }
                    }
                }
                catch (JsonArrayEnumerationComplete&) {}
                for (TUint j = 0; j < idCount; j++) {
                    try {
                        lastId = LoadTracksById(albumIds[j], TidalMetadata::eAlbum, lastId);
                        tracksFound = true;
                    }
                    catch (PinNothingToPlay&) {
                    }
                } 
            } 
        }
    }   
    catch (Exception& ex) {
        LOG_ERROR(kPipeline, "%s in TidalPins::LoadTracksByFavorites\n", ex.Message());
        return false;
    }

    if (!tracksFound) {
        THROW(PinNothingToPlay);
    }

    return true;
}

TBool TidalPins::LoadTracksByMultiplePlaylists(const Brx& aMood, TidalMetadata::EIdType aType, TBool aShuffle)
{
    AutoMutex _(iLock);
    JsonParser parser;
    InitPlaylist(aShuffle);
    Bwh playlistIds[kMaxPlaylistsPerSmartType];
    TBool tracksFound = false;

    try {
        // request tracks from smart types (returned as list of playlists - place an arbitrary limit on the number of playlists to return for now)
        iJsonResponse.Reset();
        TBool success = iTidal.TryGetIds(iJsonResponse, aMood, aType, kMaxPlaylistsPerSmartType); // send request to Tidal
        if (!success) {
            return false;
        }
        
        // response is list of playlists, so need to loop through playlists
        parser.Reset();
        parser.Parse(iJsonResponse.Buffer());
        TUint idCount = 0;
        if (parser.HasKey(Brn("totalNumberOfItems"))) {
            TUint playlists = parser.Num(Brn("totalNumberOfItems"));
            if (playlists != 0) {
                auto parserItems = JsonParserArray::Create(parser.String(Brn("items")));
                JsonParser parserItem;
                try {
                    for (TUint i = 0; i < kMaxPlaylistsPerSmartType; i++) {
                        parserItem.Parse(parserItems.NextObject());
                        playlistIds[i].Grow(50);
                        playlistIds[i].ReplaceThrow(parserItem.String(Brn("uuid"))); // parse response from Tidal
                        idCount++;
                        if (playlistIds[i].Bytes() == 0) {
                            return false;
                        }
                    }
                }
                catch (JsonArrayEnumerationComplete&) {}
                TUint lastId = 0;
                for (TUint j = 0; j < idCount; j++) {
                    try {
                        lastId = LoadTracksById(playlistIds[j], aType, lastId);
                        tracksFound = true;
                    }
                    catch (PinNothingToPlay&) {
                    }
                }  
            }
        }
    }   
    catch (Exception& ex) {
        LOG_ERROR(kPipeline, "%s in TidalPins::LoadTracksByMultiplePlaylists\n", ex.Message());
        return false;
    }

    if (!tracksFound) {
        THROW(PinNothingToPlay);
    }

    return true;
}

TBool TidalPins::LoadTracksByQuery(const Brx& aQuery, TidalMetadata::EIdType aType, TBool aShuffle)
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
            TBool success = iTidal.TryGetId(iJsonResponse, aQuery, aType); // send request to tidal
            if (!success) {
                return false;
            }
            inputBuf.ReplaceThrow(TidalMetadata::FirstIdFromJson(iJsonResponse.Buffer(), aType)); // parse response from tidal
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
    }   
    catch (Exception& ex) {
        LOG_ERROR(kMedia, "%s in TidalPins::LoadTracksByQuery\n", ex.Message());
        return false;
    }

    if (!tracksFound) {
        THROW(PinNothingToPlay);
    }

    return lastId;
}

TBool TidalPins::LoadTracksByPath(const Brx& aPath, TBool aShuffle)
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
            lastId = LoadTracksById(aPath, TidalMetadata::ePath, lastId);
            tracksFound = true;
        }
        catch (PinNothingToPlay&) {
        }
    }   
    catch (Exception& ex) {
        LOG_ERROR(kMedia, "%s in TidalPins::LoadTracksByPath\n", ex.Message());
        return false;
    }

    if (!tracksFound) {
        THROW(PinNothingToPlay);
    }

    return lastId;
}

TBool TidalPins::LoadAlbumsByPath(const Brx& aPath, TBool aShuffle)
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
        TBool success = iTidal.TryGetIdsByRequest(iJsonResponse, aPath, kMaxAlbums); // send request to Tidal
        if (!success) {
            return false;
        }
        
        // response is list of albums, so need to loop through albums
        parser.Reset();
        parser.Parse(iJsonResponse.Buffer());
        TUint idCount = 0;
        if (parser.HasKey(Brn("totalNumberOfItems"))) {
            TUint albums = parser.Num(Brn("totalNumberOfItems"));
            if (albums != 0) {
                auto parserItems = JsonParserArray::Create(parser.String(Brn("items")));
                JsonParser parserItem;
                try {
                    for (TUint i = 0; i < kMaxAlbums; i++) {
                        parserItem.Parse(parserItems.NextObject());
                        albumIds[i].Grow(20);
                        albumIds[i].ReplaceThrow(parserItem.String(Brn("id"))); // parse response from Tidal
                        idCount++;
                        if (albumIds[i].Bytes() == 0) {
                            return false;
                        }
                    }
                }
                catch (JsonArrayEnumerationComplete&) {}
                for (TUint j = 0; j < idCount; j++) {
                    try {
                        lastId = LoadTracksById(albumIds[j], TidalMetadata::eAlbum, lastId);
                        tracksFound = true;
                    }
                    catch (PinNothingToPlay&) {
                    }
                } 
            } 
        }
    }   
    catch (Exception& ex) {
        LOG_ERROR(kPipeline, "%s in TidalPins::LoadTracksByFavorites\n", ex.Message());
        return false;
    }

    if (!tracksFound) {
        THROW(PinNothingToPlay);
    }

    return true;
}

TUint TidalPins::LoadTracksById(const Brx& aId, TidalMetadata::EIdType aType, TUint aPlaylistId)
{
    TidalMetadata tm(iTrackFactory);
    TUint offset = 0;
    TUint total = iMaxTracks;
    TUint newId = 0;
    TUint currId = aPlaylistId;
    TBool initPlay = (aPlaylistId == 0);
    TBool isPlayable = false;
    JsonParser parser;

    // id to list of tracks
    LOG(kMedia, "TidalPins::LoadTracksById: %.*s\n", PBUF(aId));
    while (offset < total) {
        try {
            iJsonResponse.Reset();
            TBool success = false;
            if (aType == TidalMetadata::ePath) {
                success = iTidal.TryGetTracksByRequest(iJsonResponse, aId, kTrackLimitPerRequest, offset);
            }
            else {
                success = iTidal.TryGetTracksById(iJsonResponse, aId, aType, kTrackLimitPerRequest, offset);
            }
            if (!success) {
                return false;
            }
            offset += kTrackLimitPerRequest;

            parser.Reset();
            parser.Parse(iJsonResponse.Buffer());
            if (parser.HasKey(Brn("totalNumberOfItems"))) { 
                TUint tracks = parser.Num(Brn("totalNumberOfItems"));
                if (tracks == 0) {
                    break;
                }
                if (tracks < total) {
                    total = tracks;
                }
                auto parserItems = JsonParserArray::Create(parser.String(Brn("items")));

                try {
                    for (;;) {
                        auto* track = tm.TrackFromJson(parserItems.NextObject());
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
                auto* track = tm.TrackFromJson(iJsonResponse.Buffer());
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
            LOG_ERROR(kMedia, "%s in TidalPins::LoadTracksById (finding tracks)\n", ex.Message());
            return false;
        }
        
    }

    if (!isPlayable) {
        THROW(PinNothingToPlay);
    }

    return currId;
}

TBool TidalPins::IsValidId(const Brx& aRequest, TidalMetadata::EIdType aType) {
    if (aType == TidalMetadata::ePlaylist) {
        return IsValidUuid(aRequest);
    }
    else if (aType == TidalMetadata::eGenre || aRequest == TidalMetadata::kIdTypeSmart) {
        return true;
    }
    // artist/album/track
    for (TUint i = 0; i<aRequest.Bytes(); i++) {
        if (!Ascii::IsDigit(aRequest[i])) {
            return false;
        }
    }
    return true;
}

TBool TidalPins::IsValidUuid(const Brx& aRequest) {
    for (TUint i = 0; i<aRequest.Bytes(); i++) {
        if (!Ascii::IsDigit(aRequest[i]) && !Ascii::IsHex(aRequest[i]) && aRequest[i] != '-') {
            return false;
        }
    }
    return true;
}

void TidalPins::InitPlaylist(TBool aShuffle)
{
    iCpPlaylist->SyncDeleteAll();
    iCpPlaylist->SyncSetShuffle(aShuffle);
}
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

void TidalPins::Invoke(const IPin& aPin)
{
    PinUri pin(aPin);
    if (pin.Mode() == PinUri::EMode::eTidal) {
        switch (pin.Type()) {
            case PinUri::EType::eArtist: LoadTracksByArtist(pin.Value(), pin.Shuffle()); break;
            case PinUri::EType::eAlbum: LoadTracksByAlbum(pin.Value(), pin.Shuffle()); break;
            case PinUri::EType::eTrack: LoadTracksByTrack(pin.Value(), pin.Shuffle()); break;
            case PinUri::EType::ePlaylist: LoadTracksByPlaylist(pin.Value(), pin.Shuffle()); break;
            case PinUri::EType::eGenre: LoadTracksByGenre(pin.Value(), pin.Shuffle()); break;
            case PinUri::EType::eMood: LoadTracksByMood(pin.Value(), pin.Shuffle()); break;
            case PinUri::EType::eSmart: {
                switch (pin.SmartType()) {
                    case PinUri::ESmartType::eNew: LoadTracksByNew(pin.Shuffle()); break;
                    case PinUri::ESmartType::eRecommended: LoadTracksByRecommended(pin.Shuffle()); break;
                    case PinUri::ESmartType::eTop20: LoadTracksByTop20(pin.Shuffle()); break;
                    case PinUri::ESmartType::eExclusive: LoadTracksByExclusive(pin.Shuffle()); break;
                    case PinUri::ESmartType::eRising: LoadTracksByRising(pin.Shuffle()); break;
                    case PinUri::ESmartType::eDiscovery: LoadTracksByDiscovery(pin.Shuffle()); break;
                    case PinUri::ESmartType::eFavorites: LoadTracksByFavorites(pin.Shuffle()); break;
                    case PinUri::ESmartType::eSavedPlaylist: LoadTracksBySavedPlaylist(pin.Shuffle()); break;
                }
                break;
            }
            default: {
                return;
            }
        }
    }
}

const TChar* TidalPins::Mode() const
{
    return PinUri::GetModeString(PinUri::EMode::eTidal);
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
    Bwh albumIds[kMaxFavoriteAlbums];
    TUint lastId = 0;

    try {
        // load favorite tracks
        lastId = LoadTracksById(TidalMetadata::kIdTypeUserSpecific, TidalMetadata::eFavorites, lastId);

        // request favorite albums (returned as list - place an arbitrary limit on the number of albums to return for now)
        iJsonResponse.Reset();
        TBool success = iTidal.TryGetIds(iJsonResponse, TidalMetadata::kIdTypeUserSpecific, TidalMetadata::eFavorites, kMaxFavoriteAlbums); // send request to Tidal
        if (!success) {
            return false;
        }
        
        // response is list of albums, so need to loop through albums
        parser.Reset();
        parser.Parse(iJsonResponse.Buffer());
        TUint idCount = 0;
        if (parser.HasKey(Brn("totalNumberOfItems"))) {
            TUint albums = parser.Num(Brn("totalNumberOfItems"));
            if (albums == 0) {
                return false;
            }
            auto parserItems = JsonParserArray::Create(parser.String(Brn("items")));
            JsonParser parserItem;
            try {
                for (TUint i = 0; i < kMaxFavoriteAlbums; i++) {
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
                lastId = LoadTracksById(albumIds[j], TidalMetadata::eAlbum, lastId);
            }  
        }
    }   
    catch (Exception& ex) {
        LOG_ERROR(kPipeline, "%s in TidalPins::LoadTracksByFavorites\n", ex.Message());
        return false;
    }

    return true;
}

TBool TidalPins::LoadTracksByMultiplePlaylists(const Brx& aMood, TidalMetadata::EIdType aType, TBool aShuffle)
{
    AutoMutex _(iLock);
    JsonParser parser;
    InitPlaylist(aShuffle);
    Bwh playlistIds[kMaxPlaylistsPerSmartType];

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
            if (playlists == 0) {
                return false;
            }
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
                lastId = LoadTracksById(playlistIds[j], aType, lastId);
            }  
        }
    }   
    catch (Exception& ex) {
        LOG_ERROR(kPipeline, "%s in TidalPins::LoadTracksByMultiplePlaylists\n", ex.Message());
        return false;
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
        lastId = LoadTracksById(inputBuf, aType, lastId);
    }   
    catch (Exception& ex) {
        LOG_ERROR(kMedia, "%s in TidalPins::LoadTracksByQuery\n", ex.Message());
        return false;
    }

    return lastId;
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
            TBool success = iTidal.TryGetTracksById(iJsonResponse, aId, aType, kTrackLimitPerRequest, offset);
            if (!success) {
                return false;
            }
            offset += kTrackLimitPerRequest;

            parser.Reset();
            parser.Parse(iJsonResponse.Buffer());
            if (parser.HasKey(Brn("totalNumberOfItems"))) { 
                TUint tracks = parser.Num(Brn("totalNumberOfItems"));
                if (tracks == 0) {
                    return false;
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
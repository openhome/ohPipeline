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

TBool TidalPins::LoadTracksByArtist(const Brx& aArtist)
{
    return LoadTracksByQuery(aArtist, TidalMetadata::eArtist);
}

TBool TidalPins::LoadTracksByAlbum(const Brx& aAlbum)
{
    return LoadTracksByQuery(aAlbum, TidalMetadata::eAlbum);
}

TBool TidalPins::LoadTracksByTrack(const Brx& aTrack)
{
    return TidalPins::LoadTracksByQuery(aTrack, TidalMetadata::eTrack);
}

TBool TidalPins::LoadTracksByPlaylist(const Brx& aPlaylist)
{
    return TidalPins::LoadTracksByQuery(aPlaylist, TidalMetadata::ePlaylist);
}

TBool TidalPins::LoadTracksBySavedPlaylists()
{
    return TidalPins::LoadTracksByMultiplePlaylists(TidalMetadata::eSavedPlaylist);
}

TBool TidalPins::LoadTracksByFavorites()
{
    return TidalPins::LoadTracksByQuery(TidalMetadata::kIdTypeUserSpecific, TidalMetadata::eFavorites);
}

TBool TidalPins::LoadTracksByGenre(const Brx& aGenre)
{
    return TidalPins::LoadTracksByQuery(aGenre, TidalMetadata::eGenre);
}

TBool TidalPins::LoadTracksByMood(const Brx& aMood)
{
    return TidalPins::LoadTracksByMultiplePlaylists(aMood, TidalMetadata::eMood);
}

TBool TidalPins::LoadTracksByNew()
{
    return TidalPins::LoadTracksBySmartType(TidalMetadata::eSmartNew);
} 

TBool TidalPins::LoadTracksByRecommended()
{
    return TidalPins::LoadTracksBySmartType(TidalMetadata::eSmartRecommended);
} 

TBool TidalPins::LoadTracksByTop20()
{
    return TidalPins::LoadTracksBySmartType(TidalMetadata::eSmartTop20);
} 

TBool TidalPins::LoadTracksByExclusive()
{
    return TidalPins::LoadTracksByMultiplePlaylists(TidalMetadata::eSmartExclusive);
} 

TBool TidalPins::LoadTracksByRising()
{
    return TidalPins::LoadTracksBySmartType(TidalMetadata::eSmartRising);
} 

TBool TidalPins::LoadTracksByDiscovery()
{
    return TidalPins::LoadTracksBySmartType(TidalMetadata::eSmartDiscovery);
} 

TBool TidalPins::LoadTracksBySmartType(TidalMetadata::EIdType aType)
{
    return LoadTracksByQuery(TidalMetadata::kIdTypeSmart, aType);
}

TBool TidalPins::LoadTracksByMultiplePlaylists(TidalMetadata::EIdType aType)
{
    return LoadTracksByMultiplePlaylists(Brx::Empty(), aType);
}

TBool TidalPins::LoadTracksByMultiplePlaylists(const Brx& aMood, TidalMetadata::EIdType aType)
{
    AutoMutex _(iLock);
    JsonParser parser;
    iCpPlaylist->SyncDeleteAll();
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

TBool TidalPins::LoadTracksByQuery(const Brx& aQuery, TidalMetadata::EIdType aType)
{
    AutoMutex _(iLock);
    TUint lastId = 0;
    JsonParser parser;
    iCpPlaylist->SyncDeleteAll();
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
    else if (aType == TidalMetadata::eGenre || aRequest == TidalMetadata::kIdTypeUserSpecific || aRequest == TidalMetadata::kIdTypeSmart) {
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

TBool TidalPins::Test(const Brx& aType, const Brx& aInput, IWriterAscii& aWriter)
{
    if (aType == Brn("help")) {
        aWriter.Write(Brn("tidalpin_artist (input: Artist ID or search string)"));
        aWriter.Write(Brn(" "));
        aWriter.WriteNewline(); // can't get this to work
        aWriter.Write(Brn("tidalpin_album (input: Album ID or search string)"));
        aWriter.Write(Brn(" "));
        aWriter.WriteNewline(); // can't get this to work
        aWriter.Write(Brn("tidalpin_track (input: Track ID or search string)"));
        aWriter.Write(Brn(" "));
        aWriter.WriteNewline(); // can't get this to work
        aWriter.Write(Brn("tidalpin_playlist (input: Playlist UUID or search string)"));
        aWriter.Write(Brn(" "));
        aWriter.WriteNewline(); // can't get this to work
        aWriter.Write(Brn("tidalpin_savedplaylist (input: None)"));
        aWriter.Write(Brn(" "));
        aWriter.WriteNewline(); // can't get this to work
        aWriter.Write(Brn("tidalpin_favorites (input: None)"));
        aWriter.Write(Brn(" "));
        aWriter.WriteNewline(); // can't get this to work
        aWriter.Write(Brn("tidalpin_genre (input: Genre search string)"));
        aWriter.Write(Brn(" "));
        aWriter.WriteNewline(); // can't get this to work
        aWriter.Write(Brn("tidalpin_mood (input: Mood search string)"));
        aWriter.Write(Brn(" "));
        aWriter.WriteNewline(); // can't get this to work
        aWriter.Write(Brn("tidalpin_new (input: None)"));
        aWriter.Write(Brn(" "));
        aWriter.WriteNewline(); // can't get this to work
        aWriter.Write(Brn("tidalpin_recommended (input: None)"));
        aWriter.Write(Brn(" "));
        aWriter.WriteNewline(); // can't get this to work
        aWriter.Write(Brn("tidalpin_top20 (input: None)"));
        aWriter.Write(Brn(" "));
        aWriter.WriteNewline(); // can't get this to work
        aWriter.Write(Brn("tidalpin_exclusive (input: None)"));
        aWriter.Write(Brn(" "));
        aWriter.WriteNewline(); // can't get this to work
        aWriter.Write(Brn("tidalpin_rising (input: None)"));
        aWriter.Write(Brn(" "));
        aWriter.WriteNewline(); // can't get this to work
        aWriter.Write(Brn("tidalpin_discovery (input: None)"));
        aWriter.Write(Brn(" "));
        aWriter.WriteNewline(); // can't get this to work
        return true;
    }
    else if (aType == Brn("tidalpin_artist")) {
        aWriter.Write(Brn("Complete"));
        return LoadTracksByArtist(aInput);
    }
    else if (aType == Brn("tidalpin_album")) {
        aWriter.Write(Brn("Complete"));
        return LoadTracksByAlbum(aInput);
    }
    else if (aType == Brn("tidalpin_track")) {
        aWriter.Write(Brn("Complete"));
        return LoadTracksByTrack(aInput);
    }
    else if (aType == Brn("tidalpin_playlist")) {
        aWriter.Write(Brn("Complete"));
        return LoadTracksByPlaylist(aInput);
    }
    else if (aType == Brn("tidalpin_savedplaylist")) {
        aWriter.Write(Brn("Complete"));
        return LoadTracksBySavedPlaylists();
    }
    else if (aType == Brn("tidalpin_favorites")) {
        aWriter.Write(Brn("Complete"));
        return LoadTracksByFavorites();
    }
    else if (aType == Brn("tidalpin_genre")) {
        aWriter.Write(Brn("Complete"));
        return LoadTracksByGenre(aInput);
    }
    else if (aType == Brn("tidalpin_mood")) {
        aWriter.Write(Brn("Complete"));
        return LoadTracksByMood(aInput);
    }
    else if (aType == Brn("tidalpin_new")) {
        aWriter.Write(Brn("Complete"));
        return LoadTracksByNew();
    }
    else if (aType == Brn("tidalpin_recommended")) {
        aWriter.Write(Brn("Complete"));
        return LoadTracksByRecommended();
    }
    else if (aType == Brn("tidalpin_top20")) {
        aWriter.Write(Brn("Complete"));
        return LoadTracksByTop20();
    }
    else if (aType == Brn("tidalpin_exclusive")) {
        aWriter.Write(Brn("Complete"));
        return LoadTracksByExclusive();
    }
    else if (aType == Brn("tidalpin_rising")) {
        aWriter.Write(Brn("Complete"));
        return LoadTracksByRising();
    }
    else if (aType == Brn("tidalpin_discovery")) {
        aWriter.Write(Brn("Complete"));
        return LoadTracksByDiscovery();
    }
    return false;
}
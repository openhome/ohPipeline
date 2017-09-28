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
    return LoadTracksById(aArtist, TidalMetadata::eArtist);
}

TBool TidalPins::LoadTracksByAlbum(const Brx& aAlbum)
{
    return LoadTracksById(aAlbum, TidalMetadata::eAlbum);
}

TBool TidalPins::LoadTracksByTrack(const Brx& aTrack)
{
    return TidalPins::LoadTracksById(aTrack, TidalMetadata::eTrack);
}

TBool TidalPins::LoadTracksByPlaylist(const Brx& aPlaylist)
{
    return TidalPins::LoadTracksById(aPlaylist, TidalMetadata::ePlaylist);
}

TBool TidalPins::LoadTracksBySavedPlaylist()
{
    return TidalPins::LoadTracksById(TidalMetadata::kIdTypeUserSpecific, TidalMetadata::eSavedPlaylist);
}

TBool TidalPins::LoadTracksByFavorites()
{
    return TidalPins::LoadTracksById(TidalMetadata::kIdTypeUserSpecific, TidalMetadata::eFavorites);
}

TBool TidalPins::LoadTracksByGenre(const Brx& aGenre)
{
    return TidalPins::LoadTracksById(aGenre, TidalMetadata::eGenre);
}

TBool TidalPins::LoadTracksByMood(const Brx& aMood)
{
    return TidalPins::LoadTracksById(aMood, TidalMetadata::eMood);
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
    return TidalPins::LoadTracksBySmartType(TidalMetadata::eSmartExclusive);
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
    return LoadTracksById(TidalMetadata::kIdTypeSmart, aType);
}

TBool TidalPins::LoadTracksById(const Brx& aId, TidalMetadata::EIdType aType)
{
    AutoMutex _(iLock);
    TidalMetadata tm(iTrackFactory);
    TUint offset = 0;
    TUint total = iMaxTracks;
    TUint newId = 0;
    TUint currId = 0;
    TBool initPlay = false;
    TBool isPlayable = false;
    JsonParser parser;
    iCpPlaylist->SyncDeleteAll();
    Bwh inputBuf(64);

    try {
        if (aId.Bytes() == 0) {
            return false;
        }
        else if (!IsValidId(aId, aType)) {
            iJsonResponse.Reset();
            TBool success = iTidal.TryGetId(iJsonResponse, aId, aType); // send request to tidal
            if (!success) {
                return false;
            }
            inputBuf.ReplaceThrow(tm.FirstIdFromJson(iJsonResponse.Buffer(), aType)); // parse response from tidal
            if (inputBuf.Bytes() == 0) {
                return false;
            }
        }
        else {
            inputBuf.ReplaceThrow(aId);
        }
    }   
    catch (Exception& ex) {
        LOG_ERROR(kMedia, "%s in TidalPins::LoadTracksById (finding ID)\n", ex.Message());
        return false;
    }

    LOG(kMedia, "TidalPins::LoadTracksById: %.*s\n", PBUF(inputBuf));
    while (offset < total) {
        try {
            iJsonResponse.Reset();
            TBool success = iTidal.TryGetTracksById(iJsonResponse, inputBuf, aType, kTrackLimitPerRequest, offset);
            if (!success) {
                return false;
            }
            offset += kTrackLimitPerRequest;

            parser.Reset();
            parser.Parse(iJsonResponse.Buffer());
            if (parser.HasKey(Brn("totalNumberOfItems"))) { 
                TUint tracks = parser.Num(Brn("totalNumberOfItems"));
                if (tracks < total) {
                    total = tracks;
                }
                auto parserItems = JsonParserArray::Create(parser.String(Brn("items")));

                try {
                    for (;;) {
                        JsonParser parserItem;
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
            if (!initPlay && isPlayable) {
                initPlay = true;
                iCpPlaylist->SyncPlay();
            }
        }
        catch (Exception& ex) {
            LOG_ERROR(kMedia, "%s in TidalPins::LoadTracksById (finding tracks)\n", ex.Message());
            return false;
        }
        
    }
    return total;
}

TBool TidalPins::IsValidId(const Brx& aRequest, TidalMetadata::EIdType aType) {
    if (aType == TidalMetadata::ePlaylist) {
        return IsValidUuid(aRequest);
    }
    else if (aType == TidalMetadata::eSavedPlaylist) {
        return false; // initial response is list of playlists, so need to drill deeper to get the latest playlist ID
    }
    else if (aType == TidalMetadata::eGenre) {
        // could perform a check here using API to list all valid genre strings: https://api.tidal.com/v1/genres?countryCode={{countryCode}}
        // would need to call THROW(TidalRequestInvalid); on fail
        return true;
    }
    else if (aType == TidalMetadata::eMood) {
        // could perform a check here using API to list all valid mood strings: https://api.tidal.com/v1/moods?countryCode={{countryCode}}
        // would need to call THROW(TidalRequestInvalid); on fail
        return false; // only has a playlist option currently, so need to drill deeper to get the first playlist ID
    }
    else if (aRequest == TidalMetadata::kIdTypeUserSpecific) {
        // no additional user input
        return true;
    }
    else if (aRequest == TidalMetadata::kIdTypeSmart) {
        // could perform a check here using API to list all valid featured strings: https://api.tidal.com/v1/featured?countryCode={{countryCode}}
        // would need to call THROW(TidalRequestInvalid); on fail
        // rising and discovery have no validation
        return (aType != TidalMetadata::eSmartExclusive); // exclusive only has a playlist option currently, so need to drill deeper to get the first playlist ID. All other smart IDs are track based
    }

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
        return LoadTracksBySavedPlaylist();
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
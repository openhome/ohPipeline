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
#include <OpenHome/Net/Core/CpDeviceDv.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Net;
using namespace OpenHome::Configuration;

QobuzPins::QobuzPins(Qobuz& aQobuz, DvDeviceStandard& aDevice, Media::TrackFactory& aTrackFactory, CpStack& aCpStack, TUint aMaxTracks)
    : iLock("QPIN")
    , iQobuz(aQobuz)
    , iJsonResponse(kJsonResponseChunks)
    , iTrackFactory(aTrackFactory)
    , iCpStack(aCpStack)
    , iMaxTracks(aMaxTracks)
{
    CpDeviceDv* cpDevice = CpDeviceDv::New(iCpStack, aDevice);
    iCpPlaylist = new CpProxyAvOpenhomeOrgPlaylist1(*cpDevice);
    cpDevice->RemoveRef(); // iProxy will have claimed a reference to the device so no need for us to hang onto another
}

QobuzPins::~QobuzPins()
{
    delete iCpPlaylist;
}

void QobuzPins::Invoke(const IPin& aPin)
{
    PinUri pin(aPin);

    if (pin.Mode() == PinUri::EMode::eQobuz) {
        switch (pin.Type()) {
            case PinUri::EType::eArtist: LoadTracksByArtist(pin.Value(), pin.Shuffle()); break;
            case PinUri::EType::eAlbum: LoadTracksByAlbum(pin.Value(), pin.Shuffle()); break;
            case PinUri::EType::eTrack: LoadTracksByTrack(pin.Value(), pin.Shuffle()); break;
            case PinUri::EType::ePlaylist: LoadTracksByPlaylist(pin.Value(), pin.Shuffle()); break;
            case PinUri::EType::eSmart: {
                switch (pin.SmartType()) {
                    // optional genre parameter to filter smart playlists
                    case PinUri::ESmartType::eNew: LoadTracksByNew(pin.SmartGenre(), pin.Shuffle()); break;
                    case PinUri::ESmartType::eRecommended: LoadTracksByRecommended(pin.SmartGenre(), pin.Shuffle()); break;
                    case PinUri::ESmartType::eMostStreamed: LoadTracksByMostStreamed(pin.SmartGenre(), pin.Shuffle()); break;
                    case PinUri::ESmartType::eBestSellers: LoadTracksByBestSellers(pin.SmartGenre(), pin.Shuffle()); break;
                    case PinUri::ESmartType::eAwardWinning: LoadTracksByAwardWinning(pin.SmartGenre(), pin.Shuffle()); break;
                    case PinUri::ESmartType::eMostFeatured: LoadTracksByMostFeatured(pin.SmartGenre(), pin.Shuffle()); break;
                    case PinUri::ESmartType::eFavorites: LoadTracksByFavorites(pin.Shuffle()); break;
                    case PinUri::ESmartType::ePurchased: LoadTracksByPurchased(pin.Shuffle()); break;
                    case PinUri::ESmartType::eCollection: LoadTracksByCollection(pin.Shuffle()); break;
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

const TChar* QobuzPins::Mode() const
{
    return PinUri::GetModeString(PinUri::EMode::eQobuz);
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
    Bwh albumIds[kMaxAlbumsPerSmartType];

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
        TBool success = iQobuz.TryGetIds(iJsonResponse, inputBuf, aType, kMaxAlbumsPerSmartType); // send request to Qobuz
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
            if (albums == 0) {
                return false;
            }
            auto parserItems = JsonParserArray::Create(parser.String(Brn("items")));
            JsonParser parserItem;
            try {
                for (TUint i = 0; i < kMaxAlbumsPerSmartType; i++) {
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
                lastId = LoadTracksById(albumIds[j], aType, lastId);
            }  
        }
    }   
    catch (Exception& ex) {
        LOG_ERROR(kPipeline, "%s in QobuzPins::LoadTracksBySmartType\n", ex.Message());
        return false;
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
        lastId = LoadTracksById(inputBuf, aType, lastId);

        if (aType == QobuzMetadata::ePurchased || aType == QobuzMetadata::eCollection) {
            // should not be required but collection endpoint not working correctly (only returns albums, no tracks)
            lastId = LoadTracksById(inputBuf, QobuzMetadata::ePurchasedTracks, lastId);
        }

    }   
    catch (Exception& ex) {
        LOG_ERROR(kPipeline, "%s in QobuzPins::LoadTracksByQuery\n", ex.Message());
        return false;
    }

    return lastId;
}

TUint QobuzPins::LoadTracksById(const Brx& aId, QobuzMetadata::EIdType aType, TUint aPlaylistId)
{
    QobuzMetadata qm(iTrackFactory);
    TUint offset = 0;
    TUint total = iMaxTracks;
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
            TBool success = iQobuz.TryGetTracksById(iJsonResponse, aId, aType, kTrackLimitPerRequest, offset);
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
                    return false;
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
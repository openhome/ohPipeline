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
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Net/Core/CpDeviceDv.h>
#include <OpenHome/Private/Parser.h>

#include <algorithm>
#include <atomic>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Net;
using namespace OpenHome::Configuration;

// Pin mode
static const TChar* kPinModeTidal = "tidal";
static const Brn    kBufPinModeTidal = Brn(kPinModeTidal);

// Pin types
static const TChar* kPinTypeArtist = "artist";
static const TChar* kPinTypeAlbum = "album";
static const TChar* kPinTypeGenre = "genre";
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


TidalPins::TidalPins(Tidal& aTidal,
                     Environment& aEnv,
                     DvDeviceStandard& aDevice,
                     Media::TrackFactory& aTrackFactory,
                     CpStack& aCpStack,
                     IThreadPool& aThreadPool)
    : iLock("TPIN")
    , iTidal(aTidal)
    , iJsonResponse(kJsonResponseChunks)
    , iTidalMetadata(aTrackFactory)
    , iPin(iPinIdProvider)
    , iEnv(aEnv)
    , iInterrupted(false)
{
    CpDeviceDv* cpDevice = CpDeviceDv::New(aCpStack, aDevice);
    iCpPlaylist = new CpProxyAvOpenhomeOrgPlaylist1(*cpDevice);
    cpDevice->RemoveRef(); // iProxy will have claimed a reference to the device so no need for us to hang onto another
    iThreadPoolHandle = aThreadPool.CreateHandle(MakeFunctor(*this, &TidalPins::Invoke),
                                                 "TidalPins", ThreadPoolPriority::Medium);
}

TidalPins::~TidalPins()
{
    iThreadPoolHandle->Destroy();
    delete iCpPlaylist;
}

void TidalPins::BeginInvoke(const IPin& aPin, Functor aCompleted)
{
    if (aPin.Mode() != kBufPinModeTidal) {
        return;
    }
    AutoPinComplete completion(aCompleted);
    iInterrupted.store(false);
    iTidal.Interrupt(false);
    iTidal.Login(iToken);
    (void)iPin.TryUpdate(aPin.Mode(), aPin.Type(), aPin.Uri(), aPin.Title(),
                         aPin.Description(), aPin.ArtworkUri(), aPin.Shuffle());
    completion.Cancel();
    iCompleted = aCompleted;
    (void)iThreadPoolHandle->TrySchedule();
}

void TidalPins::Cancel()
{
    iInterrupted.store(true);
    iTidal.Interrupt(true);
}

const TChar* TidalPins::Mode() const
{
    return kPinModeTidal;
}

void TidalPins::Invoke()
{
    AutoFunctor _(iCompleted);
    iCpPlaylist->SyncTracksMax(iMaxPlaylistTracks);
    TBool res = false;
    try {
        PinUri pinUri(iPin);
        Brn val;
        if (Brn(pinUri.Type()) == Brn(kPinTypeTrack)) {
            if (pinUri.TryGetValue(kPinKeyTrackId, val)) {
                res = LoadByStringQuery(val, TidalMetadata::eTrack, iPin.Shuffle());
            }
            else {
                THROW(PinUriMissingRequiredParameter);
            }
        }
        else if (Brn(pinUri.Type()) == Brn(kPinTypeGenre) ||
                 Brn(pinUri.Type()) == Brn(kPinTypeContainer) ||
                 Brn(pinUri.Type()) == Brn(kPinTypePlaylist) ||
                 Brn(pinUri.Type()) == Brn(kPinTypeArtist) ||
                 Brn(pinUri.Type()) == Brn(kPinTypeAlbum)) {
            if (pinUri.TryGetValue(kPinKeyPath, val)) {
                res = LoadByPath(val, pinUri, iPin.Shuffle());
            }
            else if (pinUri.TryGetValue(kPinKeyId, val)) {
                // test only - load by string query as if done by a control point
                res = LoadByStringQuery(val, TidalMetadata::StringToIdType(pinUri.Type()), iPin.Shuffle());
            }
            else {
                THROW(PinUriMissingRequiredParameter);
            }
        }
        else {
            LOG_ERROR(kPipeline, "TidalPins::Invoke - unsupported type - %.*s\n", PBUF(iPin.Type()));
            THROW(PinTypeNotSupported);
        }
    }
    catch (PinUriMissingRequiredParameter&) {
        LOG_ERROR(kPipeline, "TidalPins::Invoke - missing parameter in %.*s\n", PBUF(iPin.Uri()));
        throw;
    }

    if (!res) {
        THROW(PinInvokeError);
    }
}

TBool TidalPins::LoadByPath(const Brx& aPath, const PinUri& aPinUri, TBool aShuffle)
{
    TBool res = false;
    Brn response(Brx::Empty());
    aPinUri.TryGetValue(kPinKeyResponseType, response);
    if (response == Brn(kPinResponseTracks)) {
        res = LoadTracks(aPath, aShuffle);
    }
    else if (response == Brn(kPinResponseAlbums)) {
        res = LoadContainers(aPath, TidalMetadata::eAlbum, aShuffle);
    }
    else if (response == Brn(kPinResponsePlaylists)) {
        res = LoadContainers(aPath, TidalMetadata::ePlaylist, aShuffle);
    }
    else {
        THROW(PinUriMissingRequiredParameter);
    }
    return res;
}

TBool TidalPins::LoadByStringQuery(const Brx& aQuery, TidalMetadata::EIdType aIdType, TBool aShuffle)
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
        // track/artist/album/playlist/genre search string to id
        else if (!IsValidId(aQuery, aIdType)) {
            iJsonResponse.Reset();
            TBool success = iTidal.TryGetId(iJsonResponse, aQuery, aIdType); // send request to tidal
            if (!success) {
                return false;
            }
            inputBuf.ReplaceThrow(TidalMetadata::FirstIdFromJson(iJsonResponse.Buffer(), aIdType)); // parse response from tidal
            if (inputBuf.Bytes() == 0) {
                return false;
            }
        }
        else {
            inputBuf.ReplaceThrow(aQuery);
        }
        try {
            TUint count = 0;
            lastId = LoadTracksById(inputBuf, aIdType, lastId, count);
            tracksFound = true;
        }
        catch (PinNothingToPlay&) {
        }
    }   
    catch (Exception& ex) {
        LOG_ERROR(kMedia, "%s in TidalPins::LoadByStringQuery\n", ex.Message());
        return false;
    }

    if (!tracksFound) {
        THROW(PinNothingToPlay);
    }

    return lastId;
}

TBool TidalPins::LoadTracks(const Brx& aPath, TBool aShuffle)
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
            TUint count = 0;
            lastId = LoadTracksById(aPath, TidalMetadata::eNone, lastId, count);
            tracksFound = true;
        }
        catch (PinNothingToPlay&) {
        }
    }   
    catch (Exception& ex) {
        LOG_ERROR(kMedia, "%s in TidalPins::LoadTracks\n", ex.Message());
        return false;
    }

    if (!tracksFound) {
        THROW(PinNothingToPlay);
    }

    return lastId;
}

TBool TidalPins::LoadContainers(const Brx& aPath, TidalMetadata::EIdType aIdType, TBool aShuffle)
{
    AutoMutex _(iLock);
    const TChar* kIdString = (aIdType == TidalMetadata::eAlbum) ? "id" : "uuid";
    const TUint kIdSize = (aIdType == TidalMetadata::eAlbum) ? 20 : 40;
    JsonParser parser;
    InitPlaylist(aShuffle);
    TUint lastId = 0;
    TUint tracksFound = 0;
    TUint containersFound = 0;
    Bwh containerIds[kItemLimitPerRequest];
    for (TUint i = 0; i < kItemLimitPerRequest; i++) {
        containerIds[i].Grow(kIdSize);
    }

    TUint start, end;
    TUint total = GetTotalItems(parser, aPath, TidalMetadata::eNone, true, start, end); // aIdType relevant to tracks, not containers
    TUint offset = start;

    do {
        try {
            iJsonResponse.Reset();
            TBool success = iTidal.TryGetIdsByRequest(iJsonResponse, aPath, kItemLimitPerRequest, offset); // send request to Tidal
            if (!success) {
                return false;
            }
            UpdateOffset(total, end, true, offset);
            
            // response is list of containers, so need to loop through containers
            parser.Reset();
            parser.Parse(iJsonResponse.Buffer());
            TUint idCount = 0;
            auto parserItems = JsonParserArray::Create(parser.String("items"));
            JsonParser parserItem;
            try {
                for (TUint i = 0; i < kItemLimitPerRequest; i++) {
                    parserItem.Parse(parserItems.NextObject());
                    containerIds[i].ReplaceThrow(parserItem.String(kIdString)); // parse response from Tidal
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
                    containersFound++;
                    if ( (tracksFound >= iMaxPlaylistTracks) || (containersFound >= total) ) {
                        return true;
                    }
                }
                catch (PinNothingToPlay&) {
                }
            } 
        }   
        catch (Exception& ex) {
            LOG_ERROR(kPipeline, "%s in TidalPins::LoadContainers\n", ex.Message());
            return false;
        }
    } while (offset != end);

    if (tracksFound == 0) {
        THROW(PinNothingToPlay);
    }

    return true;
}

TUint TidalPins::LoadTracksById(const Brx& aId, TidalMetadata::EIdType aIdType, TUint aPlaylistId, TUint& aCount)
{
    if (iInterrupted.load()) {
        LOG(kMedia, "TidalPins::LoadTracksById - interrupted\n");
        THROW(TidalPinsInterrupted);
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
    LOG(kMedia, "TidalPins::LoadTracksById: %.*s\n", PBUF(aId));
    do {
        try {
            iJsonResponse.Reset();
            TBool success = false;
            auto connection = aCount < iMaxPlaylistTracks - 1 ? Tidal::Connection::KeepAlive : Tidal::Connection::Close;
            if (aIdType == TidalMetadata::eNone) {
                success = iTidal.TryGetIdsByRequest(iJsonResponse, aId, kItemLimitPerRequest, offset, connection);
            }
            else {
                success = iTidal.TryGetTracksById(iJsonResponse, aId, aIdType, kItemLimitPerRequest, offset, connection);
            }
            if (!success) {
                return aPlaylistId;
            }
            UpdateOffset(total, end, false, offset);

            parser.Reset();
            parser.Parse(iJsonResponse.Buffer());
            try {
                if (parser.HasKey("items")) {
                    auto parserItems = JsonParserArray::Create(parser.String("items"));
                    for (;;) {
                        track = iTidalMetadata.TrackFromJson(parserItems.NextObject());
                        if (track != nullptr) {
                            aCount++;
                            iCpPlaylist->SyncInsert(currId, (*track).Uri(), (*track).MetaData(), newId);
                            track->RemoveRef();
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
                    // special case for only one track (no 'items' object)
                    track = iTidalMetadata.TrackFromJson(iJsonResponse.Buffer());
                    if (track != nullptr) {
                        aCount++;
                        iCpPlaylist->SyncInsert(currId, (*track).Uri(), (*track).MetaData(), newId);
                        track->RemoveRef();
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
            LOG_ERROR(kMedia, "%s in TidalPins::LoadTracksById (finding tracks)\n", ex.Message());
            if (track != nullptr) {
                track->RemoveRef();
            }
            throw;
        }
    } while (offset != end);

    if (!isPlayable) {
        THROW(PinNothingToPlay);
    }

    return currId;
}

TUint TidalPins::GetTotalItems(JsonParser& aParser, const Brx& aId, TidalMetadata::EIdType aIdType, TBool aIsContainer, TUint& aStartIndex, TUint& aEndIndex)
{
    TUint total = 0;
    try {
        iJsonResponse.Reset();
        TBool success = false;
        if (aIdType == TidalMetadata::eNone) {
            success = iTidal.TryGetIdsByRequest(iJsonResponse, aId, 1, 0);
        }
        else {
            success = iTidal.TryGetTracksById(iJsonResponse, aId, aIdType, 1, 0);
        }
        if (success) {
            aParser.Reset();
            aParser.Parse(iJsonResponse.Buffer());
            if (aParser.HasKey("totalNumberOfItems")) { 
                total = aParser.Num("totalNumberOfItems");
            }
            else {
                total = 1; // tidal glitch - total tag is not included if only one item
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

void TidalPins::UpdateOffset(TUint aTotalItems, TUint aEndIndex, TBool aIsContainer, TUint& aOffset)
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

TBool TidalPins::IsValidId(const Brx& aRequest, TidalMetadata::EIdType aIdType) {
    if (aIdType == TidalMetadata::ePlaylist) {
        return IsValidUuid(aRequest);
    }
    else if (aIdType == TidalMetadata::eGenre) {
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

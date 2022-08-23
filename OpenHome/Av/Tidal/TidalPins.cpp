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
static const TChar* kPinTypeMix = "mix";

// Pin params
static const TChar* kPinKeyTrackId = "trackId";
static const TChar* kPinKeyPath = "path";
static const TChar* kPinKeyResponseType = "response";
static const TChar* kPinKeyVersion = "version";
static const TChar* kPinKeyTokenId = "token";

// Pin response types
static const TChar* kPinResponseTracks = "tracks";
static const TChar* kPinResponseAlbums = "albums";
static const TChar* kPinResponseArtists = "artists";
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

TBool TidalPins::SupportsVersion(TUint version) const
{
    return version >= kMinSupportedVersion && version <= kMaxSupportedVersion;
}


void TidalPins::Invoke()
{
    AutoFunctor _(iCompleted);
    iCpPlaylist->SyncTracksMax(iMaxPlaylistTracks);
    TBool res = false;
    try {
        PinUri pinUri(iPin);
        Brn val;
        Brn tokenId;

        const TBool hasVersion = pinUri.TryGetValue(kPinKeyVersion, val);
        const TBool hasTokenId = pinUri.TryGetValue(kPinKeyTokenId, tokenId);
        const TBool isV2 = hasVersion && val == Brn("2");

        // Needs a version and if V2, needs a tokenId
        if (!hasVersion || (isV2 && !hasTokenId))
        {
            THROW(PinUriMissingRequiredParameter);
        }

        //If V2 pin, we *MUST* only use OAUTH. Fallback there to support V1 playback
        Tidal::AuthenticationConfig authConfig =
        {
            !isV2,     //fallbackIfTokenNotPresent
            tokenId    //oauthTokenId
        };

        Log::Print("Working with:\nfallbackIfNoTokenPresent: %d\noauthTokenId: %.*s\n", 
                   authConfig.fallbackIfTokenNotPresent, 
                   PBUF(authConfig.oauthTokenId.Bytes() == 0 ? Brn("None")
                                                             : Brn(authConfig.oauthTokenId)));

        const Brx& type = Brn(pinUri.Type());
        if (type == Brn(kPinTypeTrack)) {
            if (pinUri.TryGetValue(kPinKeyTrackId, val)) {
                res = LoadByStringQuery(val, TidalMetadata::eTrack, iPin.Shuffle(), authConfig);
            }
            else {
                THROW(PinUriMissingRequiredParameter);
            }
        }
        else if ( type == Brn(kPinTypeGenre) ||
                  type == Brn(kPinTypeContainer) ||
                  type == Brn(kPinTypePlaylist) ||
                  type == Brn(kPinTypeArtist) ||
                  type == Brn(kPinTypeAlbum) ||
                  type == Brn(kPinTypeMix)) {
            if (pinUri.TryGetValue(kPinKeyPath, val)) {
                res = LoadByPath(val, pinUri, iPin.Shuffle(), authConfig);
            }
            else {
                // Previous had a 'test only' branch as well, but this is no longer required
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

TBool TidalPins::LoadByPath(const Brx& aPath,
                            const PinUri& aPinUri,
                            TBool aShuffle,
                            const Tidal::AuthenticationConfig& aAuthConfig)
{
    TBool res = false;
    Brn response(Brx::Empty());
    aPinUri.TryGetValue(kPinKeyResponseType, response);
    if (response == Brn(kPinResponseTracks)) {
        res = LoadTracks(aPath, aShuffle, aAuthConfig);
    }
    else if (response == Brn(kPinResponseAlbums)) {
        res = LoadContainers(aPath, TidalMetadata::eAlbum, aShuffle, aAuthConfig);
    }
    else if (response == Brn(kPinResponsePlaylists)) {
        res = LoadContainers(aPath, TidalMetadata::ePlaylist, aShuffle, aAuthConfig);
    }
    else if (response == Brn(kPinResponseArtists)) {
        res = LoadContainers(aPath, TidalMetadata::eArtist, aShuffle, aAuthConfig);
    }
    else {
        THROW(PinUriMissingRequiredParameter);
    }
    return res;
}

TBool TidalPins::LoadByStringQuery(const Brx& aQuery,
                                   TidalMetadata::EIdType aIdType,
                                   TBool aShuffle,
                                   const Tidal::AuthenticationConfig& aAuthConfig)
{
    AutoMutex _(iLock);
    TUint lastId = 0;
    InitPlaylist(aShuffle);
    Bwh inputBuf(64);
    TUint tracksFound = 0;

    if (aQuery.Bytes() == 0) {
        return false;
    }

    if (!IsValidId(aQuery, aIdType)) {
        Log::Print("TidalPins::LoadByStringQuery - Invalid item ID %.*s (Type: %.*s)\n", PBUF(aQuery), PBUF(TidalMetadata::IdTypeToString(aIdType)));
        return false;
    }

    if (aQuery.Bytes() > inputBuf.MaxBytes()) {
        Log::Print("TidalPins::LoadByStringQuery - ID too long. Space: %u, size needed: %u (Type: %.*s)\n", inputBuf.MaxBytes(), aQuery.Bytes(), PBUF(TidalMetadata::IdTypeToString(aIdType)));
    }

    inputBuf.Replace(aQuery);

    try {
        lastId = LoadTracksById(inputBuf, aIdType, lastId, tracksFound, aAuthConfig);
    }   
    catch (PinNothingToPlay&) { // Do nothing...
    }
    catch (Exception& ex) {
        LOG_ERROR(kMedia, "%s in TidalPins::LoadByStringQuery\n", ex.Message());
        return false;
    }

    if (tracksFound == 0) {
        THROW(PinNothingToPlay);
    }

    return lastId;
}

TBool TidalPins::LoadTracks(const Brx& aPath,
                            TBool aShuffle,
                            const Tidal::AuthenticationConfig& aAuthConfig)
{
    AutoMutex _(iLock);
    TUint lastId = 0;
    InitPlaylist(aShuffle);
    TUint tracksFound = 0;

    if (aPath.Bytes() == 0) {
        return false;
    }

    try {
        lastId = LoadTracksById(aPath, TidalMetadata::eNone, lastId, tracksFound, aAuthConfig);
    }   
    catch (PinNothingToPlay&) { // Do nothing...
    }
    catch (Exception& ex) {
        LOG_ERROR(kMedia, "%s in TidalPins::LoadTracks\n", ex.Message());
        return false;
    }

    if (tracksFound == 0) {
        THROW(PinNothingToPlay);
    }

    return lastId;
}

TBool TidalPins::LoadContainers(const Brx& aPath,
                                TidalMetadata::EIdType aIdType,
                                TBool aShuffle,
                                const Tidal::AuthenticationConfig& aAuthConfig)
{
    AutoMutex _(iLock);
    const TChar* kIdString = (aIdType == TidalMetadata::ePlaylist) ? "uuid" : "id";
    const TUint kIdSize = (aIdType == TidalMetadata::ePlaylist) ? 40 : 20;
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
    TUint total = GetTotalItems(parser, aPath, TidalMetadata::eNone, true, start, end, aAuthConfig); // aIdType relevant to tracks, not containers
    TUint offset = start;

    do {
        try {
            iJsonResponse.Reset();
            TBool success = iTidal.TryGetIdsByRequest(iJsonResponse, aPath, kItemLimitPerRequest, offset, aAuthConfig); // send request to Tidal
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

            for (TUint i = 0; i < kItemLimitPerRequest; i++) {
                Brn obj;
                if (!parserItems.TryNextObject(obj)) {
                    break;
                }

                parserItem.Parse(obj);

                // Some TIDAL responses are nested in a wrapper object
                // featuring a 'created' date/time
                if (parserItem.HasKey("item")) {
                    parserItem.Parse(parserItem.String("item"));
                }

                containerIds[i].ReplaceThrow(parserItem.String(kIdString)); // parse response from Tidal
                idCount++;
                if (containerIds[i].Bytes() == 0) {
                    return false;
                }
            }

            for (TUint j = 0; j < idCount; j++) {
                try {
                    lastId = LoadTracksById(containerIds[j], aIdType, lastId, tracksFound, aAuthConfig);
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
            LOG_ERROR(kPipeline, "%s in TidalPins::LoadContainers\n", ex.Message());
            return false;
        }
    } while (offset != end);

    if (tracksFound == 0) {
        THROW(PinNothingToPlay);
    }

    return true;
}

TUint TidalPins::LoadTracksById(const Brx& aId,
                                TidalMetadata::EIdType aIdType,
                                TUint aPlaylistId,
                                TUint& aCount,
                                const Tidal::AuthenticationConfig& aAuthConfig)
{
    if (iInterrupted.load()) {
        LOG(kMedia, "TidalPins::LoadTracksById - interrupted\n");
        THROW(PinInterrupted);
    }

    TUint newId = 0;
    TUint currId = aPlaylistId;
    TBool initPlay = (aPlaylistId == 0);
    TBool isPlayable = false;
    JsonParser parser;
    Media::Track* track = nullptr;

    TUint start, end;
    TUint total = GetTotalItems(parser, aId, aIdType, false, start, end, aAuthConfig);
    TUint offset = start;

    // id to list of tracks
    LOG(kMedia, "TidalPins::LoadTracksById: %.*s\n", PBUF(aId));
    do {
        try {
            iJsonResponse.Reset();
            TBool success = false;
            auto connection = aCount < iMaxPlaylistTracks - 1 ? Tidal::Connection::KeepAlive : Tidal::Connection::Close;
            if (aIdType == TidalMetadata::eNone) {
                success = iTidal.TryGetIdsByRequest(iJsonResponse, aId, kItemLimitPerRequest, offset, aAuthConfig, connection);
            }
            else {
                success = iTidal.TryGetTracksById(iJsonResponse, aId, aIdType, kItemLimitPerRequest, offset, aAuthConfig, connection);
            }
            if (!success) {
                THROW(PinNothingToPlay);
            }
            UpdateOffset(total, end, false, offset);

            parser.Reset();
            parser.Parse(iJsonResponse.Buffer());

            if (parser.HasKey("items")) {
                auto parserItems = JsonParserArray::Create(parser.String("items"));
                Brn obj;
                while(parserItems.TryNextObject(obj)) {
                    track = iTidalMetadata.TrackFromJson(obj,
                                                         aAuthConfig.oauthTokenId);
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
                // special case for only one track (no 'items' object)
                track = iTidalMetadata.TrackFromJson(iJsonResponse.Buffer(),
                                                     aAuthConfig.oauthTokenId);
                if (track != nullptr) {
                    aCount++;
                    iCpPlaylist->SyncInsert(currId, (*track).Uri(), (*track).MetaData(), newId);
                    track->RemoveRef();
                    track = nullptr;
                    currId = newId;
                    isPlayable = true;
                }
            }

            if (initPlay && isPlayable) {
                initPlay = false;
                Thread::Sleep(300);
                iCpPlaylist->SyncPlay();
            }
        }
        catch (Exception& ex) {
            LOG_ERROR(kMedia, "%s in TidalPins::LoadTracksById (finding tracks)\n", ex.Message());
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

TUint TidalPins::GetTotalItems(JsonParser& aParser,
                               const Brx& aId,
                               TidalMetadata::EIdType aIdType,
                               TBool aIsContainer,
                               TUint& aStartIndex,
                               TUint& aEndIndex,
                               const Tidal::AuthenticationConfig& aAuthConfig)
{
    TUint total = 0;
    try {
        iJsonResponse.Reset();
        TBool success = false;
        if (aIdType == TidalMetadata::eNone) {
            success = iTidal.TryGetIdsByRequest(iJsonResponse, aId, 1, 0, aAuthConfig);
        }
        else {
            success = iTidal.TryGetTracksById(iJsonResponse, aId, aIdType, 1, 0, aAuthConfig);
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

TBool TidalPins::IsValidId(const Brx& aRequest, TidalMetadata::EIdType aIdType)
{
    // Function is currently only called with items of type 'Track'. All other types currently report invalid.
   if (aIdType != TidalMetadata::eTrack) {
       return false;
   }

    for (TUint i = 0; i<aRequest.Bytes(); i++) {
        if (!Ascii::IsDigit(aRequest[i])) {
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

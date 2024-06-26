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
#include <OpenHome/Private/Json.h>
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
static const TChar* kPinKeyShuffleMode = "shuffleMode";

// Shuffle Modes
static const Brn kShuffleModeNone("none");
static const Brn kShuffleModeDefault("default");
static const Brn kShuffleModeWhenRequired("whenRequired");

// Pin response types
static const TChar* kPinResponseTracks = "tracks";
static const TChar* kPinResponseAlbums = "albums";
static const TChar* kPinResponseArtists = "artists";
static const TChar* kPinResponsePlaylists = "playlists";

// NOTE: Sometimes the TIDAL API just ignores the 'limit' and 'offset' parameters provided in the request and gives us anything
//       Code below here ensures we correctly process the number of item(s) returned, not just what we expect to have been returned!
//       Known effected endpoints: mixes/{id}/items - Always returns all mix items (~ 100) for artist & track radios
static TUint GetRealFetchedItemCount(JsonParser& aParser, TUint expectedItemCount)
{
    if (aParser.HasKey("limit")) {
        TUint actualReturnedNumberOfItems = static_cast<TUint>(aParser.Num("limit"));
        if (actualReturnedNumberOfItems > expectedItemCount) {
            Log::Print("TidalPins::GetRealFetchedItemCount - WARNING!! Asked for %u item(s) but TIDAL returned %u item(s). Processing all %u item(s), but this may take a while.\n",
                       expectedItemCount,
                       actualReturnedNumberOfItems,
                       actualReturnedNumberOfItems);
        }

        return actualReturnedNumberOfItems;
    }

    return expectedItemCount;
}


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

        EShuffleMode shuffleMode = GetShuffleMode(pinUri);

        const Brx& type = Brn(pinUri.Type());
        if (type == Brn(kPinTypeTrack)) {
            if (pinUri.TryGetValue(kPinKeyTrackId, val)) {
                res = LoadByStringQuery(val, TidalMetadata::eTrack, iPin.Shuffle(), shuffleMode, authConfig);
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
                res = LoadByPath(val, pinUri, iPin.Shuffle(), shuffleMode, authConfig);
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
                            TBool aPinShuffled,
                            EShuffleMode aShuffleMode,
                            const Tidal::AuthenticationConfig& aAuthConfig)
{
    TBool res = false;
    Brn response(Brx::Empty());
    aPinUri.TryGetValue(kPinKeyResponseType, response);
    if (response == Brn(kPinResponseTracks)) {
        res = LoadTracks(aPath, aPinShuffled, aShuffleMode, aAuthConfig);
    }
    else if (response == Brn(kPinResponseAlbums)) {
        res = LoadContainers(aPath, TidalMetadata::eAlbum, aPinShuffled, aShuffleMode, aAuthConfig);
    }
    else if (response == Brn(kPinResponsePlaylists)) {
        res = LoadContainers(aPath, TidalMetadata::ePlaylist, aPinShuffled, aShuffleMode, aAuthConfig);
    }
    else if (response == Brn(kPinResponseArtists)) {
        res = LoadContainers(aPath, TidalMetadata::eArtist, aPinShuffled, aShuffleMode, aAuthConfig);
    }
    else {
        THROW(PinUriMissingRequiredParameter);
    }
    return res;
}

TBool TidalPins::LoadByStringQuery(const Brx& aQuery,
                                   TidalMetadata::EIdType aIdType,
                                   TBool aPinShuffled,
                                   EShuffleMode aShuffleMode,
                                   const Tidal::AuthenticationConfig& aAuthConfig)
{
    AutoMutex _(iLock);
    TUint lastId = 0;
    InitPlaylist(aPinShuffled);
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
        lastId = LoadTracksById(inputBuf, aIdType, lastId, tracksFound, aPinShuffled, aShuffleMode, aAuthConfig);
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
                            TBool aPinShuffled,
                            EShuffleMode aShuffleMode,
                            const Tidal::AuthenticationConfig& aAuthConfig)
{
    AutoMutex _(iLock);
    TUint lastId = 0;
    InitPlaylist(aPinShuffled);
    TUint tracksFound = 0;

    if (aPath.Bytes() == 0) {
        return false;
    }

    try {
        lastId = LoadTracksById(aPath, TidalMetadata::eNone, lastId, tracksFound, aPinShuffled, aShuffleMode, aAuthConfig);
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
                                TBool aPinShuffled,
                                EShuffleMode aShuffleMode,
                                const Tidal::AuthenticationConfig& aAuthConfig)
{
    AutoMutex _(iLock);
    const TChar* kIdString = (aIdType == TidalMetadata::ePlaylist) ? "uuid" : "id";
    const TUint kIdSize = (aIdType == TidalMetadata::ePlaylist) ? 40 : 20;
    JsonParser parser;
    InitPlaylist(aPinShuffled);
    TUint lastId = 0;
    TUint tracksFound = 0;
    TUint containersFound = 0;
    Bwh containerIds[kItemLimitPerRequest];
    for (TUint i = 0; i < kItemLimitPerRequest; i++) {
        containerIds[i].Grow(kIdSize);
    }

    const TBool shuffleLoadOrder = ShouldShuffleLoadOrder(aPinShuffled, aShuffleMode);

    TUint start, end;
    TUint total = GetTotalItems(parser, aPath, TidalMetadata::eNone, true, shuffleLoadOrder, start, end, aAuthConfig); // aIdType relevant to tracks, not containers
    TUint offset = start;

    do {
        try {
            iJsonResponse.Reset();
            TBool success = iTidal.TryGetIdsByRequest(iJsonResponse, aPath, kItemLimitPerRequest, offset, aAuthConfig); // send request to Tidal
            if (!success) {
                return false;
            }

            // response is list of containers, so need to loop through containers
            parser.Reset();
            parser.Parse(iJsonResponse.Buffer());

            const TUint fetchedItemCount = GetRealFetchedItemCount(parser, kItemLimitPerRequest);
            UpdateOffset(total, fetchedItemCount, end, true, shuffleLoadOrder, offset);
            
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
                    lastId = LoadTracksById(containerIds[j], aIdType, lastId, tracksFound, aPinShuffled, aShuffleMode, aAuthConfig);
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
    } while (shuffleLoadOrder ? offset != end
                              : offset < end);

    if (tracksFound == 0) {
        THROW(PinNothingToPlay);
    }

    return true;
}

TUint TidalPins::LoadTracksById(const Brx& aId,
                                TidalMetadata::EIdType aIdType,
                                TUint aPlaylistId,
                                TUint& aCount,
                                TBool aPinShuffled,
                                EShuffleMode aShuffleMode,
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

    const TBool shuffleLoadOrder = ShouldShuffleLoadOrder(aPinShuffled, aShuffleMode);

    TUint start, end;
    TUint total = GetTotalItems(parser, aId, aIdType, false, shuffleLoadOrder, start, end, aAuthConfig);
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

            parser.Reset();
            parser.Parse(iJsonResponse.Buffer());

            const TUint fetchedItemCount = GetRealFetchedItemCount(parser, kItemLimitPerRequest);
            UpdateOffset(total, fetchedItemCount, end, false, shuffleLoadOrder, offset);

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
    } while (shuffleLoadOrder ? offset != end
                              : offset < end);

    if (!isPlayable) {
        THROW(PinNothingToPlay);
    }

    return currId;
}

TUint TidalPins::GetTotalItems(JsonParser& aParser,
                               const Brx& aId,
                               TidalMetadata::EIdType aIdType,
                               TBool aIsContainer,
                               TBool aShouldShuffleLoadOrder,
                               TUint& aStartIndex,
                               TUint& aEndIndex,
                               const Tidal::AuthenticationConfig& aAuthConfig)
{
    // Track = single item
    if (aIdType == TidalMetadata::eTrack) {
        aStartIndex = 0;
        aEndIndex = 1;
        return 1;
    }

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

    if (aShouldShuffleLoadOrder) {
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
    }

    return total;
}

void TidalPins::UpdateOffset(TUint aTotalItems, TUint aFetchedCount, TUint aEndIndex, TBool aIsContainer, TBool aShouldShuffleLoadOrder, TUint& aOffset)
{
    aOffset += aFetchedCount;
    if (!aShouldShuffleLoadOrder) {
        return;
    }

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

TidalPins::EShuffleMode TidalPins::GetShuffleMode(PinUri& aPinUri)
{
    Brn shuffleMode;

    if (!aPinUri.TryGetValue(kPinKeyShuffleMode, shuffleMode)) {
        LOG_INFO(kMedia, "TidalPins::GetShuffleMode - Using: Default (Inferred)\n");
        return EShuffleMode::Default;
    }

    if (shuffleMode == kShuffleModeNone) {
        LOG_INFO(kMedia, "TidalPins::GetShuffleMode - Using: None\n");
        return EShuffleMode::None;
    }
    else if (shuffleMode == kShuffleModeDefault) {
        LOG_INFO(kMedia, "TidalPins::GetShuffleMode - Using: Default\n");
        return EShuffleMode::Default;
    }
    else if (shuffleMode == kShuffleModeWhenRequired) {
        LOG_INFO(kMedia, "TidalPins::GetShuffleMode - Using: WhenRequired\n");
        return EShuffleMode::WhenRequired;
    }
    else {
        LOG_INFO(kMedia, "TidalPins::GetShuffleMode - Usiing: Default (Unknown mode (%.*s) requested)\n", PBUF(shuffleMode));
        return EShuffleMode::Default;
    }
}

TBool TidalPins::ShouldShuffleLoadOrder(TBool aPinShuffled, EShuffleMode aShuffleMode)
{
    switch (aShuffleMode)
    {
        case EShuffleMode::None:
            return false;

        case EShuffleMode::Default:
            return true;

        case EShuffleMode::WhenRequired:
            return aPinShuffled;

        default: // Not reached
            return true;
    }
}


// TIDALPinRefresher
TidalPinRefresher::TidalPinRefresher(Tidal& aTidal)
    : iTidal(aTidal)
{ }

TidalPinRefresher::~TidalPinRefresher()
{ }


const TChar* TidalPinRefresher::Mode() const
{
    return kPinModeTidal;
}

static Brn TryGetItemIdFromPinPath(const Brx& aPinPath, const Brx& aExpectedType)
{
    Brn segment(Brx::Empty());
    Parser pathParser(aPinPath);

    // Sadly, the pinPath is an escaped URL. We don't really want to have to unescape it, so we'll attempt to parse
    // around the case everything is %age encoded.

    // TIDAL URLs always start: https://<base_url>/<v1>/... With %age encoding, this means we want to skip the first 5
    // '%' encoded characters. This should take us to the remainder of the URL
    pathParser.NextNth(5, '%');
    pathParser.Forward(2); // Consume the %age encoded HEX value for the trailing slash

    // The next segments in TIDAL URLS are <item_type>/<id>....
    segment.Set(pathParser.Next('%'));
    if (segment != aExpectedType) {
        LOG_ERROR(kMedia, "TidalPins::TryGetItemIdFromPinPath - Expected a type: %.*s, but found %.*s\n", PBUF(segment), PBUF(aExpectedType));
        return Brx::Empty();
    }

    pathParser.Forward(2); // Comsume the %age encoded HEX value for the trailing slash

    // Finally, this next segment should contain the item ID we're interested in.
    segment.Set(pathParser.Next('%'));
    return segment;
}


EPinMetadataStatus TidalPinRefresher::TryRefreshMixPinMetadata(const IPin& aPin, Pin& aUpdated, const Brx& aPinPath, Tidal::AuthenticationConfig& aAuthConfig)
{
    Brn mixId = TryGetItemIdFromPinPath(aPinPath, Brn("mixes"));
    if (mixId.Bytes() == 0) {
        LOG_ERROR(kMedia, "TidalPinRefresher::TryRefreshMixPinMetadata - Failed to extract TIDAL ID from pin path: %.*s\n", PBUF(aPinPath));
        return EPinMetadataStatus::Error;
    }

    // There are 2 types of Mixes - "Daily" mixes and Artist/Track "Radios".
    // TIDAL provides no API allowing us to confirm the status of Artist/Track "Radios". You ask for tracks and you get them back but have no idea if they
    // are part of the radio/mix for the original track & artist.
    // The "Daily" mixes can be accessed through the "Mix" endpoint. However, this doesn't include "My Daily Discovery" which can only be accessed using
    // the endpoint below.

    // NOTE: This endpoint doesn't respect the Limit & Offset params, but we must provide them to our internal TIDAL function call.
    const TUint kRequestLimit = 15;
    const TUint kRequestOffset = 0;
    const Brn kMixRequestUrl("https://api.tidalhifi.com/v1/pages/my_collection_my_mixes?deviceType=PHONE");

    WriterBwh jsonResponse(4096); // Mix response can be quite large

    if (!iTidal.TryGetIdsByRequest(jsonResponse, kMixRequestUrl, kRequestLimit, kRequestOffset, aAuthConfig)) {
        LOG_ERROR(kMedia, "TidalPinRefresher::TryRefreshMixPinMetadata - TIDAL API request failed to get user mixes!\n");
        return EPinMetadataStatus::Unresolvable;
    }

    // The repsonse from the mix endpoint above is a pretty horrible mess of nested JSON objects, more so than their normal API endpoints
    /* ....
     * "rows" = JSON array of objects (only ever 1 of these...)
     *     "modules" = JSON array of objects (only every 1 on of these...)
     *         "pagedList" = JSON object, similar to a standard paged API response
     *             "items" = JSON array of 'Mix' items
     *                 [ MIX OBJECT]
     *                 "title" = Mix Name
     *                 "images" = JSON object of type 'Image'
     *                     "SMALL|MEDIUM|LARGE" = Names of the available images
     *                         "url" = Actual URL of the image
     */

    try {
        JsonParser p;
        Brn firstItem(Brx::Empty());

        p.Parse(jsonResponse.Buffer());

        if (p.HasKey("rows")) {
            JsonParserArray ap = JsonParserArray::Create(p.String("rows"));
            firstItem = ap.NextObject();
            p.Parse(firstItem);
            if (p.HasKey("modules")) {
                ap = JsonParserArray::Create(p.String("modules"));
                firstItem = ap.NextObject();
                p.Parse(firstItem);
                if (p.HasKey("pagedList")) {
                    p.Parse(p.String("pagedList"));
                    if (p.HasKey("items")) {
                        Brn obj;
                        ap = JsonParserArray::Create(p.String("items"));
                        while(ap.TryNextObject(obj)) {
                            // Phew - we should now have access to each of the user's Mixes now.
                            p.Parse(obj);

                            if (!p.HasKey("id")) {
                                continue;
                            }

                            Brn jsonId = p.String("id");
                            if (jsonId == mixId) {
                                // Found matching mix. Really only the artwork will change, so we'll check that.
                                // NOTE: We only care about the "SMALL" image as this is what CPs set for the pin.
                                p.Parse(p.String("images"));
                                p.Parse(p.String("SMALL"));
                                Brn artwork = p.String("url");

                                // Only update if the artwork has changed!
                                if (artwork != aPin.ArtworkUri()) {
                                    aUpdated.TryUpdate(aPin.Mode(),
                                                       aPin.Type(),
                                                       aPin.Uri(),
                                                       aPin.Title(),
                                                       aPin.Description(),
                                                       artwork,
                                                       aPin.Shuffle());
                                    return EPinMetadataStatus::Changed;
                                }
                                else {
                                    return EPinMetadataStatus::Same;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    catch (const AssertionFailed&) {
        throw;
    }

    catch (Exception& ex) {
        LOG_ERROR(kMedia, "TidalPinRefresher::TryRefreshMixPinMetadata - '%s' error occured when trying to refresh metadata.\n", ex.Message());
        return EPinMetadataStatus::Error;
    }

    // By this point we'll have to assume it's an artist or track "Radio" mix where we can't confirm if it has changed.
    return EPinMetadataStatus::Same;
}



EPinMetadataStatus TidalPinRefresher::RefreshPinMetadata(const IPin& aPin, Pin& aUpdated)
{
    if (aPin.Type() != Brn(kPinTypeMix)) {
        return EPinMetadataStatus::Same; // We only support checking the status of Mix pins
    }

    PinUri pinHelper(aPin);

    // First, need to grab bits and pieces from the pin URI that we'll require to refresh this item...
    Brn path;
    Brn version;
    Brn tokenId;

    const TBool hasPath    = pinHelper.TryGetValue(kPinKeyPath, path);
    const TBool hasVersion = pinHelper.TryGetValue(kPinKeyVersion, version);
    const TBool hasTokenId = pinHelper.TryGetValue(kPinKeyTokenId, tokenId);
    const TBool isV2       = hasVersion && version == Brn("2");

    // For TIDAL pins, we'll enforce that only V2+ pins can be refreshed, as these contain an OAUTH token ID.
    // This is required because we can refresh 'Mix' types, which are specific to the user's account ID provided as part
    // of the OAuth token!
    if (!hasPath || !isV2 || !hasTokenId)
    {
        LOG_ERROR(kMedia, "TidalPinRefresher::RefreshPinMetadata - Pin has a required parameter missing.\n");
        return EPinMetadataStatus::Error; // Can't resolve this pin as we don't have the correct information.
    }

    Tidal::AuthenticationConfig authConfig =
        {
            false,     //fallbackIfTokenNotPresent
            tokenId    //oauthTokenId
        };

    return TryRefreshMixPinMetadata(aPin, aUpdated, path, authConfig);
}



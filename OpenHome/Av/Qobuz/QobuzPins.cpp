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
static const TChar* kPinKeyTrackId = "trackId";
static const TChar* kPinKeyPath = "path";
static const TChar* kPinKeyResponseType = "response";
static const TChar* kPinKeyShuffleMode = "shuffleMode";

// Pin response types
static const TChar* kPinResponseTracks = "tracks";
static const TChar* kPinResponseAlbums = "albums";
static const TChar* kPinResponseArtists = "artists";
static const TChar* kPinResponsePlaylists = "playlists";

// JSON Property Keys
static const Brn kPropertyAlbums("albums");
static const Brn kPropertyArtists("artists");
static const Brn kPropertyPlaylists("playlists");
static const Brn kPropertyTracks("tracks");
static const Brn kPropertyTracksAppearsOn("tracks_appears_on");

// Shuffle Modes
static const Brn kShuffleModeNone("none");
static const Brn kShuffleModeDefault("default");
static const Brn kShuffleModeWhenRequired("whenRequired");

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

TBool QobuzPins::SupportsVersion(TUint version) const
{
    return version >= kMinSupportedVersion && version <= kMaxSupportedVersion;
}


void QobuzPins::Invoke()
{
    AutoFunctor _(iCompleted);
    iCpPlaylist->SyncTracksMax(iMaxPlaylistTracks);
    TBool res = false;
    try {
        PinUri pinUri(iPin);
        Brn val;

        EShuffleMode shuffleMode = GetShuffleMode(pinUri);

        if (Brn(pinUri.Type()) == Brn(kPinTypeTrack)) {
            if (pinUri.TryGetValue(kPinKeyTrackId, val)) {
                res = LoadByStringQuery(val, QobuzMetadata::eTrack, iPin.Shuffle(), shuffleMode);
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
                res = LoadByPath(val, pinUri, iPin.Shuffle(), shuffleMode);
            }
            else {
                // Previous had a 'test only' branch as well, but this is no longer required
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

TBool QobuzPins::LoadByPath(const Brx& aPath, const PinUri& aPinUri, TBool aPinShuffle, EShuffleMode aShuffleMode)
{
    TBool res = false;
    Brn response(Brx::Empty());
    aPinUri.TryGetValue(kPinKeyResponseType, response);
    if (response == Brn(kPinResponseTracks)) {
        res = LoadTracks(aPath, aPinShuffle, aShuffleMode);
    }
    else if (response == Brn(kPinResponseAlbums)) {
        res = LoadContainers(aPath, QobuzMetadata::eAlbum, aPinShuffle, aShuffleMode);
    }
    else if (response == Brn(kPinResponsePlaylists)) {
        res = LoadContainers(aPath, QobuzMetadata::ePlaylist, aPinShuffle, aShuffleMode);
    }
    else if (response == Brn(kPinResponseArtists)) {
        res = LoadContainers(aPath, QobuzMetadata::eArtist, aPinShuffle, aShuffleMode);
    }
    else {
        THROW(PinUriMissingRequiredParameter);
    }
    return res;
}

TBool QobuzPins::LoadByStringQuery(const Brx& aQuery, QobuzMetadata::EIdType aIdType, TBool aPinShuffle, EShuffleMode aShuffleMode)
{
    AutoMutex _(iLock);
    TUint lastId = 0;
    InitPlaylist(aPinShuffle);
    Bwh inputBuf(64);
    TUint tracksFound = 0;

    if (aQuery.Bytes() == 0) {
        return false;
    }

    if (!IsValidId(aQuery, aIdType)) {
        Log::Print("QobuzPins::LoadByStringQuery - Invalid item ID %.*s (Type: %.*s)\n", PBUF(aQuery), PBUF(QobuzMetadata::IdTypeToString(aIdType)));
        return false;
    }

    if (aQuery.Bytes() > inputBuf.MaxBytes()) {
        Log::Print("QobuzPins::LoadByStringQuery - ID too long. Space: %u, size needed: %u (Type: %.*s)\n", inputBuf.MaxBytes(), aQuery.Bytes(), PBUF(QobuzMetadata::IdTypeToString(aIdType)));
    }

    inputBuf.Replace(aQuery);

    try {
        lastId = LoadTracksById(inputBuf, aIdType, lastId, tracksFound, aPinShuffle, aShuffleMode);
    }
    catch (PinNothingToPlay&) { // Do nothing...
    }
    catch (Exception& ex) {
        LOG_ERROR(kMedia, "%s in QobuzPins::LoadByStringQuery\n", ex.Message());
        return false;
    }

    if (tracksFound == 0) {
        THROW(PinNothingToPlay);
    }

    return lastId;
}

TBool QobuzPins::LoadTracks(const Brx& aPath, TBool aPinShuffle, EShuffleMode aShuffleMode)
{
    AutoMutex _(iLock);
    TUint lastId = 0;
    InitPlaylist(aPinShuffle);
    TUint tracksFound = 0;

    if (aPath.Bytes() == 0) {
        return false;
    }

    try {
        lastId = LoadTracksById(aPath, QobuzMetadata::eNone, lastId, tracksFound, aPinShuffle, aShuffleMode);
    }
    catch (PinNothingToPlay&) {
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

TBool QobuzPins::LoadContainers(const Brx& aPath, QobuzMetadata::EIdType aIdType, TBool aPinShuffled, EShuffleMode aShuffleMode)
{
    AutoMutex _(iLock);
    JsonParser parser;
    InitPlaylist(aPinShuffled);
    TUint lastId = 0;
    TUint tracksFound = 0;
    TUint containersFound = 0;
    Bwh containerIds[kItemLimitPerRequest];
    for (TUint i = 0; i < kItemLimitPerRequest; i++) {
        containerIds[i].Grow(20);
    }

    const TBool shuffleLoadOrder = ShouldShuffleLoadOrder(aPinShuffled, aShuffleMode);

    TUint start, end;
    TUint total = GetTotalItems(parser, aPath, QobuzMetadata::eNone, true, shuffleLoadOrder, start, end); // aIdType relevant to tracks, not containers
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

            FindResponse(parser);

            auto parserItems = JsonParserArray::Create(parser.String(Brn("items")));
            JsonParser parserItem;

            for (TUint i = 0; i < kItemLimitPerRequest; i++) {
                Brn obj;
                if (!parserItems.TryNextObject(obj)) {
                    break;
                }

                parserItem.Parse(obj);
                containerIds[i].ReplaceThrow(parserItem.String(Brn("id"))); // parse response from Qobuz
                idCount++;
                if (containerIds[i].Bytes() == 0) {
                    return false;
                }
            }

            for (TUint j = 0; j < idCount; j++) {
                try {
                    lastId = LoadTracksById(containerIds[j], aIdType, lastId, tracksFound, aPinShuffled, aShuffleMode);
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
    } while (shuffleLoadOrder ? offset != end
                              : offset < end);

    if (tracksFound == 0) {
        THROW(PinNothingToPlay);
    }

    return true;
}

TUint QobuzPins::LoadTracksById(const Brx& aId, QobuzMetadata::EIdType aIdType, TUint aPlaylistId, TUint& aCount, TBool aPinShuffled, EShuffleMode aShuffleMode)
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

    const TBool shuffleLoadOrder = ShouldShuffleLoadOrder(aPinShuffled, aShuffleMode);


    TUint start, end;
    TUint total = GetTotalItems(parser, aId, aIdType, false, shuffleLoadOrder, start, end);
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

            if (parser.HasKey(kPropertyTracks)) {
                parser.Parse(parser.String(kPropertyTracks));
            }
            else if (parser.HasKey(kPropertyTracksAppearsOn)) {
                parser.Parse(parser.String(kPropertyTracksAppearsOn));
            }

            // Most Qobuz containers only provide required metadata in the parent container object, instead of the track objects directly.
            // We'll pre-parse the parent and provide that information when constructing tracks to reduce the amount of work we have to do.
            const TBool hasParentMetadata = iQobuzMetadata.TryParseParentMetadata(iJsonResponse.Buffer(), iParentMetadata);

            if (parser.HasKey("items")) {
                auto parserItems = JsonParserArray::Create(parser.String("items"));
                Brn obj;
                while(parserItems.TryNextObject(obj)) {
                    track = iQobuzMetadata.TrackFromJson(hasParentMetadata, iParentMetadata, obj);
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
                track = iQobuzMetadata.TrackFromJson(hasParentMetadata, iParentMetadata, iJsonResponse.Buffer());
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
            LOG_ERROR(kPipeline, "%s in QobuzPins::LoadTracksById \n", ex.Message());
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

TUint QobuzPins::GetTotalItems(JsonParser& aParser, const Brx& aId, QobuzMetadata::EIdType aIdType, TBool aIsContainer, TBool aShouldShuffleLoadOrder, TUint& aStartIndex, TUint& aEndIndex)
{
    // Track = single item
    if (aIdType == QobuzMetadata::eTrack) {
        aStartIndex = 0;
        aEndIndex = 1;
        return 1;
    }

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

            FindResponse(aParser);

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

TBool QobuzPins::IsValidId(const Brx& aRequest, QobuzMetadata::EIdType aIdType)
{
    // Function is currently only called with items of type 'Track'. All other types currently report invalid.
    if (aIdType != QobuzMetadata::EIdType::eTrack) {
        return false;
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

void QobuzPins::FindResponse(JsonParser& aParser)
{
    if (aParser.HasKey(kPropertyAlbums)) {
        aParser.Parse(aParser.String(kPropertyAlbums));
    }
    else if (aParser.HasKey(kPropertyPlaylists)) {
        aParser.Parse(aParser.String(kPropertyPlaylists));
    }
    else if (aParser.HasKey(kPropertyTracks)) {
        aParser.Parse(aParser.String(kPropertyTracks));
    }
    else if (aParser.HasKey(kPropertyArtists)) {
        aParser.Parse(aParser.String(kPropertyArtists));
    }
    else if (aParser.HasKey(kPropertyTracksAppearsOn)) {
        aParser.Parse(aParser.String(kPropertyTracksAppearsOn));
    }
}

QobuzPins::EShuffleMode QobuzPins::GetShuffleMode(PinUri& aPinUri)
{
    Brn shuffleMode;

    if (!aPinUri.TryGetValue(kPinKeyShuffleMode, shuffleMode)) {
        LOG_INFO(kMedia, "QobuzPins::GetShuffleMode - Using: Default (Inferred)\n");
        return EShuffleMode::Default;
    }

    if (shuffleMode == kShuffleModeNone) {
        LOG_INFO(kMedia, "QobuzPins::GetShuffleMode - Using: None\n");
        return EShuffleMode::None;
    }
    else if (shuffleMode == kShuffleModeDefault) {
        LOG_INFO(kMedia, "QobuzPins::GetShuffleMode - Using: Default\n");
        return EShuffleMode::Default;
    }
    else if (shuffleMode == kShuffleModeWhenRequired) {
        LOG_INFO(kMedia, "QobuzPins::GetShuffleMode - Using: WhenRequired\n");
        return EShuffleMode::WhenRequired;
    }
    else {
        LOG_INFO(kMedia, "QobuzPins::GetShuffleMode - Usiing: Default (Unknown mode (%.*s) requested)\n", PBUF(shuffleMode));
        return EShuffleMode::Default;
    }
}

TBool QobuzPins::ShouldShuffleLoadOrder(TBool aPinShuffled, EShuffleMode aShuffleMode)
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


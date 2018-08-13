#include <OpenHome/Av/Pins/PodcastPins.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Av/Utils/FormUrl.h>
#include <OpenHome/Json.h>
#include <OpenHome/Net/Core/CpDeviceDv.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Private/Timer.h>
#include <Generated/CpAvOpenhomeOrgRadio1.h>
#include <Generated/CpAvOpenhomeOrgPlaylist1.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Net;

// Pin modes
static const TChar* kPinModeItunesEpisode = "itune";
static const TChar* kPinModeItunesList = "ituneslist";

// Pin types
static const TChar* kPinTypePodcast = "podcast";

// Pin params
static const TChar* kPinKeyEpisodeId = "id";
static const TChar* kPinKeyPath = "path";

// Store values
static const Brn kStoreKeyITunesPodcast("Pins.PodcastITunes");

const TUint kTimerDurationMs = (1000 * 60 * 60 * 1); // every hour
//const TUint kTimerDurationMs = 1000 * 60; // 1 min - TEST ONLY

// PodcastPinsLatestEpisode

PodcastPinsLatestEpisode::PodcastPinsLatestEpisode(Net::DvDeviceStandard& aDevice, Media::TrackFactory& aTrackFactory, Net::CpStack& aCpStack, Configuration::IStoreReadWrite& aStore)
{
    iPodcastPins = PodcastPins::GetInstance(aTrackFactory, aCpStack.Env(), aStore);

    CpDeviceDv* cpDevice = CpDeviceDv::New(aCpStack, aDevice);
    iCpRadio = new CpProxyAvOpenhomeOrgRadio1(*cpDevice);
    cpDevice->RemoveRef(); // iProxy will have claimed a reference to the device so no need for us to hang onto another
}

PodcastPinsLatestEpisode::~PodcastPinsLatestEpisode()
{
    delete iCpRadio;
}

void PodcastPinsLatestEpisode::BeginInvoke(const IPin& aPin, Functor aCompleted)
{
    AutoFunctor _(aCompleted);
    PinUri pin(aPin);
    TBool res = false;
    if (Brn(pin.Mode()) == Brn(kPinModeItunesEpisode)) {
        if (Brn(pin.Type()) == Brn(kPinTypePodcast)) {
            Brn episodeId;
            if (pin.TryGetValue(kPinKeyEpisodeId, episodeId)) {
                res = iPodcastPins->LoadPodcastLatest(episodeId, *this);
            }
            else {
                THROW(PinUriMissingRequiredParameter);
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

void PodcastPinsLatestEpisode::Cancel()
{
}

const TChar* PodcastPinsLatestEpisode::Mode() const
{
    return kPinModeItunesEpisode;
}

void PodcastPinsLatestEpisode::Init(TBool /*aShuffle*/)
{
    // Single shot so nothing to delete or shuffle
}

void PodcastPinsLatestEpisode::Load(Media::Track& aTrack)
{
    iCpRadio->SyncSetChannel(aTrack.Uri(), aTrack.MetaData());
}

void PodcastPinsLatestEpisode::Play()
{
    iCpRadio->SyncPlay();
}

TBool PodcastPinsLatestEpisode::SingleShot()
{
    return true;
}

// PodcastPinsEpisodeList

PodcastPinsEpisodeList::PodcastPinsEpisodeList(Net::DvDeviceStandard& aDevice, Media::TrackFactory& aTrackFactory, Net::CpStack& aCpStack, Configuration::IStoreReadWrite& aStore)
    : iLastId(0)
{
    iPodcastPins = PodcastPins::GetInstance(aTrackFactory, aCpStack.Env(), aStore);

    CpDeviceDv* cpDevice = CpDeviceDv::New(aCpStack, aDevice);
    iCpPlaylist = new CpProxyAvOpenhomeOrgPlaylist1(*cpDevice);
    cpDevice->RemoveRef(); // iProxy will have claimed a reference to the device so no need for us to hang onto another
}

PodcastPinsEpisodeList::~PodcastPinsEpisodeList()
{
    delete iCpPlaylist;
}

void PodcastPinsEpisodeList::BeginInvoke(const IPin& aPin, Functor aCompleted)
{
    AutoFunctor _(aCompleted);
    PinUri pin(aPin);
    TBool res = false;
    if (Brn(pin.Mode()) == Brn(kPinModeItunesList)) {
        if (Brn(pin.Type()) == Brn(kPinTypePodcast)) {
            Brn episodeId;
            if (pin.TryGetValue(kPinKeyEpisodeId, episodeId)) {
                res = iPodcastPins->LoadPodcastList(episodeId, *this, aPin.Shuffle());
            }
            else {
                THROW(PinUriMissingRequiredParameter);
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

void PodcastPinsEpisodeList::Cancel()
{
}

const TChar* PodcastPinsEpisodeList::Mode() const
{
    return kPinModeItunesList;
}

void PodcastPinsEpisodeList::Init(TBool aShuffle)
{
    iCpPlaylist->SyncDeleteAll();
    iLastId = 0;
    iCpPlaylist->SyncSetShuffle(aShuffle);
}

void PodcastPinsEpisodeList::Load(Media::Track& aTrack)
{
    TUint newId;
    iCpPlaylist->SyncInsert(iLastId, aTrack.Uri(), aTrack.MetaData(), newId);
    iLastId = newId;
}

void PodcastPinsEpisodeList::Play()
{
    iCpPlaylist->SyncPlay();
}

TBool PodcastPinsEpisodeList::SingleShot()
{
    return false;
}

// PodcastPins

PodcastPins* PodcastPins::iInstance = nullptr;

PodcastPins* PodcastPins::GetInstance(Media::TrackFactory& aTrackFactory, Environment& aEnv, Configuration::IStoreReadWrite& aStore)
{
    if (iInstance == nullptr) {
        iInstance = new PodcastPins(aTrackFactory, aEnv, aStore);
    }
    return iInstance;
}

PodcastPins::PodcastPins(Media::TrackFactory& aTrackFactory, Environment& aEnv, Configuration::IStoreReadWrite& aStore)
    : iLock("PPIN")
    , iJsonResponse(kJsonResponseChunks)
    , iXmlResponse(kXmlResponseChunks)
    , iTrackFactory(aTrackFactory)
    , iStore(aStore)
    , iListenedDates(kMaxEntryBytes*kMaxEntries)
{
    iITunes = new ITunes(aEnv);

    // Don't push any mappings into iMappings yet.
    // Instead, start by populating from store. Then, if it is not full, fill up
    // to iMaxEntries bytes with inactive mappings.

    TUint mapCount = 0;
    iListenedDates.SetBytes(0);
    try {
        iStore.Read(kStoreKeyITunesPodcast, iListenedDates);
        Log::Print("PodcastPins Load listened dates from store: %.*s\n", PBUF(iListenedDates));
    }
    catch (StoreKeyNotFound&) {
        // Key not in store, so no config stored yet and nothing to parse.
        Log::Print("Store Key not found: %.*s\n", PBUF(kStoreKeyITunesPodcast));
    }

    if (iListenedDates.Bytes() > 0) {
        JsonParser parser;
        auto parserItems = JsonParserArray::Create(iListenedDates);
        try {
            for (;;) {
                parser.Parse(parserItems.NextObject());
                Brn id = parser.String("id");
                Brn date = parser.String("date");
                TUint priority = parser.Num("pty");
                if (id.Bytes() > 0 && date.Bytes() > 0) {
                    // Value was found.
                    if (mapCount >= kMaxEntries) {
                        LOG(kMedia, "PodcastPins Loaded %u stored date mappings, but more values in store. Ignoring remaining values. iListenedDates:\n%.*s\n", mapCount, PBUF(iListenedDates));
                        break;
                    }
                    else {
                        ListenedDatePooled* m = new ListenedDatePooled();
                        m->Set(id, date, priority);
                        iMappings.push_back(m);
                        mapCount++;
                    }
                }
            }
        }
        catch (JsonArrayEnumerationComplete&) {}
    }

    // If iMappings doesn't contain kMaxEntries from store, fill up with empty values
    while (iMappings.size() < kMaxEntries) {
        iMappings.push_back(new ListenedDatePooled());
    }

    iTimer = new Timer(aEnv, MakeFunctor(*this, &PodcastPins::TimerCallback), "PodcastPins");
    if (iListenedDates.Bytes() > 0) {
        StartPollingForNewEpisodes();
    }
}

PodcastPins::~PodcastPins()
{
    for (auto* m : iMappings) {
        delete m;
    }
    iMappings.clear();

    delete iITunes;
    delete iTimer;
    delete iInstance;
}

void PodcastPins::StartPollingForNewEpisodes()
{
    AutoMutex _(iLock);
    StartPollingForNewEpisodesLocked();
}

void PodcastPins::StartPollingForNewEpisodesLocked()
{
    iTimer->FireIn(50);
}

void PodcastPins::StopPollingForNewEpisodes()
{
    AutoMutex _(iLock);
    iTimer->Cancel();
}

void PodcastPins::TimerCallback()
{
    AutoMutex _(iLock);

    Brn prevEpList(iNewEpisodeList);
    iNewEpisodeList.ReplaceThrow(Brx::Empty());
    for (auto* m : iMappings) {
        if (m->Id().Bytes() > 0) {
            TBool newEpisode = CheckForNewEpisodeById(m->Id());
            if (newEpisode) {
                if (iNewEpisodeList.Bytes() > 0) {
                    iNewEpisodeList.TryAppend(",");
                }
                iNewEpisodeList.TryAppend(m->Id());
            }
        }
    }

    if (iNewEpisodeList != prevEpList) {
        LOG(kMedia, "PodcastPins New episode found for IDs: %.*s\n", PBUF(iNewEpisodeList));
        for (auto it=iEpisodeObservers.begin(); it!=iEpisodeObservers.end(); ++it) {
            // notify event that new episoide is available for given IDs
            (*it)->NewPodcastEpisodesAvailable(iNewEpisodeList);
        }
    }

    iTimer->FireIn(kTimerDurationMs);
}

TBool PodcastPins::LoadPodcastLatest(const Brx& aQuery, IPodcastTransportHandler& aHandler)
{
    return LoadByQuery(aQuery, aHandler, false);
}

TBool PodcastPins::LoadPodcastList(const Brx& aQuery, IPodcastTransportHandler& aHandler, TBool aShuffle)
{
    return LoadByQuery(aQuery, aHandler, aShuffle);
}

TBool PodcastPins::CheckForNewEpisode(const Brx& aQuery)
{
    AutoMutex _(iLock);
    Bwh inputBuf(64);

    try {
        if (aQuery.Bytes() == 0) {
            return false;
        }
        //search string to id
        else if (!IsValidId(aQuery)) {
            iJsonResponse.Reset();
            TBool success = iITunes->TryGetPodcastId(iJsonResponse, aQuery); // send request to iTunes
            if (!success) {
                return false;
            }
            inputBuf.ReplaceThrow(ITunesMetadata::FirstIdFromJson(iJsonResponse.Buffer())); // parse response from iTunes
            if (inputBuf.Bytes() == 0) {
                return false;
            }
        }
        else {
            inputBuf.ReplaceThrow(aQuery);
        }
        return CheckForNewEpisodeById(inputBuf);
    }   
    catch (Exception& ex) {
        LOG_ERROR(kMedia, "%s in PodcastPins::CheckForNewEpisode\n", ex.Message());
        return false;
    }
}

TBool PodcastPins::LoadByQuery(const Brx& aQuery, IPodcastTransportHandler& aHandler, TBool aShuffle)
{
    AutoMutex _(iLock);
    aHandler.Init(aShuffle);
    Bwh inputBuf(64);

    try {
        if (aQuery.Bytes() == 0) {
            return false;
        }
        //search string to id
        else if (!IsValidId(aQuery)) {
            iJsonResponse.Reset();
            TBool success = iITunes->TryGetPodcastId(iJsonResponse, aQuery); // send request to iTunes
            if (!success) {
                return false;
            }
            inputBuf.ReplaceThrow(ITunesMetadata::FirstIdFromJson(iJsonResponse.Buffer())); // parse response from iTunes
            if (inputBuf.Bytes() == 0) {
                return false;
            }
        }
        else {
            inputBuf.ReplaceThrow(aQuery);
        }
        LoadById(inputBuf, aHandler);
    }   
    catch (Exception& ex) {
        LOG_ERROR(kMedia, "%s in PodcastPins::LoadByQuery\n", ex.Message());
        return false;
    }

    return true;
}

TBool PodcastPins::LoadById(const Brx& aId, IPodcastTransportHandler& aHandler)
{
    ITunesMetadata im(iTrackFactory);
    JsonParser parser;
    TBool isPlayable = false;
    Parser xmlParser;
    Brn date;
    PodcastInfo* podcast = nullptr;

    // id to streamable url
    LOG(kMedia, "PodcastPins::LoadById: %.*s\n", PBUF(aId));
    try {
        iJsonResponse.Reset();
        TBool success = iITunes->TryGetPodcastById(iJsonResponse, aId);
        if (!success) {
            return false;
        }

        parser.Reset();
        parser.Parse(iJsonResponse.Buffer());
        if (parser.HasKey(Brn("resultCount"))) { 
            TUint results = parser.Num(Brn("resultCount"));
            if (results == 0) {
                return false;
            }
            auto parserItems = JsonParserArray::Create(parser.String(Brn("results")));
            podcast = new PodcastInfo(parserItems.NextObject(), aId);

            iXmlResponse.Reset();
            success = iITunes->TryGetPodcastEpisodeInfo(iXmlResponse, podcast->FeedUrl(), aHandler.SingleShot());
            if (!success) {
                return false;
            }
            xmlParser.Set(iXmlResponse.Buffer());

            while (!xmlParser.Finished()) {
                try {
                    Brn item = PodcastEpisode::GetNextXmlValueByTag(xmlParser, Brn("item"));

                    auto* track = im.GetNextEpisodeTrack(*podcast, item);
                    if (track != nullptr) {
                        aHandler.Load(*track);
                        track->RemoveRef();
                        isPlayable = true;
                        if (date.Bytes() == 0) {
                            date = Brn(im.GetNextEpisodePublishedDate(item));
                        }
                        if (aHandler.SingleShot()) {
                            break;
                        }
                    }
                }
                catch (ReaderError&) {
                    if (aHandler.SingleShot()) {
                        LOG_ERROR(kMedia, "PodcastPins::LoadById (ReaderError). Could not find a valid episode for latest - allocate a larger response block?\n");
                    }
                    break; 
                }
            }
        }
        if (isPlayable) {
            aHandler.Play();
            // store these so SetLastLoadedPodcastAsListened will work as expected
            iLastSelectedId.ReplaceThrow(aId);
            iLastSelectedDate.ReplaceThrow(date);
            // immediately save episode date as listened, meaning SetLastLoadedPodcastAsListened does not need to be called
            SetLastListenedEpisodeDateLocked(aId, date);
            // make sure episode polling is active (if not run on startup)
            StartPollingForNewEpisodesLocked();
        }
    }
    catch (Exception& ex) {
        LOG_ERROR(kMedia, "%s in PodcastPins::LoadById\n", ex.Message());
        if (podcast != nullptr) {
            delete podcast;
        }
        return false;
    }  
    if (podcast != nullptr) {
        delete podcast;
    }
    return true;
}

TBool PodcastPins::CheckForNewEpisodeById(const Brx& aId)
{
    ITunesMetadata im(iTrackFactory);
    JsonParser parser;
    Parser xmlParser;
    PodcastInfo* podcast = nullptr;

    // id to streamable url
    LOG(kMedia, "PodcastPins::CheckForNewEpisodeById: %.*s\n", PBUF(aId));
    try {
        iJsonResponse.Reset();
        TBool success = iITunes->TryGetPodcastById(iJsonResponse, aId);
        if (!success) {
            return false;
        }

        parser.Reset();
        parser.Parse(iJsonResponse.Buffer());
        if (parser.HasKey(Brn("resultCount"))) { 
            TUint results = parser.Num(Brn("resultCount"));
            if (results == 0) {
                return false;
            }
            auto parserItems = JsonParserArray::Create(parser.String(Brn("results")));
            podcast = new PodcastInfo(parserItems.NextObject(), aId);

            iXmlResponse.Reset();
            success = iITunes->TryGetPodcastEpisodeInfo(iXmlResponse, podcast->FeedUrl(), true); // get latest episode info only
            if (!success) {
                return false;
            }
            xmlParser.Set(iXmlResponse.Buffer());

            while (!xmlParser.Finished()) {
                try {
                    Brn item = PodcastEpisode::GetNextXmlValueByTag(xmlParser, Brn("item"));
                    Brn latestEpDate = Brn(im.GetNextEpisodePublishedDate(item));
                    Brn lastListenedEpDate = Brn(GetLastListenedEpisodeDateLocked(aId));
                    return (latestEpDate != lastListenedEpDate);
                    
                }
                catch (ReaderError&) {
                    LOG_ERROR(kMedia, "PodcastPins::CheckForNewEpisodeById (ReaderError). Could not find a valid episode for latest - allocate a larger response block?\n");
                    break; 
                }
            }
        }
    }
    catch (Exception& ex) {
        LOG_ERROR(kMedia, "%s in PodcastPins::CheckForNewEpisodeById\n", ex.Message());
        if (podcast != nullptr) {
            delete podcast;
        }
        return false;
    }  
    if (podcast != nullptr) {
        delete podcast;
    }
    return false;
}

const Brx& PodcastPins::GetLastListenedEpisodeDateLocked(const Brx& aId)
{
    for (auto* m : iMappings) {
        if (m->Id() == aId) {
            return m->Date();
        }
    }
    return Brx::Empty();
}

void PodcastPins::SetLastLoadedPodcastAsListened()
{
    AutoMutex _(iLock);
    SetLastListenedEpisodeDateLocked(iLastSelectedId, iLastSelectedDate);
}

void PodcastPins::SetLastListenedEpisodeDateLocked(const Brx& aId, const Brx& aDate)
{
    if (aId.Bytes() > 0 && aDate.Bytes() > 0) {
        // replace existing Id with new date and highest priority
        TBool found = false;
        TUint currPriority = 0;
        for (auto* m : iMappings) {
            if (m->Id() == aId) {
                currPriority = m->Priority(); // save current priority for adjusting others
                m->Set(aId, aDate, kMaxEntries);
                found = true;
                break;
            }
        }
        // Adjust other priorities: any mapping with a priority > currPriority should be decremented
        for (auto* m : iMappings) {
            if (m->Id() != aId) {
                if (m->Priority() > currPriority) {
                    m->DecPriority();
                }
            }
        }
        // if new entry, replace last entry of sorted list
        iMappings.sort(ListenedDatePooled::Compare);
        if (!found) {
            iMappings.back()->Set(aId, aDate, kMaxEntries);
        }
        // write mappings to store as json
        iListenedDates.SetBytes(0);
        OpenHome::WriterBuffer writerJson(iListenedDates);
        WriterJsonArray writer(writerJson);
        for (auto* m : iMappings) {
            if (m->Id().Bytes() > 0 && m->Date().Bytes() > 0) {
                WriterJsonObject dateWriter = writer.CreateObject();
                dateWriter.WriteString("id", m->Id());
                dateWriter.WriteString("date", m->Date());
                dateWriter.WriteInt("pty", m->Priority());
                dateWriter.WriteEnd();
            }
        }
        writer.WriteEnd();
        writerJson.WriteFlush();
        iStore.Write(kStoreKeyITunesPodcast, iListenedDates);
    }
}

TBool PodcastPins::IsValidId(const Brx& aRequest) {
    for (TUint i = 0; i<aRequest.Bytes(); i++) {
        if (!Ascii::IsDigit(aRequest[i])) {
            return false;
        }
    }
    return true;
}

void PodcastPins::AddNewPodcastEpisodesObserver(IPodcastPinsObserver& aObserver)
{
    AutoMutex _(iLock);
    iEpisodeObservers.push_back(&aObserver);
    // Notify new observer immediately with its initial values.
    aObserver.NewPodcastEpisodesAvailable(iNewEpisodeList);
}

namespace OpenHome {
    namespace Av {
    
    class ITunes2DidlTagMapping
    {
    public:
        ITunes2DidlTagMapping(const TChar* aITunesKey, const TChar* aDidlTag, const OpenHome::Brx& aNs)
            : iITunesKey(aITunesKey)
            , iDidlTag(aDidlTag)
            , iNs(aNs)
        {}
    public:
        OpenHome::Brn iITunesKey;
        OpenHome::Brn iDidlTag;
        OpenHome::Brn iNs;
    };
    
} // namespace Av
} // namespace OpenHome

const Brn ITunesMetadata::kNsDc("dc=\"http://purl.org/dc/elements/1.1/\"");
const Brn ITunesMetadata::kNsUpnp("upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\"");
const Brn ITunesMetadata::kNsOh("oh=\"http://www.openhome.org\"");
const Brn ITunesMetadata::kMediaTypePodcast("podcast");

ITunesMetadata::ITunesMetadata(Media::TrackFactory& aTrackFactory)
    : iTrackFactory(aTrackFactory)
{
}

Media::Track* ITunesMetadata::GetNextEpisodeTrack(PodcastInfo& aPodcast, const Brx& aXmlItem)
{
    try {
        ParseITunesMetadata(aPodcast, aXmlItem);
        return iTrackFactory.CreateTrack(iTrackUri, iMetaDataDidl);
    }
    catch (AssertionFailed&) {
        throw;
    }
    catch (Exception&) {
        LOG_ERROR(kMedia, "ITunesMetadata::GetNextEpisode failed to parse metadata - trackBytes=%u\n", iTrackUri.Bytes());
        if (iTrackUri.Bytes() > 0) {
            return iTrackFactory.CreateTrack(iTrackUri, Brx::Empty());
        }
        return nullptr;
    }
}

const Brx& ITunesMetadata::GetNextEpisodePublishedDate(const Brx& aXmlItem)
{
    try {
        PodcastEpisode* episode = new PodcastEpisode(aXmlItem);
        return episode->PublishedDate();
    }
    catch (AssertionFailed&) {
        throw;
    }
    catch (Exception&) {
        LOG_ERROR(kMedia, "ITunesMetadata::GetNextEpisodePublishedDate failed to find episode date\n");
        return Brx::Empty();
    }
}

Brn ITunesMetadata::FirstIdFromJson(const Brx& aJsonResponse)
{
    try {
        JsonParser parser;
        parser.Parse(aJsonResponse);
        if (parser.Num(Brn("resultCount")) == 0) {
            THROW(ITunesResponseInvalid);
        }
        auto parserArray = JsonParserArray::Create(parser.String("results"));
        if (parserArray.Type() == JsonParserArray::ValType::Null) {
            THROW(ITunesResponseInvalid);
        }
        parser.Parse(parserArray.NextObject());
        if (parser.HasKey(Brn("collectionId"))) {
            return parser.String(Brn("collectionId"));
        }
        else if (parser.HasKey(Brn("trackId"))) {
            return parser.String(Brn("trackId"));
        }
    }
    catch (AssertionFailed&) {
        throw;
    }
    catch (Exception&) {
        throw;
    }
    return Brx::Empty();
}

void ITunesMetadata::ParseITunesMetadata(PodcastInfo& aPodcast, const Brx& aXmlItem)
{
    iTrackUri.ReplaceThrow(Brx::Empty());
    iMetaDataDidl.ReplaceThrow(Brx::Empty());

    TryAppend("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    TryAppend("<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\">");
    TryAppend("<item id=\"");
    TryAppend(aPodcast.Id());;
    TryAppend("\" parentID=\"-1\" restricted=\"1\">");
    TryAppend(">");
    TryAddTag(Brn("upnp:artist"), kNsUpnp, Brx::Empty(), aPodcast.Artist());
    TryAddTag(Brn("upnp:album"), kNsUpnp, Brx::Empty(), aPodcast.Name());
    TryAddTag(Brn("upnp:albumArtURI"), kNsUpnp, Brx::Empty(), aPodcast.ArtworkUrl());
    TryAddTag(Brn("upnp:class"), kNsUpnp, Brx::Empty(), Brn("object.item.audioItem.musicTrack"));
    PodcastEpisode* episode = new PodcastEpisode(aXmlItem);  // get Episode Title, release date, duration, and streamable url
    LOG(kMedia, "Podcast Title: %.*s\n", PBUF(episode->Title()));
    LOG(kMedia, "    Published Date: %.*s\n", PBUF(episode->PublishedDate()));
    LOG(kMedia, "    Duration: %ds\n", episode->Duration());
    LOG(kMedia, "    Url: %.*s\n", PBUF(episode->Url()));
    iTrackUri.ReplaceThrow(episode->Url());
    TryAddTag(Brn("dc:title"), kNsDc, Brx::Empty(), episode->Title());
    TryAppend("<res");
    TryAddAttribute("http-get:*:*:*", "protocolInfo");
    if (episode->Duration() > 0) {
        TryAppend(" duration=\"");
        TUint duration = episode->Duration();
        const TUint secs = duration % 60;
        duration /= 60;
        const TUint mins = duration % 60;
        const TUint hours = duration / 60;
        Bws<32> formatted;
        formatted.AppendPrintf("%u:%02u:%02u.000", hours, mins, secs);
        TryAppend(formatted);
        TryAppend("\"");
    }
    
    TryAppend(">");
    if (iTrackUri.Bytes() > 0) {
        WriterBuffer writer(iMetaDataDidl);
        Converter::ToXmlEscaped(writer, iTrackUri);
    }
    TryAppend("</res>");
    TryAppend("</item>");
    TryAppend("</DIDL-Lite>");
    delete episode;
}

void ITunesMetadata::TryAddAttribute(JsonParser& aParser, const TChar* aITunesKey, const TChar* aDidlAttr)
{
    if (aParser.HasKey(aITunesKey)) {
        TryAppend(" ");
        TryAppend(aDidlAttr);
        TryAppend("=\"");
        TryAppend(aParser.String(aITunesKey));
        TryAppend("\"");
    }
}

void ITunesMetadata::TryAddAttribute(const TChar* aValue, const TChar* aDidlAttr)
{
    TryAppend(" ");
    TryAppend(aDidlAttr);
    TryAppend("=\"");
    TryAppend(aValue);
    TryAppend("\"");
}

void ITunesMetadata::TryAddTag(JsonParser& aParser, const Brx& aITunesKey,
                           const Brx& aDidlTag, const Brx& aNs)
{
    if (!aParser.HasKey(aITunesKey)) {
        return;
    }
    Brn val = aParser.String(aITunesKey);
    Bwn valEscaped(val.Ptr(), val.Bytes(), val.Bytes());
    Json::Unescape(valEscaped);
    TryAddTag(aDidlTag, aNs, Brx::Empty(), valEscaped);
}

void ITunesMetadata::TryAddTag(const Brx& aDidlTag, const Brx& aNs,
                           const Brx& aRole, const Brx& aValue)
{
    TryAppend("<");
    TryAppend(aDidlTag);
    TryAppend(" xmlns:");
    TryAppend(aNs);
    if (aRole.Bytes() > 0) {
        TryAppend(" role=\"");
        TryAppend(aRole);
        TryAppend("\"");
    }
    TryAppend(">");
    WriterBuffer writer(iMetaDataDidl);
    Converter::ToXmlEscaped(writer, aValue);
    TryAppend("</");
    TryAppend(aDidlTag);
    TryAppend(">");
}

void ITunesMetadata::TryAppend(const TChar* aStr)
{
    Brn buf(aStr);
    TryAppend(buf);
}

void ITunesMetadata::TryAppend(const Brx& aBuf)
{
    if (!iMetaDataDidl.TryAppend(aBuf)) {
        THROW(BufferOverflow);
    }
}

const Brn ITunes::kHost("itunes.apple.com");

ITunes::ITunes(Environment& aEnv)
    : iLock("ITUN")
    , iEnv(aEnv)
    , iReaderBuf(iSocket)
    , iReaderUntil(iReaderBuf)
    , iWriterBuf(iSocket)
    , iWriterRequest(iSocket)
    , iReaderResponse(aEnv, iReaderUntil)
{
    iReaderResponse.AddHeader(iHeaderContentLength);
}

ITunes::~ITunes()
{
}

TBool ITunes::TryGetPodcastId(WriterBwh& aWriter, const Brx& aQuery)
{
    Bws<kMaxPathAndQueryBytes> pathAndQuery("");

    pathAndQuery.TryAppend("/search?term=");
    Uri::Escape(pathAndQuery, aQuery);
    pathAndQuery.TryAppend("&media=");
    pathAndQuery.TryAppend(ITunesMetadata::kMediaTypePodcast);
    pathAndQuery.TryAppend("&entity=");
    pathAndQuery.TryAppend(ITunesMetadata::kMediaTypePodcast);

    TBool success = false;
    try {
        iSocket.Open(iEnv);
        success = TryGetJsonResponse(aWriter, pathAndQuery, 1); // only interested in one podcast collection at a time
        iSocket.Close();
    }
    catch (NetworkError&) {
    }
    return success;
}

TBool ITunes::TryGetPodcastById(WriterBwh& aWriter, const Brx& aId)
{
    Bws<kMaxPathAndQueryBytes> pathAndQuery("");

    pathAndQuery.TryAppend("/lookup?id=");
    Uri::Escape(pathAndQuery, aId);
    pathAndQuery.TryAppend("&media=");
    pathAndQuery.TryAppend(ITunesMetadata::kMediaTypePodcast);
    pathAndQuery.TryAppend("&entity=");
    pathAndQuery.TryAppend(ITunesMetadata::kMediaTypePodcast);

    TBool success = false;
    try {
        iSocket.Open(iEnv);
        success = TryGetJsonResponse(aWriter, pathAndQuery, 1); // only interested in one podcast collection at a time
        iSocket.Close();
    }
    catch (NetworkError&) {
    }
    return success;
}

TBool ITunes::TryGetPodcastEpisodeInfo(WriterBwh& aWriter, const Brx& aXmlFeedUrl, TBool aLatestOnly) {
    TBool success = false;
    TUint blocksToRead = kSingleEpisodesBlockSize;
    if (!aLatestOnly) {
        blocksToRead = kMultipleEpisodesBlockSize;
    }
    try {
        iSocket.Open(iEnv);
        // Get xml response using given feed url
        success = TryGetXmlResponse(aWriter, aXmlFeedUrl, blocksToRead);
        iSocket.Close();
    }
    catch (NetworkError&) {
    }
    return success;
}

TBool ITunes::TryGetXmlResponse(WriterBwh& aWriter, const Brx& aFeedUrl, TUint aBlocksToRead)
{
    AutoMutex _(iLock);
    TBool success = false;
    Uri xmlFeedUri(aFeedUrl);
    if (!TryConnect(xmlFeedUri.Host(), kPort)) {
        LOG_ERROR(kMedia, "ITunes::TryGetXmlResponse - connection failure\n");
        return false;
    }

    try {
        LOG(kMedia, "Write podcast feed request: %.*s\n", PBUF(aFeedUrl));
        WriteRequestHeaders(Http::kMethodGet, xmlFeedUri.Host(), xmlFeedUri.PathAndQuery(), kPort);

        iReaderResponse.Read();
        const TUint code = iReaderResponse.Status().Code();
        if (code != 200) {
            LOG_ERROR(kPipeline, "Http error - %d - in response to ITunes TryGetXmlResponse.  Some/all of response is:\n", code);
            Brn buf = iReaderUntil.Read(kReadBufferBytes);
            LOG_ERROR(kPipeline, "%.*s\n", PBUF(buf));
            THROW(ReaderError);
        }  
        
        TInt count = aBlocksToRead * kReadBufferBytes;
        TInt length = iHeaderContentLength.ContentLength();
        if (length > 0 && length < count) {
            count = length;
        }
        //LOG(kMedia, "Read ITunes::TryGetXmlResponse (%d): ", count);
        while(count > 0) {
            Brn buf = iReaderUntil.Read(kReadBufferBytes);
            //LOG(kMedia, buf);
            aWriter.Write(buf);
            count -= buf.Bytes();
        }   
        //LOG(kMedia, "\n");     

        success = true;
    }
    catch (HttpError&) {
        LOG_ERROR(kPipeline, "HttpError in ITunesMetadata::TryGetResponse\n");
    }
    catch (ReaderError&) {
        if ( aWriter.Buffer().Bytes() > 0 ) {
            // lazy reading of xml has to account for this, particularly when there is no content length header and the length of the feed is less than our 'count'
            success = true;
        }
        else {
            LOG_ERROR(kPipeline, "ReaderError in ITunesMetadata::TryGetResponse\n");
        }    
    }
    catch (WriterError&) {
        LOG_ERROR(kPipeline, "WriterError in ITunesMetadata::TryGetResponse\n");
    }
    return success;
}

TBool ITunes::TryGetJsonResponse(WriterBwh& aWriter, Bwx& aPathAndQuery, TUint aLimit)
{
    AutoMutex _(iLock);
    TBool success = false;

    if (!TryConnect(kHost, kPort)) {
        LOG_ERROR(kMedia, "ITunes::TryGetResponse - connection failure\n");
        return false;
    }
    aPathAndQuery.TryAppend("&limit=");
    Ascii::AppendDec(aPathAndQuery, aLimit);

    try {
        LOG(kMedia, "Write ITunes request: http://%.*s%.*s\n", PBUF(kHost), PBUF(aPathAndQuery));
        WriteRequestHeaders(Http::kMethodGet, kHost, aPathAndQuery, kPort);

        iReaderResponse.Read();
        const TUint code = iReaderResponse.Status().Code();
        if (code != 200) {
            LOG_ERROR(kPipeline, "Http error - %d - in response to ITunes TryGetResponse.  Some/all of response is:\n", code);
            Brn buf = iReaderUntil.Read(kReadBufferBytes);
            LOG_ERROR(kPipeline, "%.*s\n", PBUF(buf));
            THROW(ReaderError);
        }  
        
        TUint count = iHeaderContentLength.ContentLength();
        //LOG(kMedia, "Read ITunes response (%d): ", count);
        while(count > 0) {
            Brn buf = iReaderUntil.Read(kReadBufferBytes);
            //LOG(kMedia, buf);
            aWriter.Write(buf);
            count -= buf.Bytes();
        }   
        //LOG(kMedia, "\n");     

        success = true;
    }
    catch (HttpError&) {
        LOG_ERROR(kPipeline, "HttpError in ITunes::TryGetResponse\n");
    }
    catch (ReaderError&) {
        LOG_ERROR(kPipeline, "ReaderError in ITunes::TryGetResponse\n");
    }
    catch (WriterError&) {
        LOG_ERROR(kPipeline, "WriterError in ITunes::TryGetResponse\n");
    }
    return success;
}

void ITunes::Interrupt(TBool aInterrupt)
{
    iSocket.Interrupt(aInterrupt);
}

TBool ITunes::TryConnect(const Brx& aHost, TUint aPort)
{
    Endpoint ep;
    try {
        ep.SetAddress(aHost);
        ep.SetPort(aPort);
        iSocket.Connect(ep, kConnectTimeoutMs);
    }
    catch (NetworkTimeout&) {
        return false;
    }
    catch (NetworkError&) {
        return false;
    }
    return true;
}

void ITunes::WriteRequestHeaders(const Brx& aMethod, const Brx& aHost, const Brx& aPathAndQuery, TUint aPort, TUint aContentLength)
{
    iWriterRequest.WriteMethod(aMethod, aPathAndQuery, Http::eHttp11);
    Http::WriteHeaderHostAndPort(iWriterRequest, aHost, aPort);
    if (aContentLength > 0) {
        Http::WriteHeaderContentLength(iWriterRequest, aContentLength);
    }
    Http::WriteHeaderContentType(iWriterRequest, Brn("application/x-www-form-urlencoded"));
    Http::WriteHeaderConnectionClose(iWriterRequest);
    iWriterRequest.WriteFlush();
}

PodcastInfo::PodcastInfo(const Brx& aJsonObj, const Brx& aId)
    : iName(512)
    , iFeedUrl(1024)
    , iArtist(256)
    , iArtworkUrl(1024)
    , iId(aId)
{
    Parse(aJsonObj);
}

void PodcastInfo::Parse(const Brx& aJsonObj)
{
    JsonParser parser;
    parser.Parse(aJsonObj);

    if (parser.HasKey("kind")) {
        if (parser.String("kind") != ITunesMetadata::kMediaTypePodcast) {
            THROW(ITunesResponseInvalid);
        }
    }
    if (!parser.HasKey("feedUrl")) {
        THROW(ITunesResponseInvalid);
    }

    try {
        iName.ReplaceThrow(parser.String("collectionName"));
    }
    catch (Exception&) {
        iName.ReplaceThrow(Brx::Empty());
    }

    try {
        iFeedUrl.ReplaceThrow(parser.String("feedUrl"));
    }
    catch (Exception&) {
        iFeedUrl.ReplaceThrow(Brx::Empty());
    }

    try {
        iArtist.ReplaceThrow(parser.String("artistName"));
    }
    catch (Exception&) {
        iArtist.ReplaceThrow(Brx::Empty());
    }

    try {
        iArtworkUrl.ReplaceThrow(parser.String("artworkUrl600"));
    }
    catch (Exception&) {
        iArtworkUrl.ReplaceThrow(Brx::Empty());
    }
}

PodcastInfo::~PodcastInfo()
{

}

const Brx& PodcastInfo::Name()
{
    return iName;
}

const Brx& PodcastInfo::FeedUrl()
{
    return iFeedUrl;
}

const Brx& PodcastInfo::Artist()
{
    return iArtist;
}

const Brx& PodcastInfo::ArtworkUrl()
{
    return iArtworkUrl;
}

const Brx& PodcastInfo::Id()
{
    return iId;
}

PodcastEpisode::PodcastEpisode(const Brx& aXmlItem)
    : iTitle(512)
    , iUrl(1024)
    , iPublishedDate(50)
    , iDuration(0)
{
    Parse(aXmlItem);
}

void PodcastEpisode::Parse(const Brx& aXmlItem)
{
    /*<item>
        <title>Podcast 103: Hard Man Ross Kemp, Shaun Ryder & Warwick Davies</title>
        <pubDate>Fri, 03 Nov 2017 00:00:00 GMT</pubDate>
        <enclosure url="http://fs.geronimo.thisisglobal.com/audio/efe086bfd3564d9e894ba7430c41543b.mp3?referredby=rss" type="audio/mpeg" length="124948886"/>
        <itunes:duration>1:26:45</itunes:duration>
    </item>*/
    Parser xmlParser;

    try {
        xmlParser.Set(aXmlItem);
        Brn title = Ascii::Trim(PodcastEpisode::GetNextXmlValueByTag(xmlParser, Brn("title")));
        iTitle.ReplaceThrow(title);
        Converter::FromXmlEscaped(iTitle);
    }
    catch (Exception&) {
        iTitle.ReplaceThrow(Brx::Empty());
    }
    
    try {
        xmlParser.Set(aXmlItem);
        Brn date = PodcastEpisode::GetNextXmlValueByTag(xmlParser, Brn("pubDate"));
        iPublishedDate.ReplaceThrow(date);
    }
    catch (Exception&) {
        iPublishedDate.ReplaceThrow(Brx::Empty());
    }

    try {
        xmlParser.Set(iPublishedDate);
        xmlParser.Next(',');
        Brn prettyDate = Ascii::Trim(xmlParser.Remaining()).Split(0, 11); // correct format is 'Thu, 07 Jun 2017'
        iTitle.TryAppend(Brn(" ("));
        iTitle.TryAppend(prettyDate);
        iTitle.TryAppend(Brn(")"));
    }
    catch (Exception&) {
        // leave title with no date
    }
    
    try {
        xmlParser.Set(aXmlItem);
        Brn duration = PodcastEpisode::GetNextXmlValueByTag(xmlParser, Brn("itunes:duration"));
        Parser durParser(duration);
        TUint count = 0;
        TUint times[3] = {0, 0, 0};
        while (!durParser.Finished()) {
            times[count] = Ascii::Uint(durParser.Next(':'));
            count++;
        }
        switch (count) {
            case 1: { iDuration = times[0]; break; }
            case 2: { iDuration = times[0]*60 + times[1]; break; }
            case 3: { iDuration = times[0]*3600 + times[1]*60 + times[2]; break; }
            default: { iDuration = 0; break; }
        }
    }
    catch (Exception&) {
        iDuration = 0;
    }
    
    try {
        xmlParser.Set(aXmlItem);
        Brn enclosure = PodcastEpisode::GetNextXmlValueByTag(xmlParser, Brn("enclosure"));
        Brn url = PodcastEpisode::GetFirstXmlAttribute(enclosure, Brn("url"));
        if (url.BeginsWith(Brn("https"))) {
            iUrl.ReplaceThrow(Brn("http"));
            iUrl.TryAppend(url.Split(5, url.Bytes()-5));
        }
        else if (url.BeginsWith(Brn("http"))) {
            iUrl.ReplaceThrow(url);
        }
        else {
            THROW(UriError);
        }
        Converter::FromXmlEscaped(iUrl);
    }
    catch (Exception& ex) {
        LOG(kMedia, "PodcastEpisode::Parse %s (Error retrieving podcast URL). Podcast is not playable\n", ex.Message());
        throw;
    }
}

PodcastEpisode::~PodcastEpisode()
{

}

const Brx& PodcastEpisode::Title()
{
    return iTitle;
}

const Brx& PodcastEpisode::Url()
{
    return iUrl;
}

const Brx& PodcastEpisode::PublishedDate()
{
    return iPublishedDate;
}

TUint PodcastEpisode::Duration()
{
    return iDuration;
}

Brn PodcastEpisode::GetFirstXmlAttribute(const Brx& aXml, const Brx& aAttribute)
{
    Parser parser;
    parser.Set(aXml);

    Brn buf;
    while (!parser.Finished()) {
        parser.Next(' ');
        if (parser.Next('=') == aAttribute) {
            parser.Next('"');
            return parser.Next('"');
        }
    }
    THROW(ReaderError);
}

Brn PodcastEpisode::GetNextXmlValueByTag(Parser& aParser, const Brx& aTag)
{
    Brn remaining = aParser.Remaining();
    TInt indexOffset = aParser.Index();

    Brn buf;
    TInt start = -1;
    TInt end = -1;
    TBool startFound = false;
    TBool endFound = false;
    while (!aParser.Finished()) {
        aParser.Next('<');
        start = aParser.Index();
        buf.Set(aParser.Next('>'));
        if (buf.BeginsWith(aTag)) {
            if (aParser.At(-2) == '/') {
                // tag with no true value, but info stored as attribute instead
                end = aParser.Index()-2;
                return remaining.Split(start-indexOffset, end-start);
            }
            else {
                start = aParser.Index();
                startFound = true;
                break;
            }
        }
    }
    if (startFound) {
        while (!aParser.Finished()) {
            aParser.Next('<');
            end = aParser.Index() - 1;
            buf.Set(aParser.Next('>'));
            Bwh endTag(aTag.Bytes()+1, aTag.Bytes()+1);
            endTag.ReplaceThrow(Brn("/"));
            endTag.TryAppend(aTag);
            if (buf.BeginsWith(endTag)) {
                endFound = true;
                break;
            }
        }

        if (endFound) {
            return remaining.Split(start-indexOffset, end-start);
        }
    }
    THROW(ReaderError);
}

// ListenedDatePooled

ListenedDatePooled::ListenedDatePooled()
    : iId(Brx::Empty())
    , iDate(Brx::Empty())
    , iPriority(0)
{
}

void ListenedDatePooled::Set(const Brx& aId, const Brx& aDate, TUint aPriority)
{
    iId.Replace(aId);
    iDate.Replace(aDate);
    iPriority = aPriority;
}

const Brx& ListenedDatePooled::Id() const
{
    return iId;
}

const Brx& ListenedDatePooled::Date() const
{
    return iDate;
}

const TUint ListenedDatePooled::Priority() const
{
    return iPriority;
}

void ListenedDatePooled::DecPriority()
{
    if (iPriority > 0) {
        iPriority--;
    }
}

TBool ListenedDatePooled::Compare(const ListenedDatePooled* aFirst, const ListenedDatePooled* aSecond)
{
    if (aFirst->Priority() == aSecond->Priority() &&
        aFirst->Date() == aSecond->Date() &&
        aFirst->Id() == aSecond->Id()) {
        return false;
    }
    return (aFirst->Priority() >= aSecond->Priority());
}
#include <OpenHome/Av/Pins/PodcastPinsTuneIn.h>
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
static const TChar* kPinModeTuneInEpisode = "tunein";
static const TChar* kPinModeTuneInList = "tuneinlist";

// Pin types
static const TChar* kPinTypePodcast = "podcast";

// Pin params
static const TChar* kPinKeyEpisodeId = "id";
static const TChar* kPinKeyPath = "path";

// Store values
static const Brn kStoreKeyTuneInPodcast("Pins.PodcastTuneIn");

const TUint kTimerDurationMs = (1000 * 60 * 60 * 1); // every hour
//const TUint kTimerDurationMs = 1000 * 60; // 1 min - TEST ONLY

// PodcastPinsLatestEpisodeTuneIn

PodcastPinsLatestEpisodeTuneIn::PodcastPinsLatestEpisodeTuneIn(Net::DvDeviceStandard& aDevice, Media::TrackFactory& aTrackFactory, Net::CpStack& aCpStack, Configuration::IStoreReadWrite& aStore)
{
    iPodcastPins = PodcastPinsTuneIn::GetInstance(aTrackFactory, aCpStack.Env(), aStore);

    CpDeviceDv* cpDevice = CpDeviceDv::New(aCpStack, aDevice);
    iCpRadio = new CpProxyAvOpenhomeOrgRadio1(*cpDevice);
    cpDevice->RemoveRef(); // iProxy will have claimed a reference to the device so no need for us to hang onto another
}

PodcastPinsLatestEpisodeTuneIn::~PodcastPinsLatestEpisodeTuneIn()
{
    delete iCpRadio;
}

void PodcastPinsLatestEpisodeTuneIn::Invoke(const IPin& aPin)
{
    PinUri pin(aPin);
    TBool res = false;
    if (Brn(pin.Mode()) == Brn(kPinModeTuneInEpisode)) {
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

const TChar* PodcastPinsLatestEpisodeTuneIn::Mode() const
{
    return kPinModeTuneInEpisode;
}

void PodcastPinsLatestEpisodeTuneIn::Init(TBool /*aShuffle*/)
{
    // Single shot so nothing to delete or shuffle
}

void PodcastPinsLatestEpisodeTuneIn::Load(Media::Track& aTrack)
{
    iCpRadio->SyncSetChannel(aTrack.Uri(), aTrack.MetaData());
}

void PodcastPinsLatestEpisodeTuneIn::Play()
{
    iCpRadio->SyncPlay();
}

TBool PodcastPinsLatestEpisodeTuneIn::SingleShot()
{
    return true;
}

// PodcastPinsEpisodeListTuneIn

PodcastPinsEpisodeListTuneIn::PodcastPinsEpisodeListTuneIn(Net::DvDeviceStandard& aDevice, Media::TrackFactory& aTrackFactory, Net::CpStack& aCpStack, Configuration::IStoreReadWrite& aStore)
    : iLastId(0)
{
    iPodcastPins = PodcastPinsTuneIn::GetInstance(aTrackFactory, aCpStack.Env(), aStore);

    CpDeviceDv* cpDevice = CpDeviceDv::New(aCpStack, aDevice);
    iCpPlaylist = new CpProxyAvOpenhomeOrgPlaylist1(*cpDevice);
    cpDevice->RemoveRef(); // iProxy will have claimed a reference to the device so no need for us to hang onto another
}

PodcastPinsEpisodeListTuneIn::~PodcastPinsEpisodeListTuneIn()
{
    delete iCpPlaylist;
}

void PodcastPinsEpisodeListTuneIn::Invoke(const IPin& aPin)
{
    PinUri pin(aPin);
    TBool res = false;
    if (Brn(pin.Mode()) == Brn(kPinModeTuneInList)) {
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

const TChar* PodcastPinsEpisodeListTuneIn::Mode() const
{
    return kPinModeTuneInList;
}

void PodcastPinsEpisodeListTuneIn::Init(TBool aShuffle)
{
    iCpPlaylist->SyncDeleteAll();
    iLastId = 0;
    iCpPlaylist->SyncSetShuffle(aShuffle);
}

void PodcastPinsEpisodeListTuneIn::Load(Media::Track& aTrack)
{
    TUint newId;
    iCpPlaylist->SyncInsert(iLastId, aTrack.Uri(), aTrack.MetaData(), newId);
    iLastId = newId;
}

void PodcastPinsEpisodeListTuneIn::Play()
{
    iCpPlaylist->SyncPlay();
}

TBool PodcastPinsEpisodeListTuneIn::SingleShot()
{
    return false;
}

// PodcastPinsTuneIn

PodcastPinsTuneIn* PodcastPinsTuneIn::iInstance = nullptr;

PodcastPinsTuneIn* PodcastPinsTuneIn::GetInstance(Media::TrackFactory& aTrackFactory, Environment& aEnv, Configuration::IStoreReadWrite& aStore)
{
    if (iInstance == nullptr) {
        iInstance = new PodcastPinsTuneIn(aTrackFactory, aEnv, aStore);
    }
    return iInstance;
}

PodcastPinsTuneIn::PodcastPinsTuneIn(Media::TrackFactory& aTrackFactory, Environment& aEnv, Configuration::IStoreReadWrite& aStore)
    : iLock("PPIN")
    , iJsonResponse(kJsonResponseChunks)
    , iXmlResponse(kXmlResponseChunks)
    , iTrackFactory(aTrackFactory)
    , iStore(aStore)
    , iListenedDates(kMaxEntryBytes*kMaxEntries)
{
    iTuneIn = new TuneIn(aEnv);

    // Don't push any mappings into iMappings yet.
    // Instead, start by populating from store. Then, if it is not full, fill up
    // to iMaxEntries bytes with inactive mappings.

    TUint mapCount = 0;
    iListenedDates.SetBytes(0);
    try {
        iStore.Read(kStoreKeyTuneInPodcast, iListenedDates);
        Log::Print("PodcastPinsTuneIn Load listened dates from store: %.*s\n", PBUF(iListenedDates));
    }
    catch (StoreKeyNotFound&) {
        // Key not in store, so no config stored yet and nothing to parse.
        Log::Print("Store Key not found: %.*s\n", PBUF(kStoreKeyTuneInPodcast));
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
                        LOG(kMedia, "PodcastPinsTuneIn Loaded %u stored date mappings, but more values in store. Ignoring remaining values. iListenedDates:\n%.*s\n", mapCount, PBUF(iListenedDates));
                        break;
                    }
                    else {
                        ListenedDatePooledTuneIn* m = new ListenedDatePooledTuneIn();
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
        iMappings.push_back(new ListenedDatePooledTuneIn());
    }

    iTimer = new Timer(aEnv, MakeFunctor(*this, &PodcastPinsTuneIn::TimerCallback), "PodcastPinsTuneIn");
    if (iListenedDates.Bytes() > 0) {
        StartPollingForNewEpisodes();
    }
}

PodcastPinsTuneIn::~PodcastPinsTuneIn()
{
    for (auto* m : iMappings) {
        delete m;
    }
    iMappings.clear();

    delete iTuneIn;
    delete iTimer;
    delete iInstance;
}

void PodcastPinsTuneIn::StartPollingForNewEpisodes()
{
    AutoMutex _(iLock);
    StartPollingForNewEpisodesLocked();
}

void PodcastPinsTuneIn::StartPollingForNewEpisodesLocked()
{
    iTimer->FireIn(50);
}

void PodcastPinsTuneIn::StopPollingForNewEpisodes()
{
    AutoMutex _(iLock);
    iTimer->Cancel();
}

void PodcastPinsTuneIn::TimerCallback()
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
        LOG(kMedia, "PodcastPinsTuneIn New episode found for IDs: %.*s\n", PBUF(iNewEpisodeList));
        for (auto it=iEpisodeObservers.begin(); it!=iEpisodeObservers.end(); ++it) {
            // notify event that new episoide is available for given IDs
            (*it)->NewPodcastEpisodesAvailable(iNewEpisodeList);
        }
    }

    iTimer->FireIn(kTimerDurationMs);
}

TBool PodcastPinsTuneIn::LoadPodcastLatest(const Brx& aQuery, IPodcastTransportHandler& aHandler)
{
    return LoadByQuery(aQuery, aHandler, false);
}

TBool PodcastPinsTuneIn::LoadPodcastList(const Brx& aQuery, IPodcastTransportHandler& aHandler, TBool aShuffle)
{
    return LoadByQuery(aQuery, aHandler, aShuffle);
}

TBool PodcastPinsTuneIn::CheckForNewEpisode(const Brx& aQuery)
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
            TBool success = iTuneIn->TryGetPodcastId(iJsonResponse, aQuery); // send request to TuneIn
            if (!success) {
                return false;
            }
            inputBuf.ReplaceThrow(TuneInMetadata::FirstIdFromJson(iJsonResponse.Buffer())); // parse response from TuneIn
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
        LOG_ERROR(kMedia, "%s in PodcastPinsTuneIn::CheckForNewEpisode\n", ex.Message());
        return false;
    }
}

TBool PodcastPinsTuneIn::LoadByQuery(const Brx& aQuery, IPodcastTransportHandler& aHandler, TBool aShuffle)
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
            TBool success = iTuneIn->TryGetPodcastId(iJsonResponse, aQuery); // send request to TuneIn
            if (!success) {
                return false;
            }
            inputBuf.ReplaceThrow(TuneInMetadata::FirstIdFromJson(iJsonResponse.Buffer())); // parse response from TuneIn
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
        LOG_ERROR(kMedia, "%s in PodcastPinsTuneIn::LoadByQuery\n", ex.Message());
        return false;
    }

    return true;
}

TBool PodcastPinsTuneIn::LoadById(const Brx& aId, IPodcastTransportHandler& aHandler)
{
    TuneInMetadata im(iTrackFactory);
    JsonParser parser;
    TBool isPlayable = false;
    Parser xmlParser;
    Brn date;
    PodcastInfoTuneIn* podcast = nullptr;

    // id to streamable url
    LOG(kMedia, "PodcastPinsTuneIn::LoadById: %.*s\n", PBUF(aId));
    try {
        iJsonResponse.Reset();
        TBool success = iTuneIn->TryGetPodcastById(iJsonResponse, aId);
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
            podcast = new PodcastInfoTuneIn(parserItems.NextObject(), aId);

            iXmlResponse.Reset();
            success = iTuneIn->TryGetPodcastEpisodeInfo(iXmlResponse, podcast->FeedUrl(), aHandler.SingleShot());
            if (!success) {
                return false;
            }
            xmlParser.Set(iXmlResponse.Buffer());

            while (!xmlParser.Finished()) {
                try {
                    Brn item = PodcastEpisodeTuneIn::GetNextXmlValueByTag(xmlParser, Brn("item"));

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
                        LOG_ERROR(kMedia, "PodcastPinsTuneIn::LoadById (ReaderError). Could not find a valid episode for latest - allocate a larger response block?\n");
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
        LOG_ERROR(kMedia, "%s in PodcastPinsTuneIn::LoadById\n", ex.Message());
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

TBool PodcastPinsTuneIn::CheckForNewEpisodeById(const Brx& aId)
{
    TuneInMetadata im(iTrackFactory);
    JsonParser parser;
    Parser xmlParser;
    PodcastInfoTuneIn* podcast = nullptr;

    // id to streamable url
    LOG(kMedia, "PodcastPinsTuneIn::CheckForNewEpisodeById: %.*s\n", PBUF(aId));
    try {
        iJsonResponse.Reset();
        TBool success = iTuneIn->TryGetPodcastById(iJsonResponse, aId);
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
            podcast = new PodcastInfoTuneIn(parserItems.NextObject(), aId);

            iXmlResponse.Reset();
            success = iTuneIn->TryGetPodcastEpisodeInfo(iXmlResponse, podcast->FeedUrl(), true); // get latest episode info only
            if (!success) {
                return false;
            }
            xmlParser.Set(iXmlResponse.Buffer());

            while (!xmlParser.Finished()) {
                try {
                    Brn item = PodcastEpisodeTuneIn::GetNextXmlValueByTag(xmlParser, Brn("item"));
                    Brn latestEpDate = Brn(im.GetNextEpisodePublishedDate(item));
                    Brn lastListenedEpDate = Brn(GetLastListenedEpisodeDateLocked(aId));
                    return (latestEpDate != lastListenedEpDate);
                    
                }
                catch (ReaderError&) {
                    LOG_ERROR(kMedia, "PodcastPinsTuneIn::CheckForNewEpisodeById (ReaderError). Could not find a valid episode for latest - allocate a larger response block?\n");
                    break; 
                }
            }
        }
    }
    catch (Exception& ex) {
        LOG_ERROR(kMedia, "%s in PodcastPinsTuneIn::CheckForNewEpisodeById\n", ex.Message());
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

const Brx& PodcastPinsTuneIn::GetLastListenedEpisodeDateLocked(const Brx& aId)
{
    for (auto* m : iMappings) {
        if (m->Id() == aId) {
            return m->Date();
        }
    }
    return Brx::Empty();
}

void PodcastPinsTuneIn::SetLastLoadedPodcastAsListened()
{
    AutoMutex _(iLock);
    SetLastListenedEpisodeDateLocked(iLastSelectedId, iLastSelectedDate);
}

void PodcastPinsTuneIn::SetLastListenedEpisodeDateLocked(const Brx& aId, const Brx& aDate)
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
        iMappings.sort(ListenedDatePooledTuneIn::Compare);
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
        iStore.Write(kStoreKeyTuneInPodcast, iListenedDates);
    }
}

TBool PodcastPinsTuneIn::IsValidId(const Brx& aRequest) {
    for (TUint i = 0; i<aRequest.Bytes(); i++) {
        if (!Ascii::IsDigit(aRequest[i])) {
            return false;
        }
    }
    return true;
}

void PodcastPinsTuneIn::AddNewPodcastEpisodesObserver(IPodcastPinsObserver& aObserver)
{
    AutoMutex _(iLock);
    iEpisodeObservers.push_back(&aObserver);
    // Notify new observer immediately with its initial values.
    aObserver.NewPodcastEpisodesAvailable(iNewEpisodeList);
}

namespace OpenHome {
    namespace Av {
    
    class TuneIn2DidlTagMapping
    {
    public:
        TuneIn2DidlTagMapping(const TChar* aTuneInKey, const TChar* aDidlTag, const OpenHome::Brx& aNs)
            : iTuneInKey(aTuneInKey)
            , iDidlTag(aDidlTag)
            , iNs(aNs)
        {}
    public:
        OpenHome::Brn iTuneInKey;
        OpenHome::Brn iDidlTag;
        OpenHome::Brn iNs;
    };
    
} // namespace Av
} // namespace OpenHome

const Brn TuneInMetadata::kNsDc("dc=\"http://purl.org/dc/elements/1.1/\"");
const Brn TuneInMetadata::kNsUpnp("upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\"");
const Brn TuneInMetadata::kNsOh("oh=\"http://www.openhome.org\"");
const Brn TuneInMetadata::kMediaTypePodcast("podcast");

TuneInMetadata::TuneInMetadata(Media::TrackFactory& aTrackFactory)
    : iTrackFactory(aTrackFactory)
{
}

Media::Track* TuneInMetadata::GetNextEpisodeTrack(PodcastInfoTuneIn& aPodcast, const Brx& aXmlItem)
{
    try {
        ParseTuneInMetadata(aPodcast, aXmlItem);
        return iTrackFactory.CreateTrack(iTrackUri, iMetaDataDidl);
    }
    catch (AssertionFailed&) {
        throw;
    }
    catch (Exception&) {
        LOG_ERROR(kMedia, "TuneInMetadata::GetNextEpisode failed to parse metadata - trackBytes=%u\n", iTrackUri.Bytes());
        if (iTrackUri.Bytes() > 0) {
            return iTrackFactory.CreateTrack(iTrackUri, Brx::Empty());
        }
        return nullptr;
    }
}

const Brx& TuneInMetadata::GetNextEpisodePublishedDate(const Brx& aXmlItem)
{
    try {
        PodcastEpisodeTuneIn* episode = new PodcastEpisodeTuneIn(aXmlItem);
        return episode->PublishedDate();
    }
    catch (AssertionFailed&) {
        throw;
    }
    catch (Exception&) {
        LOG_ERROR(kMedia, "TuneInMetadata::GetNextEpisodePublishedDate failed to find episode date\n");
        return Brx::Empty();
    }
}

Brn TuneInMetadata::FirstIdFromJson(const Brx& aJsonResponse)
{
    try {
        JsonParser parser;
        parser.Parse(aJsonResponse);
        if (parser.Num(Brn("resultCount")) == 0) {
            THROW(TuneInResponseInvalid);
        }
        auto parserArray = JsonParserArray::Create(parser.String("results"));
        if (parserArray.Type() == JsonParserArray::ValType::Null) {
            THROW(TuneInResponseInvalid);
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

void TuneInMetadata::ParseTuneInMetadata(PodcastInfoTuneIn& aPodcast, const Brx& aXmlItem)
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
    PodcastEpisodeTuneIn* episode = new PodcastEpisodeTuneIn(aXmlItem);  // get Episode Title, release date, duration, and streamable url
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

void TuneInMetadata::TryAddAttribute(JsonParser& aParser, const TChar* aTuneInKey, const TChar* aDidlAttr)
{
    if (aParser.HasKey(aTuneInKey)) {
        TryAppend(" ");
        TryAppend(aDidlAttr);
        TryAppend("=\"");
        TryAppend(aParser.String(aTuneInKey));
        TryAppend("\"");
    }
}

void TuneInMetadata::TryAddAttribute(const TChar* aValue, const TChar* aDidlAttr)
{
    TryAppend(" ");
    TryAppend(aDidlAttr);
    TryAppend("=\"");
    TryAppend(aValue);
    TryAppend("\"");
}

void TuneInMetadata::TryAddTag(JsonParser& aParser, const Brx& aTuneInKey,
                           const Brx& aDidlTag, const Brx& aNs)
{
    if (!aParser.HasKey(aTuneInKey)) {
        return;
    }
    Brn val = aParser.String(aTuneInKey);
    Bwn valEscaped(val.Ptr(), val.Bytes(), val.Bytes());
    Json::Unescape(valEscaped);
    TryAddTag(aDidlTag, aNs, Brx::Empty(), valEscaped);
}

void TuneInMetadata::TryAddTag(const Brx& aDidlTag, const Brx& aNs,
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

void TuneInMetadata::TryAppend(const TChar* aStr)
{
    Brn buf(aStr);
    TryAppend(buf);
}

void TuneInMetadata::TryAppend(const Brx& aBuf)
{
    if (!iMetaDataDidl.TryAppend(aBuf)) {
        THROW(BufferOverflow);
    }
}

const Brn TuneIn::kHost("opml.radiotime.com");

TuneIn::TuneIn(Environment& aEnv)
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

TuneIn::~TuneIn()
{
}

TBool TuneIn::TryGetPodcastId(WriterBwh& aWriter, const Brx& aQuery)
{
    Bws<kMaxPathAndQueryBytes> pathAndQuery("");

    pathAndQuery.TryAppend("/search?term=");
    Uri::Escape(pathAndQuery, aQuery);
    pathAndQuery.TryAppend("&media=");
    pathAndQuery.TryAppend(TuneInMetadata::kMediaTypePodcast);
    pathAndQuery.TryAppend("&entity=");
    pathAndQuery.TryAppend(TuneInMetadata::kMediaTypePodcast);

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

TBool TuneIn::TryGetPodcastById(WriterBwh& aWriter, const Brx& aId)
{
    Bws<kMaxPathAndQueryBytes> pathAndQuery("");

    pathAndQuery.TryAppend("/lookup?id=");
    Uri::Escape(pathAndQuery, aId);
    pathAndQuery.TryAppend("&media=");
    pathAndQuery.TryAppend(TuneInMetadata::kMediaTypePodcast);
    pathAndQuery.TryAppend("&entity=");
    pathAndQuery.TryAppend(TuneInMetadata::kMediaTypePodcast);

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

TBool TuneIn::TryGetPodcastEpisodeInfo(WriterBwh& aWriter, const Brx& aXmlFeedUrl, TBool aLatestOnly) {
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

TBool TuneIn::TryGetXmlResponse(WriterBwh& aWriter, const Brx& aFeedUrl, TUint aBlocksToRead)
{
    AutoMutex _(iLock);
    TBool success = false;
    Uri xmlFeedUri(aFeedUrl);
    if (!TryConnect(xmlFeedUri.Host(), kPort)) {
        LOG_ERROR(kMedia, "TuneIn::TryGetXmlResponse - connection failure\n");
        return false;
    }

    try {
        LOG(kMedia, "Write podcast feed request: %.*s\n", PBUF(aFeedUrl));
        WriteRequestHeaders(Http::kMethodGet, xmlFeedUri.Host(), xmlFeedUri.PathAndQuery(), kPort);

        iReaderResponse.Read();
        const TUint code = iReaderResponse.Status().Code();
        if (code != 200) {
            LOG_ERROR(kPipeline, "Http error - %d - in response to TuneIn TryGetXmlResponse.  Some/all of response is:\n", code);
            Brn buf = iReaderUntil.Read(kReadBufferBytes);
            LOG_ERROR(kPipeline, "%.*s\n", PBUF(buf));
            THROW(ReaderError);
        }  
        
        TInt count = aBlocksToRead * kReadBufferBytes;
        TInt length = iHeaderContentLength.ContentLength();
        if (length > 0 && length < count) {
            count = length;
        }
        //LOG(kMedia, "Read TuneIn::TryGetXmlResponse (%d): ", count);
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
        LOG_ERROR(kPipeline, "HttpError in TuneInMetadata::TryGetResponse\n");
    }
    catch (ReaderError&) {
        if ( aWriter.Buffer().Bytes() > 0 ) {
            // lazy reading of xml has to account for this, particularly when there is no content length header and the length of the feed is less than our 'count'
            success = true;
        }
        else {
            LOG_ERROR(kPipeline, "ReaderError in TuneInMetadata::TryGetResponse\n");
        }    
    }
    catch (WriterError&) {
        LOG_ERROR(kPipeline, "WriterError in TuneInMetadata::TryGetResponse\n");
    }
    return success;
}

TBool TuneIn::TryGetJsonResponse(WriterBwh& aWriter, Bwx& aPathAndQuery, TUint aLimit)
{
    AutoMutex _(iLock);
    TBool success = false;

    if (!TryConnect(kHost, kPort)) {
        LOG_ERROR(kMedia, "TuneIn::TryGetResponse - connection failure\n");
        return false;
    }
    aPathAndQuery.TryAppend("&limit=");
    Ascii::AppendDec(aPathAndQuery, aLimit);

    try {
        LOG(kMedia, "Write TuneIn request: http://%.*s%.*s\n", PBUF(kHost), PBUF(aPathAndQuery));
        WriteRequestHeaders(Http::kMethodGet, kHost, aPathAndQuery, kPort);

        iReaderResponse.Read();
        const TUint code = iReaderResponse.Status().Code();
        if (code != 200) {
            LOG_ERROR(kPipeline, "Http error - %d - in response to TuneIn TryGetResponse.  Some/all of response is:\n", code);
            Brn buf = iReaderUntil.Read(kReadBufferBytes);
            LOG_ERROR(kPipeline, "%.*s\n", PBUF(buf));
            THROW(ReaderError);
        }  
        
        TUint count = iHeaderContentLength.ContentLength();
        //LOG(kMedia, "Read TuneIn response (%d): ", count);
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
        LOG_ERROR(kPipeline, "HttpError in TuneIn::TryGetResponse\n");
    }
    catch (ReaderError&) {
        LOG_ERROR(kPipeline, "ReaderError in TuneIn::TryGetResponse\n");
    }
    catch (WriterError&) {
        LOG_ERROR(kPipeline, "WriterError in TuneIn::TryGetResponse\n");
    }
    return success;
}

void TuneIn::Interrupt(TBool aInterrupt)
{
    iSocket.Interrupt(aInterrupt);
}

TBool TuneIn::TryConnect(const Brx& aHost, TUint aPort)
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

void TuneIn::WriteRequestHeaders(const Brx& aMethod, const Brx& aHost, const Brx& aPathAndQuery, TUint aPort, TUint aContentLength)
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

PodcastInfoTuneIn::PodcastInfoTuneIn(const Brx& aJsonObj, const Brx& aId)
    : iName(512)
    , iFeedUrl(1024)
    , iArtist(256)
    , iArtworkUrl(1024)
    , iId(aId)
{
    Parse(aJsonObj);
}

void PodcastInfoTuneIn::Parse(const Brx& aJsonObj)
{
    JsonParser parser;
    parser.Parse(aJsonObj);

    if (parser.HasKey("kind")) {
        if (parser.String("kind") != TuneInMetadata::kMediaTypePodcast) {
            THROW(TuneInResponseInvalid);
        }
    }
    if (!parser.HasKey("feedUrl")) {
        THROW(TuneInResponseInvalid);
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

PodcastInfoTuneIn::~PodcastInfoTuneIn()
{

}

const Brx& PodcastInfoTuneIn::Name()
{
    return iName;
}

const Brx& PodcastInfoTuneIn::FeedUrl()
{
    return iFeedUrl;
}

const Brx& PodcastInfoTuneIn::Artist()
{
    return iArtist;
}

const Brx& PodcastInfoTuneIn::ArtworkUrl()
{
    return iArtworkUrl;
}

const Brx& PodcastInfoTuneIn::Id()
{
    return iId;
}

PodcastEpisodeTuneIn::PodcastEpisodeTuneIn(const Brx& aXmlItem)
    : iTitle(512)
    , iUrl(1024)
    , iPublishedDate(50)
    , iDuration(0)
{
    Parse(aXmlItem);
}

void PodcastEpisodeTuneIn::Parse(const Brx& aXmlItem)
{
/*
<opml>
  <body>
    <outline text="Recent Episodes" key="topics">
        <outline type="audio" 
                 text="The Frank Skinner Show - Fringe Fun (1h, 11m)" 
                 URL="http://opml.radiotime.com/Tune.ashx?id=t123369693&sid=p244756&formats=mp3,aac,ogg,hls&partnerId=ah2rjr68&locale=enGB&username=edm22" 
                 guide_id="t123369693" 
                 stream_type="download" 
                 topic_duration="4283" 
                 subtext="Saturday Aug 4" 
                 item="topic" 
                 image="http://cdn-radiotime-logos.tunein.com/p244756q.png" 
                 current_track="Saturday Aug 4" 
                 now_playing_id="t123369693"
        />
    </outline>
  </body>
</opml>
*/
    Parser xmlParser;

    try {
        xmlParser.Set(aXmlItem);
        Brn title = Ascii::Trim(PodcastEpisodeTuneIn::GetNextXmlValueByTag(xmlParser, Brn("title")));
        iTitle.ReplaceThrow(title);
        Converter::FromXmlEscaped(iTitle);
    }
    catch (Exception&) {
        iTitle.ReplaceThrow(Brx::Empty());
    }
    
    try {
        xmlParser.Set(aXmlItem);
        Brn date = PodcastEpisodeTuneIn::GetNextXmlValueByTag(xmlParser, Brn("pubDate"));
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
        Brn duration = PodcastEpisodeTuneIn::GetNextXmlValueByTag(xmlParser, Brn("itunes:duration"));
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
        Brn enclosure = PodcastEpisodeTuneIn::GetNextXmlValueByTag(xmlParser, Brn("enclosure"));
        Brn url = PodcastEpisodeTuneIn::GetFirstXmlAttribute(enclosure, Brn("url"));
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
        LOG(kMedia, "PodcastEpisodeTuneIn::Parse %s (Error retrieving podcast URL). Podcast is not playable\n", ex.Message());
        throw;
    }
}

PodcastEpisodeTuneIn::~PodcastEpisodeTuneIn()
{

}

const Brx& PodcastEpisodeTuneIn::Title()
{
    return iTitle;
}

const Brx& PodcastEpisodeTuneIn::Url()
{
    return iUrl;
}

const Brx& PodcastEpisodeTuneIn::PublishedDate()
{
    return iPublishedDate;
}

TUint PodcastEpisodeTuneIn::Duration()
{
    return iDuration;
}

Brn PodcastEpisodeTuneIn::GetFirstXmlAttribute(const Brx& aXml, const Brx& aAttribute)
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

Brn PodcastEpisodeTuneIn::GetNextXmlValueByTag(Parser& aParser, const Brx& aTag)
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

// ListenedDatePooledTuneIn

ListenedDatePooledTuneIn::ListenedDatePooledTuneIn()
    : iId(Brx::Empty())
    , iDate(Brx::Empty())
    , iPriority(0)
{
}

void ListenedDatePooledTuneIn::Set(const Brx& aId, const Brx& aDate, TUint aPriority)
{
    iId.Replace(aId);
    iDate.Replace(aDate);
    iPriority = aPriority;
}

const Brx& ListenedDatePooledTuneIn::Id() const
{
    return iId;
}

const Brx& ListenedDatePooledTuneIn::Date() const
{
    return iDate;
}

const TUint ListenedDatePooledTuneIn::Priority() const
{
    return iPriority;
}

void ListenedDatePooledTuneIn::DecPriority()
{
    if (iPriority > 0) {
        iPriority--;
    }
}

TBool ListenedDatePooledTuneIn::Compare(const ListenedDatePooledTuneIn* aFirst, const ListenedDatePooledTuneIn* aSecond)
{
    if (aFirst->Priority() == aSecond->Priority() &&
        aFirst->Date() == aSecond->Date() &&
        aFirst->Id() == aSecond->Id()) {
        return false;
    }
    return (aFirst->Priority() >= aSecond->Priority());
}
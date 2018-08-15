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
#include <OpenHome/Av/Radio/TuneIn.h>
#include <OpenHome/ThreadPool.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Net;

// Pin modes
static const TChar* kPinModeTuneInList = "tuneinlist";

// Pin types
static const TChar* kPinTypePodcast = "podcast";

// Pin params
static const TChar* kPinKeyEpisodeId = "id";
static const TChar* kPinKeyPath = "path";

// Store values
static const Brn kStoreKeyTuneInPodcast("Pins.PodcastTuneIn");

const TUint kTimerDurationMs = (1000 * 60 * 60 * 12); // 12 hours
//const TUint kTimerDurationMs = 1000 * 60; // 1 min - TEST ONLY

// PodcastPinsLatestEpisodeTuneIn

PodcastPinsLatestEpisodeTuneIn::PodcastPinsLatestEpisodeTuneIn(Net::DvDeviceStandard& aDevice, Media::TrackFactory& aTrackFactory, Net::CpStack& aCpStack, Configuration::IStoreReadWrite& aStore, const Brx& aPartnerId)
{
    iPodcastPins = PodcastPinsTuneIn::GetInstance(aTrackFactory, aCpStack.Env(), aStore, aPartnerId);

    CpDeviceDv* cpDevice = CpDeviceDv::New(aCpStack, aDevice);
    iCpRadio = new CpProxyAvOpenhomeOrgRadio1(*cpDevice);
    cpDevice->RemoveRef(); // iProxy will have claimed a reference to the device so no need for us to hang onto another
}

PodcastPinsLatestEpisodeTuneIn::~PodcastPinsLatestEpisodeTuneIn()
{
    delete iCpRadio;
}

void PodcastPinsLatestEpisodeTuneIn::LoadPodcast(const IPin& aPin)
{
    TBool res = false;
    try {
        PinUri pinUri(aPin);
        if (Brn(pinUri.Type()) == Brn(kPinTypePodcast)) {
            Brn val;
            if (pinUri.TryGetValue(kPinKeyEpisodeId, val)) {
                res = iPodcastPins->LoadPodcastLatestById(val, *this);
            }
            else if (pinUri.TryGetValue(kPinKeyPath, val)) {
                res = iPodcastPins->LoadPodcastLatestByPath(val, *this);
            }
            else {
                THROW(PinUriMissingRequiredParameter);
            }
        }
        else {
            THROW(PinTypeNotSupported);
        }
    }
    catch (PinUriMissingRequiredParameter&) {
        LOG_ERROR(kPipeline, "PodcastPinsLatestEpisodeTuneIn::LoadPodcast - missing parameter in %.*s\n", PBUF(aPin.Uri()));
        throw;
    }

    if (!res) {
        THROW(PinInvokeError);
    }
}

void PodcastPinsLatestEpisodeTuneIn::Cancel()
{
    iPodcastPins->Cancel();
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

PodcastPinsEpisodeListTuneIn::PodcastPinsEpisodeListTuneIn(Net::DvDeviceStandard& aDevice, Media::TrackFactory& aTrackFactory, Net::CpStack& aCpStack, Configuration::IStoreReadWrite& aStore, IThreadPool& aThreadPool)
    : iLastId(0)
    , iPin(iPinIdProvider)
{
    iPodcastPins = PodcastPinsTuneIn::GetInstance(aTrackFactory, aCpStack.Env(), aStore, Brx::Empty());

    CpDeviceDv* cpDevice = CpDeviceDv::New(aCpStack, aDevice);
    iCpPlaylist = new CpProxyAvOpenhomeOrgPlaylist1(*cpDevice);
    cpDevice->RemoveRef(); // iProxy will have claimed a reference to the device so no need for us to hang onto another
    iThreadPoolHandle = aThreadPool.CreateHandle(MakeFunctor(*this, &PodcastPinsEpisodeListTuneIn::Invoke),
                                                 "TuneInListPins", ThreadPoolPriority::Medium);
}

PodcastPinsEpisodeListTuneIn::~PodcastPinsEpisodeListTuneIn()
{
    iThreadPoolHandle->Destroy();
    delete iCpPlaylist;
}

void PodcastPinsEpisodeListTuneIn::BeginInvoke(const IPin& aPin, Functor aCompleted)
{
    if (aPin.Mode() != Brn(kPinModeTuneInList)) {
        return;
    }
    AutoPinComplete completion(aCompleted);
    (void)iPin.TryUpdate(aPin.Mode(), aPin.Type(), aPin.Uri(), aPin.Title(),
                         aPin.Description(), aPin.ArtworkUri(), aPin.Shuffle());
    completion.Cancel();
    iCompleted = aCompleted;
    (void)iThreadPoolHandle->TrySchedule();
}

void PodcastPinsEpisodeListTuneIn::Invoke()
{
    AutoFunctor _(iCompleted);
    TBool res = false;
     try {
        PinUri pinUri(iPin);
        if (Brn(pinUri.Type()) == Brn(kPinTypePodcast)) {
            Brn val;
            if (pinUri.TryGetValue(kPinKeyEpisodeId, val)) {
                res = iPodcastPins->LoadPodcastListById(val, *this, iPin.Shuffle());
            }
            else if (pinUri.TryGetValue(kPinKeyPath, val)) {
                res = iPodcastPins->LoadPodcastListByPath(val, *this, iPin.Shuffle());
            }
            else {
                THROW(PinUriMissingRequiredParameter);
            }
        }
        else {
            THROW(PinTypeNotSupported);
        }
    }
    catch (PinUriMissingRequiredParameter&) {
        LOG_ERROR(kPipeline, "PodcastPinsEpisodeListTuneIn::Invoke - missing parameter in %.*s\n", PBUF(iPin.Uri()));
        throw;
    }

    if (!res) {
        THROW(PinInvokeError);
    }
}

void PodcastPinsEpisodeListTuneIn::Cancel()
{
    iPodcastPins->Cancel();
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
Brh PodcastPinsTuneIn::iPartnerId;

PodcastPinsTuneIn* PodcastPinsTuneIn::GetInstance(Media::TrackFactory& aTrackFactory, Environment& aEnv, Configuration::IStoreReadWrite& aStore, const Brx& aPartnerId)
{
    if (iInstance == nullptr) {
        iInstance = new PodcastPinsTuneIn(aTrackFactory, aEnv, aStore);
    }

    if (iPartnerId.Bytes() == 0 && aPartnerId.Bytes() > 0) {
        iPartnerId.Set(aPartnerId);
    }

    return iInstance;
}

const Brx& PodcastPinsTuneIn::GetPartnerId()
{
    return iPartnerId;
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

void PodcastPinsTuneIn::Cancel()
{
    iTuneIn->Interrupt(true);
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

TBool PodcastPinsTuneIn::LoadPodcastLatestById(const Brx& aId, IPodcastTransportHandler& aHandler)
{
    return LoadByPath(iTuneIn->GetPathFromId(aId), aHandler, false);
}

TBool PodcastPinsTuneIn::LoadPodcastLatestByPath(const Brx& aPath, IPodcastTransportHandler& aHandler)
{
    return LoadByPath(aPath, aHandler, false);
}

TBool PodcastPinsTuneIn::LoadPodcastListById(const Brx& aId, IPodcastTransportHandler& aHandler, TBool aShuffle)
{
    return LoadByPath(iTuneIn->GetPathFromId(aId), aHandler, aShuffle);
}

TBool PodcastPinsTuneIn::LoadPodcastListByPath(const Brx& aPath, IPodcastTransportHandler& aHandler, TBool aShuffle)
{
    return LoadByPath(aPath, aHandler, aShuffle);
}

TBool PodcastPinsTuneIn::CheckForNewEpisode(const Brx& aId)
{
    AutoMutex _(iLock);

    try {
        if (aId.Bytes() == 0) {
            return false;
        }
        return CheckForNewEpisodeById(aId);
    }   
    catch (Exception& ex) {
        LOG_ERROR(kMedia, "%s in PodcastPinsTuneIn::CheckForNewEpisode\n", ex.Message());
        return false;
    }
}

TBool PodcastPinsTuneIn::LoadByPath(const Brx& aPath, IPodcastTransportHandler& aHandler, TBool aShuffle)
{
    AutoMutex _(iLock);
    aHandler.Init(aShuffle);

    TuneInMetadata tm(iTrackFactory);
    TBool isPlayable = false;
    Parser xmlParser;
    Brn date;
    PodcastInfoTuneIn* podcast = nullptr;

    try {
        if (aPath.Bytes() == 0) {
            return false;
        }
        
        LOG(kMedia, "PodcastPinsTuneIn::LoadByPath: %.*s\n", PBUF(aPath));
        iXmlResponse.Reset();
        TBool success = iTuneIn->TryGetPodcastFromPath(iXmlResponse, aPath);
        if (!success) {
            return false;
        }

        podcast = new PodcastInfoTuneIn(aPath);

        try {
            xmlParser.Set(iXmlResponse.Buffer());
            Brn topLevelContainer = PodcastPins::GetNextXmlValueByTag(xmlParser, Brn("outline"));
            xmlParser.Set(topLevelContainer);
            while (!xmlParser.Finished()) {
                Brn item = PodcastPins::GetNextXmlValueByTag(xmlParser, Brn("outline"));
                Brn type = PodcastPins::GetFirstXmlAttribute(item, Brn("type"));
                if (type == TuneInMetadata::kMediaTypePodcast) {
                    auto* track = tm.GetNextEpisodeTrack(podcast->Id(), item, aHandler.SingleShot());
                    if (track != nullptr) {
                        aHandler.Load(*track);
                        track->RemoveRef();
                        isPlayable = true;
                        if (date.Bytes() == 0) {
                            date = Brn(tm.GetNextEpisodePublishedDate(item));
                        }
                        if (aHandler.SingleShot()) {
                            break;
                        }
                    }
                }
            }
        }
        catch (ReaderError&) {
            if (aHandler.SingleShot()) {
                LOG_ERROR(kMedia, "PodcastPinsTuneIn::LoadByPath (ReaderError). Could not find a valid episode for latest - allocate a larger response block?\n");
            }
        }
         if (isPlayable) {
            aHandler.Play();
            // store these so SetLastLoadedPodcastAsListened will work as expected
            iLastSelectedId.ReplaceThrow(podcast->Id());
            iLastSelectedDate.ReplaceThrow(date);
            // immediately save episode date as listened, meaning SetLastLoadedPodcastAsListened does not need to be called
            SetLastListenedEpisodeDateLocked(podcast->Id(), date);
            // make sure episode polling is active (if not run on startup)
            StartPollingForNewEpisodesLocked();
        }
    }   
    catch (Exception& ex) {
        LOG_ERROR(kMedia, "%s in PodcastPinsTuneIn::LoadByPath\n", ex.Message());
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
    TuneInMetadata tm(iTrackFactory);
    Parser xmlParser;

    try {
        LOG(kMedia, "PodcastPinsTuneIn::CheckForNewEpisodeById: %.*s\n", PBUF(aId));
        iXmlResponse.Reset();
        TBool success = iTuneIn->TryGetPodcastById(iXmlResponse, aId);
        if (!success) {
            return false;
        }

        try {
            xmlParser.Set(iXmlResponse.Buffer());
            Brn topLevelContainer = PodcastPins::GetNextXmlValueByTag(xmlParser, Brn("outline"));
            xmlParser.Set(topLevelContainer);
            while (!xmlParser.Finished()) {
                Brn item = PodcastPins::GetNextXmlValueByTag(xmlParser, Brn("outline"));
                Brn type = PodcastPins::GetFirstXmlAttribute(item, Brn("type"));
                if (type == TuneInMetadata::kMediaTypePodcast) {
                    Brn latestEpDate = Brn(tm.GetNextEpisodePublishedDate(item));
                    Brn lastListenedEpDate = Brn(GetLastListenedEpisodeDateLocked(aId));
                    return (latestEpDate != lastListenedEpDate);
                }
            }
        }
        catch (ReaderError&) {
            LOG_ERROR(kMedia, "PodcastPinsTuneIn::CheckForNewEpisodeById (ReaderError). Could not find a valid episode for latest - allocate a larger response block?\n");
        }
    }   
    catch (Exception& ex) {
        LOG_ERROR(kMedia, "%s in PodcastPinsTuneIn::CheckForNewEpisodeById\n", ex.Message());
        return false;
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
        iStore.Write(kStoreKeyTuneInPodcast, iListenedDates);
    }
}

void PodcastPinsTuneIn::AddNewPodcastEpisodesObserver(IPodcastPinsObserver& aObserver)
{
    AutoMutex _(iLock);
    iEpisodeObservers.push_back(&aObserver);
    // Notify new observer immediately with its initial values.
    aObserver.NewPodcastEpisodesAvailable(iNewEpisodeList);
}

const Brn TuneInMetadata::kNsDc("dc=\"http://purl.org/dc/elements/1.1/\"");
const Brn TuneInMetadata::kNsUpnp("upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\"");
const Brn TuneInMetadata::kNsOh("oh=\"http://www.openhome.org\"");
const Brn TuneInMetadata::kMediaTypePodcast("audio");

TuneInMetadata::TuneInMetadata(Media::TrackFactory& aTrackFactory)
    : iTrackFactory(aTrackFactory)
{
}

Media::Track* TuneInMetadata::GetNextEpisodeTrack(const Brx& aPodcastId, const Brx& aXmlItem, TBool aLatestOnly)
{
    try {
        ParseTuneInMetadata(aPodcastId, aXmlItem, aLatestOnly);
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

void TuneInMetadata::ParseTuneInMetadata(const Brx& aPodcastId, const Brx& aXmlItem, TBool aLatestOnly)
{
    iTrackUri.ReplaceThrow(Brx::Empty());
    iMetaDataDidl.ReplaceThrow(Brx::Empty());

    TryAppend("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    TryAppend("<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\">");
    TryAppend("<item id=\"");
    TryAppend(aPodcastId);
    TryAppend("\" parentID=\"-1\" restricted=\"1\">");
    TryAppend(">");
    PodcastEpisodeTuneIn* episode = new PodcastEpisodeTuneIn(aXmlItem);  // get Episode Title, release date, duration, artwork, and streamable url
    if (!aLatestOnly) {
        //TryAddTag(Brn("upnp:artist"), kNsUpnp, Brx::Empty(), aPodcast.Artist());
        TryAddTag(Brn("upnp:album"), kNsUpnp, Brx::Empty(), Brn("Podcast Collection")); // only relevant for podcast lists
    }
    TryAddTag(Brn("upnp:albumArtURI"), kNsUpnp, Brx::Empty(), episode->ArtworkUrl());
    TryAddTag(Brn("upnp:class"), kNsUpnp, Brx::Empty(), Brn("object.item.audioItem.musicTrack"));
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

void TuneInMetadata::TryAddAttribute(const TChar* aValue, const TChar* aDidlAttr)
{
    TryAppend(" ");
    TryAppend(aDidlAttr);
    TryAppend("=\"");
    TryAppend(aValue);
    TryAppend("\"");
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

TuneIn::TuneIn(Environment& aEnv)
    : iLock("ITUN")
    , iEnv(aEnv)
    , iReaderBuf(iSocket)
    , iReaderUntil(iReaderBuf)
    , iWriterBuf(iSocket)
    , iWriterRequest(iSocket)
    , iReaderResponse(aEnv, iReaderUntil)
    , iPath(1024)
{
    iReaderResponse.AddHeader(iHeaderContentLength);
}

TuneIn::~TuneIn()
{
}

const Brx& TuneIn::GetPathFromId(const Brx& aId)
{
    SetPathFromId(iPath, aId);
    return iPath;
}

void TuneIn::SetPathFromId(Bwx& aPath, const Brx& aId)
{
    aPath.Replace(Brx::Empty());
    aPath.Append(TuneInApi::kTuneInStationRequest);
    if (aId.Split(0, 1) == Brn("p")) {
        aPath.Append(TuneInApi::kTuneInPodcastBrowse);
    }
    aPath.Append(TuneInApi::kTuneInItemId);
    aPath.Append(aId);
    aPath.Append(TuneInApi::kFormats);
    aPath.Append(TuneInApi::kPartnerId);
    aPath.Append(PodcastPinsTuneIn::GetPartnerId());
}

TBool TuneIn::TryGetPodcastFromPath(IWriter& aWriter, const Brx& aPath)
{
    TBool success = false;
    try {
        iSocket.Open(iEnv);
        success = TryGetXmlResponse(aWriter, aPath, kMultipleEpisodesBlockSize); // tune in only has one response type with all episodes
        iSocket.Close();
    }
    catch (NetworkError&) {
    }
    return success;
}

TBool TuneIn::TryGetPodcastById(IWriter& aWriter, const Brx& aId)
{
    SetPathFromId(iPath, aId);
    return TryGetPodcastFromPath(aWriter, iPath);
}

TBool TuneIn::TryGetPodcastEpisodeInfoById(IWriter& aWriter, const Brx& aId) {
    TBool success = false;
    SetPathFromId(iPath, aId);
    try {
        iSocket.Open(iEnv);
        // Get xml response using given feed url
        success = TryGetXmlResponse(aWriter, iPath, kMultipleEpisodesBlockSize); // tune in only has one response type with all episodes
        iSocket.Close();
    }
    catch (NetworkError&) {
    }
    return success;
}

TBool TuneIn::TryGetXmlResponse(IWriter& aWriter, const Brx& aFeedUrl, TUint aBlocksToRead)
{
    AutoMutex _(iLock);
    TBool success = false;

    try {
        Bwh uri(1024);
        Uri::Unescape(uri, aFeedUrl);
        Uri xmlFeedUri(uri);
        if (!TryConnect(xmlFeedUri.Host(), kPort)) {
            LOG_ERROR(kMedia, "TuneIn::TryGetXmlResponse - connection failure\n");
            return false;
        }

        LOG(kMedia, "Write podcast feed request: %.*s\n", PBUF(uri));
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
        //Log::Print("Read TuneIn::TryGetXmlResponse (%d): ", count);
        while(count > 0) {
            Brn buf = iReaderUntil.Read(kReadBufferBytes);
            //Log::Print(buf);
            aWriter.Write(buf);
            count -= buf.Bytes();
        }   
        //Log::Print("\n");     

        success = true;
    }
    catch (HttpError&) {
        LOG_ERROR(kPipeline, "HttpError in TuneInMetadata::TryGetResponse\n");
    }
    catch (ReaderError&) {
        if ( ((WriterBwh&)aWriter).Buffer().Bytes() > 0 ) {
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
    catch (Exception& ex) {
        LOG_ERROR(kPipeline, "%s in TuneIn::TryGetXmlResponse\n", ex.Message());
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

PodcastInfoTuneIn::PodcastInfoTuneIn(const Brx& aFeedUrl)
    : iFeedUrl(1024)
    , iId(32)
{
    Parse(aFeedUrl);
}

void PodcastInfoTuneIn::Parse(const Brx& aFeedUrl)
{
    // http://opml.radiotime.com/Tune.ashx?c=pbrowse&id=p244756...
    try {
        Uri::Unescape(iFeedUrl, aFeedUrl);

        Parser parser(iFeedUrl);
        while (!parser.Finished()) {
            Brn entry(parser.Next('&'));
            if (entry.Bytes() > 0) {
                OpenHome::Parser pe(entry);
                Brn key(pe.Next('='));
                Brn val(pe.Remaining());
                if (key == Brn("id")) {
                    iId.ReplaceThrow(val);
                    break;
                }
            }
        }
    }
    catch (Exception&) {
        THROW(TuneInRequestInvalid);
    }
}

PodcastInfoTuneIn::~PodcastInfoTuneIn()
{

}

const Brx& PodcastInfoTuneIn::FeedUrl()
{
    return iFeedUrl;
}

const Brx& PodcastInfoTuneIn::Id()
{
    return iId;
}

PodcastEpisodeTuneIn::PodcastEpisodeTuneIn(const Brx& aXmlItem)
    : iTitle(512)
    , iUrl(1024)
    , iArtworkUrl(1024)
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
    try {
        Brn type = PodcastPins::GetFirstXmlAttribute(aXmlItem, Brn("type"));
        if (type != TuneInMetadata::kMediaTypePodcast) {
            throw;
        }
    }
    catch (Exception&) {
        THROW(TuneInResponseInvalid);
    }

    try {
        Brn url = PodcastPins::GetFirstXmlAttribute(aXmlItem, Brn("URL"));
        iUrl.ReplaceThrow(url);
        Converter::FromXmlEscaped(iUrl);
    }
    catch (Exception& ex) {
        LOG(kMedia, "PodcastEpisodeTuneIn::Parse %s (Error retrieving podcast URL). Podcast is not playable\n", ex.Message());
        THROW(TuneInResponseInvalid);
    }

    try {
        Brn title = PodcastPins::GetFirstXmlAttribute(aXmlItem, Brn("text"));
        iTitle.ReplaceThrow(title);
        Converter::FromXmlEscaped(iTitle);
    }
    catch (Exception&) {
        iTitle.ReplaceThrow(Brx::Empty());
    }

    try {
        Brn art = PodcastPins::GetFirstXmlAttribute(aXmlItem, Brn("image"));
        iArtworkUrl.ReplaceThrow(art);
        Converter::FromXmlEscaped(iArtworkUrl);
    }
    catch (Exception&) {
        iArtworkUrl.ReplaceThrow(Brx::Empty());
    }

    try {
        Brn date = PodcastPins::GetFirstXmlAttribute(aXmlItem, Brn("current_track"));
        iPublishedDate.ReplaceThrow(date);
    }
    catch (Exception&) {
        iPublishedDate.ReplaceThrow(Brx::Empty());
    }

    try {
        iTitle.TryAppend(Brn(" ("));
        iTitle.TryAppend(iPublishedDate);
        iTitle.TryAppend(Brn(")"));
    }
    catch (Exception&) {
        // leave title with no date
    }
    
    try {
        Brn duration = PodcastPins::GetFirstXmlAttribute(aXmlItem, Brn("topic_duration")); // seconds
        iDuration = Ascii::Uint(duration);
    }
    catch (Exception&) {
        iDuration = 0;
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

const Brx& PodcastEpisodeTuneIn::ArtworkUrl()
{
    return iArtworkUrl;
}

const Brx& PodcastEpisodeTuneIn::PublishedDate()
{
    return iPublishedDate;
}

TUint PodcastEpisodeTuneIn::Duration()
{
    return iDuration;
}
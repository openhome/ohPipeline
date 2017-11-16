#include <OpenHome/Av/PodcastPins.h>
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

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Net;


PodcastPins::PodcastPins(DvDeviceStandard& aDevice, Media::TrackFactory& aTrackFactory, CpStack& aCpStack)
    : iLock("PPIN")
    , iJsonResponse(kJsonResponseChunks)
    , iXmlResponse(kXmlResponseChunks)
    , iTrackFactory(aTrackFactory)
    , iCpStack(aCpStack)
{
    CpDeviceDv* cpDevice = CpDeviceDv::New(iCpStack, aDevice);
    iCpRadio = new CpProxyAvOpenhomeOrgRadio1(*cpDevice);
    iCpPlaylist = new CpProxyAvOpenhomeOrgPlaylist1(*cpDevice);
    cpDevice->RemoveRef(); // iProxy will have claimed a reference to the device so no need for us to hang onto another

    iITunes = new ITunes(iCpStack.Env());
}

PodcastPins::~PodcastPins()
{
    delete iCpRadio;
    delete iCpPlaylist;
    delete iITunes;
}

TBool PodcastPins::LoadPodcastLatest(const Brx& aQuery)
{
    return LoadByQuery(aQuery, true);
}

TBool PodcastPins::LoadPodcastList(const Brx& aQuery)
{
    return LoadByQuery(aQuery, false);
}

TBool PodcastPins::LoadByQuery(const Brx& aQuery, TBool aLatestOnly)
{
    AutoMutex _(iLock);
    JsonParser parser;
    if (!aLatestOnly) {
        iCpPlaylist->SyncDeleteAll();
    }
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
        LoadById(inputBuf, aLatestOnly);
    }   
    catch (Exception& ex) {
        LOG_ERROR(kMedia, "%s in PodcastPins::LoadByQuery\n", ex.Message());
        return false;
    }

    return true;
}

TBool PodcastPins::LoadById(const Brx& aId, TBool aLatestOnly)
{
    ITunesMetadata im(iTrackFactory);
    JsonParser parser;
    TUint newId = 0;
    TUint currId = 0;
    TBool isPlayable = false;
    Parser xmlParser;
    Brn remaining;
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
            success = iITunes->TryGetPodcastEpisodeInfo(iXmlResponse, podcast->FeedUrl(), aLatestOnly);
            if (!success) {
                return false;
            }
            xmlParser.Set(iXmlResponse.Buffer());

            while (!xmlParser.Finished()) {
                try {
                    Brn item = PodcastEpisode::GetNextXmlValueByTag(xmlParser, Brn("item"));

                    auto* track = im.GetNextEpisode(*podcast, item);
                    if (track != nullptr) {
                        if (aLatestOnly) {
                            iCpRadio->SyncSetChannel((*track).Uri(), (*track).MetaData());
                            track->RemoveRef();
                            isPlayable = true;
                            break;
                        }
                        else {
                            iCpPlaylist->SyncInsert(currId, (*track).Uri(), (*track).MetaData(), newId);
                            LOG(kMedia, "Load playlist track - new ID is %d\n", newId);
                            track->RemoveRef();
                            currId = newId;
                            isPlayable = true;
                        }
                    }
                }
                catch (ReaderError&) {
                    if (aLatestOnly) {
                        LOG_ERROR(kMedia, "PodcastPins::LoadById (ReaderError). Could not find a valid episode for latest - allocate a larger response block?\n");
                    }
                    break; 
                }
            }
        }
        if (isPlayable) {
            if (aLatestOnly) {
                iCpRadio->SyncPlay();
            }
            else {
                iCpPlaylist->SyncPlay();
            }
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

TBool PodcastPins::IsValidId(const Brx& aRequest) {
    for (TUint i = 0; i<aRequest.Bytes(); i++) {
        if (!Ascii::IsDigit(aRequest[i])) {
            return false;
        }
    }
    return true;
}

TBool PodcastPins::Test(const Brx& aType, const Brx& aInput, IWriterAscii& aWriter)
{
    if (aType == Brn("help")) {
        aWriter.Write(Brn("podcastpin_latest (input: iTunes podcast ID or search string)"));
        aWriter.Write(Brn(" "));
        aWriter.WriteNewline(); // can't get this to work
        aWriter.Write(Brn("podcastpin_list (input: iTunes podcast ID or search string)"));
        aWriter.Write(Brn(" "));
        aWriter.WriteNewline(); // can't get this to work
        return true;
    }
    else if (aType == Brn("podcastpin_latest")) {
        aWriter.Write(Brn("Complete"));
        return LoadPodcastLatest(aInput);
    }
    else if (aType == Brn("podcastpin_list")) {
        aWriter.Write(Brn("Complete"));
        return LoadPodcastList(aInput);
    }
    return false;
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

Media::Track* ITunesMetadata::GetNextEpisode(PodcastInfo& aPodcast, const Brx& aXmlItem)
{
    try {
        ParseITunesMetadata(aPodcast, aXmlItem);
        return iTrackFactory.CreateTrack(iTrackUri, iMetaDataDidl);
    }
    catch (AssertionFailed&) {
        throw;
    }
    catch (Exception&) {
        LOG_ERROR(kMedia, "ITunesMetadata::TrackFromJson failed to parse metadata - trackBytes=%u\n", iTrackUri.Bytes());
        if (iTrackUri.Bytes() > 0) {
            return iTrackFactory.CreateTrack(iTrackUri, Brx::Empty());
        }
        return nullptr;
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

    pathAndQuery.Append("/search?term=");
    Uri::Escape(pathAndQuery, aQuery);
    pathAndQuery.Append("&media=");
    pathAndQuery.Append(ITunesMetadata::kMediaTypePodcast);
    pathAndQuery.Append("&entity=");
    pathAndQuery.Append(ITunesMetadata::kMediaTypePodcast);

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

    pathAndQuery.Append("/lookup?id=");
    Uri::Escape(pathAndQuery, aId);
    pathAndQuery.Append("&media=");
    pathAndQuery.Append(ITunesMetadata::kMediaTypePodcast);
    pathAndQuery.Append("&entity=");
    pathAndQuery.Append(ITunesMetadata::kMediaTypePodcast);

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
        LOG_ERROR(kPipeline, "ReaderError in ITunesMetadata::TryGetResponse\n");
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
    aPathAndQuery.Append("&limit=");
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
    , iReleaseDate(50)
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
        iReleaseDate.ReplaceThrow(date.Split(0, 16));
        iTitle.AppendThrow(Brn(" ("));
        iTitle.AppendThrow(iReleaseDate);
        iTitle.AppendThrow(Brn(")"));
    }
    catch (Exception&) {
        iReleaseDate.ReplaceThrow(Brx::Empty());
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
            iUrl.AppendThrow(url.Split(5, url.Bytes()-5));
        }
        else {
            iUrl.ReplaceThrow(url);
        }
        Converter::FromXmlEscaped(iUrl);
    }
    catch (Exception& ex) {
        LOG(kMedia, "PodcastEpisode::Parse %s (Error retrieving podcast URL). Podcast is not playable\n", ex.Message());
        throw;
    }

    LOG(kMedia, "Podcast Title: %.*s\n", PBUF(iTitle));
    LOG(kMedia, "    Release Date: %.*s\n", PBUF(iReleaseDate));
    LOG(kMedia, "    Duration: %ds\n", iDuration);
    LOG(kMedia, "    Url: %.*s\n", PBUF(iUrl));
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

const Brx& PodcastEpisode::ReleaseDate()
{
    return iReleaseDate;
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
            endTag.Append(aTag);
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
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
            TBool success = iITunes->TryGetId(iJsonResponse, aQuery); // send request to iTunes
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
    ITunesMetadata im(*iITunes, iTrackFactory);
    JsonParser parser;
    TBool isPlayable = false;

    // id to streamable url
    Log::Print("PodcastPins::LoadById: %.*s\n", PBUF(aId));
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

            try {
                auto* track = im.TrackFromJson(parserItems.NextObject());
                if (track != nullptr) {
                    if (aLatestOnly) {
                        iCpRadio->SyncSetChannel((*track).Uri(), (*track).MetaData());
                    }
                    else {
                        //iCpPlaylist-> // id crap
                    }
                    track->RemoveRef();
                    isPlayable = true;
                }
            }
            catch (JsonArrayEnumerationComplete&) {}
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
        return false;
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

ITunesMetadata::ITunesMetadata(ITunes& aITunes, Media::TrackFactory& aTrackFactory)
    : iTunes(aITunes)
    , iTrackFactory(aTrackFactory)
    , iXmlResponse(kXmlResponseChunks)
{
}

Media::Track* ITunesMetadata::TrackFromJson(const Brx& aMetadata)
{
    try {
        ParseITunesMetadata(aMetadata);
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

void ITunesMetadata::ParseITunesMetadata(const Brx& aMetadata)
{
    static const ITunes2DidlTagMapping kITunes2Didl[] ={
        { "artistName", "upnp:artist", kNsUpnp },
        { "collectionName", "upnp:album", kNsUpnp },
        { "artworkUrl600", "upnp:albumArtURI", kNsUpnp, },
        { "artworkUrl100", "upnp:albumArtURI", kNsUpnp, },
        { "artworkUrl60", "upnp:albumArtURI", kNsUpnp, },
    };
    static const TUint kNumITunes2DidlMappings = sizeof kITunes2Didl / sizeof kITunes2Didl[0];

    iTrackUri.ReplaceThrow(Brx::Empty());
    iMetaDataDidl.ReplaceThrow(Brx::Empty());
    JsonParser parser;
    parser.Parse(aMetadata);

    if (parser.HasKey("kind")) {
        if (parser.String("kind") != kMediaTypePodcast) {
            THROW(ITunesResponseInvalid);
        }
    }
    if (!parser.HasKey("feedUrl")) {
        THROW(ITunesResponseInvalid);
    }

    TryAppend("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    TryAppend("<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\">");
    TryAppend("<item");
    TryAddAttribute(parser, "id", "id");
    TryAppend(" parentID=\"-1\" restricted=\"1\">");
    TryAppend(">");
    for (TUint i=0; i<kNumITunes2DidlMappings; i++) {
        auto& mapping = kITunes2Didl[i];
        TryAddTag(parser, mapping.iITunesKey, mapping.iDidlTag, mapping.iNs);
    }
    TryAddTag(Brn("upnp:class"), kNsUpnp, Brx::Empty(), Brn("object.item.audioItem.musicTrack"));
    PodcastEpisode* episode = GetLatestEpisodeFromFeed(parser.String("feedUrl"));  // get Episode Title, release date, duration, and streamable url
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

PodcastEpisode* ITunesMetadata::GetLatestEpisodeFromFeed(const Brx& aFeedUrl) {
    TBool success = false;
    try {
        iTunes.iSocket.Open(iTunes.iEnv);
        // Get xml response using given feed url
        success = TryGetResponse(iXmlResponse, aFeedUrl, 1);
        iTunes.iSocket.Close();
    }
    catch (NetworkError&) {
    }

    if (success) {
        // Parse out 1st <item> tag in xml
        Brn item = ITunes::GetFirstValue(iXmlResponse.Buffer(), Brn("item"));

        // create PodcastEpisode based on first item (assumption made that first item will always be the latest)
        return new PodcastEpisode(item);
    }
    else {
        THROW(ITunesResponseInvalid);
    }
}

TBool ITunesMetadata::TryGetResponse(WriterBwh& aWriter, const Brx& aFeedUrl, TUint aBlocksToRead)
{
    AutoMutex _(iTunes.iLock);
    TBool success = false;
    Uri xmlFeedUri(aFeedUrl);
    if (!TryConnect(xmlFeedUri)) {
        LOG_ERROR(kMedia, "ITunesMetadata::TryGetResponse - connection failure\n");
        return false;
    }

    try {
        Log::Print("Write podcast feed request: %.*s\n", PBUF(aFeedUrl));
        WriteRequestHeaders(Http::kMethodGet, xmlFeedUri);

        iTunes.iReaderResponse.Read();
        const TUint code = iTunes.iReaderResponse.Status().Code();
        if (code != 200) {
            LOG_ERROR(kPipeline, "Http error - %d - in response to ITunesMetadata TryGetResponse (xml feed).  Some/all of response is:\n", code);
            Brn buf = iTunes.iReaderUntil.Read(iTunes.kReadBufferBytes);
            LOG_ERROR(kPipeline, "%.*s\n", PBUF(buf));
            THROW(ReaderError);
        }  
        
        TInt count = aBlocksToRead * iTunes.kReadBufferBytes;
        TInt length = iTunes.iHeaderContentLength.ContentLength();
        if (length > 0 && length < count) {
            count = length;
        }
        //Log::Print("Read ITunesMetadata response - xml feed (%d): ", count);
        while(count > 0) {
            Brn buf = iTunes.iReaderUntil.Read(iTunes.kReadBufferBytes);
            //Log::Print(buf);
            aWriter.Write(buf);
            count -= buf.Bytes();
        }   
        //Log::Print("\n");     

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

void ITunesMetadata::WriteRequestHeaders(const Brx& aMethod, Uri& aFeedUri, TUint aContentLength)
{
    iTunes.iWriterRequest.WriteMethod(aMethod, aFeedUri.PathAndQuery(), Http::eHttp11);
    Http::WriteHeaderHostAndPort(iTunes.iWriterRequest, aFeedUri.Host(), iTunes.kPort);
    if (aContentLength > 0) {
        Http::WriteHeaderContentLength(iTunes.iWriterRequest, aContentLength);
    }
    Http::WriteHeaderContentType(iTunes.iWriterRequest, Brn("application/x-www-form-urlencoded"));
    Http::WriteHeaderConnectionClose(iTunes.iWriterRequest);
    iTunes.iWriterRequest.WriteFlush();
}

TBool ITunesMetadata::TryConnect(Uri& aUri)
{
    Endpoint ep;
    try {
        ep.SetAddress(aUri.Host());
        ep.SetPort(iTunes.kPort);
        iTunes.iSocket.Connect(ep, iTunes.kConnectTimeoutMs);
    }
    catch (NetworkTimeout&) {
        return false;
    }
    catch (NetworkError&) {
        return false;
    }
    return true;
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

void ITunesMetadata::TryAddTagsFromArray(JsonParser& aParser,
                                     const Brx& aITunesKey, const Brx& aDidlTag,
                                     const Brx& aNs, const Brx& aRole)
{
    if (!aParser.HasKey(aITunesKey)) {
        return;
    }
    auto parserArray = JsonParserArray::Create(aParser.String(aITunesKey));
    if (parserArray.Type() == JsonParserArray::ValType::Null) {
        return;
    }
    try {
        for (;;) {
            Brn val = parserArray.NextString();
            Bwn valEscaped(val.Ptr(), val.Bytes(), val.Bytes());
            Json::Unescape(valEscaped);
            TryAddTag(aDidlTag, aNs, aRole, valEscaped);
        }
    }
    catch (JsonArrayEnumerationComplete&) {}
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

TBool ITunes::TryGetId(WriterBwh& aWriter, const Brx& aQuery)
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
        success = TryGetResponse(aWriter, pathAndQuery, 1); // only interested in one podcast collection at a time
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
        success = TryGetResponse(aWriter, pathAndQuery, 1); // only interested in one podcast collection at a time
        iSocket.Close();
    }
    catch (NetworkError&) {
    }
    return success;
}

TBool ITunes::TryGetResponse(WriterBwh& aWriter, Bwx& aPathAndQuery, TUint aLimit)
{
    AutoMutex _(iLock);
    TBool success = false;

    if (!TryConnect(kPort)) {
        LOG_ERROR(kMedia, "ITunes::TryGetResponse - connection failure\n");
        return false;
    }
    aPathAndQuery.Append("&limit=");
    Ascii::AppendDec(aPathAndQuery, aLimit);

    try {
        Log::Print("Write ITunes request: http://%.*s%.*s\n", PBUF(kHost), PBUF(aPathAndQuery));
        WriteRequestHeaders(Http::kMethodGet, aPathAndQuery, kPort);

        iReaderResponse.Read();
        const TUint code = iReaderResponse.Status().Code();
        if (code != 200) {
            LOG_ERROR(kPipeline, "Http error - %d - in response to ITunes TryGetResponse.  Some/all of response is:\n", code);
            Brn buf = iReaderUntil.Read(kReadBufferBytes);
            LOG_ERROR(kPipeline, "%.*s\n", PBUF(buf));
            THROW(ReaderError);
        }  
        
        TUint count = iHeaderContentLength.ContentLength();
        //Log::Print("Read ITunes response (%d): ", count);
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

TBool ITunes::TryConnect(TUint aPort)
{
    Endpoint ep;
    try {
        ep.SetAddress(kHost);
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

void ITunes::WriteRequestHeaders(const Brx& aMethod, const Brx& aPathAndQuery, TUint aPort, TUint aContentLength)
{
    iWriterRequest.WriteMethod(aMethod, aPathAndQuery, Http::eHttp11);
    Http::WriteHeaderHostAndPort(iWriterRequest, kHost, aPort);
    if (aContentLength > 0) {
        Http::WriteHeaderContentLength(iWriterRequest, aContentLength);
    }
    Http::WriteHeaderContentType(iWriterRequest, Brn("application/x-www-form-urlencoded"));
    Http::WriteHeaderConnectionClose(iWriterRequest);
    iWriterRequest.WriteFlush();
}


Brn ITunes::ReadInt(ReaderUntil& aReader, const Brx& aTag)
{ // static
    (void)aReader.ReadUntil('\"');
    for (;;) {
        Brn buf = aReader.ReadUntil('\"');
        if (buf == aTag) {
            break;
        }
    }

    (void)aReader.ReadUntil(':');
    Brn buf = aReader.ReadUntil(','); // FIXME - assumes aTag isn't the last element in this container
    return buf;
}

Brn ITunes::ReadString(ReaderUntil& aReader, const Brx& aTag)
{ // static
    (void)aReader.ReadUntil('\"');
    for (;;) {
        Brn buf = aReader.ReadUntil('\"');
        if (buf == aTag) {
            break;
        }
    }
    (void)aReader.ReadUntil('\"');
    Brn buf = aReader.ReadUntil('\"');
    return buf;
}

Brn ITunes::GetFirstAttribute(const Brx& aXml, const Brx& aAttribute)
{
    Parser parser;
    parser.Set(aXml);

    Brn buf;
    for (;;) {
        parser.Next(' ');
        if (parser.Next('=') == aAttribute) {
            parser.Next('"');
            return parser.Next('"');
        }
    }
}

Brn ITunes::GetFirstValue(const Brx& aXml, const Brx& aTag)
{
    Parser parser;
    parser.Set(aXml);

    Brn buf;
    TInt start = -1;
    TInt end = -1;
    for (;;) {
        parser.Next('<');
        start = parser.Index();
        buf.Set(parser.Next('>'));
        if (buf.BeginsWith(aTag)) {
            if (parser.At(-2) == '/') {
                // tag with no true value, but info stored as attribute instead
                end = parser.Index()-2;
                return aXml.Split(start, end-start);
            }
            else {
                start = parser.Index();
                break;
            }
        }
    }
    for (;;) {
        parser.Next('<');
        end = parser.Index() - 1;
        buf.Set(parser.Next('>'));
        Bwh endTag(aTag.Bytes()+1, aTag.Bytes()+1);
        endTag.ReplaceThrow(Brn("/"));
        endTag.Append(aTag);
        if (buf.BeginsWith(endTag)) {
            break;
        }
    }

    return aXml.Split(start, end-start);
}

PodcastEpisode::PodcastEpisode(const Brx& aXmlItem)
    : iTitle(1024)
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

    try {
        Brn title = Ascii::Trim(ITunes::GetFirstValue(aXmlItem, Brn("title")));
        iTitle.ReplaceThrow(title);
        Converter::FromXmlEscaped(iTitle);
    }
    catch (Exception&) {
        iTitle.ReplaceThrow(Brx::Empty());
    }
    
    try {
        Brn date = ITunes::GetFirstValue(aXmlItem, Brn("pubDate"));
        iReleaseDate.ReplaceThrow(date.Split(0, 16));
        iTitle.AppendThrow(Brn(" ("));
        iTitle.AppendThrow(iReleaseDate);
        iTitle.AppendThrow(Brn(")"));
    }
    catch (Exception&) {
        iReleaseDate.ReplaceThrow(Brx::Empty());
    }
    
    try {
        Brn duration = ITunes::GetFirstValue(aXmlItem, Brn("itunes:duration"));
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
        Brn enclosure = ITunes::GetFirstValue(aXmlItem, Brn("enclosure"));
        Brn url = ITunes::GetFirstAttribute(enclosure, Brn("url"));
        if (url.BeginsWith(Brn("https"))) {
            iUrl.ReplaceThrow(Brn("http"));
            iUrl.AppendThrow(url.Split(5, url.Bytes()-5));
        }
        else {
            iUrl.ReplaceThrow(url);
        }
        Converter::FromXmlEscaped(iUrl);
    }
    catch (Exception&) {
        iUrl.ReplaceThrow(Brx::Empty());
    }

    Log::Print("Podcast Title: %.*s\n", PBUF(iTitle));
    Log::Print("    Release Date: %.*s\n", PBUF(iReleaseDate));
    Log::Print("    Duration: %ds\n", iDuration);
    Log::Print("    Url: %.*s\n", PBUF(iUrl));
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

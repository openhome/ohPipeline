#include <OpenHome/Av/Tidal/TidalMetadata.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Json.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Parser.h>

namespace OpenHome {
namespace Av {

class Tidal2DidlTagMapping
{
public:
    Tidal2DidlTagMapping(const TChar* aTidalKey, const TChar* aDidlTag, const OpenHome::Brx& aNs)
        : iTidalKey(aTidalKey)
        , iDidlTag(aDidlTag)
        , iNs(aNs)
        , iRole(OpenHome::Brx::Empty())
        , iTidalSubKey(OpenHome::Brx::Empty())
        , iIsImage(false)
        , iResolution(TidalMetadata::eNone)
    {}
    Tidal2DidlTagMapping(const TChar* aTidalKey, const TChar* aDidlTag, const OpenHome::Brx& aNs, const TChar* aRole)
        : iTidalKey(aTidalKey)
        , iDidlTag(aDidlTag)
        , iNs(aNs)
        , iRole(aRole)
        , iTidalSubKey(OpenHome::Brx::Empty())
        , iIsImage(false)
        , iResolution(TidalMetadata::eNone)
    {}
    Tidal2DidlTagMapping(const TChar* aTidalKey, const TChar* aDidlTag, const OpenHome::Brx& aNs, const TChar* aSubKey, TBool aIsImage, TidalMetadata::EImageResolution aResolution)
        : iTidalKey(aTidalKey)
        , iDidlTag(aDidlTag)
        , iNs(aNs)
        , iRole(OpenHome::Brx::Empty())
        , iTidalSubKey(aSubKey)
        , iIsImage(aIsImage)
        , iResolution(aResolution)
    {}
public:
    OpenHome::Brn iTidalKey;
    OpenHome::Brn iDidlTag;
    OpenHome::Brn iNs;
    OpenHome::Brn iRole;
    OpenHome::Brn iTidalSubKey;
    TBool iIsImage;
    TidalMetadata::EImageResolution iResolution;
};

} // namespace Av
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Av;

const Brn TidalMetadata::kNsDc("dc=\"http://purl.org/dc/elements/1.1/\"");
const Brn TidalMetadata::kNsUpnp("upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\"");
const Brn TidalMetadata::kNsOh("oh=\"http://www.openhome.org\"");
const Brn TidalMetadata::kImageResourceBaseUrl("https://resources.tidal.com/images/");
const Brn TidalMetadata::kImageResourceResolutionLow("320x320"); // 80x80 | 160x160 | 320x320 | 640x640 | 750x750 | 1080x1080 | 1280x1280
const Brn TidalMetadata::kImageResourceResolutionMed("640x640");
const Brn TidalMetadata::kImageResourceResolutionHigh("1280x1280");
const Brn TidalMetadata::kImageResourceExtension(".jpg");


TidalMetadata::TidalMetadata(Media::TrackFactory& aTrackFactory)
    : iTrackFactory(aTrackFactory)
{
}

Media::Track* TidalMetadata::TrackFromJson(const Brx& aMetadata)
{
    try {
        ParseTidalMetadata(aMetadata);
        return iTrackFactory.CreateTrack(iTrackUri, iMetaDataDidl);
    }
    catch (AssertionFailed&) {
        throw;
    }
    catch (Exception&) {
        LOG_ERROR(kMedia, "TidalMetadata::TrackFromJson failed to parse metadata - trackBytes=%u\n", iTrackUri.Bytes());
        if (iTrackUri.Bytes() > 0) {
            return iTrackFactory.CreateTrack(iTrackUri, Brx::Empty());
        }
        return nullptr;
    }
}

const Brx& TidalMetadata::FirstIdFromJson(const Brx& aJsonResponse)
{
    static const Brn types[] = { Brn("artists"), Brn("albums"), Brn("playlists"), Brn("tracks") };
    try {
        JsonParser parser;
        parser.Parse(aJsonResponse);
        for (TUint i = 0; i < sizeof(types); i++) {
            if (parser.HasKey(types[i])) {
                parser.Parse(parser.String(types[i]));
                if (parser.Num(Brn("totalNumberOfItems")) == 0) {
                    continue;
                }
                auto parserArray = JsonParserArray::Create(parser.String("items"));
                if (parserArray.Type() == JsonParserArray::ValType::Null) {
                    continue;
                }
                parser.Parse(parserArray.NextObject());
                if (parser.HasKey(Brn("id"))) {
                    return parser.String(Brn("id"));
                }
            }
        }
    }
    catch (AssertionFailed&) {
        throw;
    }
    catch (Exception&) {
        return Brx::Empty();
    }
    return Brx::Empty();
}

void TidalMetadata::ParseTidalMetadata(const Brx& aMetadata)
{
    static const Tidal2DidlTagMapping kTidal2Didl[] ={
        { "title", "dc:title", kNsDc },
        //{ "trackNumber", "upnp:originalTrackNumber", kNsUpnp },
    };
    static const TUint kNumTidal2DidlMappings = sizeof kTidal2Didl / sizeof kTidal2Didl[0];

    static const Tidal2DidlTagMapping kTidalArray2Didl[] ={
        //{ "artists", "upnp:artist", kNsUpnp, "" }, ??
    };
    static const TUint kNumTidalArray2DidlMappings = sizeof kTidalArray2Didl / sizeof kTidalArray2Didl[0];

    static const Tidal2DidlTagMapping kTidalObj2Didl[] ={
        { "album", "upnp:albumArtURI", kNsUpnp, "cover", true, eLow },
        { "album", "upnp:albumArtURI", kNsUpnp, "cover", true, eMed },
        { "album", "upnp:albumArtURI", kNsUpnp, "cover", true, eHigh },
        { "album", "upnp:album", kNsUpnp, "title", false, eNone },
        { "artist", "upnp:artist", kNsUpnp, "name", false, eNone },
    };
    static const TUint kNumTidalObj2DidlMappings = sizeof kTidalObj2Didl / sizeof kTidalObj2Didl[0];

    iTrackUri.Replace(Brx::Empty());
    iMetaDataDidl.Replace(Brx::Empty());
    JsonParser parser;
    parser.Parse(aMetadata);
    if (parser.HasKey("allowStreaming")) {
        if (!parser.Bool("allowStreaming")) {
            throw;
        }
    }
    if (parser.HasKey("streamReady")) {
        if (!parser.Bool("streamReady")) {
            throw;
        }
    }
    //if (parser.HasKey("url")) { // streamable tidal url
    //    iTrackUri.ReplaceThrow(parser.String("url")); 
    //}
    if (parser.HasKey("id")) { // special linn style tidal url
        iTrackUri.ReplaceThrow(Brn("tidal://track?version=1&trackId="));
        iTrackUri.AppendThrow(parser.String("id"));
    }
    TryAppend("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    TryAppend("<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\">");
    TryAppend("<item");
    //TryAddAttribute(parser, "id", "id");
    //TryAppend(" parentID=\"-1\" restricted=\"1\">");
    TryAppend(">");
    for (TUint i=0; i<kNumTidal2DidlMappings; i++) {
        auto& mapping = kTidal2Didl[i];
        TryAddTag(parser, mapping.iTidalKey, mapping.iDidlTag, mapping.iNs);
    }
    TryAddTag(Brn("upnp:class"), kNsUpnp, Brx::Empty(), Brn("object.item.audioItem.musicTrack"));
    for (TUint i=0; i<kNumTidalObj2DidlMappings; i++) {
        auto& mapping = kTidalObj2Didl[i];
        TryAddTagsFromObj(parser, mapping.iTidalKey, mapping.iDidlTag, mapping.iNs, mapping.iTidalSubKey, mapping.iIsImage, mapping.iResolution);
    }
    for (TUint i=0; i<kNumTidalArray2DidlMappings; i++) {
        auto& mapping = kTidalArray2Didl[i];
        TryAddTagsFromArray(parser, mapping.iTidalKey, mapping.iDidlTag, mapping.iNs, mapping.iRole);
    }
    TryAppend("<res");
    TryAddAttribute("http-get:*:*:*", "protocolInfo");
    if (parser.HasKey("duration")) {
        TryAppend(" duration=\"");
        TUint duration = Ascii::Uint(parser.String("duration"));
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
}

void TidalMetadata::TryAddAttribute(JsonParser& aParser, const TChar* aTidalKey, const TChar* aDidlAttr)
{
    if (aParser.HasKey(aTidalKey)) {
        TryAppend(" ");
        TryAppend(aDidlAttr);
        TryAppend("=\"");
        TryAppend(aParser.String(aTidalKey));
        TryAppend("\"");
    }
}

void TidalMetadata::TryAddAttribute(const TChar* aValue, const TChar* aDidlAttr)
{
    TryAppend(" ");
    TryAppend(aDidlAttr);
    TryAppend("=\"");
    TryAppend(aValue);
    TryAppend("\"");
}

void TidalMetadata::TryAddTagsFromObj(JsonParser& aParser,
                                     const Brx& aTidalKey, const Brx& aDidlTag,
                                     const Brx& aNs, const Brx& aTidalSubKey, TBool aIsImage, EImageResolution aResolution)
{
    if (!aParser.HasKey(aTidalKey)) {
        return;
    }
    JsonParser nestedParser;
    nestedParser.Parse(aParser.String(aTidalKey));
    Bwh val(1024);
    Bwn valEscaped;
    if (aIsImage) {
        val.ReplaceThrow(kImageResourceBaseUrl);
        Parser idParser(nestedParser.String(aTidalSubKey));
        while (!idParser.Finished()) {
            val.AppendThrow(idParser.Next('-')); // replace '-' with '/' in value
            val.AppendThrow(Brn("/"));
        }
        if (aResolution == eLow) {
            val.AppendThrow(kImageResourceResolutionLow);
        }
        else if (aResolution == eMed) {
            val.AppendThrow(kImageResourceResolutionMed);
        }
        else {
            val.AppendThrow(kImageResourceResolutionHigh);
        }
        val.AppendThrow(kImageResourceExtension);
    }
    else {
        val.ReplaceThrow(nestedParser.String(aTidalSubKey));
    }
    valEscaped.Set(val.Ptr(), val.Bytes(), val.Bytes());
    Json::Unescape(valEscaped);
    TryAddTag(aDidlTag, aNs, Brx::Empty(), valEscaped);
}

void TidalMetadata::TryAddTagsFromArray(JsonParser& aParser,
                                     const Brx& aTidalKey, const Brx& aDidlTag,
                                     const Brx& aNs, const Brx& aRole)
{
    if (!aParser.HasKey(aTidalKey)) {
        return;
    }
    auto parserArray = JsonParserArray::Create(aParser.String(aTidalKey));
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

void TidalMetadata::TryAddTag(JsonParser& aParser, const Brx& aTidalKey,
                           const Brx& aDidlTag, const Brx& aNs)
{
    if (!aParser.HasKey(aTidalKey)) {
        return;
    }
    Brn val = aParser.String(aTidalKey);
    Bwn valEscaped(val.Ptr(), val.Bytes(), val.Bytes());
    Json::Unescape(valEscaped);
    TryAddTag(aDidlTag, aNs, Brx::Empty(), valEscaped);
}

void TidalMetadata::TryAddTag(const Brx& aDidlTag, const Brx& aNs,
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

void TidalMetadata::TryAppend(const TChar* aStr)
{
    Brn buf(aStr);
    TryAppend(buf);
}

void TidalMetadata::TryAppend(const Brx& aBuf)
{
    if (!iMetaDataDidl.TryAppend(aBuf)) {
        THROW(BufferOverflow);
    }
}

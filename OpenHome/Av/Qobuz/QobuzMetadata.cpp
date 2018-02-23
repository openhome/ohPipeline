#include <OpenHome/Av/Qobuz/QobuzMetadata.h>
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

class Qobuz2DidlTagMapping
{
public:
    Qobuz2DidlTagMapping(const TChar* aQobuzKey, const TChar* aDidlTag, const OpenHome::Brx& aNs)
        : iQobuzKey(aQobuzKey)
        , iDidlTag(aDidlTag)
        , iNs(aNs)
        , iQobuzSubKey(OpenHome::Brx::Empty())
    {}
    Qobuz2DidlTagMapping(const TChar* aQobuzKey, const TChar* aDidlTag, const OpenHome::Brx& aNs, const TChar* aSubKey)
        : iQobuzKey(aQobuzKey)
        , iDidlTag(aDidlTag)
        , iNs(aNs)
        , iQobuzSubKey(aSubKey)
    {}
public:
    OpenHome::Brn iQobuzKey;
    OpenHome::Brn iDidlTag;
    OpenHome::Brn iNs;
    OpenHome::Brn iQobuzSubKey;
};

} // namespace Av
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Av;

const Brn QobuzMetadata::kNsDc("dc=\"http://purl.org/dc/elements/1.1/\"");
const Brn QobuzMetadata::kNsUpnp("upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\"");
const Brn QobuzMetadata::kNsOh("oh=\"http://www.openhome.org\"");
const Brn QobuzMetadata::kIdTypeArtist("artist");
const Brn QobuzMetadata::kIdTypeAlbum("album");
const Brn QobuzMetadata::kIdTypeTrack("track");
const Brn QobuzMetadata::kIdTypePlaylist("playlist");
const Brn QobuzMetadata::kIdTypeSavedPlaylist("collection");
const Brn QobuzMetadata::kIdTypeFavorites("collection");
const Brn QobuzMetadata::kIdTypePurchased("collection");
const Brn QobuzMetadata::kIdTypePurchasedTracks("purchase");
const Brn QobuzMetadata::kIdTypeCollection("collection");
const Brn QobuzMetadata::kSmartTypeNew("album");
const Brn QobuzMetadata::kSmartTypeRecommended("album");
const Brn QobuzMetadata::kSmartTypeMostStreamed("album");
const Brn QobuzMetadata::kSmartTypeBestSellers("album");
const Brn QobuzMetadata::kSmartTypeAwardWinning("album");
const Brn QobuzMetadata::kSmartTypeMostFeatured("album");
const Brn QobuzMetadata::kIdTypeSmart("smart");
const Brn QobuzMetadata::kIdTypeUserSpecific("users");
const Brn QobuzMetadata::kGenreNone("none");

QobuzMetadata::QobuzMetadata(Media::TrackFactory& aTrackFactory)
    : iTrackFactory(aTrackFactory)
{
}

Media::Track* QobuzMetadata::TrackFromJson(const Brx& aJsonResponse, const Brx& aTrackObj, EIdType aType)
{
    try {
        ParseQobuzMetadata(aJsonResponse, aTrackObj, aType);
        return iTrackFactory.CreateTrack(iTrackUri, iMetaDataDidl);
    }
    catch (AssertionFailed&) {
        throw;
    }
    catch (Exception&) {
        if (iTrackUri.Bytes() > 0) {
            return iTrackFactory.CreateTrack(iTrackUri, Brx::Empty());
        }
        return nullptr;
    }
}

Brn QobuzMetadata::FirstIdFromJson(const Brx& aJsonResponse, EIdType aType)
{
    try {
        JsonParser parser;
        parser.Parse(aJsonResponse);
        Bws<20> pluralKey(IdTypeToString(aType));
        pluralKey.Append("s");
        if (parser.HasKey(pluralKey)) {
            parser.Parse(parser.String(pluralKey));
            if (parser.Num(Brn("total")) == 0) {
                THROW(QobuzResponseInvalid);
            }
            auto parserArray = JsonParserArray::Create(parser.String("items"));
            if (parserArray.Type() == JsonParserArray::ValType::Null) {
                THROW(QobuzResponseInvalid);
            }
            parser.Parse(parserArray.NextObject());
            if (parser.HasKey(Brn("id"))) {
                return parser.String(Brn("id"));
            }
        }
        else {
            THROW(QobuzResponseInvalid);
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

Brn QobuzMetadata::GenreIdFromJson(const Brx& aJsonResponse, const Brx& aGenre)
{
    try {
        JsonParser parser;
        parser.Reset();
        parser.Parse(aJsonResponse);
        if (parser.HasKey(Brn("genres"))) { 
            parser.Parse(parser.String(Brn("genres")));
            TUint total = parser.Num(Brn("total"));
            if (total == 0) {
                THROW(QobuzResponseInvalid);
            }
            auto parserItems = JsonParserArray::Create(parser.String(Brn("items")));
            try {
                for (;;) {
                    JsonParser parserItem;
                    parserItem.Parse(parserItems.NextObject());
                    if (Ascii::Contains(parserItem.String(Brn("name")), aGenre) || Ascii::Contains(parserItem.String(Brn("slug")), aGenre)) {
                        return parserItem.String(Brn("id"));
                    }
                }
            }
            catch (JsonArrayEnumerationComplete&) {}
        }
        else {
            THROW(QobuzResponseInvalid);
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

void QobuzMetadata::ParseQobuzMetadata(const Brx& aJsonResponse, const Brx& aTrackObj, EIdType /*aType*/)
{
    static const Qobuz2DidlTagMapping kQobuz2Didl[] ={
        { "title", "dc:title", kNsDc },
        { "track_number", "upnp:originalTrackNumber", kNsUpnp },
    };
    static const TUint kNumQobuz2DidlMappings = sizeof kQobuz2Didl / sizeof kQobuz2Didl[0];

    static const Qobuz2DidlTagMapping kQobuzObj2Didl[] ={
        { "image", "upnp:albumArtURI", kNsUpnp, "small"},
        { "image", "upnp:albumArtURI", kNsUpnp, "large"},
        { "artist", "upnp:artist", kNsUpnp, "name"},
    };
    static const TUint kNumQobuzObj2DidlMappings = sizeof kQobuzObj2Didl / sizeof kQobuzObj2Didl[0];

    iTrackUri.Replace(Brx::Empty());
    iMetaDataDidl.Replace(Brx::Empty());
    JsonParser parser;
    TBool useHighLevelData = false;

    // validate higher level metadata
    parser.Parse(aJsonResponse);
    if (parser.HasKey("product_type")) {
        useHighLevelData = true;
    }
    else {
        parser.Parse(aTrackObj);
    }

    if (parser.HasKey("streamable")) {
        if (!parser.Bool("streamable")) {
            THROW(QobuzResponseInvalid);
        }
    }

    // validate track specific metadata
    parser.Parse(aTrackObj);
    if (!parser.HasKey("id")) {
        // track uri based on id, so will be invalid without one
        THROW(QobuzResponseInvalid);
    }

    TryAppend("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    TryAppend("<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\">");
    TryAppend("<item>");
    TryAddTag(Brn("upnp:class"), kNsUpnp, Brx::Empty(), Brn("object.item.audioItem.musicTrack"));
    // higher level metadata (qobuz leaves album name, artist, image urls outside of track objects)
    if (useHighLevelData) {
        parser.Parse(aJsonResponse);
    }
    else {
        if (parser.HasKey("album")) {
            parser.Parse(parser.String("album"));
        }
    }
    TryAddTag(parser, Brn("title"), Brn("upnp:album"), kNsUpnp);
    for (TUint i=0; i<kNumQobuzObj2DidlMappings; i++) {
        auto& mapping = kQobuzObj2Didl[i];
        TryAddTagFromObj(parser, mapping.iQobuzKey, mapping.iDidlTag, mapping.iNs, mapping.iQobuzSubKey);
    }

    // track specific metadata
    parser.Parse(aTrackObj);
    for (TUint i=0; i<kNumQobuz2DidlMappings; i++) {
        auto& mapping = kQobuz2Didl[i];
        TryAddTag(parser, mapping.iQobuzKey, mapping.iDidlTag, mapping.iNs);
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

    // special linn style Qobuz url (non-streamable, gets converted later)
    iTrackUri.ReplaceThrow(Brn("qobuz://track?version=2&trackId="));
    iTrackUri.AppendThrow(parser.String("id"));
    if (iTrackUri.Bytes() > 0) {
        WriterBuffer writer(iMetaDataDidl);
        Converter::ToXmlEscaped(writer, iTrackUri);
    }
    TryAppend("</res>");

    if (parser.HasKey("composer")) {
        TryAddTagFromObj(parser, Brn("composer"), Brn("upnp:artist"), kNsUpnp, Brn("name"), Brn("composer"));
    }

    TryAppend("</item>");
    TryAppend("</DIDL-Lite>");
}

void QobuzMetadata::TryAddAttribute(JsonParser& aParser, const TChar* aQobuzKey, const TChar* aDidlAttr)
{
    if (aParser.HasKey(aQobuzKey)) {
        TryAppend(" ");
        TryAppend(aDidlAttr);
        TryAppend("=\"");
        TryAppend(aParser.String(aQobuzKey));
        TryAppend("\"");
    }
}

void QobuzMetadata::TryAddAttribute(const TChar* aValue, const TChar* aDidlAttr)
{
    TryAppend(" ");
    TryAppend(aDidlAttr);
    TryAppend("=\"");
    TryAppend(aValue);
    TryAppend("\"");
}

void QobuzMetadata::TryAddTagFromObj(JsonParser& aParser,
    const Brx& aQobuzKey, const Brx& aDidlTag,
    const Brx& aNs, const Brx& aQobuzSubKey)
{
    TryAddTagFromObj(aParser, aQobuzKey, aDidlTag, aNs, aQobuzSubKey, Brx::Empty());
}

void QobuzMetadata::TryAddTagFromObj(JsonParser& aParser,
                                     const Brx& aQobuzKey, const Brx& aDidlTag,
                                     const Brx& aNs, const Brx& aQobuzSubKey, const Brx& aRole)
{
    if (!aParser.HasKey(aQobuzKey)) {
        return;
    }
    JsonParser nestedParser;
    nestedParser.Parse(aParser.String(aQobuzKey));
    Bwh val(1024);
    Bwn valEscaped;
    val.ReplaceThrow(nestedParser.String(aQobuzSubKey));
    valEscaped.Set(val.Ptr(), val.Bytes(), val.Bytes());
    Json::Unescape(valEscaped);
    TryAddTag(aDidlTag, aNs, aRole, valEscaped);
}

void QobuzMetadata::TryAddTagsFromArray(JsonParser& aParser,
                                     const Brx& aQobuzKey, const Brx& aDidlTag,
                                     const Brx& aNs, const Brx& aRole)
{
    if (!aParser.HasKey(aQobuzKey)) {
        return;
    }
    auto parserArray = JsonParserArray::Create(aParser.String(aQobuzKey));
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

void QobuzMetadata::TryAddTag(JsonParser& aParser, const Brx& aQobuzKey,
                           const Brx& aDidlTag, const Brx& aNs)
{
    if (!aParser.HasKey(aQobuzKey)) {
        return;
    }
    Brn val = aParser.String(aQobuzKey);
    Bwn valEscaped(val.Ptr(), val.Bytes(), val.Bytes());
    Json::Unescape(valEscaped);
    TryAddTag(aDidlTag, aNs, Brx::Empty(), valEscaped);
}

void QobuzMetadata::TryAddTag(const Brx& aDidlTag, const Brx& aNs,
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

void QobuzMetadata::TryAppend(const TChar* aStr)
{
    Brn buf(aStr);
    TryAppend(buf);
}

void QobuzMetadata::TryAppend(const Brx& aBuf)
{
    if (!iMetaDataDidl.TryAppend(aBuf)) {
        THROW(BufferOverflow);
    }
}

const Brx& QobuzMetadata::IdTypeToString(EIdType aType)
{
    switch (aType) {
        case eArtist: return kIdTypeArtist;
        case eAlbum: return kIdTypeAlbum;
        case eTrack: return kIdTypeTrack;
        case ePlaylist: return kIdTypePlaylist;
        case eSavedPlaylist: return kIdTypeSavedPlaylist;
        case eFavorites: return kIdTypeFavorites;
        case ePurchased: return kIdTypePurchased;
        case ePurchasedTracks: return kIdTypePurchasedTracks;
        case eCollection: return kIdTypeCollection;
        case eSmartNew: return kSmartTypeNew;
        case eSmartRecommended: return kSmartTypeRecommended;
        case eSmartMostStreamed: return kSmartTypeMostStreamed;
        case eSmartBestSellers: return kSmartTypeBestSellers;
        case eSmartAwardWinning: return kSmartTypeAwardWinning;
        case eSmartMostFeatured: return kSmartTypeMostFeatured;
    }
    return Brx::Empty();
}

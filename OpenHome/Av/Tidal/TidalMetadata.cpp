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
#include <OpenHome/Av/Pins/Pins.h>

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
        , iResolution(TidalMetadata::eNoImage)
    {}
    Tidal2DidlTagMapping(const TChar* aTidalKey, const TChar* aDidlTag, const OpenHome::Brx& aNs, const TChar* aRole)
        : iTidalKey(aTidalKey)
        , iDidlTag(aDidlTag)
        , iNs(aNs)
        , iRole(aRole)
        , iTidalSubKey(OpenHome::Brx::Empty())
        , iIsImage(false)
        , iResolution(TidalMetadata::eNoImage)
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
const Brn TidalMetadata::kIdTypeArtist("artists");
const Brn TidalMetadata::kIdTypeAlbum("albums");
const Brn TidalMetadata::kIdTypeTrack("tracks");
const Brn TidalMetadata::kIdTypePlaylist("playlists");
const Brn TidalMetadata::kIdTypeSavedPlaylist("saved");
const Brn TidalMetadata::kIdTypeFavorites("favorites");
const Brn TidalMetadata::kIdTypeGenre("genres");
const Brn TidalMetadata::kIdTypeMood("moods");
const Brn TidalMetadata::kSmartTypeNew("featured/new");
const Brn TidalMetadata::kSmartTypeRecommended("featured/recommended");
const Brn TidalMetadata::kSmartTypeTop20("featured/top");
const Brn TidalMetadata::kSmartTypeExclusive("featured/exclusive");
const Brn TidalMetadata::kSmartTypeRising("rising/new");
const Brn TidalMetadata::kSmartTypeDiscovery("discovery/new");
const Brn TidalMetadata::kIdTypeSmart("smart");
const Brn TidalMetadata::kIdTypeUserSpecific("users");
const Brn TidalMetadata::kIdTypeNone("none");

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

Brn TidalMetadata::FirstIdFromJson(const Brx& aJsonResponse, EIdType aType)
{
    try {
        JsonParser parser;
        parser.Parse(aJsonResponse);
        if (parser.HasKey(IdTypeToString(aType)) || aType == eMood || aType == eSmartExclusive || aType == eSavedPlaylist) {
            if (aType != eMood && aType != eSmartExclusive && aType != eSavedPlaylist) {
                // mood/exclusive/saved playlist return top level object only (playlist based), all others have a higher level TYPE based response
                parser.Parse(parser.String(IdTypeToString(aType)));
            }
            if (parser.Num(Brn("totalNumberOfItems")) == 0) {
                THROW(TidalResponseInvalid);
            }
            auto parserArray = JsonParserArray::Create(parser.String("items"));
            if (parserArray.Type() == JsonParserArray::ValType::Null) {
                THROW(TidalResponseInvalid);
            }
            parser.Parse(parserArray.NextObject());
            if (parser.HasKey(Brn("id"))) {
                return parser.String(Brn("id"));
            }
            else if (parser.HasKey(Brn("uuid"))) {
                return parser.String(Brn("uuid"));
            }
        }
        else {
            THROW(TidalResponseInvalid);
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

void TidalMetadata::ParseTidalMetadata(const Brx& aMetadata)
{
    static const Tidal2DidlTagMapping kTidal2Didl[] ={
        { "title", "dc:title", kNsDc },
        { "trackNumber", "upnp:originalTrackNumber", kNsUpnp },
    };
    static const TUint kNumTidal2DidlMappings = sizeof kTidal2Didl / sizeof kTidal2Didl[0];

    //static const Tidal2DidlTagMapping kTidalArray2Didl[] ={
        //{ "artists", "upnp:artist", kNsUpnp, "" }, ??
    //};
    //static const TUint kNumTidalArray2DidlMappings = sizeof kTidalArray2Didl / sizeof kTidalArray2Didl[0];

    static const Tidal2DidlTagMapping kTidalObj2Didl[] ={
        { "album", "upnp:albumArtURI", kNsUpnp, "cover", true, eLow },
        { "album", "upnp:albumArtURI", kNsUpnp, "cover", true, eMed },
        { "album", "upnp:albumArtURI", kNsUpnp, "cover", true, eHigh },
        { "album", "upnp:album", kNsUpnp, "title", false, eNoImage },
        { "artist", "upnp:artist", kNsUpnp, "name", false, eNoImage },
    };
    static const TUint kNumTidalObj2DidlMappings = sizeof kTidalObj2Didl / sizeof kTidalObj2Didl[0];

    iTrackUri.Replace(Brx::Empty());
    iMetaDataDidl.Replace(Brx::Empty());
    JsonParser parser;
    parser.Parse(aMetadata);

    if (parser.HasKey("item")) {
        // playlists have an extra layer of indirection (item dictionary) as they can be mixed media (audio and video)
        parser.Parse(parser.String("item"));
    }
    else if (!parser.HasKey("id")) {
        // track uri based on id, so will be invalid without one
        THROW(TidalResponseInvalid);
    }

    if (parser.HasKey("allowStreaming")) {
        if (!parser.Bool("allowStreaming")) {
            THROW(TidalResponseInvalid);
        }
    }
    if (parser.HasKey("streamReady")) {
        if (!parser.Bool("streamReady")) {
            THROW(TidalResponseInvalid);
        }
    }
    //if (parser.HasKey("url")) { // streamable tidal url
    //    iTrackUri.ReplaceThrow(parser.String("url")); 
    //}
    if (parser.HasKey("id")) { // special linn style tidal url (non-streamable, gets converted later)
        iTrackUri.ReplaceThrow(Brn("tidal://track?version=1&trackId="));
        iTrackUri.AppendThrow(parser.String("id"));
    }
    TryAppend("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    TryAppend("<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\">");
    TryAppend("<item");
    TryAddAttribute(parser, "id", "id");
    TryAppend(" parentID=\"-1\" restricted=\"1\">");
    for (TUint i=0; i<kNumTidal2DidlMappings; i++) {
        auto& mapping = kTidal2Didl[i];
        TryAddTag(parser, mapping.iTidalKey, mapping.iDidlTag, mapping.iNs);
    }
    TryAddTag(Brn("upnp:class"), kNsUpnp, Brx::Empty(), Brn("object.item.audioItem.musicTrack"));
    for (TUint i=0; i<kNumTidalObj2DidlMappings; i++) {
        auto& mapping = kTidalObj2Didl[i];
        TryAddTagsFromObj(parser, mapping.iTidalKey, mapping.iDidlTag, mapping.iNs, mapping.iTidalSubKey, mapping.iIsImage, mapping.iResolution);
    }
    //for (TUint i=0; i<kNumTidalArray2DidlMappings; i++) {
    //    auto& mapping = kTidalArray2Didl[i];
    //    TryAddTagsFromArray(parser, mapping.iTidalKey, mapping.iDidlTag, mapping.iNs, mapping.iRole);
    //}
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

const Brx& TidalMetadata::IdTypeToString(EIdType aType)
{
    switch (aType) {
        case eArtist: return kIdTypeArtist;
        case eAlbum: return kIdTypeAlbum;
        case eTrack: return kIdTypeTrack;
        case ePlaylist: return kIdTypePlaylist;
        case eSavedPlaylist: return kIdTypeSavedPlaylist;
        case eFavorites: return kIdTypeFavorites;
        case eGenre: return kIdTypeGenre;
        case eMood: return kIdTypeMood;
        case eSmartNew: return kSmartTypeNew;
        case eSmartRecommended: return kSmartTypeRecommended;
        case eSmartTop20: return kSmartTypeTop20;
        case eSmartExclusive: return kSmartTypeExclusive;
        case eSmartRising: return kSmartTypeRising;
        case eSmartDiscovery: return kSmartTypeDiscovery;
        case eNone: return kIdTypeNone;
    }
    return Brx::Empty();
}

TidalMetadata::EIdType TidalMetadata::StringToIdType(const Brx& aString)
{
    Bws<10> plural(aString);
    if (aString.At(aString.Bytes()-1) != TByte('s')) {
        plural.Append("s");
    }
    if (plural == kIdTypeArtist) return eArtist;
    else if (plural == kIdTypeAlbum) return eAlbum;
    else if (plural == kIdTypeTrack) return eTrack;
    else if (plural == kIdTypePlaylist) return ePlaylist;
    else if (plural == kIdTypeGenre) return eGenre;
    else THROW(PinUriMissingRequiredParameter);
}

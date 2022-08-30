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

using namespace OpenHome;
using namespace OpenHome::Av;

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

Media::Track* TidalMetadata::TrackFromJson(const Brx& aMetadata,
                                           const Brx& aTokenId)
{
    try {
        ParseTidalMetadata(aMetadata, aTokenId);
        return iTrackFactory.CreateTrack(iTrackUri, iMetaDataDidl);
    }
    catch (AssertionFailed&) {
        throw;
    }
    catch (Exception& ex) {
        LOG_ERROR(kMedia, "TidalMetadata::TrackFromJson failed to parse metadata (%s) - trackBytes=%u\n", ex.Message(), iTrackUri.Bytes());
        if (iTrackUri.Bytes() > 0) {
            return iTrackFactory.CreateTrack(iTrackUri, Brx::Empty());
        }
        return nullptr;
    }
}

void TidalMetadata::ParseTidalMetadata(const Brx& aMetadata,
                                       const Brx& aTokenId)
{
    iTrackUri.Replace(Brx::Empty());
    iMetaDataDidl.Replace(Brx::Empty());
    JsonParser parser;
    JsonParser nestedParser;
    parser.Parse(aMetadata);

    if (parser.HasKey("item")) {
        // playlists have an extra layer of indirection (item dictionary) as they can be mixed media (audio and video)
        parser.Parse(parser.String("item"));
    }

    if (!parser.HasKey("id")) {
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

     // special linn style tidal url (non-streamable, gets converted later)
    const Brx& itemId = parser.String("id");
    iTrackUri.ReplaceThrow(Brn("tidal://track?trackId="));
    iTrackUri.AppendThrow(itemId);
    iTrackUri.AppendThrow(Brn("&version="));

    const TBool isUsingOAuth = aTokenId.Bytes() > 0;

    if (isUsingOAuth)
    {
        iTrackUri.AppendThrow("2&token=");
        iTrackUri.AppendThrow(aTokenId);
    }
    else
    {
        iTrackUri.AppendThrow("1");
    }

    Bwn unescapedBuf;
    auto unescapeVal = [&] (const Brx& aValue) {
        unescapedBuf.Set(aValue.Ptr(), aValue.Bytes(), aValue.Bytes());
        Json::Unescape(unescapedBuf);
    };

    WriterBuffer w(iMetaDataDidl);
    WriterDIDLLite writer(itemId, DIDLLite::kItemTypeTrack, w);

    if (parser.HasKey("title")) {
        unescapeVal(parser.String("title"));
        writer.WriteTitle(unescapedBuf);
    }

    if (parser.HasKey("trackNumber")) {
        unescapeVal(parser.String("trackNumber"));
        writer.WriteTrackNumber(unescapedBuf);
    }

    if (parser.HasKey("album")) {
        nestedParser.Parse(parser.String("album"));
        if (nestedParser.HasKey("title")) {
            unescapeVal(nestedParser.String("title"));
            writer.WriteAlbum(unescapedBuf);
        }

        TryWriteArtwork(writer, nestedParser);
    }

    if (parser.HasKey("artist")) {
        nestedParser.Parse(parser.String("artist"));
        if (nestedParser.HasKey("name")) {
            unescapeVal(nestedParser.String("name"));
            writer.WriteArtist(unescapedBuf);
        }
    }

    writer.WriteStreamingDetails(DIDLLite::kProtocolHttpGet, parser.Num("duration"), iTrackUri);
    writer.WriteEnd();
}

void TidalMetadata::TryWriteArtwork(WriterDIDLLite& aWriter, JsonParser& aParser)
{
    // NOTE: Assumes 'aParser' is already pointing to a valid TIDAL album object
    if (!aParser.HasKey("cover")) {
        return;
    }

    Bwh artworkUri(1024);
    Bwh baseArtworkUri(1024);
    baseArtworkUri.Replace(kImageResourceBaseUrl);

    Parser idParser(aParser.String("cover"));
    while(!idParser.Finished()) {
        baseArtworkUri.AppendThrow(idParser.Next('-')); // replace '-' with '/' in value
        baseArtworkUri.AppendThrow(Brn("/"));
    }

    auto writeArtwork = [&] (const Brx& aResolution) {
        artworkUri.ReplaceThrow(baseArtworkUri);
        artworkUri.AppendThrow(aResolution);
        artworkUri.AppendThrow(kImageResourceExtension);

        aWriter.WriteArtwork(artworkUri);
    };

    writeArtwork(kImageResourceResolutionLow);
    writeArtwork(kImageResourceResolutionMed);
    writeArtwork(kImageResourceResolutionHigh);
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

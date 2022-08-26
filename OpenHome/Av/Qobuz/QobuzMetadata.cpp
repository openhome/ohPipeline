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
#include <OpenHome/Av/Pins/Pins.h>
#include <OpenHome/Av/OhMetadata.h>

using namespace OpenHome;
using namespace OpenHome::Av;

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
const Brn QobuzMetadata::kIdTypeNone("none");
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

void QobuzMetadata::ParseQobuzMetadata(const Brx& aJsonResponse, const Brx& aTrackObj, EIdType /*aType*/)
{
    iTrackUri.Replace(Brx::Empty());
    iMetaDataDidl.Replace(Brx::Empty());
    JsonParser parserContainer;         // Parses the container object - aJsonResponse
    JsonParser parserTrack;             // Parses the track object - aTrackObj
    JsonParser nestedParser;            // Parses object properties from the above 2
    JsonParser nestedLevel2Parser;      // Sometimes there's another level of objects, so we need this parser as well. (Track -> Album -> Images)

    // First - parse the track object and ensure we have enough details to continue!
    parserTrack.Parse(aTrackObj);

    if (parserTrack.HasKey("streamable")) {
        if (!parserTrack.Bool("streamable")) {
            THROW(QobuzResponseInvalid);
        }
    }

    if (!parserTrack.HasKey("id")) {
        // track uri based on id, so will be invalid without one
        THROW(QobuzResponseInvalid);
    }

    const Brx& itemId = parserTrack.String("id");

    // special linn style Qobuz url (non-streamable, gets converted later)
    iTrackUri.ReplaceThrow(Brn("qobuz://track?version=2&trackId="));
    iTrackUri.AppendThrow(itemId);
    if (iTrackUri.Bytes() > 0) {
        WriterBuffer writer(iMetaDataDidl);
        Converter::ToXmlEscaped(writer, iTrackUri);
    }

    // Parse the parent container, so we can grab what we need.
    // validate higher level metadata
    // Qobuz tracks don't have the album/artist details directly so have to reach into the parent container
    parserContainer.Parse(aJsonResponse);
    const TBool useHighLevelData = parserContainer.HasKey("product_type");

    Bwn unescapedBuf;
    auto unescapeVal = [&] (const Brx& aValue) {
        unescapedBuf.Set(aValue.Ptr(), aValue.Bytes(), aValue.Bytes());
        Json::Unescape(unescapedBuf);
    };

    WriterDIDLLite writer(itemId, DIDLLite::kItemTypeTrack, iMetaDataDidl);

    // First - grab metadata from the track object directly.
    // We can use: Title & track number
    if (parserTrack.HasKey("title")) {
        writer.WriteTitle(parserTrack.String("title"));
    }

    if (parserTrack.HasKey("track_number")) {
        writer.WriteTrackNumber(parserTrack.String("track_number"));
    }

    writer.WriteStreamingDetails(DIDLLite::kProtocolHttpGet, parserTrack.Num("duration"), iTrackUri);

    // Then, set ourselves up for where we find the other details for a track - parent or self.
    JsonParser& detailParser = useHighLevelData ? parserContainer
                                                : parserTrack;

    // If we're grabbing the metadata from ourselves, then the album title is nested in an 'Album' object
    // This object will also contain the other details we need later on.
    if (detailParser.HasKey("album")) {
        nestedParser.Parse(detailParser.String("album"));

        if (nestedParser.HasKey("title")) {
            unescapeVal(nestedParser.String("title"));
            writer.WriteAlbum(unescapedBuf);
        }
    }
    else if (useHighLevelData && detailParser.HasKey("title")) {
        // Otherwise, the title should just be present as a property
        unescapeVal(detailParser.String("title"));
        writer.WriteAlbum(unescapedBuf);
    }

    // If we're not reaching into the parent, then we want to keep ourselves inside the 'album' object
    // as this will contain the remaining metadata we need...
    if (!useHighLevelData) {
        detailParser = nestedParser;
    }

    if (detailParser.HasKey("artist")) {
        nestedLevel2Parser.Parse(detailParser.String("artist"));
        if (nestedLevel2Parser.HasKey("name")) {
            unescapeVal(nestedLevel2Parser.String("name"));
            writer.WriteArtist(unescapedBuf);
        }
    }

    if (detailParser.HasKey("image")) {
        nestedLevel2Parser.Parse(detailParser.String("image"));

        if (nestedLevel2Parser.HasKey("small")) {
            unescapeVal(nestedLevel2Parser.String("small"));
            writer.WriteArtwork(unescapedBuf);
        }

        if (nestedLevel2Parser.HasKey("large")) {
            unescapeVal(nestedLevel2Parser.String("large"));
            writer.WriteArtwork(unescapedBuf);
        }
    }

    // TODO: Fix this...
    /*
    if (parser.HasKey("composer")) {
        TryAddTagFromObj(parser, Brn("composer"), Brn("upnp:artist"), kNsUpnp, Brn("name"), Brn("composer"));
    }
    */

    writer.WriteEnd();
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
        case eNone: return kIdTypeNone;
    }
    return Brx::Empty();
}

QobuzMetadata::EIdType QobuzMetadata::StringToIdType(const Brx& aString)
{
    if (aString == kIdTypeArtist) return eArtist;
    else if (aString == kIdTypeAlbum) return eAlbum;
    else if (aString == kIdTypeTrack) return eTrack;
    else if (aString == kIdTypePlaylist) return ePlaylist;
    else THROW(PinUriMissingRequiredParameter);
}

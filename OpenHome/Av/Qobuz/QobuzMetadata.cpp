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

Media::Track* QobuzMetadata::TrackFromJson(TBool aHasParentMetadata, const ParentMetadata& aParentMetadata, const Brx& aTrackObj, EIdType aType)
{
    try {
        ParseQobuzMetadata(aHasParentMetadata, aParentMetadata, aTrackObj, aType);
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

TBool QobuzMetadata::TryParseParentMetadata(const OpenHome::Brx& aJsonResponse, ParentMetadata& aParentMetadata)
{
    JsonParser parser;
    JsonParser nestedParser;
    parser.Parse(aJsonResponse);

    if (!parser.HasKey("product_type")) {
        return false;
    }

    auto setValue = [] (Bwx& aBuffer, const Brx& aValue) {
        aBuffer.ReplaceThrow(aValue);
        Json::Unescape(aBuffer, Json::Encoding::Utf16);
    };

    setValue(aParentMetadata.albumId, Brx::Empty());
    setValue(aParentMetadata.artistId, Brx::Empty());

    if (parser.HasKey("id")) {
        const Brx& productType = parser.String("product_type");
        if (productType == Brn("artist")) {
            setValue(aParentMetadata.artistId, parser.String("id"));
        }
        else {
            setValue(aParentMetadata.albumId, parser.String("id"));
        }
    }

    if (parser.HasKey("title")) {
        setValue(aParentMetadata.title, parser.String("title"));
    }

    if (parser.HasKey("artist")) {
        nestedParser.Parse(parser.String("artist"));
        if (nestedParser.HasKey("name")) {
            setValue(aParentMetadata.artist, nestedParser.String("name"));
        }

        if (nestedParser.HasKey("id")) {
            setValue(aParentMetadata.artistId, nestedParser.String("id"));
        }
    }

    if (parser.HasKey("album")) {
        nestedParser.Parse(parser.String("album"));
        if (nestedParser.HasKey("id")) {
            setValue(aParentMetadata.albumId, nestedParser.String("id"));
        }
    }

    if (parser.HasKey("image")) {
        nestedParser.Parse(parser.String("image"));
        if (nestedParser.HasKey("small")) {
            setValue(aParentMetadata.smallArtworkUri, nestedParser.String("small"));
        }

        if (nestedParser.HasKey("large")) {
            setValue(aParentMetadata.largeArtworkUri, nestedParser.String("large"));
        }
    }

    return true;
}

void QobuzMetadata::ParseQobuzMetadata(TBool aHasParentMetadata, const ParentMetadata& aParentMetadata, const Brx& aTrackObj, EIdType /*aType*/)
{
    iTrackUri.Replace(Brx::Empty());
    iMetaDataDidl.Replace(Brx::Empty());
    JsonParser parserTrack;             // Parses the track object - aTrackObj
    JsonParser nestedParser;            // Parses object properties from the above
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

    Bwn unescapedBuf;
    auto unescapeVal = [&] (const Brx& aValue) {
        unescapedBuf.Set(aValue.Ptr(), aValue.Bytes(), aValue.Bytes());
        Json::Unescape(unescapedBuf, Json::Encoding::Utf16);
    };

    WriterBuffer w(iMetaDataDidl);
    WriterDIDLLite writer(itemId, DIDLLite::kItemTypeTrack, w);

    // First - grab metadata from the track object directly.
    // We can use: Title, duration & track number
    if (parserTrack.HasKey("title")) {
        unescapeVal(parserTrack.String("title"));
        writer.WriteTitle(unescapedBuf);
    }

    if (parserTrack.HasKey("track_number")) {
        writer.WriteTrackNumber(parserTrack.String("track_number"));
    }

    WriterDIDLLite::StreamingDetails details;
    details.durationResolution = EDurationResolution::Seconds;
    details.duration = parserTrack.HasKey("duration") ? parserTrack.Num("duration")
                                                      : 0;
    writer.WriteStreamingDetails(DIDLLite::kProtocolHttpGet, details, iTrackUri);

    // Parent metadata is already escaped!
    if (aHasParentMetadata) {
        writer.WriteAlbum(aParentMetadata.title);
        writer.WriteArtist(aParentMetadata.artist);
        writer.WriteArtwork(aParentMetadata.smallArtworkUri);
        writer.WriteArtwork(aParentMetadata.largeArtworkUri);

        if (aParentMetadata.albumId.Bytes() > 0) {
            writer.WriteCustomMetadata("albumId", DIDLLite::kNameSpaceLinn, aParentMetadata.albumId);
        }

        if (aParentMetadata.artistId.Bytes() > 0) {
            writer.WriteCustomMetadata("artistId", DIDLLite::kNameSpaceLinn, aParentMetadata.artistId);
        }
    }
    else
    {
        // If no parent metadata, details are found in an 'Album' object
        if (parserTrack.HasKey("album")) {
            nestedParser.Parse(parserTrack.String("album"));

            if (nestedParser.HasKey("id")) {
                writer.WriteCustomMetadata("albumId", DIDLLite::kNameSpaceLinn, nestedParser.String("id"));
            }

            if (nestedParser.HasKey("title")) {
                unescapeVal(nestedParser.String("title"));
                writer.WriteAlbum(unescapedBuf);
            }

            if (nestedParser.HasKey("artist")) {
                nestedLevel2Parser.Parse(nestedParser.String("artist"));
                if (nestedLevel2Parser.HasKey("name")) {
                    unescapeVal(nestedLevel2Parser.String("name"));
                    writer.WriteArtist(unescapedBuf);
                }

                if (nestedLevel2Parser.HasKey("id")) {
                    writer.WriteCustomMetadata("artistId", DIDLLite::kNameSpaceLinn, nestedLevel2Parser.String("id"));
                }
            }

            if (nestedParser.HasKey("image")) {
                nestedLevel2Parser.Parse(nestedParser.String("image"));

                if (nestedLevel2Parser.HasKey("small")) {
                    unescapeVal(nestedLevel2Parser.String("small"));
                    writer.WriteArtwork(unescapedBuf);
                }

                if (nestedLevel2Parser.HasKey("large")) {
                    unescapeVal(nestedLevel2Parser.String("large"));
                    writer.WriteArtwork(unescapedBuf);
                }
            }
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

#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Media/Pipeline/Msg.h>

EXCEPTION(QobuzResponseInvalid);
EXCEPTION(QobuzRequestInvalid);

namespace OpenHome {
    class Environment;
    class JsonParser;
namespace Av {

class QobuzMetadata : private OpenHome::INonCopyable
{
    static const OpenHome::Brn kNsDc;
    static const OpenHome::Brn kNsUpnp;
    static const OpenHome::Brn kNsOh;
    static const OpenHome::Brn kIdTypeArtist;
    static const OpenHome::Brn kIdTypeAlbum;
    static const OpenHome::Brn kIdTypeTrack;
    static const OpenHome::Brn kIdTypePlaylist;
    // user specific
    static const OpenHome::Brn kIdTypePurchased;
    static const OpenHome::Brn kIdTypePurchasedTracks;
    static const OpenHome::Brn kIdTypeCollection;
    static const OpenHome::Brn kIdTypeSavedPlaylist;
    static const OpenHome::Brn kIdTypeFavorites;
    // smart types
    static const OpenHome::Brn kSmartTypeNew;
    static const OpenHome::Brn kSmartTypeRecommended;
    static const OpenHome::Brn kSmartTypeMostStreamed;
    static const OpenHome::Brn kSmartTypeBestSellers;
    static const OpenHome::Brn kSmartTypeAwardWinning;
    static const OpenHome::Brn kSmartTypeMostFeatured;
public:
    static const OpenHome::Brn kIdTypeSmart;
    static const OpenHome::Brn kIdTypeUserSpecific;
    static const OpenHome::Brn kIdTypeNone;
    static const OpenHome::Brn kGenreNone;
    enum EIdType {
        eNone,
        eArtist,
        eAlbum,
        eTrack,
        ePlaylist,
        eSavedPlaylist,
        eFavorites,
        ePurchased,
        ePurchasedTracks,
        eCollection,
        eSmartNew,
        eSmartRecommended,
        eSmartMostStreamed,
        eSmartBestSellers,
        eSmartAwardWinning,
        eSmartMostFeatured,
    };

    struct ParentMetadata
    {
        Brn title;
        Brn artist;
        Brn smallArtworkUri;
        Brn largeArtworkUri;
        Brn artistId;
        Brn albumId;
    };

public:
    QobuzMetadata(OpenHome::Media::TrackFactory& aTrackFactory);
    TBool TryParseParentMetadata(const OpenHome::Brx& aJsonResponse, ParentMetadata& aParentMetadata);
    OpenHome::Media::Track* TrackFromJson(TBool aHasParentMetadata, const ParentMetadata& aJsonResponse, const OpenHome::Brx& aTrackObj);
    static const Brx& IdTypeToString(EIdType aType);
    static EIdType StringToIdType(const Brx& aString);
private:
    void ParseQobuzMetadata(TBool aHasParentMetadata, const ParentMetadata& aJsonResponse, const OpenHome::Brx& aTrackObj);
private:
    OpenHome::Media::TrackFactory& iTrackFactory;
    OpenHome::Media::BwsTrackUri iTrackUri;
    OpenHome::Media::BwsTrackMetaData iMetaDataDidl;
};

} // namespace Av
} // namespace OpenHome

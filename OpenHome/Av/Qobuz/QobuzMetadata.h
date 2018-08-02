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
    static const OpenHome::Brn kGenreNone;
    enum EIdType {
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
        ePath, // workaround for now to use path url supplied by control point
    };
public:
    QobuzMetadata(OpenHome::Media::TrackFactory& aTrackFactory);
    OpenHome::Media::Track* TrackFromJson(const OpenHome::Brx& aJsonResponse, const OpenHome::Brx& aTrackObj, EIdType aType);
    static Brn FirstIdFromJson(const OpenHome::Brx& aJsonResponse, EIdType aType);
    static Brn GenreIdFromJson(const OpenHome::Brx& aJsonResponse, const OpenHome::Brx& aGenre);
    static const Brx& IdTypeToString(EIdType aType);
private:
    void ParseQobuzMetadata(const OpenHome::Brx& aJsonResponse, const OpenHome::Brx& aTrackObj, EIdType aType);
    void TryAddAttribute(OpenHome::JsonParser& aParser,
                         const TChar* aQobuzKey, const TChar* aDidlAttr);
    void TryAddAttribute(const TChar* aValue, const TChar* aDidlAttr);
    void TryAddTagsFromArray(OpenHome::JsonParser& aParser,
                             const OpenHome::Brx& aQobuzKey, const OpenHome::Brx& aDidlTag,
                             const OpenHome::Brx& aNs, const OpenHome::Brx& aRole);
    void TryAddTagFromObj(OpenHome::JsonParser& aParser,
                             const OpenHome::Brx& aQobuzKey, const OpenHome::Brx& aDidlTag,
                             const OpenHome::Brx& aNs, const Brx& aQobuzSubKey);
    void TryAddTagFromObj(OpenHome::JsonParser& aParser,
                             const OpenHome::Brx& aQobuzKey, const OpenHome::Brx& aDidlTag,
                             const OpenHome::Brx& aNs, const Brx& aQobuzSubKey, const Brx& aRole);
    void TryAddTag(OpenHome::JsonParser& aParser, const OpenHome::Brx& aQobuzKey,
                   const OpenHome::Brx& aDidlTag, const OpenHome::Brx& aNs);
    void TryAddTag(const OpenHome::Brx& aDidlTag, const OpenHome::Brx& aNs,
                   const OpenHome::Brx& aRole, const OpenHome::Brx& aValue);
    void TryAppend(const TChar* aStr);
    void TryAppend(const OpenHome::Brx& aBuf);
private:
    OpenHome::Media::TrackFactory& iTrackFactory;
    OpenHome::Media::BwsTrackUri iTrackUri;
    OpenHome::Media::BwsTrackMetaData iMetaDataDidl;
};

} // namespace Av
} // namespace OpenHome

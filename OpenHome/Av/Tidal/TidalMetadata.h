#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Media/Pipeline/Msg.h>

EXCEPTION(TidalResponseInvalid);
EXCEPTION(TidalRequestInvalid);

namespace OpenHome {
    class Environment;
    class JsonParser;
namespace Av {

class TidalMetadata : private OpenHome::INonCopyable
{
    static const OpenHome::Brn kNsDc;
    static const OpenHome::Brn kNsUpnp;
    static const OpenHome::Brn kNsOh;
    static const OpenHome::Brn kImageResourceBaseUrl;
    static const OpenHome::Brn kImageResourceResolutionLow;
    static const OpenHome::Brn kImageResourceResolutionMed;
    static const OpenHome::Brn kImageResourceResolutionHigh;
    static const OpenHome::Brn kImageResourceExtension;
    static const OpenHome::Brn kIdTypeArtist;
    static const OpenHome::Brn kIdTypeAlbum;
    static const OpenHome::Brn kIdTypeTrack;
    static const OpenHome::Brn kIdTypePlaylist;
    static const OpenHome::Brn kIdTypeGenre;
    static const OpenHome::Brn kIdTypeMood;
    // user specific
    static const OpenHome::Brn kIdTypeSavedPlaylist;
    static const OpenHome::Brn kIdTypeFavorites;
    // smart types
    static const OpenHome::Brn kSmartTypeNew;
    static const OpenHome::Brn kSmartTypeRecommended;
    static const OpenHome::Brn kSmartTypeTop20;
    static const OpenHome::Brn kSmartTypeExclusive;
    static const OpenHome::Brn kSmartTypeRising;
    static const OpenHome::Brn kSmartTypeDiscovery;
public:
    static const OpenHome::Brn kIdTypeSmart;
    static const OpenHome::Brn kIdTypeUserSpecific;
    enum EIdType {
        eArtist,
        eAlbum,
        eTrack,
        ePlaylist,
        eSavedPlaylist,
        eFavorites,
        eGenre,
        eMood,
        eSmartNew,
        eSmartRecommended,
        eSmartTop20,
        eSmartExclusive,
        eSmartRising,
        eSmartDiscovery,
        ePath, // workaround for now to use path url supplied by control point
    };
    enum EImageResolution {
        eNone,
        eLow,
        eMed,
        eHigh
    };
public:
    TidalMetadata(OpenHome::Media::TrackFactory& aTrackFactory);
    OpenHome::Media::Track* TrackFromJson(const OpenHome::Brx& aMetadata);
    static Brn FirstIdFromJson(const OpenHome::Brx& aJsonResponse, EIdType aType);
    static const Brx& IdTypeToString(EIdType aType);
private:
    void ParseTidalMetadata(const OpenHome::Brx& aMetadata);
    void TryAddAttribute(OpenHome::JsonParser& aParser,
                         const TChar* aTidalKey, const TChar* aDidlAttr);
    void TryAddAttribute(const TChar* aValue, const TChar* aDidlAttr);
    void TryAddTagsFromArray(OpenHome::JsonParser& aParser,
                             const OpenHome::Brx& aTidalKey, const OpenHome::Brx& aDidlTag,
                             const OpenHome::Brx& aNs, const OpenHome::Brx& aRole);
    void TryAddTagsFromObj(OpenHome::JsonParser& aParser,
                             const OpenHome::Brx& aTidalKey, const OpenHome::Brx& aDidlTag,
                             const OpenHome::Brx& aNs, const Brx& aTidalSubKey, TBool aIsImage, EImageResolution aResolution);
    void TryAddTag(OpenHome::JsonParser& aParser, const OpenHome::Brx& aTidalKey,
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

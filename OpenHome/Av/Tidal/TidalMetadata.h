#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Av/OhMetadata.h>
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
    static const OpenHome::Brn kIdTypeNone;
    enum EIdType {
        eNone,
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
    };
public:
    TidalMetadata(OpenHome::Media::TrackFactory& aTrackFactory);
    OpenHome::Media::Track* TrackFromJson(const OpenHome::Brx& aMetadata,
                                          const OpenHome::Brx& aTokenId);
    static const Brx& IdTypeToString(EIdType aType);
    static EIdType StringToIdType(const Brx& aString);
private:
    void ParseTidalMetadata(const OpenHome::Brx& aMetadata,
                            const OpenHome::Brx& aTokenId);

    void TryWriteArtwork(WriterDIDLLite& aWriter,
                         JsonParser& aParser);
private:
    OpenHome::Media::TrackFactory& iTrackFactory;
    OpenHome::Media::BwsTrackUri iTrackUri;
    OpenHome::Media::BwsTrackMetaData iMetaDataDidl;
};

} // namespace Av
} // namespace OpenHome

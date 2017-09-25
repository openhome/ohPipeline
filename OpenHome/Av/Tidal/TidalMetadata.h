#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Media/Pipeline/Msg.h>


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
public:
    TidalMetadata(OpenHome::Media::TrackFactory& aTrackFactory);
    OpenHome::Media::Track* TrackFromJson(const OpenHome::Brx& aMetadata);
    const OpenHome::Brx& FirstIdFromJson(const OpenHome::Brx& aJsonResponse);
    enum EImageResolution {
        eNone,
        eLow,
        eMed,
        eHigh
    };
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

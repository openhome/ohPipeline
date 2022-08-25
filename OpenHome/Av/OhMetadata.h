#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Media/Pipeline/Msg.h>

#include <string>
#include <vector>
#include <utility>

namespace OpenHome {
namespace Av {

class WriterDIDLXml;

typedef std::vector<std::pair<std::string, std::string>> OpenHomeMetadata;
typedef std::vector<std::pair<Brn, Brn>> OpenHomeMetadataBuf;

class OhMetadata : private INonCopyable
{
public:
    static Media::Track* ToTrack(const OpenHomeMetadataBuf& aMetadata,
                                 Media::TrackFactory& aTrackFactory);
    static void ToDidlLite(const OpenHomeMetadataBuf& aMetadata, Bwx& aDidl);
    static void ToUriDidlLite(const OpenHomeMetadataBuf& aMetadata, Bwx& aUri, Bwx& aDidl);
private:
    OhMetadata(const OpenHomeMetadataBuf& aMetadata);
    void Parse();
    TBool TryGetValue(const TChar* aKey, Brn& aValue) const;
    TBool TryGetValue(const Brx& aKey, Brn& aValue) const;
private:
    const OpenHomeMetadataBuf& iMetadata;
    Media::BwsTrackUri iUri;
    Media::BwsTrackMetaData iMetaDataDidl;
};

} // namespace Av
} // namespace OpenHome

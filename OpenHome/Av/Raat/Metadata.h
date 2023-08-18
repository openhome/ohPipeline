#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Pipeline/AsyncTrackReporter.h>

namespace OpenHome {
namespace Av {

class RaatMetadata
    : public Media::IAsyncMetadata
{
public:
    static const TUint kMaxMetadataSize = 255;
public:
    RaatMetadata();
public:
    void SetTitle(const Brx& aTitle);
    void SetSubtitle(const Brx& Subtitle);
    void SetSubSubtitle(const Brx& SubSubtitle);
    void SetDurationMs(TUint aDurationMs);

    const Brx& Title() const;
    const Brx& Subtitle() const;
    const Brx& SubSubtitle() const;

    void Clear();
    TBool operator==(const RaatMetadata& aMetadata) const;

public: // from IAsyncMetadata
    TUint DurationMs() const override;
private:
    Bws<kMaxMetadataSize> iTitle;
    Bws<kMaxMetadataSize> iSubtitle;
    Bws<kMaxMetadataSize> iSubSubtitle;
    TUint iDurationMs;
};

class RaatMetadataAllocated
    : public Media::Allocated
    , public Media::IAsyncMetadataAllocated
{
public:
    RaatMetadataAllocated(Media::AllocatorBase& aAllocator);
public:
    void SetTitle(const Brx& aTitle);
    void SetSubtitle(const Brx& Subtitle);
    void SetSubSubtitle(const Brx& SubSubtitle);
    void SetDurationMs(TUint aDurationMs);

    TBool operator==(const RaatMetadataAllocated& aMetadata) const;
public: // from IAsyncMetadataAllocated
    const Media::IAsyncMetadata& Metadata() const override;
    void AddReference() override;
    void RemoveReference() override;
private: // from Allocated
    void Clear() override;
private:
    RaatMetadata iMetadata;
};

class RaatTrackInfo;
class RaatMetadataHandler : public Media::IAsyncTrackClient
{
private:
    static const Brn kMode;
    static const TUint kMsPerSec = 1000;
    static const TUint kMaxMetadataCount = 2;
public:
    RaatMetadataHandler(
        Media::IAsyncTrackReporter& aAsyncTrackReporter,
        IInfoAggregator&            aInfoAggregator);

public: // from IAsyncTrackClient
    const Brx& Mode() const override;
    void WriteMetadata(
        const Brx&                      aTrackUri,
        const Media::IAsyncMetadata&    aMetadata,
        const Media::DecodedStreamInfo& aStreamInfo,
        IWriter&                        aWriter) override;
public:
    void TrackInfoChanged(const RaatTrackInfo& aTrackInfo);
private:
    Media::IAsyncTrackReporter&             iTrackReporter;
    Media::Allocator<RaatMetadataAllocated> iAllocatorMetadata;
    RaatMetadataAllocated*                  iMetadata;
    TUint                                   iTrackPositionSecs;
};


}
}
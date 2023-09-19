#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Pipeline/AsyncTrackObserver.h>
#include <OpenHome/Media/ArtworkServer.h>

namespace OpenHome {
namespace Av {

class RaatMetadata
    : public Media::IAsyncMetadata
{
public:
    RaatMetadata();
public:
    void SetTitle(const Brx& aTitle);
    void SetSubtitle(const Brx& Subtitle);
    void SetSubSubtitle(const Brx& SubSubtitle);
    void SetArtworkUri(const Brx& aUri);
    void SetDurationMs(TUint aDurationMs);

    const Brx& Title() const;
    const Brx& Subtitle() const;
    const Brx& SubSubtitle() const;
    const Brx& ArtworkUri() const;

    void Clear();
    TBool operator==(const RaatMetadata& aMetadata) const;

public: // from IAsyncMetadata
    TUint DurationMs() const override;
private:
    Bwh iTitle;
    Bwh iSubtitle;
    Bwh iSubSubtitle;
    Bwh iArtworkUri;
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
    void SetArtworkUri(const Brx& aUri);
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
class RaatMetadataHandler
    : public Media::IAsyncTrackClient
    , public Media::IArtworkServerObserver
{
private:
    static const Brn kMode;
    static const TUint kMsPerSec = 1000;
    static const TUint kMaxMetadataCount = 2;
public:
    RaatMetadataHandler(
        Media::IAsyncTrackObserver& aAsyncTrackObserver,
        IInfoAggregator&            aInfoAggregator,
        Media::IArtworkServer&      aArtworkServer);
    ~RaatMetadataHandler();

public: // from IAsyncTrackClient
    const Brx& Mode() const override;
    TBool ForceDecodedStream() const override;
    void WriteMetadata(
        const Brx&                      aTrackUri,
        const Media::IAsyncMetadata&    aMetadata,
        const Media::DecodedStreamInfo& aStreamInfo,
        IWriter&                        aWriter) override;
public: // from IArtworkServerObserver
    void ArtworkChanged(const Brx& aUri) override;
public:
    void TrackInfoChanged(const RaatTrackInfo& aTrackInfo);
private:
    Media::IAsyncTrackObserver&             iTrackObserver;
    Media::Allocator<RaatMetadataAllocated> iAllocatorMetadata;
    Media::IArtworkServer&                  iArtworkServer;
    RaatMetadataAllocated*                  iMetadata;
    TUint                                   iTrackPositionSecs;
};


}
}
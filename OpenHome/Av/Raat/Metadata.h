#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Pipeline/AsyncTrackObserver.h>
#include <OpenHome/Media/ArtworkServer.h>

namespace OpenHome {
namespace Av {

class RaatMetadata
{
public:
    RaatMetadata();
    RaatMetadata(
        const Brx& aTitle,
        const Brx& aSubtitle,
        const Brx& aSubSubtitle);
public:
    const Brx& Title() const;
    const Brx& Subtitle() const;
    const Brx& SubSubtitle() const;
public:
    TBool operator==(const RaatMetadata& aMetadata) const;
    void operator=(const RaatMetadata& aMetadata);
private:
    Bwh iTitle;
    Bwh iSubtitle;
    Bwh iSubSubtitle;
};

class RaatTrackBoundary : public Media::IAsyncTrackBoundary
{
public:
    RaatTrackBoundary();
public:
    void Set(TUint aOffsetMs, TUint aDurationMs);
public: // IAsyncTrackBoundary
    const Brx& Mode() const override;
    TUint OffsetMs() const override;
    TUint DurationMs() const override;
private:
    TUint iOffsetMs;
    TUint iDurationMs;
};

class RaatTrackPosition : public Media::IAsyncTrackPosition
{
public:
    RaatTrackPosition(TUint aPositionMs);
public: // IAsyncTrackPosition
    const Brx& Mode() const override;
    TUint PositionMs() const override;
private:
    TUint iPositionMs;
};

class RaatTrackInfo;
class RaatMetadataHandler
    : public Media::IArtworkServerObserver
    , public Media::IAsyncTrackClient

{
public:
    static const Brn kMode;
private:
    static const TUint kMsPerSec = 1000;
public:
    RaatMetadataHandler(Media::IAsyncTrackObserver& aAsyncTrackObserver, Media::IArtworkServer& aArtworkServer);
public:
    void TrackInfoChanged(const RaatTrackInfo& aTrackInfo);
public: // from IArtworkServerObserver
    void ArtworkChanged(const Brx& aUri) override;
public: // from IAsyncTrackClient
    const Brx& Mode() const override;
    void WriteMetadata(const Brx& aTrackUri, const Media::DecodedStreamInfo& aStreamInfo, IWriter& aWriter) override;
    const Media::IAsyncTrackBoundary& GetTrackBoundary() override;
private:
    Media::IAsyncTrackObserver& iTrackObserver;
    Media::IArtworkServer& iArtworkServer;
    Mutex iLock;
    RaatMetadata iMetadata;
    RaatTrackBoundary iBoundary;
    Bwh iArtworkUri;
};


}
}
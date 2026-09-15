#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Pipeline/AsyncTrackObserver.h>

namespace OpenHome {
namespace Av {

class QobuzConnectMetadata
{
public:
    QobuzConnectMetadata();
    QobuzConnectMetadata(
        const Brx& aTitle,
        const Brx& aArtist,
        const Brx& aAlbum,
        const Brx& aArtworkUri);
public:
    const Brx& Title() const;
    const Brx& Artist() const;
    const Brx& Album() const;
    const Brx& ArtworkUri() const;
public:
    TBool operator==(const QobuzConnectMetadata& aMetadata) const;
    TBool operator!=(const QobuzConnectMetadata& aMetadata) const;
    void operator=(const QobuzConnectMetadata& aMetadata);
private:
    Bwh iTitle;
    Bwh iArtist;
    Bwh iAlbum;
    Bwh iArtworkUri;
};

/**
 * A fixed (0, 0) offset/duration - Qobuz Connect's stream_metadata_callback (see
 * QbzAudioMetadata) carries title/artist/album/artwork only, no duration, so unlike RAAT's
 * equivalent (RaatTrackBoundary, driven by Roon's own transport position updates) there's no
 * source for a real boundary here yet. Reporting (0, 0) is the same "duration unknown" state
 * already used elsewhere in this codebase for sources with no duration info (e.g. internet radio).
 */
class QobuzConnectTrackBoundary : public Media::IAsyncTrackBoundary
{
public: // IAsyncTrackBoundary
    const Brx& Mode() const override;
    TUint OffsetMs() const override;
    TUint DurationMs() const override;
};

/**
 * Bridges Qobuz Connect SDK's stream_metadata_callback (title/artist/album/artwork URL - see
 * QbzAudioMetadata) to the Pipeline's generic IAsyncTrackObserver/IAsyncTrackClient mechanism,
 * which lets "now playing" metadata update without disturbing playback - unlike re-pushing a new
 * track via UriProvider::SetTrack(), which would trigger a fresh Pipeline Begin()/track
 * transition on Qobuz Connect's single persistent placeholder URI. Mirrors RaatMetadataHandler
 * (OpenHome/Av/Raat/Metadata.h) structurally, but without RAAT's artwork-server/track-position
 * machinery: Qobuz Connect's album_art_url is already a plain remote URL (unlike RAAT, which
 * pushes raw artwork bytes via its own callback and needs ArtworkHttpServer to host them
 * locally), and there's no source for live position/duration updates (see
 * QobuzConnectTrackBoundary).
 */
class QobuzConnectMetadataHandler : public Media::IAsyncTrackClient
{
public:
    static const Brn kMode;
public:
    QobuzConnectMetadataHandler(Media::IAsyncTrackObserver& aTrackObserver);
public:
    void MetadataChanged(
        const Brx& aTitle,
        const Brx& aArtist,
        const Brx& aAlbum,
        const Brx& aArtworkUri);
public: // from IAsyncTrackClient
    const Brx& Mode() const override;
    void WriteMetadata(const Brx& aTrackUri, const Media::DecodedStreamInfo& aStreamInfo, IWriter& aWriter) override;
    const Media::IAsyncTrackBoundary& GetTrackBoundary() override;
private:
    Media::IAsyncTrackObserver& iTrackObserver;
    Mutex iLock;
    QobuzConnectMetadata iMetadata;
    QobuzConnectTrackBoundary iBoundary;
};

} // namespace Av
} // namespace OpenHome

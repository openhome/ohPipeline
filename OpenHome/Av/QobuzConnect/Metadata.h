#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Pipeline/AsyncTrackObserver.h>
#include <OpenHome/Av/QobuzConnect/AudioStream.h>

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
 * Qobuz Connect's stream_metadata_callback (see QbzAudioMetadata) carries title/artist/album/
 * artwork only, no duration/position - unlike RAAT's equivalent (RaatTrackBoundary, driven by
 * Roon's own transport position updates), there's no boundary information to hand over directly
 * here. But QobuzConnectAudioStream (reached via the same IQobuzConnectAudioReader
 * ProtocolQobuzConnect itself reads from) already tracks exactly this - the SDK's own
 * QbzAudioStreamStartedCallback carries a track's real starting position and
 * QbzAudioStreamProperties.duration - so this simply reads the same values back out.
 *
 * Without this, whenever WriteMetadata()/TrackMetadataChanged() runs for a new track (which
 * happens moments after the audio stream itself starts, since Qobuz Connect delivers metadata
 * shortly after stream_started_callback), AsyncTrackObserver::UpdateDecodedStreamLocked() was
 * rebuilding the Pipeline's MsgDecodedStream from this boundary's (previously always 0, 0)
 * offset/duration - overwriting the correct values ProtocolQobuzConnect::OutputStream() had just
 * set moments earlier. Confirmed on hardware: the Pipeline briefly reported the right duration,
 * then reset straight back to 0 a couple of milliseconds later, every time.
 */
class QobuzConnectTrackBoundary : public Media::IAsyncTrackBoundary
{
public:
    QobuzConnectTrackBoundary(IQobuzConnectAudioReader& aReader);
public: // IAsyncTrackBoundary
    const Brx& Mode() const override;
    TUint OffsetMs() const override;
    TUint DurationMs() const override;
private:
    IQobuzConnectAudioReader& iReader;
};

/**
 * Bridges Qobuz Connect SDK's stream_metadata_callback (title/artist/album/artwork URL - see
 * QbzAudioMetadata) to the Pipeline's generic IAsyncTrackObserver/IAsyncTrackClient mechanism,
 * which lets "now playing" metadata update without disturbing playback - unlike re-pushing a new
 * track via UriProvider::SetTrack(), which would trigger a fresh Pipeline Begin()/track
 * transition on Qobuz Connect's single persistent placeholder URI. Mirrors RaatMetadataHandler
 * (OpenHome/Av/Raat/Metadata.h) structurally, but without RAAT's artwork-server machinery:
 * Qobuz Connect's album_art_url is already a plain remote URL (unlike RAAT, which pushes raw
 * artwork bytes via its own callback and needs ArtworkHttpServer to host them locally). Real
 * position/duration for iBoundary comes from the same aReader SourceQobuzConnect's Protocol/
 * AudioStream use - see QobuzConnectTrackBoundary's comment.
 */
class QobuzConnectMetadataHandler : public Media::IAsyncTrackClient
{
public:
    static const Brn kMode;
public:
    QobuzConnectMetadataHandler(Media::IAsyncTrackObserver& aTrackObserver, IQobuzConnectAudioReader& aReader);
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

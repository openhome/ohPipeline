#include <OpenHome/Av/QobuzConnect/Metadata.h>
#include <OpenHome/DidlLite.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;

// QobuzConnectMetadata

QobuzConnectMetadata::QobuzConnectMetadata()
    : iTitle(Brx::Empty())
    , iArtist(Brx::Empty())
    , iAlbum(Brx::Empty())
    , iArtworkUri(Brx::Empty())
{
}

QobuzConnectMetadata::QobuzConnectMetadata(
    const Brx& aTitle,
    const Brx& aArtist,
    const Brx& aAlbum,
    const Brx& aArtworkUri)

    : iTitle(aTitle)
    , iArtist(aArtist)
    , iAlbum(aAlbum)
    , iArtworkUri(aArtworkUri)
{
}

const Brx& QobuzConnectMetadata::Title() const
{
    return iTitle;
}

const Brx& QobuzConnectMetadata::Artist() const
{
    return iArtist;
}

const Brx& QobuzConnectMetadata::Album() const
{
    return iAlbum;
}

const Brx& QobuzConnectMetadata::ArtworkUri() const
{
    return iArtworkUri;
}

TBool QobuzConnectMetadata::operator==(const QobuzConnectMetadata& aMetadata) const
{
    return aMetadata.iTitle == iTitle
        && aMetadata.iArtist == iArtist
        && aMetadata.iAlbum == iAlbum
        && aMetadata.iArtworkUri == iArtworkUri;
}

TBool QobuzConnectMetadata::operator!=(const QobuzConnectMetadata& aMetadata) const
{
    return !(aMetadata == *this);
}

void QobuzConnectMetadata::operator=(const QobuzConnectMetadata& aMetadata)
{
    iTitle.Grow(aMetadata.Title().Bytes());
    iTitle.Replace(aMetadata.Title());

    iArtist.Grow(aMetadata.Artist().Bytes());
    iArtist.Replace(aMetadata.Artist());

    iAlbum.Grow(aMetadata.Album().Bytes());
    iAlbum.Replace(aMetadata.Album());

    iArtworkUri.Grow(aMetadata.ArtworkUri().Bytes());
    iArtworkUri.Replace(aMetadata.ArtworkUri());
}


// QobuzConnectTrackBoundary

QobuzConnectTrackBoundary::QobuzConnectTrackBoundary(IQobuzConnectAudioReader& aReader)
    : iReader(aReader)
{
}

const Brx& QobuzConnectTrackBoundary::Mode() const
{
    return QobuzConnectMetadataHandler::kMode;
}

TUint QobuzConnectTrackBoundary::OffsetMs() const
{
    return (TUint)iReader.InitialPositionMs();
}

TUint QobuzConnectTrackBoundary::DurationMs() const
{
    return (TUint)iReader.DurationMs();
}


// QobuzConnectMetadataHandler

const Brn QobuzConnectMetadataHandler::kMode("QOBUZCONNECT");

QobuzConnectMetadataHandler::QobuzConnectMetadataHandler(IAsyncTrackObserver& aTrackObserver, IQobuzConnectAudioReader& aReader)
    : iTrackObserver(aTrackObserver)
    , iLock("QCMD")
    , iBoundary(aReader)
{
    iTrackObserver.AddClient(*this);
}

void QobuzConnectMetadataHandler::MetadataChanged(
    const Brx& aTitle,
    const Brx& aArtist,
    const Brx& aAlbum,
    const Brx& aArtworkUri)
{
    QobuzConnectMetadata metadata(aTitle, aArtist, aAlbum, aArtworkUri);
    TBool changed = false;
    {
        AutoMutex _(iLock);
        if (iMetadata != metadata) {
            iMetadata = metadata;
            changed = true;
        }
    }
    if (changed) {
        iTrackObserver.TrackMetadataChanged(kMode);
    }
}

const Brx& QobuzConnectMetadataHandler::Mode() const
{
    return kMode;
}

void QobuzConnectMetadataHandler::WriteMetadata(const Brx& aTrackUri, const DecodedStreamInfo& aStreamInfo, IWriter& aWriter)
{
    AutoMutex _(iLock);
    static const Brn kItemId("0");
    static const Brn kParentId("0");
    static const Brn kProtocolInfo("qobuzconnect:*:audio/*:*");

    WriterDIDLLite writer(kItemId, DIDLLite::kItemTypeTrack, kParentId, aWriter);
    writer.WriteTitle(iMetadata.Title());
    writer.WriteArtist(iMetadata.Artist());
    writer.WriteAlbum(iMetadata.Album());
    writer.WriteArtwork(iMetadata.ArtworkUri());

    WriterDIDLLite::StreamingDetails details;
    details.sampleRate = aStreamInfo.SampleRate();
    details.numberOfChannels = aStreamInfo.NumChannels();
    details.bitDepth = aStreamInfo.BitDepth();
    details.duration = iBoundary.DurationMs();
    details.durationResolution = EDurationResolution::Milliseconds;

    writer.WriteStreamingDetails(kProtocolInfo, details, aTrackUri);
    writer.WriteEnd();
}

const IAsyncTrackBoundary& QobuzConnectMetadataHandler::GetTrackBoundary()
{
    return iBoundary;
}

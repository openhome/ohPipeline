#include <OpenHome/Av/Raat/Metadata.h>
#include <OpenHome/Av/Raat/Transport.h>
#include <OpenHome/Av/OhMetadata.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;

// RaatMetadata

RaatMetadata::RaatMetadata()
    : iTitle(Brx::Empty())
    , iSubtitle(Brx::Empty())
    , iSubSubtitle(Brx::Empty())
{
}

RaatMetadata::RaatMetadata(
    const Brx& aTitle,
    const Brx& aSubtitle,
    const Brx& aSubSubtitle)

    : iTitle(aTitle)
    , iSubtitle(aSubtitle)
    , iSubSubtitle(aSubSubtitle)
{
}

const Brx& RaatMetadata::Title() const
{
    return iTitle;
}

const Brx& RaatMetadata::Subtitle() const
{
    return iSubtitle;
}

const Brx& RaatMetadata::SubSubtitle() const
{
    return iSubSubtitle;
}

TBool RaatMetadata::operator==(const RaatMetadata& aMetadata) const
{
    /* This function is used to determine if the metadata has changed
     * Raat track info and artwork arrive asynchronously so here we
     * only check if track info has changed and allow artwork to arrive
     * and be processed independently
     */
    return aMetadata.iTitle == iTitle
        && aMetadata.iSubtitle == iSubtitle
        && aMetadata.iSubSubtitle == iSubSubtitle;
}

void RaatMetadata::operator=(const RaatMetadata& aMetadata)
{
    iTitle.Grow(aMetadata.Title().Bytes());
    iTitle.Replace(aMetadata.Title());

    iSubtitle.Grow(aMetadata.Subtitle().Bytes());
    iSubtitle.Replace(aMetadata.Subtitle());

    iSubSubtitle.Grow(aMetadata.SubSubtitle().Bytes());
    iSubSubtitle.Replace(aMetadata.SubSubtitle());
}


// RaatTrackBoundary

RaatTrackBoundary::RaatTrackBoundary()
    : iOffsetMs(0)
    , iDurationMs(0)
{
}

void RaatTrackBoundary::Set(TUint aOffsetMs, TUint aDurationMs)
{
    iOffsetMs = aOffsetMs;
    iDurationMs = aDurationMs;
}

const Brx& RaatTrackBoundary::Mode() const
{
    return RaatMetadataHandler::kMode;
}

TUint RaatTrackBoundary::OffsetMs() const
{
    return iOffsetMs;
}

TUint RaatTrackBoundary::DurationMs() const
{
    return iDurationMs;
}


// RaatTrackPosition

RaatTrackPosition::RaatTrackPosition(TUint aPositionMs)
    : iPositionMs(aPositionMs)
{
}

const Brx& RaatTrackPosition::Mode() const
{
    return RaatMetadataHandler::kMode;
}

TUint RaatTrackPosition::PositionMs() const
{
    return iPositionMs;
}


// RaatMetadataHandler

const Brn RaatMetadataHandler::kMode("RAAT");

RaatMetadataHandler::RaatMetadataHandler(IAsyncTrackObserver& aTrackObserver, IArtworkServer& aArtworkServer)
    : iTrackObserver(aTrackObserver)
    , iArtworkServer(aArtworkServer)
    , iLock("RAMD")
{
    iTrackObserver.AddClient(*this);
    iArtworkServer.AddObserver(*this);
}

void RaatMetadataHandler::TrackInfoChanged(const RaatTrackInfo& aTrackInfo)
{
    AutoMutex _(iLock);
    const TUint kPositionMs = aTrackInfo.GetPositionSecs() * kMsPerSec;
    const TUint kDurationMs = aTrackInfo.GetDurationSecs() * kMsPerSec;
    if (kDurationMs != 0 && kDurationMs != iBoundary.DurationMs()) {
        iBoundary.Set(kPositionMs, kDurationMs);
        iTrackObserver.TrackBoundaryChanged(iBoundary);
    }
    else {
        iTrackObserver.TrackPositionChanged(RaatTrackPosition(kPositionMs));
    }
    iBoundary.Set(kPositionMs, iBoundary.DurationMs());

    RaatMetadata metadata(
        aTrackInfo.GetTitle(),
        aTrackInfo.GetSubtitle(),
        aTrackInfo.GetSubSubtitle());

    if (iMetadata == metadata) {
        return;
    }

    iMetadata = metadata;
    iTrackObserver.TrackMetadataChanged(kMode);
}

void RaatMetadataHandler::ArtworkChanged(const Brx& aUri)
{
    AutoMutex _(iLock);
    if (aUri == Brx::Empty()) {
        iArtworkUri.SetBytes(0);
    }
    else {
        iArtworkUri.Grow(aUri.Bytes());
        iArtworkUri.Replace(aUri);
    }
    iTrackObserver.TrackMetadataChanged(kMode);
}

const Brx& RaatMetadataHandler::Mode() const
{
    return kMode;
}

void RaatMetadataHandler::WriteMetadata(const Brx& aTrackUri, const DecodedStreamInfo& aStreamInfo, IWriter& aWriter)
{
    AutoMutex _(iLock);
    static const Brn kItemId("0");
    static const Brn kParentId("0");
    static const Brn kProtocolInfo("raat:*:audio/L16:*");

    WriterDIDLLite writer(kItemId, DIDLLite::kItemTypeTrack, kParentId, aWriter);
    writer.WriteTitle(iMetadata.Title());
    writer.WriteArtist(iMetadata.Subtitle());
    writer.WriteAlbum(iMetadata.SubSubtitle());
    writer.WriteArtwork(iArtworkUri);

    WriterDIDLLite::StreamingDetails details;
    details.sampleRate = aStreamInfo.SampleRate();
    details.numberOfChannels = aStreamInfo.NumChannels();
    details.bitDepth = aStreamInfo.BitDepth();
    details.duration = iBoundary.DurationMs();
    details.durationResolution = EDurationResolution::Milliseconds;

    writer.WriteStreamingDetails(kProtocolInfo, details, aTrackUri);
    writer.WriteEnd();
}

const IAsyncTrackBoundary& RaatMetadataHandler::GetTrackBoundary()
{
    AutoMutex _(iLock);
    return iBoundary;
}


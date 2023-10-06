#include <OpenHome/Av/Raat/Metadata.h>
#include <OpenHome/Av/Raat/Transport.h>
#include <OpenHome/Av/OhMetadata.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;

// RaatMetadata

RaatMetadata::RaatMetadata()
    : iDurationMs(0)
{
    Clear();
}

void RaatMetadata::SetTitle(const Brx& aTitle)
{
    iTitle.Grow(aTitle.Bytes());
    iTitle.Replace(aTitle);
}

void RaatMetadata::SetSubtitle(const Brx& aSubtitle)
{
    iSubtitle.Grow(aSubtitle.Bytes());
    iSubtitle.Replace(aSubtitle);
}

void RaatMetadata::SetSubSubtitle(const Brx& aSubSubtitle)
{
    iSubSubtitle.Grow(aSubSubtitle.Bytes());
    iSubSubtitle.Replace(aSubSubtitle);
}

void RaatMetadata::SetArtworkUri(const Brx& aUri)
{
    iArtworkUri.Grow(aUri.Bytes());
    iArtworkUri.Replace(aUri);
}

void RaatMetadata::SetDurationMs(TUint aDurationMs)
{
    iDurationMs = aDurationMs;
}

void RaatMetadata::Clear()
{
    iTitle.SetBytes(0);
    iSubtitle.SetBytes(0);
    iSubSubtitle.SetBytes(0);
    iArtworkUri.SetBytes(0);
    iDurationMs = 0;
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
        && aMetadata.iSubSubtitle == iSubSubtitle
        && aMetadata.iDurationMs == iDurationMs;
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

const Brx& RaatMetadata::ArtworkUri() const
{
    return iArtworkUri;
}

TUint RaatMetadata::DurationMs() const
{
    return iDurationMs;
}


// RaatMetadataAllocated

RaatMetadataAllocated::RaatMetadataAllocated(OpenHome::Media::AllocatorBase& aAllocator)
    : Allocated(aAllocator)
{
}

void RaatMetadataAllocated::SetTitle(const Brx& aTitle)
{
    iMetadata.SetTitle(aTitle);
}

void RaatMetadataAllocated::SetSubtitle(const Brx& aSubtitle)
{
    iMetadata.SetSubtitle(aSubtitle);
}

void RaatMetadataAllocated::SetSubSubtitle(const Brx& aSubSubtitle)
{
    iMetadata.SetSubSubtitle(aSubSubtitle);
}

void RaatMetadataAllocated::SetArtworkUri(const Brx& aUri)
{
    iMetadata.SetArtworkUri(aUri);
}

void RaatMetadataAllocated::SetDurationMs(TUint aDurationMs)
{
    iMetadata.SetDurationMs(aDurationMs);
}

TBool RaatMetadataAllocated::operator==(const RaatMetadataAllocated& aMetadata) const
{
    return (static_cast<const RaatMetadata&>(aMetadata.Metadata()) == iMetadata);
}

const Media::IAsyncMetadata& RaatMetadataAllocated::Metadata() const
{
    return iMetadata;
}

void RaatMetadataAllocated::AddReference()
{
    AddRef();
}

void RaatMetadataAllocated::RemoveReference()
{
    RemoveRef();
}

void RaatMetadataAllocated::Clear()
{
    iMetadata.Clear();
}


// RaatMetadataHandler

const Brn RaatMetadataHandler::kMode("RAAT");

RaatMetadataHandler::RaatMetadataHandler(
    IAsyncTrackReporter&    aTrackReporter,
    IInfoAggregator&        aInfoAggregator,
    IRaatArtworkServer&     aArtworkServer)

    : iTrackReporter(aTrackReporter)
    , iAllocatorMetadata("RaatMetadata", kMaxMetadataCount, aInfoAggregator)
    , iArtworkServer(aArtworkServer)
    , iMetadata(nullptr)
    , iTrackPositionSecs(0)
{
    iTrackReporter.AddClient(*this);
    iArtworkServer.AddObserver(*this);
}

const Brx& RaatMetadataHandler::Mode() const
{
    return kMode;
}

void RaatMetadataHandler::WriteMetadata(
    const Brx&                      aTrackUri,
    const Media::IAsyncMetadata&    aMetadata,
    const Media::DecodedStreamInfo& aStreamInfo,
    IWriter&                        aWriter)
{
    static const Brn kItemId("0");
    static const Brn kParentId("0");
    static const Brn kProtocolInfo("raat:*:audio/L16:*");

    WriterDIDLLite writer(kItemId, DIDLLite::kItemTypeTrack, kParentId, aWriter);
    const auto& metadata = static_cast<const RaatMetadata&>(aMetadata);

    writer.WriteTitle(metadata.Title());
    writer.WriteArtist(metadata.Subtitle());
    writer.WriteAlbum(metadata.SubSubtitle());
    writer.WriteArtwork(metadata.ArtworkUri());

    WriterDIDLLite::StreamingDetails details;
    details.sampleRate = aStreamInfo.SampleRate();
    details.numberOfChannels = aStreamInfo.NumChannels();
    details.bitDepth = aStreamInfo.BitDepth();
    details.duration = metadata.DurationMs();
    details.durationResolution = EDurationResolution::Milliseconds;

    writer.WriteStreamingDetails(kProtocolInfo, details, aTrackUri);
    writer.WriteEnd();
}

void RaatMetadataHandler::ArtworkChanged(const Brx& aUri)
{
    if (iMetadata != nullptr) {
        iMetadata->AddReference();
        iMetadata->SetArtworkUri(aUri);
        iTrackReporter.MetadataChanged(iMetadata);
    }
}

void RaatMetadataHandler::TrackInfoChanged(const RaatTrackInfo& aTrackInfo)
{
    if (iTrackPositionSecs != aTrackInfo.GetPositionSecs()) {
        iTrackPositionSecs = aTrackInfo.GetPositionSecs();
        iTrackReporter.TrackPositionChanged(iTrackPositionSecs * kMsPerSec);
    }

    RaatMetadataAllocated* metadata = iAllocatorMetadata.Allocate();
    metadata->SetTitle(aTrackInfo.GetTitle());
    metadata->SetSubtitle(aTrackInfo.GetSubtitle());
    metadata->SetSubSubtitle(aTrackInfo.GetSubSubtitle());
    metadata->SetDurationMs(aTrackInfo.GetDurationSecs() * kMsPerSec);

    if (iMetadata != nullptr) {
        if (*iMetadata == *metadata) {
            metadata->RemoveReference();
            return;
        }
        iMetadata->RemoveReference();
    }

    iMetadata = metadata;
    iMetadata->AddReference();
    iTrackReporter.MetadataChanged(iMetadata);
    iTrackReporter.TrackOffsetChanged(iTrackPositionSecs * kMsPerSec);
}

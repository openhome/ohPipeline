#include <OpenHome/Types.h>
#include <OpenHome/Media/Pipeline/AsyncTrackReporter.h>

#include <OpenHome/Private/Printer.h>

using namespace OpenHome;
using namespace OpenHome::Media;

// AsyncStartOffset

AsyncStartOffset::AsyncStartOffset()
    : iOffsetMs(0)
{
}

void AsyncStartOffset::SetMs(TUint aOffsetMs)
{
    iOffsetMs = aOffsetMs;
}

TUint64 AsyncStartOffset::OffsetSample(TUint aSampleRate) const
{
    return (static_cast<TUint64>(iOffsetMs) * aSampleRate) / 1000;
}

TUint AsyncStartOffset::OffsetMs() const
{
    return iOffsetMs;
}

TUint AsyncStartOffset::AbsoluteDifference(TUint aOffsetMs) const
{
    TUint offsetDiff = 0;
    if (iOffsetMs >= aOffsetMs) {
        offsetDiff = iOffsetMs - aOffsetMs;
    }
    else {
        offsetDiff = aOffsetMs - iOffsetMs;
    }
    return offsetDiff;
}


// AsyncTrackReporter

const TUint AsyncTrackReporter::kSupportedMsgTypes =   eMode
                                                  | eTrack
                                                  | eDrain
                                                  | eDelay
                                                  | eMetatext
                                                  | eStreamInterrupted
                                                  | eHalt
                                                  | eFlush
                                                  | eWait
                                                  | eDecodedStream
                                                  | eBitRate
                                                  | eAudioPcm
                                                  | eAudioDsd
                                                  | eSilence
                                                  | eQuit;

AsyncTrackReporter::AsyncTrackReporter(
    IPipelineElementUpstream&   aUpstreamElement,
    MsgFactory&                 aMsgFactory,
    TrackFactory&               aTrackFactory)

    : PipelineElement(kSupportedMsgTypes)
    , iUpstreamElement(aUpstreamElement)
    , iMsgFactory(aMsgFactory)
    , iTrackFactory(aTrackFactory)
    , iClient(nullptr)
    , iMetadata(nullptr)
    , iDecodedStream(nullptr)
    , iInterceptMode(false)
    , iMsgDecodedStreamPending(false)
    , iGeneratedTrackPending(false)
    , iPipelineTrackSeen(false)
    , iTrackDurationMs(0)
    , iLock("ASTR")
{
}

AsyncTrackReporter::~AsyncTrackReporter()
{
    AutoMutex _(iLock);
    if (iMetadata != nullptr) {
        iMetadata->RemoveReference();
    }
    if (iDecodedStream != nullptr) {
        iDecodedStream->RemoveRef();
    }
}

Msg* AsyncTrackReporter::Pull()
{
    Msg* msg = nullptr;
    while (msg == nullptr) {

        if (!iInterceptMode) {
            msg = iUpstreamElement.Pull();
            msg = msg->Process(*this);

            if (iInterceptMode) {
                AutoMutex _(iLock);
                iMsgDecodedStreamPending = true;
            }
            return msg;
        }

        /*  Cannot hold iLock during a Pull() as it can block
         *  - Acquire iLock and perform checks before deciding whether to Pull()
         *  - Release iLock during Pull()
         *  - Re-acquire iLock when processing the message
         */
        {
            AutoMutex _(iLock);

            /*  Must have seen a MsgTrack and MsgDecoded stream arrive
             *  via pipeline before reporting any changes
             */
            if (iPipelineTrackSeen && iDecodedStream != nullptr) {
                if (iGeneratedTrackPending) {

                    BwsTrackMetaData metadata;
                    if (iMetadata != nullptr) {
                        WriterBuffer writerBuffer(metadata);
                        iClient->WriteMetadata(
                            iTrackUri,
                            iMetadata->Metadata(),
                            iDecodedStream->StreamInfo(),
                            writerBuffer);
                    }

                    Track* track = iTrackFactory.CreateTrack(iTrackUri, metadata);
                    auto trackMsg = iMsgFactory.CreateMsgTrack(*track, false);
                    track->RemoveRef();

                    iGeneratedTrackPending = false;
                    return trackMsg;
                }
                else if (iMsgDecodedStreamPending) {
                    auto streamMsg = CreateMsgDecodedStreamLocked();
                    UpdateDecodedStream(*streamMsg);

                    iMsgDecodedStreamPending = false;
                    return iDecodedStream;
                }
            }
        }

        msg = iUpstreamElement.Pull();
        {
            AutoMutex _(iLock);
            msg = msg->Process(*this);
        }
    }
    return msg;
}

void AsyncTrackReporter::AddClient(IAsyncTrackClient& aClient)
{
    iClients.push_back(&aClient);
}

void AsyncTrackReporter::MetadataChanged(IAsyncMetadataAllocated* aMetadata)
{
    AutoMutex _(iLock);
    if (iMetadata != nullptr) {
        iMetadata->RemoveReference(); // Any pending metadata is now invalid
        iMetadata = nullptr;
    }
    iMetadata = aMetadata;
    if (iMetadata != nullptr) {
        iTrackDurationMs = iMetadata->Metadata().DurationMs();
    }
    iGeneratedTrackPending = true;
    iMsgDecodedStreamPending = true;

    // If this metadata is being delivered as part of a track change, any start offset (be it zero or non-zero) will be updated via call to ::TrackOffsetChanged(). ::TrackOffsetChanged() will also be called if a seek occurred.

    // If this metadata arrives mid-track (i.e., because retrieval of the new metadata has been delayed, or the metadata has actually changed mid-track) the start sample for the new MsgDecodedStream should already be (roughly) correct without any extra book-keeping, as long as calls to ::TrackPosition() are being made, which update iStartOffset to avoid any playback time sync issues.
}

void AsyncTrackReporter::TrackOffsetChanged(TUint aOffsetMs)
{
    AutoMutex _(iLock);
    iStartOffset.SetMs(aOffsetMs);
    iMsgDecodedStreamPending = true;
}

void AsyncTrackReporter::TrackPositionChanged(TUint aPositionMs)
{
    AutoMutex _(iLock);
    const TUint offsetDiffAbs = iStartOffset.AbsoluteDifference(aPositionMs);
    if (offsetDiffAbs > kTrackOffsetChangeThresholdMs) {
        iMsgDecodedStreamPending = true;
    }
    iStartOffset.SetMs(aPositionMs);
}

Msg* AsyncTrackReporter::ProcessMsg(MsgMode* aMsg)
{
    for (auto* client : iClients) {
        if (aMsg->Mode() == client->Mode()) {

            /*  If iInterceptMode is already true, this must have been called with
             *  iLock held, so we can safely reset internal members that require locking
             */
            if (iInterceptMode) {
                iMsgDecodedStreamPending = true;
            }
            iInterceptMode = true;
            iClient = client;
            ClearDecodedStream();
            iPipelineTrackSeen = false;
            return aMsg;
        }
    }

    iInterceptMode = false;
    iClient = nullptr;

    return aMsg;
}

Msg* AsyncTrackReporter::ProcessMsg(MsgDecodedStream* aMsg)
{
    if (!iInterceptMode) {
        return aMsg;
    }

    const DecodedStreamInfo& info = aMsg->StreamInfo();
    ASSERT(info.SampleRate() != 0);
    ASSERT(info.NumChannels() != 0);

    UpdateDecodedStream(*aMsg);
    aMsg->RemoveRef();

    // Set flag and return nullptr to output generated MsgDecodedStream instead of this
    iMsgDecodedStreamPending = true;
    return nullptr;
}

Msg* AsyncTrackReporter::ProcessMsg(MsgTrack* aMsg)
{
    if (!iInterceptMode) {
        return aMsg;
    }

    // Cache URI for re-use in out-of-band MsgTracks
    iTrackUri.Replace(aMsg->Track().Uri());

    // Ensures in-band MsgTrack is output before any are generated from out-of-band notifications
    iPipelineTrackSeen = true;
    iGeneratedTrackPending = true;
    return aMsg;
}

void AsyncTrackReporter::ClearDecodedStream()
{
    if (iDecodedStream != nullptr) {
        iDecodedStream->RemoveRef();
        iDecodedStream = nullptr;
    }
}

void AsyncTrackReporter::UpdateDecodedStream(MsgDecodedStream& aMsg)
{
    ClearDecodedStream();
    iDecodedStream = &aMsg;
    iDecodedStream->AddRef();
}

TUint64 AsyncTrackReporter::TrackLengthJiffiesLocked() const
{
    ASSERT(iDecodedStream != nullptr);
    const DecodedStreamInfo& info = iDecodedStream->StreamInfo();
    const TUint64 trackLengthJiffies = (static_cast<TUint64>(iTrackDurationMs)*info.SampleRate()*Jiffies::PerSample(info.SampleRate()))/1000;
    return trackLengthJiffies;
}

MsgDecodedStream* AsyncTrackReporter::CreateMsgDecodedStreamLocked() const
{
    ASSERT(iDecodedStream != nullptr);
    const DecodedStreamInfo& info = iDecodedStream->StreamInfo();

    /*  Audio for current track was likely pushed into the pipeline
     *  before track offset/duration is known - use updated values here
     */

    const TUint64 trackLengthJiffies = TrackLengthJiffiesLocked();
    const TUint64 startOffset = iStartOffset.OffsetSample(info.SampleRate());

    MsgDecodedStream* msg = iMsgFactory.CreateMsgDecodedStream(
        info.StreamId(),
        info.BitRate(),
        info.BitDepth(),
        info.SampleRate(),
        info.NumChannels(),
        info.CodecName(),
        trackLengthJiffies,
        startOffset,
        info.Lossless(),
        info.Seekable(),
        info.Live(),
        info.AnalogBypass(),
        info.Format(),
        info.Multiroom(),
        info.Profile(),
        info.StreamHandler(),
        info.Ramp());

    return msg;
}

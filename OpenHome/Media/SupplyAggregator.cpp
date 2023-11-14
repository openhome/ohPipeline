#include <OpenHome/Media/SupplyAggregator.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/ClockPuller.h>

using namespace OpenHome;
using namespace OpenHome::Media;

// SupplyAggregator

SupplyAggregator::SupplyAggregator(MsgFactory& aMsgFactory, IPipelineElementDownstream& aDownStreamElement)
    : iMsgFactory(aMsgFactory)
    , iAudioEncoded(nullptr)
    , iDownStreamElement(aDownStreamElement)
{
}

SupplyAggregator::~SupplyAggregator()
{
    if (iAudioEncoded != nullptr) {
        iAudioEncoded->RemoveRef();
    }
}

void SupplyAggregator::Flush()
{
    if (iAudioEncoded != nullptr) {
        OutputEncodedAudio();
    }
}

void SupplyAggregator::Discard()
{
    if (iAudioEncoded != nullptr) {
        iAudioEncoded->RemoveRef();
        iAudioEncoded = nullptr;
    }
}

void SupplyAggregator::OutputTrack(Track& aTrack, TBool aStartOfStream)
{
    MsgTrack* msg = iMsgFactory.CreateMsgTrack(aTrack, aStartOfStream);
    Output(msg);
}

void SupplyAggregator::OutputDrain(Functor aCallback)
{
    auto msg = iMsgFactory.CreateMsgDrain(aCallback);
    Output(msg);
}

void SupplyAggregator::OutputDelay(TUint aJiffies)
{
    MsgDelay* msg = iMsgFactory.CreateMsgDelay(aJiffies);
    Output(msg);
}

void SupplyAggregator::OutputSegment(const Brx& aId)
{
    auto* msg = iMsgFactory.CreateMsgStreamSegment(aId);
    Output(msg);
}

void SupplyAggregator::OutputMetadata(const Brx& aMetadata)
{
    MsgMetaText* msg = iMsgFactory.CreateMsgMetaText(aMetadata);
    Output(msg);
}

void SupplyAggregator::OutputHalt(TUint aHaltId)
{
    MsgHalt* msg = iMsgFactory.CreateMsgHalt(aHaltId);
    Output(msg);
}

void SupplyAggregator::OutputFlush(TUint aFlushId)
{
    MsgFlush* msg = iMsgFactory.CreateMsgFlush(aFlushId);
    Output(msg);
}

void SupplyAggregator::OutputWait()
{
    MsgWait* msg = iMsgFactory.CreateMsgWait();
    Output(msg);
}

void SupplyAggregator::Output(Msg* aMsg)
{
    if (iAudioEncoded != nullptr) {
        OutputEncodedAudio();
    }
    iDownStreamElement.Push(aMsg);
}

void SupplyAggregator::OutputEncodedAudio()
{
    iDownStreamElement.Push(iAudioEncoded);
    iAudioEncoded = nullptr;
}


// SupplyAggregatorBytes

SupplyAggregatorBytes::SupplyAggregatorBytes(MsgFactory& aMsgFactory, IPipelineElementDownstream& aDownStreamElement)
    : SupplyAggregator(aMsgFactory, aDownStreamElement)
    , iDataMaxBytes(AudioData::kMaxBytes)
{
}

void SupplyAggregatorBytes::SetMaxBytes(TUint aMaxBytes)
{
    ASSERT(aMaxBytes <= AudioData::kMaxBytes);
    iDataMaxBytes = aMaxBytes;
}

void SupplyAggregatorBytes::OutputStream(const Brx& aUri, TUint64 aTotalBytes, TUint64 aStartPos, TBool aSeekable, TBool aLive, Media::Multiroom aMultiroom, IStreamHandler& aStreamHandler, TUint aStreamId, TUint aSeekPosMs)
{
    // FIXME - no metatext available
    MsgEncodedStream* msg = iMsgFactory.CreateMsgEncodedStream(aUri, Brx::Empty(), aTotalBytes, aStartPos, aStreamId, aSeekable, aLive, aMultiroom, &aStreamHandler, aSeekPosMs);
    Output(msg);
}

void SupplyAggregatorBytes::OutputPcmStream(const Brx& aUri, TUint64 aTotalBytes, TBool aSeekable, TBool aLive, Media::Multiroom aMultiroom, IStreamHandler& aStreamHandler, TUint aStreamId, const PcmStreamInfo& aPcmStream)
{
    // FIXME - no metatext available
    MsgEncodedStream* msg = iMsgFactory.CreateMsgEncodedStream(aUri, Brx::Empty(), aTotalBytes, 0, aStreamId, aSeekable, aLive, aMultiroom, &aStreamHandler, aPcmStream);
    Output(msg);
}

void SupplyAggregatorBytes::OutputPcmStream(const Brx& aUri, TUint64 aTotalBytes, TBool aSeekable, TBool aLive, Media::Multiroom aMultiroom, IStreamHandler& aStreamHandler, TUint aStreamId, const PcmStreamInfo& aPcmStream, RampType aRamp)
{
    // FIXME - no metatext available
    MsgEncodedStream* msg = iMsgFactory.CreateMsgEncodedStream(aUri, Brx::Empty(), aTotalBytes, 0, aStreamId, aSeekable, aLive, aMultiroom, &aStreamHandler, aPcmStream, aRamp);
    Output(msg);
}

void SupplyAggregatorBytes::OutputDsdStream(const Brx& aUri, TUint64 aTotalBytes, TBool aSeekable, IStreamHandler& aStreamHandler, TUint aStreamId, const DsdStreamInfo& aDsdStream)
{
    // FIXME - no metatext available
    MsgEncodedStream* msg = iMsgFactory.CreateMsgEncodedStream(aUri, Brx::Empty(), aTotalBytes, 0, aStreamId, aSeekable, &aStreamHandler, aDsdStream);
    Output(msg);
}

void SupplyAggregatorBytes::OutputData(const Brx& aData)
{
    if (aData.Bytes() == 0) {
        return;
    }
    if (iAudioEncoded == nullptr) {
        iAudioEncoded = iMsgFactory.CreateMsgAudioEncoded(aData);
    }
    else {
        const TUint consumed = iAudioEncoded->Append(aData, iDataMaxBytes);
        if (consumed < aData.Bytes()) {
            OutputEncodedAudio();
            Brn remaining = aData.Split(consumed);
            iAudioEncoded = iMsgFactory.CreateMsgAudioEncoded(remaining);
        }
    }
}


// SupplyAggregatorJiffies

SupplyAggregatorJiffies::SupplyAggregatorJiffies(MsgFactory& aMsgFactory, IPipelineElementDownstream& aDownStreamElement)
    : SupplyAggregator(aMsgFactory, aDownStreamElement)
    , iDataMaxBytes(0)
{
}

void SupplyAggregatorJiffies::OutputStream(const Brx& /*aUri*/, TUint64 /*aTotalBytes*/, TUint64 /*aStartPos*/, TBool /*aSeekable*/, TBool /*aLive*/, Media::Multiroom /*aMultiroom*/, IStreamHandler& /*aStreamHandler*/, TUint /*aStreamId*/, TUint /*aSeekPosMs*/)
{
    ASSERTS(); // can only aggregate by jiffies for PCM streams
}

void SupplyAggregatorJiffies::OutputPcmStream(const Brx& aUri, TUint64 aTotalBytes, TBool aSeekable, TBool aLive, Media::Multiroom aMultiroom, IStreamHandler& aStreamHandler, TUint aStreamId, const PcmStreamInfo& aPcmStream)
{
    // FIXME - no metatext available
    TUint ignore = kMaxPcmDataJiffies;
    const TUint jiffiesPerSample = Jiffies::PerSample(aPcmStream.SampleRate());
    iDataMaxBytes = Jiffies::ToBytes(ignore, jiffiesPerSample, aPcmStream.NumChannels(), aPcmStream.BitDepth());
    MsgEncodedStream* msg = iMsgFactory.CreateMsgEncodedStream(aUri, Brx::Empty(), aTotalBytes, 0, aStreamId, aSeekable, aLive, aMultiroom, &aStreamHandler, aPcmStream);
    Output(msg);
}

void SupplyAggregatorJiffies::OutputPcmStream(const Brx& aUri, TUint64 aTotalBytes, TBool aSeekable, TBool aLive, Media::Multiroom aMultiroom, IStreamHandler& aStreamHandler, TUint aStreamId, const PcmStreamInfo& aPcmStream, RampType aRamp)
{
    // FIXME - no metatext available
    TUint ignore = kMaxPcmDataJiffies;
    const TUint jiffiesPerSample = Jiffies::PerSample(aPcmStream.SampleRate());
    iDataMaxBytes = Jiffies::ToBytes(ignore, jiffiesPerSample, aPcmStream.NumChannels(), aPcmStream.BitDepth());
    MsgEncodedStream* msg = iMsgFactory.CreateMsgEncodedStream(aUri, Brx::Empty(), aTotalBytes, 0, aStreamId, aSeekable, aLive, aMultiroom, &aStreamHandler, aPcmStream, aRamp);
    Output(msg);
}

void SupplyAggregatorJiffies::OutputDsdStream(const Brx& /*aUri*/, TUint64 /*aTotalBytes*/, TBool /*aSeekable*/, IStreamHandler& /*aStreamHandler*/, TUint /*aStreamId*/, const DsdStreamInfo& /*aDsdStream*/)
{
    ASSERTS(); // no known clients, so no need to support yet
}

void SupplyAggregatorJiffies::OutputData(const Brx& aData)
{
    if (aData.Bytes() == 0) {
        return;
    }

    /* Don't try to split data precisely at kMaxPcmDataJiffies boundaries
       If we're passed in data that takes us over this threshold, accept as much as we can,
       passing it on immediately */
    if (iAudioEncoded == nullptr) {
        iAudioEncoded = iMsgFactory.CreateMsgAudioEncoded(aData);
    }
    else {
        const TUint consumed = iAudioEncoded->Append(aData);
        if (consumed < aData.Bytes()) {
            OutputEncodedAudio();
            Brn remaining = aData.Split(consumed);
            iAudioEncoded = iMsgFactory.CreateMsgAudioEncoded(remaining);
        }
    }
    if (iAudioEncoded->Bytes() >= iDataMaxBytes) {
        OutputEncodedAudio();
    }
}


// AutoSupplyFlush

AutoSupplyFlush::AutoSupplyFlush(SupplyAggregator& aSupply)
    : iSupply(aSupply)
{
}

AutoSupplyFlush::~AutoSupplyFlush()
{
    iSupply.Flush();
}

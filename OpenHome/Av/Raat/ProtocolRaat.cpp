#include <OpenHome/Av/Raat/ProtocolRaat.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Av/Raat/Output.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Media/SupplyAggregator.h>
#include <OpenHome/Debug-ohMediaPlayer.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;

ProtocolRaat::ProtocolRaat(Environment& aEnv, IRaatReader& aRaatReader, Media::TrackFactory& aTrackFactory)
    : Protocol(aEnv)
    , iLock("PRat")
    , iRaatReader(aRaatReader)
    , iTrackFactory(aTrackFactory)
    , iSupply(nullptr)
{
}

void ProtocolRaat::Initialise(Media::MsgFactory& aMsgFactory, Media::IPipelineElementDownstream& aDownstream)
{
    iSupply = new SupplyAggregatorBytes(aMsgFactory, aDownstream);
}

void ProtocolRaat::Interrupt(TBool aInterrupt)
{
    if (iActive) {
        LOG(kMedia, "ProtocolRaat::Interrupt(%u)\n", aInterrupt);
        if (aInterrupt) {
            iStopped = true;
            iRaatReader.Interrupt();
        }
    }
}

Media::ProtocolStreamResult ProtocolRaat::Stream(const Brx& aUri)
{
    // validate that we can handle aUri
    try {
        iRaatUri.Parse(aUri);
    }
    catch (RaatUriError&) {
        return EProtocolErrorNotSupported;
    }

    // reinitialise
    {
        AutoMutex _(iLock);
        iNextFlushId = MsgFlush::kIdInvalid;
        iStreamId = iIdProvider->NextStreamId();
        iStopped = false;
    }
    SpeakerProfile sp;
    PcmStreamInfo streamInfo;
    streamInfo.Set(
        iRaatUri.BitDepth(),
        iRaatUri.SampleRate(),
        iRaatUri.NumChannels(),
        AudioDataEndian::Little,
        sp,
        iRaatUri.SampleStart());
    iSupply->OutputPcmStream(
        aUri,
        0 /* totalBytes */,
        false /* seekable */,
        true /* live */,
        Media::Multiroom::Forbidden,
        *this,
        iStreamId,
        streamInfo);
    const auto bytesPerSample = (iRaatUri.BitDepth() / 8) * iRaatUri.NumChannels();
    iMaxBytesPerAudioChunk = AudioData::kMaxBytes - (AudioData::kMaxBytes % bytesPerSample);

    Semaphore sem("PRat", 0);
    iSupply->OutputDrain(MakeFunctor(sem, &Semaphore::Signal));
    sem.Wait();
    iRaatReader.NotifyReady();
    
    // do stuff - read from RAAT forever (until source change)
    for (; !iStopped; ) {
        iRaatReader.Read(*this);
    }

    // cleanup
    iSupply->Flush();
    TUint nextFlushId;
    {
        AutoMutex _(iLock);
        nextFlushId = iNextFlushId;
        iStreamId = IPipelineIdProvider::kStreamIdInvalid;
    }
    if (nextFlushId != MsgFlush::kIdInvalid) {
        iSupply->OutputFlush(nextFlushId);
    }
    return EProtocolStreamStopped;
}

Media::ProtocolGetResult ProtocolRaat::Get(IWriter& /*aWriter*/, const Brx& /*aUri*/, TUint64 /*aOffset*/, TUint /*aBytes*/)
{
    return EProtocolGetErrorNotSupported;
}

TUint ProtocolRaat::TryStop(TUint aStreamId)
{
    AutoMutex _(iLock);
    if (iStreamId != aStreamId) {
        return MsgFlush::kIdInvalid;
    }
    if (iNextFlushId == MsgFlush::kIdInvalid) {
        /* If a valid flushId is set then We've previously promised to send a Flush but haven't
            got round to it yet.  Re-use the same id for any other requests that come in before
            our main thread gets a chance to issue a Flush */
        iNextFlushId = iFlushIdProvider->NextFlushId();
    }
    LOG(kMedia, "ProtocolRaat::TryStop(%u), iStreamId=%u, iNextFlushId=%u\n", aStreamId, iStreamId, iNextFlushId);
    iStopped = true;
    iRaatReader.Interrupt();
    return iNextFlushId;
}

void ProtocolRaat::WriteMetadata(const Brx& aMetadata)
{
    auto track = iTrackFactory.CreateTrack(iRaatUri.AbsoluteUri(), aMetadata);
    iSupply->OutputTrack(*track, false /* startOfStream */);
}

void ProtocolRaat::WriteDelay(TUint aJiffies)
{
    aJiffies = Jiffies::kPerMs * 100; // FIXME - Roon appear to try to run at an implausibly low latency
    iSupply->OutputDelay(aJiffies);
}

void ProtocolRaat::WriteData(const Brx& aData)
{
    const TByte* ptr = aData.Ptr();
    TUint remaining = aData.Bytes();
    for (;;) {
        const auto bytes = remaining > iMaxBytesPerAudioChunk ? iMaxBytesPerAudioChunk : remaining;
        Brn data(ptr, bytes);
        iSupply->OutputData(data);
        remaining -= bytes;
        if (remaining == 0) {
            break;
        }
        ptr += bytes;
    }
}

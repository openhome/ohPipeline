#include <OpenHome/Av/Raat/ProtocolRaat.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Av/Raat/Output.h>
#include <OpenHome/Av/OhMetadata.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Media/SupplyAggregator.h>
#include <OpenHome/Debug-ohMediaPlayer.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;

// ProtocolRaat

const Brn ProtocolRaat::kUri("raat://default");

ProtocolRaat::ProtocolRaat(Environment& aEnv, IRaatReader& aRaatReader, Media::TrackFactory& aTrackFactory)
    : Protocol(aEnv)
    , DsdFiller(kDsdBlockBytes, kDsdBlockBytes, kDsdChunksPerBlock)
    , iRaatReader(aRaatReader)
    , iTrackFactory(aTrackFactory)
    , iSupply(nullptr)
    , iLock("PRat")
{
}

ProtocolRaat::~ProtocolRaat()
{
    delete iSupply;
}

void ProtocolRaat::Initialise(Media::MsgFactory& aMsgFactory, Media::IPipelineElementDownstream& aDownstream)
{
    iSupply = new SupplyAggregatorBytes(aMsgFactory, aDownstream);
}

void ProtocolRaat::Interrupt(TBool aInterrupt)
{
    if (iActive) {
        LOG(kRaat, "ProtocolRaat::Interrupt(%u)\n", aInterrupt);
        if (aInterrupt) {
            iStopped = true;
            iRaatReader.Interrupt();
        }
    }
}

Media::ProtocolStreamResult ProtocolRaat::Stream(const Brx& aUri)
{
    if (aUri != kUri) {
        return EProtocolErrorNotSupported;
    }

    // reinitialise
    {
        AutoMutex _(iLock);
        iNextFlushId = MsgFlush::kIdInvalid;
        iStreamId = iIdProvider->NextStreamId();
        iStopped = false;
    }

    const RaatStreamFormat& streamFormat = iRaatReader.StreamFormat();
    iPcmStream = (streamFormat.Format() == AudioFormat::Pcm);
    OutputStream(streamFormat);
    OutputDrain();
    iRaatReader.NotifyReady();

    // do stuff - read from RAAT forever (until source change)
    try {
        for (; !iStopped; ) {
            iRaatReader.Read(*this);
        }
    }
    catch (RaatReaderStopped&) {}

    DsdFiller::Flush(); // safe to call regardless of format
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
    LOG(kRaat, "ProtocolRaat::TryStop(%u), iStreamId=%u, iNextFlushId=%u\n", aStreamId, iStreamId, iNextFlushId);
    iStopped = true;
    iRaatReader.Interrupt();
    return iNextFlushId;
}

void ProtocolRaat::WriteChunkDsd(const TByte*& aSrc, TByte*& aDest)
{
    *aDest++ = aSrc[0];
    *aDest++ = aSrc[2];
    *aDest++ = aSrc[1];
    *aDest++ = aSrc[3];
    aSrc += 4;
}

void ProtocolRaat::OutputDsd(const Brx& aData)
{
    // Callback once DsdFiller has filled its output buffer
    iSupply->OutputData(aData);
}

void ProtocolRaat::Write(const Brx& aData)
{
    if (iPcmStream) {
        const TByte* ptr = aData.Ptr();
        TUint remaining = aData.Bytes();
        while (remaining > 0) {
            const TUint bytes = (remaining > AudioData::kMaxBytes) ? AudioData::kMaxBytes : remaining;
            Brn data(ptr, bytes);
            iSupply->OutputData(data);
            remaining -= bytes;
            ptr += bytes;
        }
    }
    else { // DSD
        DsdFiller::Push(aData);
    }
}

void ProtocolRaat::OutputStream(const RaatStreamFormat& aStreamFormat)
{
    if (iPcmStream) {
        SpeakerProfile sp;
        PcmStreamInfo streamInfo;
        streamInfo.Set(
            aStreamFormat.BitDepth(),
            aStreamFormat.SampleRate(),
            aStreamFormat.NumChannels(),
            AudioDataEndian::Little,
            sp,
            0LL); // sample start (passed asynchronously)
        iSupply->OutputPcmStream(
            kUri,
            0LL, // duration
            false, // seekable
            false, // live
            Media::Multiroom::Forbidden,
            *this,
            iStreamId,
            streamInfo);
    }
    else {
        DsdStreamInfo streamInfo;
        streamInfo.Set(
            aStreamFormat.SampleRate(),
            2,
            6,
            0LL); // sample start (passed asynchronously)
        streamInfo.SetCodec(Brn("DSD-RAAT"));
        iSupply->OutputDsdStream(
            kUri,
            0LL, // duration
            false, // seekable
            *this,
            iStreamId,
            streamInfo);
    }
}

void ProtocolRaat::OutputDrain()
{
    LOG(kRaat, "ProtocolRaat::OutputDrain()\n");
    Semaphore sem("DRAT", 0);
    iSupply->OutputDrain(MakeFunctor(sem, &Semaphore::Signal));
    try {
        sem.Wait(ISupply::kMaxDrainMs);
    }
    catch (Timeout&) {
        LOG(kPipeline, "WARNING: ProtocolRaat: timeout draining pipeline\n");
    }
}
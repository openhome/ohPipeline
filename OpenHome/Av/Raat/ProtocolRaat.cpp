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

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;

// ProtocolRaat

const Brn ProtocolRaat::kUri("raat://default");

ProtocolRaat::ProtocolRaat(Environment& aEnv, IRaatReader& aRaatReader, Media::TrackFactory& aTrackFactory)
    : Protocol(aEnv)
    , DsdFiller(kDsdBlockBytes, kDsdBlockBytes)
    , iEnv(aEnv)
    , iRaatReader(aRaatReader)
    , iTrackFactory(aTrackFactory)
    , iSupply(nullptr)
    , iState(EStreamState::eStopped)
    , iInterrupt(false)
    , iSemStateChange("PRSM", 0)
    , iLock("PRat")
{
}

ProtocolRaat::~ProtocolRaat()
{
    delete iSupply;
}

TBool ProtocolRaat::IsStreaming()
{
    return (iState.load() != EStreamState::eStopped);
}

void ProtocolRaat::NotifySetup()
{
    iSetup = true;
    iSemStateChange.Signal();
}

void ProtocolRaat::NotifyStart()
{
    iSetup = false;
    iSemStateChange.Signal();
}

TUint ProtocolRaat::FlushAsync()
{
    if (iState.load() != EStreamState::eStreaming) {
        return MsgFlush::kIdInvalid;
    }

    AutoMutex _(iLock);
    if (iNextFlushId == MsgFlush::kIdInvalid) {
        iNextFlushId = iFlushIdProvider->NextFlushId();
    }
    return iNextFlushId;
}

void ProtocolRaat::Initialise(Media::MsgFactory& aMsgFactory, Media::IPipelineElementDownstream& aDownstream)
{
    iSupply = new SupplyAggregatorBytes(aMsgFactory, aDownstream);
}

void ProtocolRaat::Interrupt(TBool aInterrupt)
{
    if (!iActive) {
        return;
    }
    if (!aInterrupt) {
        return;
    }
    LOG(kRaat, "ProtocolRaat::Interrupt(%u)\n", aInterrupt);
    if (IsStreaming()) {
        DoInterrupt();
    }
}

Media::ProtocolStreamResult ProtocolRaat::Stream(const Brx& aUri)
{
    if (aUri != kUri) {
        return EProtocolErrorNotSupported;
    }

    try {
        for (;;) {
            iState.store(EStreamState::eIdle);
            iSemStateChange.Wait();
            if (iInterrupt.load()) {
                THROW(ProtocolRaatInterrupt);
            }

            const RaatStreamFormat& streamFormat = iRaatReader.StreamFormat();
            iPcmStream = (streamFormat.Format() == AudioFormat::Pcm);
            OutputStream(streamFormat);
            iSupply->OutputDelay(kDefaultDelayJiffies);
            OutputDrain();

            if (iSetup) {
                iRaatReader.NotifyReady();
                continue;
            }

            // Stream
            iState.store(EStreamState::eStreaming);
            try {
                for (;;) {
                    iRaatReader.Read(*this);
                }
            }
            catch (RaatReaderStopped&) {}

            // Flush
            DsdFiller::Flush(); // safe to call regardless of format
            iSupply->Flush();

            TUint nextFlushId;
            {
                AutoMutex _(iLock);
                nextFlushId = iNextFlushId;
                iNextFlushId = MsgFlush::kIdInvalid;
            }
            if (nextFlushId != MsgFlush::kIdInvalid) {
                iSupply->OutputFlush(nextFlushId);
            }
        }
    }
    catch (ProtocolRaatInterrupt&) {}

    iInterrupt.store(false);
    iState.store(EStreamState::eStopped);
    return EProtocolStreamStopped;
}

Media::ProtocolGetResult ProtocolRaat::Get(IWriter& /*aWriter*/, const Brx& /*aUri*/, TUint64 /*aOffset*/, TUint /*aBytes*/)
{
    return EProtocolGetErrorNotSupported;
}

TUint ProtocolRaat::TryStop(TUint /*aStreamId*/)
{
    if (IsStreaming()) {
        DoInterrupt();
    }
    return MsgFlush::kIdInvalid;
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
    TUint streamId = iIdProvider->NextStreamId();
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
            streamId,
            streamInfo);
    }
    else {
        DsdStreamInfo streamInfo;
        streamInfo.Set(
            aStreamFormat.SampleRate(),
            2,
            6,
            0LL); // sample start (passed asynchronously)
        iSupply->OutputDsdStream(
            kUri,
            0LL, // duration
            false, // seekable
            *this,
            streamId,
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

void ProtocolRaat::DoInterrupt()
{
    iInterrupt.store(true);
    iRaatReader.Interrupt();
    iSemStateChange.Signal();
}
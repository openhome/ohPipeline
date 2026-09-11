#include <OpenHome/Av/QobuzConnect/ProtocolQobuzConnect.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/OhMetadata.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Media/SupplyAggregator.h>
#include <OpenHome/Debug-ohMediaPlayer.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;

// ProtocolQobuzConnect

const Brn ProtocolQobuzConnect::kUri("qobuzconnect://default");

ProtocolQobuzConnect::ProtocolQobuzConnect(Environment& aEnv, IQobuzConnectAudioReader& aReader, Media::TrackFactory& aTrackFactory)
    : Protocol(aEnv)
    , iEnv(aEnv)
    , iReader(aReader)
    , iTrackFactory(aTrackFactory)
    , iSupply(nullptr)
    , iState(EStreamState::eStopped)
    , iInterrupt(false)
    , iSemStateChange("PQS1", 0)
    , iSemDrain("PQS2", 0)
    , iLock("PQzC")
    , iNextFlushId(MsgFlush::kIdInvalid)
    , iSetup(false)
{
}

ProtocolQobuzConnect::~ProtocolQobuzConnect()
{
    delete iSupply;
}

TBool ProtocolQobuzConnect::IsStreaming()
{
    return (iState.load() != EStreamState::eStopped);
}

void ProtocolQobuzConnect::NotifySetup()
{
    LOG(kQobuzConnect, "ProtocolQobuzConnect::NotifySetup() state=%u\n", (TUint)iState.load());
    iSetup = true;
    iSemStateChange.Signal();
}

void ProtocolQobuzConnect::NotifyStart()
{
    LOG(kQobuzConnect, "ProtocolQobuzConnect::NotifyStart() state=%u\n", (TUint)iState.load());
    iSetup = false;
    iSemStateChange.Signal();
}

TUint ProtocolQobuzConnect::FlushAsync()
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

void ProtocolQobuzConnect::Initialise(Media::MsgFactory& aMsgFactory, Media::IPipelineElementDownstream& aDownstream)
{
    iSupply = new SupplyAggregatorBytes(aMsgFactory, aDownstream);
}

void ProtocolQobuzConnect::Interrupt(TBool aInterrupt)
{
    if (!iActive) {
        return;
    }
    if (!aInterrupt) {
        return;
    }
    LOG(kQobuzConnect, "ProtocolQobuzConnect::Interrupt(%u)\n", aInterrupt);
    if (IsStreaming()) {
        DoInterrupt();
    }
}

Media::ProtocolStreamResult ProtocolQobuzConnect::Stream(const Brx& aUri)
{
    if (aUri != kUri) {
        return EProtocolErrorNotSupported;
    }

    try {
        for (;;) {
            iState.store(EStreamState::eIdle);
            LOG(kQobuzConnect, "ProtocolQobuzConnect::Stream: waiting on iSemStateChange\n");
            iSemStateChange.Wait();
            LOG(kQobuzConnect, "ProtocolQobuzConnect::Stream: iSemStateChange woke, iSetup=%u iInterrupt=%u\n",
                iSetup, iInterrupt.load());
            if (iInterrupt.load()) {
                THROW(ProtocolQobuzConnectInterrupt);
            }

            const QobuzConnectStreamFormat& streamFormat = iReader.StreamFormat();
            OutputStream(streamFormat);
            iSupply->OutputDelay(kDefaultDelayJiffies);
            OutputDrain();

            if (iSetup) {
                LOG(kQobuzConnect, "ProtocolQobuzConnect::Stream: still iSetup, looping back to wait for NotifyStart()\n");
                continue; // wait for NotifyStart() before actually pumping audio
            }

            // Stream
            LOG(kQobuzConnect, "ProtocolQobuzConnect::Stream: entering Read loop\n");
            iState.store(EStreamState::eStreaming);
            try {
                for (;;) {
                    iReader.Read(*this);
                }
            }
            catch (QobuzConnectAudioStreamStopped&) {
                LOG(kQobuzConnect, "ProtocolQobuzConnect::Stream: Read loop exited (QobuzConnectAudioStreamStopped)\n");
            }

            // Flush
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
            LOG(kQobuzConnect, "ProtocolQobuzConnect::Stream: flushed, looping back to top\n");
        }
    }
    catch (ProtocolQobuzConnectInterrupt&) {
        LOG(kQobuzConnect, "ProtocolQobuzConnect::Stream: caught ProtocolQobuzConnectInterrupt, exiting Stream() entirely\n");
    }

    iInterrupt.store(false);
    iState.store(EStreamState::eStopped);
    return EProtocolStreamStopped;
}

Media::ProtocolGetResult ProtocolQobuzConnect::Get(IWriter& /*aWriter*/, const Brx& /*aUri*/, TUint64 /*aOffset*/, TUint /*aBytes*/)
{
    return EProtocolGetErrorNotSupported;
}

TUint ProtocolQobuzConnect::TryStop(TUint /*aStreamId*/)
{
    if (IsStreaming()) {
        DoInterrupt();
    }
    return MsgFlush::kIdInvalid;
}

void ProtocolQobuzConnect::Write(const Brx& aData)
{
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

void ProtocolQobuzConnect::OutputStream(const QobuzConnectStreamFormat& aStreamFormat)
{
    TUint streamId = iIdProvider->NextStreamId();
    SpeakerProfile sp;
    PcmStreamInfo streamInfo;
    streamInfo.Set(
        aStreamFormat.BitDepth(),
        aStreamFormat.SampleRate(),
        aStreamFormat.NumChannels(),
        AudioDataEndian::Little,
        sp,
        0LL); // sample start (Qobuz Connect reports initial position separately; not currently threaded through)
    iSupply->OutputPcmStream(
        kUri,
        0LL, // duration - not known ahead of time; Qobuz Connect reports it via QbzAudioStreamProperties.duration but that isn't threaded through yet
        false, // seekable - seeking is driven by the SDK's own seek_time API, not the Pipeline's
        false, // live
        Media::Multiroom::Forbidden,
        *this,
        streamId,
        streamInfo);
}

void ProtocolQobuzConnect::OutputDrain()
{
    LOG(kQobuzConnect, "ProtocolQobuzConnect::OutputDrain()\n");
    iSemDrain.Clear();
    iSupply->OutputDrain(MakeFunctor(iSemDrain, &Semaphore::Signal));
    try {
        iSemDrain.Wait(ISupply::kMaxDrainMs);
    }
    catch (Timeout&) {
        LOG(kPipeline, "WARNING: ProtocolQobuzConnect: timeout draining pipeline\n");
    }
}

void ProtocolQobuzConnect::DoInterrupt()
{
    LOG(kQobuzConnect, "ProtocolQobuzConnect::DoInterrupt()\n");
    iInterrupt.store(true);
    iReader.Interrupt();
    iSemStateChange.Signal();
}

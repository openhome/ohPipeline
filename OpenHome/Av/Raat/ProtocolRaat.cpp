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

ProtocolRaat::ProtocolRaat(Environment& aEnv, IRaatReader& aRaatReader, Media::TrackFactory& aTrackFactory)
    : Protocol(aEnv)
    , iLock("PRat")
    , iRaatReader(aRaatReader)
    , iTrackFactory(aTrackFactory)
    , iSupplyPcm(nullptr)
    , iSupplyDsd(nullptr)
    , iSupply(nullptr)
{
}

ProtocolRaat::~ProtocolRaat()
{
    delete iSupplyPcm;
    delete iSupplyDsd;
}

void ProtocolRaat::Initialise(Media::MsgFactory& aMsgFactory, Media::IPipelineElementDownstream& aDownstream)
{
    iSupplyPcm = new SupplyAggregatorBytes(aMsgFactory, aDownstream);
    iSupplyDsd = new RaatSupplyDsd(aMsgFactory, aDownstream);
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
    // validate that we can handle aUri
    try {
        iRaatUri.Parse(aUri);
        iPcmStream = iRaatUri.Format() == Media::AudioFormat::Pcm;
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
    if (iPcmStream) {
        iSupply = iSupplyPcm;
        const auto bytesPerSample = (iRaatUri.BitDepth() / 8) * iRaatUri.NumChannels();
        iMaxBytesPerAudioChunk = AudioData::kMaxBytes - (AudioData::kMaxBytes % bytesPerSample);
    }
    else {
        iSupply = iSupplyDsd;
        iMaxBytesPerAudioChunk = AudioData::kMaxBytes - (AudioData::kMaxBytes % 6);
    }
    OutputStream(iRaatUri.SampleStart(), 0LL);

    Semaphore sem("PRat", 0);
    iSupply->OutputDrain(MakeFunctor(sem, &Semaphore::Signal));
    sem.Wait();
    iRaatReader.NotifyReady();
    
    // do stuff - read from RAAT forever (until source change)
    try {
        for (; !iStopped; ) {
            iRaatReader.Read(*this);
        }
    }
    catch (RaatReaderStopped&) {}

    // cleanup
    if (iPcmStream) {
        iSupplyPcm->Flush();
    }
    else {
        iSupplyDsd->Flush();
    }
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

void ProtocolRaat::Stop()
{
    LOG(kRaat, "ProtocolRaat::Stop()\n");
    Interrupt(true);
}

void ProtocolRaat::Write(const Brx& aData)
{
    if (iPcmStream) {
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
    else { // DSD
        iSupply->OutputData(aData);
    }
}

void ProtocolRaat::OutputStream(TUint64 aSampleStart, TUint64 aDurationBytes)
{
    if (iPcmStream) {
        SpeakerProfile sp;
        PcmStreamInfo streamInfo;
        streamInfo.Set(
            iRaatUri.BitDepth(),
            iRaatUri.SampleRate(),
            iRaatUri.NumChannels(),
            AudioDataEndian::Little,
            sp,
            aSampleStart);
        iSupply->OutputPcmStream(
            iRaatUri.AbsoluteUri(),
            aDurationBytes,
            false /* seekable */,
            false /* live */,
            Media::Multiroom::Forbidden,
            *this,
            iStreamId,
            streamInfo);
    }
    else {
        DsdStreamInfo streamInfo;
        streamInfo.Set(iRaatUri.SampleRate(), 2, 6, aSampleStart);
        iSupply->OutputDsdStream(
            iRaatUri.AbsoluteUri(),
            aDurationBytes,
            false /* seekable */,
            *this,
            iStreamId,
            streamInfo);
    }
}


// RaatSupplyDsd

RaatSupplyDsd::RaatSupplyDsd(MsgFactory& aMsgFactory, IPipelineElementDownstream& aDownStreamElement)
    : iMsgFactory(aMsgFactory)
    , iDownStreamElement(aDownStreamElement)
{
}

RaatSupplyDsd::~RaatSupplyDsd()
{
}

void RaatSupplyDsd::Flush()
{
    if (iDsdDataBuf.Bytes() > 0) {
        auto msg = iMsgFactory.CreateMsgAudioEncoded(iDsdDataBuf);
        iDownStreamElement.Push(msg);
        iDsdDataBuf.SetBytes(0);
    }
    iDsdPartialBlock.ReplaceThrow(Brx::Empty());
}

void RaatSupplyDsd::OutputTrack(Track& aTrack, TBool aStartOfStream)
{
    auto msg = iMsgFactory.CreateMsgTrack(aTrack, aStartOfStream);
    Output(msg);
}

void RaatSupplyDsd::OutputDrain(Functor aCallback)
{
    auto msg = iMsgFactory.CreateMsgDrain(aCallback);
    Output(msg);
}

void RaatSupplyDsd::OutputDelay(TUint aJiffies)
{
    MsgDelay* msg = iMsgFactory.CreateMsgDelay(aJiffies);
    Output(msg);
}

void RaatSupplyDsd::OutputStream(
    const Brx& /*aUri*/,
    TUint64 /*aTotalBytes*/,
    TUint64 /*aStartPos*/,
    TBool /*aSeekable*/,
    TBool /*aLive*/,
    Media::Multiroom /*aMultiroom*/,
    IStreamHandler& /*aStreamHandler*/,
    TUint /*aStreamId*/,
    TUint /*aSeekPosMs*/)
{
    ASSERTS(); // only expect to handle DSD streams here
}

void RaatSupplyDsd::OutputPcmStream(
    const Brx& /*aUri*/,
    TUint64 /*aTotalBytes*/,
    TBool /*aSeekable*/,
    TBool /*aLive*/,
    Media::Multiroom /*aMultiroom*/,
    IStreamHandler& /*aStreamHandler*/,
    TUint /*aStreamId*/,
    const PcmStreamInfo& /*aPcmStream*/)
{
    ASSERTS(); // only expect to handle DSD streams here
}

void RaatSupplyDsd::OutputPcmStream(
    const Brx& /*aUri*/,
    TUint64 /*aTotalBytes*/,
    TBool /*aSeekable*/,
    TBool /*aLive*/,
    Media::Multiroom /*aMultiroom*/,
    IStreamHandler& /*aStreamHandler*/,
    TUint /*aStreamId*/,
    const PcmStreamInfo& /*aPcmStream*/,
    RampType /*aRamp*/)
{
    ASSERTS(); // only expect to handle DSD streams here
}

void RaatSupplyDsd::OutputDsdStream(
    const Brx& aUri,
    TUint64 aTotalBytes,
    TBool aSeekable,
    IStreamHandler& aStreamHandler,
    TUint aStreamId,
    const DsdStreamInfo& aDsdStream)
{
    auto msg = iMsgFactory.CreateMsgEncodedStream(aUri, Brx::Empty(), aTotalBytes, 0, aStreamId, aSeekable, &aStreamHandler, aDsdStream);
    Output(msg);
}

void RaatSupplyDsd::OutputSegment(const Brx& aId)
{
    auto* msg = iMsgFactory.CreateMsgStreamSegment(aId);
    Output(msg);
}

void RaatSupplyDsd::OutputData(const Brx& aData)
{
    const TByte* src = aData.Ptr();
    TUint srcRemaining = aData.Bytes();

    TByte* dest = const_cast<TByte*>(iDsdDataBuf.Ptr() + iDsdDataBuf.Bytes());
    TUint destRemaining = iDsdDataBuf.MaxBytes() - iDsdDataBuf.Bytes();
    if (iDsdPartialBlock.Bytes() > 0) {
        dest[0] = 0x00; // padding
        dest[1] = iDsdPartialBlock[0];
        dest[3] = 0x00; // padding
        dest[4] = iDsdPartialBlock[1];
        if (iDsdPartialBlock.Bytes() == 2) {
            dest[2] = src[0];
            dest[5] = src[1];
            srcRemaining -= 2;
            src += 2;
        }
        else if (iDsdPartialBlock.Bytes() == 4) {
            dest[2] = iDsdPartialBlock[2];
            dest[5] = iDsdPartialBlock[3];
        }
        dest += 6;
        destRemaining -= 6;
        iDsdPartialBlock.ReplaceThrow(Brx::Empty());
    }

    while (srcRemaining >= 4) {
        while (srcRemaining >= 4 && destRemaining > 0) {
            *dest++ = 0x00; // padding
            *dest++ = src[0];
            *dest++ = src[2];
            *dest++ = 0x00; // padding
            *dest++ = src[1];
            *dest++ = src[3];
            srcRemaining -= 4;
            src += 4;
            destRemaining -= 6;
        }
        iDsdDataBuf.SetBytes(iDsdDataBuf.MaxBytes() - destRemaining);
        if (destRemaining == 0) {
            auto msg = iMsgFactory.CreateMsgAudioEncoded(iDsdDataBuf);
            iDownStreamElement.Push(msg);
            iDsdDataBuf.SetBytes(0);
            dest = const_cast<TByte*>(iDsdDataBuf.Ptr());
            destRemaining = iDsdDataBuf.MaxBytes();
        }
    }
    if (srcRemaining > 0) {
        Brn rem(src, srcRemaining);
        iDsdPartialBlock.ReplaceThrow(rem);
    }
}

void RaatSupplyDsd::OutputMetadata(const Brx& aMetadata)
{
    MsgMetaText* msg = iMsgFactory.CreateMsgMetaText(aMetadata);
    Output(msg);
}

void RaatSupplyDsd::OutputHalt(TUint aHaltId)
{
    MsgHalt* msg = iMsgFactory.CreateMsgHalt(aHaltId);
    Output(msg);
}

void RaatSupplyDsd::OutputFlush(TUint aFlushId)
{
    MsgFlush* msg = iMsgFactory.CreateMsgFlush(aFlushId);
    Output(msg);
}

void RaatSupplyDsd::OutputWait()
{
    MsgWait* msg = iMsgFactory.CreateMsgWait();
    Output(msg);
}

void RaatSupplyDsd::Output(Msg* aMsg)
{
    // pass on any pending data
    iDownStreamElement.Push(aMsg);
}

#include <OpenHome/Av/Scd/Receiver/SupplyScd.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Media/Pipeline/Msg.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Scd;
using namespace OpenHome::Media;

// SupplyScd

SupplyScd::SupplyScd(MsgFactory& aMsgFactory, 
                     IPipelineElementDownstream& aDownStreamElement,
                     TUint aDsdSampleBlockWords,
                     TUint aDsdPadBytesPerChunk)
    : iMsgFactory(aMsgFactory)
    , iDownStreamElement(aDownStreamElement)
    , iAudioEncoded(nullptr)
    , iBitsPerSample(0)
    , iSamplesCapacity(0)
    , iBytesPerAudioMsg(0)
    , iDsdSampleBlockWords(aDsdSampleBlockWords)
    , iDsdPadBytesPerChunk(aDsdPadBytesPerChunk)
    , iPaddingBuffer(aDsdPadBytesPerChunk)
    , iSilenceBuffer(aDsdSampleBlockWords*4)
    , iIsDsd(true)
{
    // We pad with 0x00 while playing and 0x69 when at a track boundary
    iPaddingBuffer.SetBytes(iPaddingBuffer.MaxBytes());
    iPaddingBuffer.Fill(kDsdPadding);
    iSilenceBuffer.SetBytes(iSilenceBuffer.MaxBytes());
    iSilenceBuffer.Fill(kDsdSilence);
}

SupplyScd::~SupplyScd()
{
    Discard();
}

inline void SupplyScd::OutputEncodedAudio()
{
    if (iAudioEncoded != nullptr) {
        if (iIsDsd && iAudioEncoded->Bytes() != iBytesPerAudioMsg) {
            const TUint sampleBlockBytes = iDsdSampleBlockWords * 4;
            if (iAudioEncoded->Bytes() % sampleBlockBytes != 0) {
                const TUint remainingBytes = sampleBlockBytes - (iAudioEncoded->Bytes() % sampleBlockBytes);
                const Brn padBuf(iSilenceBuffer.Ptr(), remainingBytes);
                iAudioEncoded->Append(padBuf);
            }
        }
        iDownStreamElement.Push(iAudioEncoded);
        iAudioEncoded = nullptr;
    }
}

void SupplyScd::OutputData(TUint aNumSamples, IReader& aReader)
{
    ReaderProtocolN reader(aReader, iAudioBuf);
    while (aNumSamples > 0) {
        const TUint samples = std::min(iSamplesCapacity, aNumSamples);
        iAudioBuf.SetBytes(0);
        Brn data = reader.Read((samples * iBitsPerSample) / 8);
        aNumSamples -= samples;
        while (data.Bytes() > 0) {
            if (iAudioEncoded == nullptr) {
                iAudioEncoded = iMsgFactory.CreateMsgAudioEncoded(Brx::Empty());
            }
            const TUint bytes = std::min(data.Bytes(), iBytesPerAudioMsg - iAudioEncoded->Bytes());
            Brn split = data.Split(bytes);
            data.Set(data.Ptr(), bytes);
            iAudioEncoded->Append(data);
            data.Set(split);
            if (iAudioEncoded->Bytes() == iBytesPerAudioMsg) {
                OutputEncodedAudio();
            }
        }
    }
}

void SupplyScd::OutputDataDsd(TUint aNumSamples, IReader& aReader)
{
    ReaderProtocolN reader(aReader, iAudioBuf);
    while (aNumSamples > 0) {
        const TUint samples = std::min(iSamplesCapacity, aNumSamples);
        iAudioBuf.SetBytes(0);
        Brn data = reader.Read((samples * iBitsPerSample) / 8);
        aNumSamples -= samples;
        while (data.Bytes() > 0) {
            if (iAudioEncoded == nullptr) {
                iAudioEncoded = iMsgFactory.CreateMsgAudioEncoded(Brx::Empty());
            }
            /*
             * Optimisation for pass-through case, where incoming data does not need to be repacked, saving on mem copies.
             */
            if (iDsdPadBytesPerChunk == 0) {
                const TUint bytes = std::min(data.Bytes(), iBytesPerAudioMsg - iAudioEncoded->Bytes());
                Brn split = data.Split(bytes);
                data.Set(data.Ptr(), bytes);
                iAudioEncoded->Append(data);
                data.Set(split);
            }
            else {
                TUint totalBytesPerChunk = kDsdPlayableBytesPerChunk + iDsdPadBytesPerChunk;
                TUint inputChunks = data.Bytes() / kDsdPlayableBytesPerChunk;
                TUint outputChunks = (iBytesPerAudioMsg - iAudioEncoded->Bytes()) / totalBytesPerChunk;
                const TUint remainingChunks = std::min(inputChunks, outputChunks);
                const TByte* inPtr = data.Ptr();
                for (TUint i = 0; i < remainingChunks; i++) {
                    WriteBlockDsd(inPtr);
                }
                data.Set(data.Split(remainingChunks * kDsdPlayableBytesPerChunk));
            }
            if (iAudioEncoded->Bytes() == iBytesPerAudioMsg) {
                OutputEncodedAudio();
            }
        }
    }
}

inline void SupplyScd::WriteBlockDsd(const TByte*& aPtr)
{
    const TUint paddingBytes = iDsdPadBytesPerChunk / 2;
    const Brn padBuf(iPaddingBuffer.Ptr(), paddingBytes);
    for (TUint i = 0; i < kDsdChannelCount; i++) {
        iAudioEncoded->Append(padBuf);
        const Brn audioBuf(aPtr, 2);
        iAudioEncoded->Append(audioBuf);
        aPtr += 2;
    }
}

void SupplyScd::Flush()
{
    OutputEncodedAudio();
}

void SupplyScd::Discard()
{
    if (iAudioEncoded != nullptr) {
        iAudioEncoded->RemoveRef();
        iAudioEncoded = nullptr;
    }
}

void SupplyScd::OutputTrack(Track& aTrack, TBool aStartOfStream)
{
    auto msg = iMsgFactory.CreateMsgTrack(aTrack, aStartOfStream);
    Output(msg);
}

void SupplyScd::OutputDrain(Functor aCallback)
{
    auto msg = iMsgFactory.CreateMsgDrain(aCallback);
    Output(msg);
}

void SupplyScd::OutputDelay(TUint /*aJiffies*/)
{
    ASSERTS();
}

void SupplyScd::OutputStream(const Brx& /*aUri*/,
                             TUint64 /*aTotalBytes*/, TUint64 /*aStartPos*/,
                             TBool /*aSeekable*/, TBool /*aLive*/, Multiroom /*aMultiroom*/,
                             IStreamHandler& /*aStreamHandler*/, TUint /*aStreamId*/)
{
    ASSERTS(); // only expect to receive PCM streams
}

void SupplyScd::OutputPcmStream(const Brx& aUri, TUint64 aTotalBytes,
                                TBool aSeekable, TBool aLive, Multiroom aMultiroom,
                                IStreamHandler& aStreamHandler, TUint aStreamId,
                                const PcmStreamInfo& aPcmStream)
{
    auto msg = iMsgFactory.CreateMsgEncodedStream(aUri, Brx::Empty(), aTotalBytes,
                                                  0LL, // FIXME - seek support will require that Protocol can set this
                                                  aStreamId, aSeekable, aLive, aMultiroom,
                                                  &aStreamHandler, aPcmStream);
    iIsDsd = false;
    iBitsPerSample = aPcmStream.BitDepth() * aPcmStream.NumChannels();
    const auto bytesPerSample = iBitsPerSample / 8;
    iSamplesCapacity = iAudioBuf.MaxBytes() / bytesPerSample;
    iBytesPerAudioMsg = iSamplesCapacity * bytesPerSample;
    Output(msg);
}

void SupplyScd::OutputDsdStream(const Brx& aUri, TUint64 aTotalBytes,
                                TBool aSeekable, IStreamHandler& aStreamHandler,
                                TUint aStreamId, const DsdStreamInfo& aDsdStream)
{
    auto msg = iMsgFactory.CreateMsgEncodedStream(aUri, Brx::Empty(), aTotalBytes,
                                                  0LL, aStreamId, aSeekable,
                                                  &aStreamHandler, aDsdStream);
    iIsDsd = true;
    iBitsPerSample = aDsdStream.NumChannels();
    iSamplesCapacity = (iAudioBuf.MaxBytes() * 8) / iBitsPerSample;
    iBytesPerAudioMsg = (iSamplesCapacity * iBitsPerSample) / 8;
    Output(msg);
}

void SupplyScd::OutputSegment(const Brx& /*aId*/)
{
    ASSERTS(); // chunked streams not supported.
}

void SupplyScd::OutputData(const Brx& /*aData*/)
{
    ASSERTS(); // not supported - use OutputData(TUint, IReader&) insread
}

void SupplyScd::OutputMetadata(const Brx& aMetadata)
{
    auto msg = iMsgFactory.CreateMsgMetaText(aMetadata);
    Output(msg);
}

void SupplyScd::OutputHalt(TUint aHaltId)
{
    Functor empty;
    auto msg = iMsgFactory.CreateMsgHalt(aHaltId, empty);
    Output(msg);
}

void SupplyScd::OutputFlush(TUint aFlushId)
{
    auto msg = iMsgFactory.CreateMsgFlush(aFlushId);
    Output(msg);
}

void SupplyScd::OutputWait()
{
    auto msg = iMsgFactory.CreateMsgWait();
    Output(msg);
}

void SupplyScd::Output(Msg* aMsg)
{
    OutputEncodedAudio();
    iDownStreamElement.Push(aMsg);
}

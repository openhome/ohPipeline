#include <OpenHome/Av/Scd/Receiver/SupplyScd.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Media/Pipeline/Msg.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Scd;
using namespace OpenHome::Media;

// SupplyScd

SupplyScd::SupplyScd(MsgFactory& aMsgFactory, IPipelineElementDownstream& aDownStreamElement)
    : iMsgFactory(aMsgFactory)
    , iDownStreamElement(aDownStreamElement)
    , iAudioEncoded(nullptr)
    , iBytesPerSample(0)
    , iSamplesCapacity(0)
    , iBytesPerAudioMsg(0)
{
}

SupplyScd::~SupplyScd()
{
    Discard();
}

inline void SupplyScd::OutputEncodedAudio()
{
    if (iAudioEncoded != nullptr) {
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
        Brn data = reader.Read(samples * iBytesPerSample);
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
    iBytesPerSample = (aPcmStream.BitDepth() / 8) * aPcmStream.NumChannels();
    iSamplesCapacity = iAudioBuf.MaxBytes() / iBytesPerSample;
    iBytesPerAudioMsg = iSamplesCapacity * iBytesPerSample;
    Output(msg);
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
    ASSERTS();
}

void SupplyScd::Output(Msg* aMsg)
{
    OutputEncodedAudio();
    iDownStreamElement.Push(aMsg);
}

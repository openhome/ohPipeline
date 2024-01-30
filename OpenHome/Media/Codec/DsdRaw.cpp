#include <OpenHome/Media/Codec/CodecController.h>
#include <OpenHome/Media/Codec/CodecFactory.h>
#include <OpenHome/Media/Codec/Container.h>
#include <OpenHome/Media/Codec/DsdFiller.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Media/Debug.h>

namespace OpenHome {
namespace Media {
namespace Codec {

class CodecDsdRaw
    : public CodecBase
    , private DsdFiller
{
private:
    static const TUint kInputBufferSizeMax = 4096;
public:
    CodecDsdRaw(TUint aSampleBlockWords, TUint aPaddingBytes);
private: // from CodecBase
    TBool Recognise(const EncodedStreamInfo& aStreamInfo) override;
    void StreamInitialise() override;
    void Process() override;
    TBool TrySeek(TUint aStreamId, TUint64 aSample) override;
private: // from DsdFiller
    void WriteChunkDsd(const TByte*& aSrc, TByte*& aDest) override;
    void OutputDsd(const Brx& aData) override;
private:
    const TUint iSampleBlockWords;
    const TUint iPaddingBytes;

    Bws<kInputBufferSizeMax> iInputBuffer;

    TUint iSampleRate;
    TUint iNumChannels;
    TUint64 iStartSample;

    TUint64 iTrackOffsetJiffies;
    TUint64 iTrackLengthJiffies;
};

} // namespace Codec
} // namespace Media
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Codec;

CodecBase* CodecFactory::NewDsdRaw(TUint aSampleBlockWords, TUint aPaddingBytes)
{ // static
    return new CodecDsdRaw(aSampleBlockWords, aPaddingBytes);
}


CodecDsdRaw::CodecDsdRaw(TUint aSampleBlockWords, TUint aPaddingBytes)
    : CodecBase("DSD-RAW", kCostVeryLow)
    , DsdFiller(
        (aSampleBlockWords * 4) - (aPaddingBytes * 4),  // BlockBytesInput
        (aSampleBlockWords * 4))                        // BlockBytesOutput
    , iSampleBlockWords(aSampleBlockWords)
    , iPaddingBytes(aPaddingBytes)
{
}

TBool CodecDsdRaw::Recognise(const EncodedStreamInfo& aStreamInfo)
{
    if (aStreamInfo.StreamFormat() != EncodedStreamInfo::Format::Dsd) {
        return false;
    }
    iSampleRate = aStreamInfo.SampleRate();
    iNumChannels = aStreamInfo.NumChannels();
    iStartSample = aStreamInfo.StartSample();
    return true;
}

void CodecDsdRaw::StreamInitialise()
{
    try {
        const TUint64 lenBytes = iController->StreamLength();
        const TUint64 numSamples = lenBytes * 8 / iNumChannels; // 1 bit per subsample
        iTrackLengthJiffies = numSamples * Jiffies::PerSample(iSampleRate);
        iTrackOffsetJiffies = (((TUint64)iStartSample) * Jiffies::kPerSecond) / iSampleRate;
        SpeakerProfile spStereo;
        iController->OutputDecodedStreamDsd(
            iSampleRate,
            iNumChannels,
            Brn("DSD"),
            iTrackLengthJiffies,
            iStartSample,
            spStereo);
    }
    catch (SampleRateInvalid&) {
        THROW(CodecStreamCorrupt);
    }
}

void CodecDsdRaw::Process()
{
    try {
        iInputBuffer.SetBytes(0);
        iController->ReadNextMsg(iInputBuffer);
        DsdFiller::Push(iInputBuffer);
    }
    catch (CodecStreamEnded&) {
        DsdFiller::Drain();
        throw; // caught by CodecController
    }
}

TBool CodecDsdRaw::TrySeek(TUint /*aStreamId*/, TUint64 /*aSample*/)
{
    return false;
}

void CodecDsdRaw::WriteChunkDsd(const TByte*& aSrc, TByte*& aDest)
{
    // CodecDsdRaw only pads and passes the data
    const TUint padding = iPaddingBytes / 2;
    for (TUint i = 0; i < padding; i++) {
        *aDest++ = 0x00;
    }
    *aDest++ = aSrc[0];
    *aDest++ = aSrc[1];
    for (TUint i = 0; i < padding; i++) {
        *aDest++ = 0x00;
    }
    *aDest++ = aSrc[2];
    *aDest++ = aSrc[3];
    aSrc += 4;
}

void CodecDsdRaw::OutputDsd(const Brx& aData)
{
    // Called by DsdFiller once its output buffer is full
    iTrackOffsetJiffies += iController->OutputAudioDsd(aData, iNumChannels, iSampleRate, iSampleBlockWords, iTrackOffsetJiffies, iPaddingBytes);
}
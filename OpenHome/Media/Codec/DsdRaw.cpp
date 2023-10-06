#include <OpenHome/Media/Codec/CodecController.h>
#include <OpenHome/Media/Codec/CodecFactory.h>
#include <OpenHome/Media/Codec/Container.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Media/Debug.h>

namespace OpenHome {
namespace Media {
namespace Codec {

class CodecDsdRaw : public CodecBase
{
private:
    static const TUint kInputBufferSizeMax = 4096;
    static const TUint kOutputBufferSizeMax = AudioData::kMaxBytes;
    static const TUint kPendingBufferSizeMax = 16; // FIXME - not determined programmatically - assumes input block size of 16 bytes
    static const TUint kSilenceByteDsd = 0x69;
public:
    CodecDsdRaw(TUint aSampleBlockWords, TUint aPaddingBytes);
private: // from CodecBase
    TBool Recognise(const EncodedStreamInfo& aStreamInfo) override;
    void StreamInitialise() override;
    void Process() override;
    TBool TrySeek(TUint aStreamId, TUint64 aSample) override;
private:
    const TUint iSampleBlockWords;
    const TUint iPaddingBytes;

    Bws<kInputBufferSizeMax> iInputBuffer;
    Bws<kOutputBufferSizeMax> iOutputBuffer;
    Bws<kPendingBufferSizeMax> iPending;

    TUint iSampleRate;
    TUint iNumChannels;
    TUint64 iStartSample;
    BwsCodecName iCodecName;

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
    : CodecBase("DSD", kCostVeryLow)
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
    iCodecName.Replace(aStreamInfo.CodecName());
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
            iCodecName,
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
    const TUint kSampleBlockBytes = iSampleBlockWords * 4; // output block size (expected to be 24 bytes for now)
    const TUint kSampleBlockBytesInput = kSampleBlockBytes - (iPaddingBytes * 4); // input block size (expected to be 16 bytes for now)
    const TUint kSampleBlockWordsInput = kSampleBlockBytesInput / 4;

    iInputBuffer.SetBytes(0);
    iInputBuffer.Replace(iPending);
    try {
        iController->ReadNextMsg(iInputBuffer);
        const TByte* src = iInputBuffer.Ptr();
        TUint inputBlocks = iInputBuffer.Bytes() / kSampleBlockBytesInput;
        TUint inputBytes = inputBlocks * kSampleBlockBytesInput;

        while (inputBlocks > 0) {
            TByte* dst = const_cast<TByte*>(iOutputBuffer.Ptr() + iOutputBuffer.Bytes());
            TUint outputBlocks = iOutputBuffer.BytesRemaining() / kSampleBlockBytes;
            const TUint blocks = std::min(inputBlocks, outputBlocks);
            const TUint bytes = blocks * kSampleBlockBytes;

            for (TUint i = 0; i < blocks; i++) {
                for (TUint j = 0; j < kSampleBlockWordsInput; j++) {
                    *dst++ = 0x00;
                    *dst++ = src[0];
                    *dst++ = src[1];
                    *dst++ = 0x00;
                    *dst++ = src[2];
                    *dst++ = src[3];
                    src += 4;
                }
            }
            iOutputBuffer.SetBytes(iOutputBuffer.Bytes() + bytes);
            inputBlocks -= blocks;
            outputBlocks -= blocks;

            if (outputBlocks == 0) {
                iTrackOffsetJiffies += iController->OutputAudioDsd(iOutputBuffer, iNumChannels, iSampleRate, iSampleBlockWords, iTrackOffsetJiffies, iPaddingBytes);
                iOutputBuffer.SetBytes(0);
            }
        }
        iPending.Replace(iInputBuffer.Ptr() + inputBytes, iInputBuffer.Bytes() - inputBytes);
    }
    catch (CodecStreamEnded&) {
        if (iPending.Bytes() == 0) {
            throw; // caught by CodecController
        }

        // Fill the remaining space in iPending with DSD silence to create a full sample block
        const TUint remaining = kSampleBlockBytesInput - iPending.Bytes();
        for (TUint i = iPending.Bytes(); i < iPending.Bytes() + remaining; i++) {
            iPending[i] = kSilenceByteDsd;
        }

        // Write the full sample block to output with padding
        // Guaranteed to have enough space in output buffer to accept this final sample block
        const TByte* src = iPending.Ptr();
        TByte* dst = const_cast<TByte*>(iOutputBuffer.Ptr() + iOutputBuffer.Bytes());

        for (TUint i = 0; i < kSampleBlockWordsInput; i++) {
            *dst++ = 0x00;
            *dst++ = src[0];
            *dst++ = src[1];
            *dst++ = 0x00;
            *dst++ = src[2];
            *dst++ = src[3];
            src += 4;
        }
        iOutputBuffer.SetBytes(iOutputBuffer.Bytes() + kSampleBlockBytes);
        iTrackOffsetJiffies += iController->OutputAudioDsd(iOutputBuffer, iNumChannels, iSampleRate, iSampleBlockWords, iTrackOffsetJiffies, iPaddingBytes);
        iOutputBuffer.SetBytes(0);
        iPending.SetBytes(0);
        throw; // caught by CodecController
    }
}

TBool CodecDsdRaw::TrySeek(TUint /*aStreamId*/, TUint64 /*aSample*/)
{
    return false;
}

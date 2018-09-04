#include <OpenHome/Media/Codec/AacFdkBase.h>
#include <OpenHome/Private/Arch.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Media/MimeTypeList.h>

#include <string.h>

extern "C" {
#include <aacdecoder_lib.h>
}


using namespace OpenHome;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Codec;


// CodecAacFdkBase

const Brn CodecAacFdkBase::kCodecAac("AAC");

CodecAacFdkBase::CodecAacFdkBase(const TChar* aId, IMimeTypeList& aMimeTypeList)
    : CodecBase(aId)
    , iDecoderHandle(nullptr)
{
    aMimeTypeList.Add("audio/aac");
    aMimeTypeList.Add("audio/aacp");
}

CodecAacFdkBase::~CodecAacFdkBase()
{
}

TBool CodecAacFdkBase::SupportsMimeType(const Brx& aMimeType)
{
    static const Brn kMimeAac("audio/aac");
    static const Brn kMimeAacp("audio/aacp");
    if (aMimeType == kMimeAac || aMimeType == kMimeAacp) {
        return true;
    }
    return false;
}

void CodecAacFdkBase::StreamInitialise()
{
    LOG(kCodec, "CodecAacFdkBase::StreamInitialise\n");

    iSampleRate = 0;
    iOutputSampleRate = 0;
    iBitrateMax = 0;
    iBitrateAverage = 0;
    iChannels = 0;
    iBitDepth = 0;
    iSamplesTotal = 0;
    iTotalSamplesOutput = 0;
    iTrackLengthJiffies = 0;
    iTrackOffset = 0;

    iNewStreamStarted = false;
    iStreamEnded = false;

    iInBuf.SetBytes(0);
    iOutBuf.SetBytes(0);
}

void CodecAacFdkBase::StreamCompleted()
{
    LOG(kCodec, "CodecAacFdkBase::StreamCompleted\n");
    aacDecoder_Close(iDecoderHandle);
    iDecoderHandle = nullptr;
}

TBool CodecAacFdkBase::TrySeek(TUint /*aStreamId*/, TUint64 /*aSample*/)
{
    return false;
}

void CodecAacFdkBase::Process()
{
    if (iNewStreamStarted) {
        THROW(CodecStreamStart);
    }
    if (iStreamEnded) {
        THROW(CodecStreamEnded);
    }
}

// flush any remaining samples from the decoded buffer
void CodecAacFdkBase::FlushOutput()
{
    if ((iStreamEnded || iNewStreamStarted) && iOutBuf.Bytes() > 0) {
#ifdef FDK_LITTLE_ENDIAN
        iTrackOffset += iController->OutputAudioPcm(iOutBuf, iChannels, iOutputSampleRate, iBitDepth, AudioDataEndian::Little, iTrackOffset);
#else
        iTrackOffset += iController->OutputAudioPcm(iOutBuf, iChannels, iOutputSampleRate, iBitDepth, AudioDataEndian::Big, iTrackOffset);
#endif
        iOutBuf.SetBytes(0);
    }
    //LOG(kCodec, "CodecAac::Process complete - total samples = %lld\n", iTotalSamplesOutput);
}

void CodecAacFdkBase::DecodeFrame()
{
    TUint size = iInBuf.Bytes();
    TUint valid = size;
    UINT bufBytes = size;
    const TByte* bufPtr = iInBuf.Ptr();
    while (valid > 0) {
        auto errFill = aacDecoder_Fill(iDecoderHandle, (UCHAR**) &bufPtr, &bufBytes, (UINT*) &valid);
        if (errFill != AAC_DEC_OK) {
            LOG(kCodec, "CodecAacFdkBase::DecodeFrame errFill: %u\n", errFill);
            THROW(CodecStreamCorrupt);
        }

        auto errDecode = aacDecoder_DecodeFrame(iDecoderHandle, (INT_PCM*) iOutBuf.Ptr(), (const INT) iOutBuf.BytesRemaining(), 0);

        // If AAC_DEC_TRANSPORT_SYNC_ERROR encountered should "just feed new bitstream data" (see aacdecoder_lib.h).
        if (errDecode != AAC_DEC_OK && errDecode != AAC_DEC_TRANSPORT_SYNC_ERROR) {
            LOG(kCodec, "CodecAacFdkBase::DecodeFrame errDecode: %u\n", errDecode);
            THROW(CodecStreamCorrupt);
        }

        auto* info = aacDecoder_GetStreamInfo(iDecoderHandle);
        if (info == nullptr) {
            LOG(kCodec, "CodecAacFdkBase::DecodeFrame aacDecoder_GetStreamInfo return nullptr\n");
            THROW(CodecStreamCorrupt);
        }


        // Pick up any stream changes. Only output sample rate or output channels liable to change here.
        // iOutputSampleRate and iChannels will be 0 if initial decoded stream has not yet been output - so should be output upon decoding stream and getting stream info.
        if (info->sampleRate < 0) {
            THROW(CodecStreamCorrupt);
        }
        if (info->numChannels < 0) {
            THROW(CodecStreamCorrupt);
        }

        if (iOutputSampleRate != static_cast<TUint>(info->sampleRate)
                || iChannels != static_cast<TUint>(info->numChannels)) {
            LOG(kCodec, "CodecAacFdkBase::DecodeFrame Sample rate/channel count changed. iOutputSampleRate: %u, info->sampleRate: %d, iChannels: %u, info->numChannels: %d\n", iOutputSampleRate, info->sampleRate, iChannels, info->numChannels);

            iOutputSampleRate = info->sampleRate;
            iChannels = info->numChannels;

            iController->OutputStreamInterrupted(); // Output this in case change in reported output format would cause glitching if this transitioned abruptly.
            iController->OutputDecodedStream(iBitrateAverage, iBitDepth, iOutputSampleRate, iChannels, kCodecAac, iTrackLengthJiffies, 0, false, DeriveProfile(iChannels));
        }

        const TUint frameSize = info->frameSize * info->numChannels;    // Total number of samples to output across all channels.
        const TUint bytesOut = frameSize * (iBitDepth / 8);             // Each sample is (iBitDepth / 8) bytes.
        iOutBuf.SetBytes(bytesOut);

        // Output samples
        const TUint samplesToWrite = iOutBuf.Bytes() / (iChannels * (iBitDepth / 8));   // Only output full samples, and ensure each set of samples includes all channels.
        const TUint bytes = samplesToWrite * (iBitDepth / 8) * iChannels;

        Brn outBuf(iOutBuf.Ptr(), bytes);
        if (outBuf.Bytes() > 0) {

#ifdef FDK_LITTLE_ENDIAN
            iTrackOffset += iController->OutputAudioPcm(outBuf, iChannels, iOutputSampleRate, iBitDepth, AudioDataEndian::Little, iTrackOffset);
#else
            iTrackOffset += iController->OutputAudioPcm(outBuf, iChannels, iOutputSampleRate, iBitDepth, AudioDataEndian::Big, iTrackOffset);
#endif

            iTotalSamplesOutput += samplesToWrite;

            iOutBuf.Replace(iOutBuf.Ptr() + outBuf.Bytes(), iOutBuf.Bytes() - outBuf.Bytes());
        }
        
        //LOG(kCodec, "CodecAac::iSamplesWrittenTotal: %llu\n", iTotalSamplesOutput);
    }
}

void CodecAacFdkBase::InitialiseDecoderMp4(const Brx& aAudioSpecificConfig)
{
    ASSERT(iDecoderHandle == nullptr);
    iDecoderHandle = aacDecoder_Open(TT_MP4_RAW, 1);

    // Set up decoder with "audio specific config".
    const TByte* ascPtr = aAudioSpecificConfig.Ptr();
    const TUint ascBytes = aAudioSpecificConfig.Bytes();
    auto err = aacDecoder_ConfigRaw(iDecoderHandle, (UCHAR**) &ascPtr, (const UINT*) &ascBytes);
    if (err != AAC_DEC_OK) {
        THROW(CodecStreamCorrupt);
    }
}

void CodecAacFdkBase::InitialiseDecoderAdts()
{
    ASSERT(iDecoderHandle == nullptr);
    iDecoderHandle = aacDecoder_Open(TT_MP4_ADTS, 1);
}

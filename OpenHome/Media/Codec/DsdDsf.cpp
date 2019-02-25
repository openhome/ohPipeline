#include <OpenHome/Media/Codec/CodecController.h>
#include <OpenHome/Media/Codec/CodecFactory.h>
#include <OpenHome/Media/Codec/Container.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Media/MimeTypeList.h>

#include <string.h>
#include <algorithm>

namespace OpenHome {
namespace Media {
namespace Codec {

class CodecDsdDsf : public CodecBase
{
private:
    static const TUint kDataBlockBytes = 4096;
    static const TUint kInputBufMaxBytes = 2*kDataBlockBytes; // 2 channels
    static const TUint kSubSamplesPerByte = 8;
    static const TUint kSamplesPerByte = kSubSamplesPerByte/2;
    static const TUint64 kSampleBlockRoundingMask = ~((kInputBufMaxBytes*kSamplesPerByte)-1);
    static const TUint kPlayableBytesPerChunk = 4;

    static const TUint64 kChunkHeaderBytes = 12;
    static const TUint64 kChunkDsdBytes = 28;
    static const TUint kChunkDataSize = 4;
    static const TUint kDsdChunkDataSize = 24;
    static const TUint kHeaderChunkDataSize = 8;

public:
    CodecDsdDsf(IMimeTypeList& aMimeTypeList, TUint aSampleBlockWords, TUint aPadBytesPerChunk);
private: // from CodecBase
    TBool Recognise(const EncodedStreamInfo& aStreamInfo) override;
    void StreamInitialise() override;
    void Process() override;
    TBool TrySeek(TUint aStreamId, TUint64 aSample) override;
private:
    void ProcessHeader();
    void ProcessDsdChunk();
    void ProcessFmtChunk();
    void ProcessDataChunk();
    void ProcessMetadataChunk();

    TBool ReadChunkId(const OpenHome::Brx& aBuf);
    void SendMsgDecodedStream(TUint64 aStartSample);
    TBool StreamIsValid() const;
    inline void WriteBlock(TByte*& aDest, const TByte*& aLeftPtr, const TByte*& aRightPtr, TUint aNumChunks);
    void TransferToOutputBuffer();
    void CheckReinterleave();
    void ShowBufLeader() const;

    static TUint64 LeUint64At(Brx& aBuf, TUint aOffset);
    static TUint8 ReverseBits8(TUint8 aData);
    static void LogBuf(const Brx& aBuf);


private:
    Bws<kInputBufMaxBytes> iInputBuffer;
    Bwh iOutputBuffer;
    TUint iChannelCount;
    TUint iSampleRate;
    TUint iBitDepth;
    TUint64 iAudioBytesTotal;
    TUint64 iAudioBytesRemaining;
    TUint64 iFileSize;
    TUint iBitRate;
    TUint64 iTrackStart;
    TUint64 iTrackOffsetJiffies;
    TUint64 iTrackLengthJiffies;
    TUint iBlockSizePerChannel;
    TUint iFormatVersion;
    TUint iFormatId;
    TUint iChannelType;
    TUint64 iSampleCount;
    TUint64 iAudioBytesTotalPlayable;
    TBool iInitialAudio;
    TUint64 iChunkFmtBytes;
    const TUint iSampleBlockWords;
    const TUint iPadBytesPerChunk;
    const TUint iTotalBytesPerChunk;
};

} // namespace Codec
} // namespace Media
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Codec;

CodecBase* CodecFactory::NewDsdDsf(IMimeTypeList& aMimeTypeList, TUint aSampleBlockWords, TUint aPadBytesPerChunk)
{ // static
    return new CodecDsdDsf(aMimeTypeList, aSampleBlockWords, aPadBytesPerChunk);
}

CodecDsdDsf::CodecDsdDsf(IMimeTypeList& aMimeTypeList, TUint aSampleBlockWords, TUint aPadBytesPerChunk)
    : CodecBase("DSD-DSF", kCostLow)
    , iOutputBuffer(AudioData::kMaxBytes)
    , iSampleBlockWords(aSampleBlockWords)
    , iPadBytesPerChunk(aPadBytesPerChunk)
    , iTotalBytesPerChunk(kPlayableBytesPerChunk + aPadBytesPerChunk)
{
    ASSERT((iSampleBlockWords * 4) % iTotalBytesPerChunk == 0);
    aMimeTypeList.Add("audio/dsf");
    aMimeTypeList.Add("audio/x-dsf");
}


void CodecDsdDsf::CheckReinterleave()
{
    Log::Print("DSD CheckReinterleave:\n");
    iInputBuffer.SetBytes(0);
    // left
    for (TUint i = 0 ; i < kDataBlockBytes ; ++i) {
        iInputBuffer.Append((TByte)(i&0x7f));
    }
    // right
    for (TUint i = 0 ; i < kDataBlockBytes ; ++i) {
        iInputBuffer.Append((TByte)((i&0x7f) | 0x80));
    }

    TransferToOutputBuffer();
    //ShowBufLeader();
}


void CodecDsdDsf::ShowBufLeader() const
{
    Log::Print("LF: ");
    Log::PrintHex(iInputBuffer.Split(0, 20));
    Log::Print("\n");

    Log::Print("RF: ");
    Log::PrintHex(iInputBuffer.Split(kDataBlockBytes, 20));
    Log::Print("\n");

    Log::Print("OP: ");
    Log::PrintHex(iOutputBuffer.Split(0, 60));
    Log::Print("\n");
}


void CodecDsdDsf::StreamInitialise()
{
    iChannelCount = 0;
    iSampleRate = 0;
    iBitDepth = 0;
    iBitRate = 0;
    iSampleCount = 0;

    iAudioBytesTotal = 0;
    iAudioBytesRemaining = 0;
    iAudioBytesTotalPlayable = 0;

    iFileSize = 0;
    iTrackStart = 0;
    iTrackOffsetJiffies = 0;
    iInputBuffer.SetBytes(0);
    iOutputBuffer.SetBytes(0);

    iInitialAudio = true;
}

inline void CodecDsdDsf::WriteBlock(TByte*& aDest, const TByte*& aLeftPtr, const TByte*& aRightPtr, TUint aNumChunks)
{
    // padding is MSB and PCM silence in case stream is passed to an Exakt device that tries to play it as PCM

    TUint paddingByte = iPadBytesPerChunk / 2;
    for (TUint i = 0; i < aNumChunks; i++)
    {
        for (TUint j = 0; j < paddingByte; j++) {
            *aDest++ = 0x00; // padding
        }
        *aDest++ = ReverseBits8(*aLeftPtr++);
        *aDest++ = ReverseBits8(*aLeftPtr++);

        for (TUint j = 0; j < paddingByte; j++) {
            *aDest++ = 0x00; // padding
        }
        *aDest++ = ReverseBits8(*aRightPtr++);
        *aDest++ = ReverseBits8(*aRightPtr++);
    }
}

void CodecDsdDsf::TransferToOutputBuffer()
{
    const TByte* lPtr = iInputBuffer.Ptr();
    const TByte* rPtr = lPtr + kDataBlockBytes;
    TByte* dest = const_cast<TByte*>(iOutputBuffer.Ptr() + iOutputBuffer.Bytes());
    // numChunks represents how many stereo pairs of samples make up a sampleBlock (how many groups of 16xR, 16xR)
    const TUint numChunks = iSampleBlockWords - iPadBytesPerChunk;
    TUint inputChunks = iInputBuffer.Bytes() / kPlayableBytesPerChunk;

    do {
        TUint outputChunks = iOutputBuffer.BytesRemaining() / iTotalBytesPerChunk;
        TUint remainingChunks = std::min(inputChunks, outputChunks);
        TUint numBlocks = remainingChunks / numChunks;

        for (TUint i = 0; i < numBlocks; i++)
        {
            WriteBlock(dest, lPtr, rPtr, numChunks);
        }
        iOutputBuffer.SetBytes(iOutputBuffer.Bytes() + (remainingChunks * iTotalBytesPerChunk));
        outputChunks -= remainingChunks;
        inputChunks -= remainingChunks;

        if (iAudioBytesRemaining == 0 && inputChunks == 0) // All audio has been transferred to output buffer.
        {
            TUint sampleBlockBytes = iSampleBlockWords * 4;
            TUint outputBufferBytes = iOutputBuffer.Bytes();
            TUint remainingBytes = iOutputBuffer.Bytes() % sampleBlockBytes;

            if (remainingBytes != 0)
            {
                // Pad partial end block with DSD silence to make a full block
                const TByte kDsdSilence = 0x69;
                TUint paddingBytes = sampleBlockBytes - remainingBytes;
                iOutputBuffer.SetBytes(outputBufferBytes + paddingBytes);
                for (TUint i = 0; i < paddingBytes; i++)
                {
                    iOutputBuffer[outputBufferBytes++] = kDsdSilence;
                }
            }
            iOutputBuffer.SetBytes(outputBufferBytes);
            outputChunks = 0;
        }

        if (outputChunks == 0)
        {
            iTrackOffsetJiffies += iController->OutputAudioDsd(iOutputBuffer, iChannelCount, iSampleRate, iSampleBlockWords, iTrackOffsetJiffies, iPadBytesPerChunk);
            iOutputBuffer.SetBytes(0);
            dest = const_cast<TByte*>(iOutputBuffer.Ptr());
        }

    } while (inputChunks > 0);
}

void CodecDsdDsf::LogBuf(const Brx& aBuf)
{
    Log::Print("\nLogBuf bytes= %d\n", aBuf.Bytes());
    for(TUint i=0; i<aBuf.Bytes(); ++i)
    {
        Log::Print("%x ", aBuf[i]);
    }
    Log::Print("\n\n");
}

void CodecDsdDsf::Process()
{
    if (iChannelCount == 0)  // first call
    {
        ProcessHeader();

        Log::Print("DSD:\n");
        Log::Print("  iChannelCount = %u\n", iChannelCount);
        Log::Print("  iSampleRate = %u\n", iSampleRate);
        Log::Print("  iBitDepth = %u\n", iBitDepth);
        Log::Print("  iAudioBytesTotal = %llu\n", iAudioBytesTotal);
        Log::Print("  iAudioBytesRemaining = %llu   (%lld blocks)\n", iAudioBytesRemaining, iAudioBytesRemaining/kInputBufMaxBytes);
        Log::Print("  iFileSize = %llu\n", iFileSize);
        Log::Print("  iBitRate = %u\n", iBitRate);
        //Log::Print("  iTrackStart = %llu\n", iTrackStart);
        Log::Print("  iTrackOffsetJiffies = %llu\n", iTrackOffsetJiffies);
        Log::Print("  iTrackLengthJiffies = %llu (%llu secs)\n", iTrackLengthJiffies, iTrackLengthJiffies/Jiffies::kPerSecond);
        Log::Print("  iBlockSizePerChannel = %u\n", iBlockSizePerChannel);
        Log::Print("  iFormatVersion = %u\n", iFormatVersion);
        Log::Print("  iFormatId = %u\n", iFormatId);
        Log::Print("  iChannelType = %u\n", iChannelType);
        Log::Print("  iSampleCount = %llu\n", iSampleCount);
        Log::Print("  iAudioBytesTotalPlayable = %llu\n", iAudioBytesTotalPlayable);

        SendMsgDecodedStream(0);
        iInputBuffer.SetBytes(0);
    }
    else
    {
        if (iAudioBytesRemaining == 0)
        {  // check for end of file
            THROW(CodecStreamEnded);
        }

        iInputBuffer.SetBytes(0);
        iController->Read(iInputBuffer, std::min(iInputBuffer.MaxBytes(), (TUint32)iAudioBytesRemaining));
        if (iAudioBytesRemaining >= kInputBufMaxBytes)
        {
            iAudioBytesRemaining -= kInputBufMaxBytes;
        }
        else
        {
            iAudioBytesRemaining = 0;
        }

        TransferToOutputBuffer();

        if (iInitialAudio) {
            //ShowBufLeader();
            iInitialAudio = false;
        }
    }
}

TBool CodecDsdDsf::TrySeek(TUint aStreamId, TUint64 aSample)
{
    aSample &= kSampleBlockRoundingMask; // round sample down to nearest block
    TUint64 bytePos = (aSample * iChannelCount / 8);

    ASSERT(bytePos%kInputBufMaxBytes==0);


    TUint64 headerBytes = kChunkDsdBytes+iChunkFmtBytes+kChunkHeaderBytes;

    if (!iController->TrySeekTo(aStreamId, bytePos+headerBytes))
    {
        return false;
    }

    iTrackOffsetJiffies = (TUint64)aSample * Jiffies::PerSample(iSampleRate);

    iAudioBytesRemaining = iAudioBytesTotal-bytePos;

    iInputBuffer.SetBytes(0);
    SendMsgDecodedStream(aSample);

    return true;
}

TBool CodecDsdDsf::Recognise(const EncodedStreamInfo& aStreamInfo)
{
	if (aStreamInfo.StreamFormat() != EncodedStreamInfo::Format::Encoded)
    {
        return false;
    }

    return ReadChunkId(Brn("DSD "));
}

void CodecDsdDsf::ProcessHeader()
{
    LOG(kMedia, "CodecDsdDsf::ProcessHeader()\n");

    // format of DSD header taken from http://dsd-guide.com/sites/default/files/white-papers/DSFFileFormatSpec_E.pdf

    // We expect chunks in this order:
    // - DSD chunk
    // - fmt chunk
    // - data chunk
    // - metadata chunk

    ProcessDsdChunk();
    ProcessFmtChunk();
    ProcessDataChunk();
    ProcessMetadataChunk();

}

void CodecDsdDsf::ProcessDsdChunk()
{
    //We shouldn't be in the dsd codec unless this says 'DSD '
    //This isn't a track corrupt issue as it was previously checked by Recognise
    ASSERT(ReadChunkId(Brn("DSD "))); // sets iInputBuffer bytes to 0

    iController->Read(iInputBuffer, kDsdChunkDataSize);

    if(LeUint64At(iInputBuffer, kChunkDataSize) != kChunkDsdBytes) //DSD chunk size must be 28
    {
        THROW(CodecStreamCorrupt);
    }

    iFileSize = LeUint64At(iInputBuffer, kChunkHeaderBytes);
}

void CodecDsdDsf::ProcessFmtChunk()
{
    if(!ReadChunkId(Brn("fmt "))) // sets iInputBuffer bytes to 0
    {
        THROW(CodecStreamCorrupt);
    }

    iController->Read(iInputBuffer, kHeaderChunkDataSize);

    iChunkFmtBytes = LeUint64At(iInputBuffer, kChunkDataSize);
    TUint64 chunkFmtDataBytes = iChunkFmtBytes-kChunkHeaderBytes;

    // Read in remainder of "fmt " chunk.
    iController->Read(iInputBuffer, (TUint32)chunkFmtDataBytes);

    iFormatVersion = Converter::LeUint32At(iInputBuffer, 12);
    iFormatId = Converter::LeUint32At(iInputBuffer, 16);
    iChannelType = Converter::LeUint32At(iInputBuffer, 20);
    iChannelCount = Converter::LeUint32At(iInputBuffer, 24);
    iSampleRate = Converter::LeUint32At(iInputBuffer, 28);
    iBitDepth = Converter::LeUint32At(iInputBuffer, 32);
    iSampleCount = LeUint64At(iInputBuffer, 36);
    iBlockSizePerChannel = Converter::LeUint32At(iInputBuffer, 44);
    iTrackLengthJiffies = iSampleCount * Jiffies::PerSample(iSampleRate);

    if ( (iSampleCount%8) != 0)
    {
        iSampleCount &= ~(0x7);
        Log::Print("CodecDsdDsf::ProcessFmtChunk  stream contains a partial 8 bit sample block - truncating, may cause glitch \n");
    }

    iAudioBytesTotalPlayable = 2*(iSampleCount/8);  // *2/8  (2 channels, 8 samples per byte)

    if (!StreamIsValid())
    {
        THROW(CodecStreamCorrupt);
    }

}

void CodecDsdDsf::ProcessDataChunk()
{
    if(!ReadChunkId(Brn("data"))) // sets iInputBuffer bytes to 0
    {
        THROW(CodecStreamCorrupt);
    }

    iController->Read(iInputBuffer, kHeaderChunkDataSize);

    iAudioBytesTotal = LeUint64At(iInputBuffer, kChunkDataSize)-kChunkHeaderBytes;
    if ((iAudioBytesTotal%2) !=0)
    {
        THROW(CodecStreamCorrupt);
    }

    iAudioBytesRemaining = iAudioBytesTotal;
}

void CodecDsdDsf::ProcessMetadataChunk()
{

}

void CodecDsdDsf::SendMsgDecodedStream(TUint64 aStartSample)
{
    iController->OutputDecodedStreamDsd(iSampleRate, iChannelCount, Brn("DsdDsf"), iTrackLengthJiffies, aStartSample, DeriveProfile(iChannelCount));
}


TUint64 CodecDsdDsf::LeUint64At(Brx& aBuf, TUint aOffset)
{
    TUint64 val = Converter::LeUint32At(aBuf, aOffset);
    val += ((TUint64)Converter::LeUint32At(aBuf, aOffset+4))<<32;
    return val;
}


TUint8 CodecDsdDsf::ReverseBits8(TUint8 aData)
{
    aData = (((aData & 0xaa) >> 1) | ((aData & 0x55) << 1));
    aData = (((aData & 0xcc) >> 2) | ((aData & 0x33) << 2));
    return((aData >> 4) | (aData << 4));
}


TBool CodecDsdDsf::ReadChunkId(const Brx& aId)
{
    iInputBuffer.SetBytes(0);
    iController->Read(iInputBuffer, kChunkDataSize);

    return (iInputBuffer == aId);
}

TBool CodecDsdDsf::StreamIsValid() const
{
    if (iFileSize == 0)
    {
        return false;
    }

    if ( (iBitDepth!=1) || (iChannelCount != 2) || (iSampleRate == 0) )
    {
        return false;
    }

    if (iBlockSizePerChannel!=kDataBlockBytes)
    {
        return false;
    }

    return true;
}

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
    static const TUint kOutputBufMaxBytes = kInputBufMaxBytes; 
    static const TUint64 kSampleBlockRoundingMask = ~(kInputBufMaxBytes-1);  

    static const TUint64 kChunkDsdHeaderBytes = 12;
    static const TUint64 kChunkDsdDataBytes = 28;
    static const TUint64 kChunkDsdBytes = kChunkDsdHeaderBytes+kChunkDsdDataBytes;
    
    static const TUint64 kChunkFmtHeaderBytes = 12;  // 4 for ID, 8 for data byte count, 4 data bytes
    static const TUint64 kChunkDataHeaderBytes = 12;  // 4 for ID, 8 for data byte count

    static const TUint kSampleBlockBits = 32; // audio is written out as 16x left, then 16x right

public:
    CodecDsdDsf(IMimeTypeList& aMimeTypeList);
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
    void ReinterleaveToOutputBuffer();
    void CheckReinterleave();
    void ShowBufLeader() const;

    static TUint64 LeUint64At(Brx& aBuf, TUint aOffset);
    static TUint8 ReverseBits8(TUint8 aData);

private:
    Bws<kInputBufMaxBytes> iInputBuffer;
    Bws<kOutputBufMaxBytes> iOutputBuffer; 
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
    TBool iInitialAudio;
    TUint64 iHeaderSizeBytes;
};

} // namespace Codec
} // namespace Media
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Codec;

CodecBase* CodecFactory::NewDsdDsf(IMimeTypeList& aMimeTypeList)
{ // static
    return new CodecDsdDsf(aMimeTypeList);
}

CodecDsdDsf::CodecDsdDsf(IMimeTypeList& aMimeTypeList)
    :CodecBase("DSD", kCostLow)
    ,iHeaderSizeBytes(kChunkDsdBytes+kChunkFmtHeaderBytes+kChunkDataHeaderBytes) // increased later once we know size of fmt chunk
{
    aMimeTypeList.Add("audio/dsf");
    aMimeTypeList.Add("audio/x-dsf");

    iOutputBuffer.SetBytes(iOutputBuffer.MaxBytes());
    //CheckReinterleave();
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

    ReinterleaveToOutputBuffer();
    ShowBufLeader();
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
    iFileSize = 0;
    iTrackStart = 0;
    iTrackOffsetJiffies = 0;
    iInputBuffer.SetBytes(0);

    iInitialAudio = true;
}

void CodecDsdDsf::ReinterleaveToOutputBuffer()
{
    const TByte* lPtr = iInputBuffer.Ptr();
    const TByte* rPtr = lPtr + kDataBlockBytes;
    TByte* oPtr = const_cast<TByte*>(iOutputBuffer.Ptr());

    TUint loopCount = kDataBlockBytes/2;

    for (TUint i = 0 ; i < loopCount ; ++i) {
        // pack left channel
        *oPtr++ = ReverseBits8(*lPtr++);
        *oPtr++ = ReverseBits8(*lPtr++);
        // pack right channel
        *oPtr++ = ReverseBits8(*rPtr++);
        *oPtr++ = ReverseBits8(*rPtr++);
    }
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
        Log::Print("  iAudioBytesRemaining = %llu\n", iAudioBytesRemaining);
        Log::Print("  iFileSize = %llu\n", iFileSize);
        Log::Print("  iBitRate = %u\n", iBitRate);
        Log::Print("  iTrackStart = %llu\n", iTrackStart);
        Log::Print("  iTrackOffsetJiffies = %llu\n", iTrackOffsetJiffies);
        Log::Print("  iTrackLengthJiffies = %llu (%llu secs)\n", iTrackLengthJiffies, iTrackLengthJiffies/Jiffies::kPerSecond);
        Log::Print("  iBlockSizePerChannel = %u\n", iBlockSizePerChannel);
        Log::Print("  iFormatVersion = %u\n", iFormatVersion);
        Log::Print("  iFormatId = %u\n", iFormatId);
        Log::Print("  iChannelType = %u\n", iChannelType);
        Log::Print("  iSampleCount = %llu\n", iSampleCount);


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
        iController->Read(iInputBuffer, iInputBuffer.MaxBytes());

        ReinterleaveToOutputBuffer();

        if (iInitialAudio) {
            ShowBufLeader();
            iInitialAudio = false;
        }


        iTrackOffsetJiffies += iController->OutputAudioDsd(iOutputBuffer, iChannelCount, iSampleRate, kSampleBlockBits, iTrackOffsetJiffies);
        iAudioBytesRemaining -= iInputBuffer.Bytes();
    }
}

TBool CodecDsdDsf::TrySeek(TUint aStreamId, TUint64 aSample)
{
    aSample &= kSampleBlockRoundingMask; // round down to block boundary
    TUint64 bytePos = (aSample * iChannelCount * iBitDepth / 8);
    bytePos += iHeaderSizeBytes; // account for header bytyes

    if (!iController->TrySeekTo(aStreamId, bytePos))
    {
        return false;
    }
    
    iTrackOffsetJiffies = ((TUint64)aSample * Jiffies::kPerSecond) / iSampleRate; 
    
    iInputBuffer.SetBytes(0);
    SendMsgDecodedStream(aSample);

    return true;
}




TBool CodecDsdDsf::Recognise(const EncodedStreamInfo& aStreamInfo)
{
	if (aStreamInfo.RawPcm())
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
    ASSERT(ReadChunkId(Brn("DSD ")));

    iController->Read(iInputBuffer, 24);

    if(LeUint64At(iInputBuffer, 4) != kChunkDsdDataBytes) //DSD chunk size must be 28
    {
        THROW(CodecStreamCorrupt);
    }

    iFileSize = LeUint64At(iInputBuffer, 12);
}

void CodecDsdDsf::ProcessFmtChunk()
{
    if(!ReadChunkId(Brn("fmt ")))
    {
        THROW(CodecStreamCorrupt);
    }

    iController->Read(iInputBuffer, 8);

    TUint64 chunkBytes = LeUint64At(iInputBuffer, 4);
    iHeaderSizeBytes += chunkBytes;

    // Read in remainder of "fmt " chunk.
    iController->Read(iInputBuffer, (TUint32)chunkBytes-12);

    iFormatVersion = Converter::LeUint32At(iInputBuffer, 12);
    iFormatId = Converter::LeUint32At(iInputBuffer, 16);
    iChannelType = Converter::LeUint32At(iInputBuffer, 20);
    iChannelCount = Converter::LeUint32At(iInputBuffer, 24);
    iSampleRate = Converter::LeUint32At(iInputBuffer, 28);
    iBitDepth = Converter::LeUint32At(iInputBuffer, 32);
    iSampleCount = LeUint64At(iInputBuffer, 36);
    iBlockSizePerChannel = Converter::LeUint32At(iInputBuffer, 44);
    iTrackLengthJiffies = iSampleCount * Jiffies::PerSample(iSampleRate);
    
    if (!StreamIsValid())
    {
        THROW(CodecStreamCorrupt);
    }

}

void CodecDsdDsf::ProcessDataChunk()
{
    if(!ReadChunkId(Brn("data")))
    {
        THROW(CodecStreamCorrupt);
    }

    iController->Read(iInputBuffer, 8);

    iAudioBytesTotal = (TUint32)LeUint64At(iInputBuffer, 4)-12;
    iAudioBytesRemaining = iAudioBytesTotal;
}

void CodecDsdDsf::ProcessMetadataChunk()
{

}

void CodecDsdDsf::SendMsgDecodedStream(TUint64 aStartSample)
{
    iController->OutputDecodedStreamDsd(iSampleRate, iChannelCount, Brn("Dsd"), iTrackLengthJiffies, aStartSample, DeriveProfile(iChannelCount));
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
    iController->Read(iInputBuffer, 4);

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

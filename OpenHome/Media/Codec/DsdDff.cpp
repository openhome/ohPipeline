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

class CodecDsdDff : public CodecBase
{
private:
    static const TUint kBlockSize = 1024;
    static const TUint kInputBufMaxBytes = 2*kBlockSize; 
    static const TUint kOutputBufMaxBytes = 2*kBlockSize; 
    static const TUint kSubSamplesPerByte = 8;
    static const TUint kSamplesPerByte = kSubSamplesPerByte/2;
    static const TUint64 kSampleBlockRoundingMask = ~(kInputBufMaxBytes-1); // round down to nearset block of 8 samples (1 byte)  
    static const TUint kSampleBlockBits = 32; // audio is written out as 16x left, then 16x right
    static const TUint64 kChunkHeaderBytes = 12; 

public:
    CodecDsdDff(IMimeTypeList& aMimeTypeList);
private: // from CodecBase
    TBool Recognise(const EncodedStreamInfo& aStreamInfo) override;
    void StreamInitialise() override;
    void Process() override;
    TBool TrySeek(TUint aStreamId, TUint64 aSample) override;
private:
    void ProcessFormChunk();
    void ProcessFverChunk();
    void ProcessPropChunk();
    void ProcessDsdChunk();
    TUint64 ReadChunkHeader(const Brx& aExpectedId);
    TUint64 ReadChunkHeader();
    void ReadId(const Brx& aId);
    void SendMsgDecodedStream(TUint64 aStartSample);
    void TransferToOutputBuffer();

    static TUint64 BeUint64At(Brx& aBuf, TUint aOffset);

private:
    Bws<kInputBufMaxBytes> iInputBuffer; 
    Bws<kOutputBufMaxBytes> iOutputBuffer; 
    TUint iChannelCount;
    TUint iSampleRate;
    TUint iBitDepth;
    TUint64 iAudioBytesTotal;
    TUint64 iAudioBytesRemaining;
    TUint64 iFileSizeBytes;
    TUint iBitRate;
    TUint64 iTrackOffsetJiffies;
    TUint64 iTrackLengthJiffies;
    TUint64 iSampleCount;
    TUint64 iFileHeaderSizeBytes;
};

} // namespace Codec
} // namespace Media
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Codec;


CodecBase* CodecFactory::NewDsdDff(IMimeTypeList& aMimeTypeList)
{ // static
    return new CodecDsdDff(aMimeTypeList);
}

CodecDsdDff::CodecDsdDff(IMimeTypeList& aMimeTypeList)
    :CodecBase("DSD-DFF", kCostLow)
{
    aMimeTypeList.Add("audio/dff");
    aMimeTypeList.Add("audio/x-dff");
}


TBool CodecDsdDff::Recognise(const EncodedStreamInfo& aStreamInfo)
{
    if (aStreamInfo.StreamFormat() != EncodedStreamInfo::Format::Encoded) {
        return false;
    }

    try
    {
        ReadId(Brn("FRM8"));
    }
    catch(CodecStreamCorrupt)
    {
        return false;
    }

    return true;
}


void CodecDsdDff::ProcessFormChunk()
{
   // format of DSDDFF header taken from file:///F:/Document%20Library/STANDARDS/Specifications/DSD/DSDIFF_1.5_Spec.pdf

    // We expect one FRM8 chunk containing the following chunks (in order)
    // - FVER chunk
    // - PROP chunk - contains other "local chunks"
    // - DSD chunk


    // Form chunk format:
    // 
    // ID="FRM8" - 4 bytes
    // ChunkSize - 8 bytes
    // ID="DSD " - 4 bytes

    TUint64 chunkDataBytes = ReadChunkHeader(Brn("FRM8"));
    iFileSizeBytes = kChunkHeaderBytes+chunkDataBytes;

    ReadId(Brn("DSD "));
    iFileHeaderSizeBytes += kChunkHeaderBytes+4;    

    ProcessFverChunk();
    ProcessPropChunk();
    ProcessDsdChunk();
}


void CodecDsdDff::ProcessFverChunk()
{
    // version - (ChunkSize=4) bytes  example // 0x01050000 version 1.5.0.0 DSDIFF
    TUint64 chunkDataBytes = ReadChunkHeader(Brn("FVER"));
    iFileHeaderSizeBytes += kChunkHeaderBytes+chunkDataBytes;    

    if (chunkDataBytes != 4) // this chunk size always = 4
    {
        Log::Print("CodecDsdDff::ProcessFverChunk()  corrupt! \n");
        THROW(CodecStreamCorrupt);
    }
    iController->Read(iInputBuffer, (TUint)chunkDataBytes); // read version string

}

void CodecDsdDff::ProcessDsdChunk()
{
    //Log::Print("CodecDsdDff::ProcessDsdChunk() \n");
    iAudioBytesTotal = ReadChunkHeader(Brn("DSD ")); // could be "DSD " or "DST "

    if ( (iAudioBytesTotal%2) != 0)
    {
        THROW(CodecStreamCorrupt);
    }
    
    iFileHeaderSizeBytes += kChunkHeaderBytes;   
    iAudioBytesRemaining = iAudioBytesTotal;
    Log::Print("CodecDsdDff::ProcessDsdChunk()   iAudioBytesTotal=%lld \n", iAudioBytesTotal);
}

void CodecDsdDff::ProcessPropChunk()
{
    // container chunk  - contains a number of "local"" chunks

    TUint64 chunkDataBytes = ReadChunkHeader(Brn("PROP"));
    iFileHeaderSizeBytes += kChunkHeaderBytes+chunkDataBytes;    
    if (chunkDataBytes < 4)
    {
        THROW(CodecStreamCorrupt);
    }
    
    
    TUint64 propChunkRemainingBytes = chunkDataBytes;

    ReadId(Brn("SND "));

    propChunkRemainingBytes -= 4;

    TUint64 bytesRead = 0;

    while(bytesRead<propChunkRemainingBytes)
    {
        TUint64 localChunkDataBytes = ReadChunkHeader(); // reads 12 bytes
        iController->Read(iInputBuffer, (TUint)localChunkDataBytes);

        bytesRead += 12+localChunkDataBytes;
        //Log::Print("CodecDsdDff::ProcessPropChunk() bytesRead= %lld \n", bytesRead);
        
        Brn localChunkId = iInputBuffer.Split(0, 4);
        //Log::Print("CodecDsdDff::ProcessPropChunk() id= %.*s   localchunk bytes= %lld   bytesRead= %lld  propChunkBytes= %lld \n", PBUF(localChunkId), localChunkDataBytes, bytesRead, propChunkBytes);
        
        if (localChunkId == Brn("FS  "))
        {
            //Log::Print("CodecDsdDff::ProcessPropChunk()  FS \n");
            iSampleRate = Converter::BeUint32At(iInputBuffer, 12);
        }
        else if (localChunkId == Brn("CHNL"))
        {
            //Log::Print("CodecDsdDff::ProcessPropChunk()  CHNL \n");
            iChannelCount = Converter::BeUint16At(iInputBuffer, 12);
            if (iChannelCount!=2)
            {
                Log::Print("CodecDsdDff::ProcessPropChunk()  CHNL  iChannelCount!=2 unsupported \n");
                THROW(CodecStreamFeatureUnsupported);
            }
        }
        else if (localChunkId == Brn("CMPR"))
        {
            //Log::Print("CodecDsdDff::ProcessPropChunk()  CMPR \n");

        }        
        else if (localChunkId == Brn("ABSS"))
        {
            //Log::Print("CodecDsdDff::ProcessPropChunk() ABSS \n");

        }        
        else if (localChunkId == Brn("LSCO"))
        {
            //Log::Print("CodecDsdDff::ProcessPropChunk()  LSCO \n");
        }
    }
}


void CodecDsdDff::StreamInitialise()
{
    //Log::Print("CodecDsdDff::StreamInitialise()  \n");
    iFileHeaderSizeBytes = 0;
    iChannelCount = 0;
    iSampleCount = 0;
    iBitDepth = 0;
    iSampleRate = 0;
    iBitRate = 0;
    iAudioBytesTotal = 0;
    iAudioBytesRemaining = 0;
    iTrackOffsetJiffies = 0;
    iTrackLengthJiffies = 0;

    ProcessFormChunk(); // Could throw CodecStreamEnded/CodecStreamCorrupt.

    iSampleCount = iAudioBytesTotal * 4; // *8 samples per byte, /2 stereo samples


    Log::Print("DSDDFF:\n");
    Log::Print("  iChannelCount = %u\n", iChannelCount);
    Log::Print("  iSampleRate = %u\n", iSampleRate);
    Log::Print("  iBitDepth = %u\n", iBitDepth);
    Log::Print("  iAudioBytesTotal = %llu\n", iAudioBytesTotal);
    Log::Print("  iAudioBytesRemaining = %llu\n", iAudioBytesRemaining);
    Log::Print("  iFileSizeBytes = %llu\n", iFileSizeBytes);
    Log::Print("  iBitRate = %u\n", iBitRate);
    Log::Print("  iTrackOffsetJiffies = %llu\n", iTrackOffsetJiffies);
    Log::Print("  iTrackLengthJiffies = %llu\n", iTrackLengthJiffies);
    Log::Print("  iSampleCount = %llu\n", iSampleCount);

    iTrackLengthJiffies = iSampleCount * Jiffies::PerSample(iSampleRate);
    iOutputBuffer.SetBytes(iOutputBuffer.MaxBytes()); 

    SendMsgDecodedStream(0);
}

void CodecDsdDff::Process()
{
    if (iAudioBytesRemaining == 0)
    {  
        THROW(CodecStreamEnded);
    }

    iInputBuffer.SetBytes(0);
    TUint32 bytesToRead = std::min(iInputBuffer.MaxBytes(), (TUint32)iAudioBytesRemaining);
    iController->Read(iInputBuffer, bytesToRead);

    iAudioBytesRemaining -= iInputBuffer.Bytes();
    if ( (iInputBuffer.Bytes()%2) != 0)
    {
        THROW(CodecStreamCorrupt);
    }

    TransferToOutputBuffer();

    iTrackOffsetJiffies += iController->OutputAudioDsd(iOutputBuffer, iChannelCount, iSampleRate, kSampleBlockBits, iTrackOffsetJiffies);
}

void CodecDsdDff::TransferToOutputBuffer()
{
    const TByte* inPtr = iInputBuffer.Ptr();
    TByte* oPtr = const_cast<TByte*>(iOutputBuffer.Ptr());

    TUint loopCount = iInputBuffer.Bytes()/4;

    for (TUint i = 0 ; i < loopCount ; ++i) 
    {
        // pack left channel
        oPtr[0] = inPtr[0];
        oPtr[1] = inPtr[2];
        // pack right channel
        oPtr[2] = inPtr[1];
        oPtr[3] = inPtr[3];
        // advance i/o ptrs
        oPtr += 4;
        inPtr += 4;
    }

    // end of stream - deal with padding
    if (iAudioBytesRemaining==0)
    {
        TUint outputBufferBytes = iInputBuffer.Bytes();
        
        ASSERT( (outputBufferBytes%2) == 0); // this shouldn't happen - we checked for it earlier
        
        if ( (outputBufferBytes%4) != 0) // stream ends with half a word
        {
            //Log::Print("CodecDsdDff::TransferToOutputBuffer()      outputBufferBytes=%x   padding partial word at end of stream ######################################\n", outputBufferBytes);
            // Pad partial end word, with (DSD) silence, to make a full word
            const TByte kDsdSilence = 0x69;
            outputBufferBytes += 2; // increase output byte count to accommodate padding
            iOutputBuffer[outputBufferBytes-1] = kDsdSilence; // left channel
            iOutputBuffer[outputBufferBytes-3] = kDsdSilence; // right channel
        }

        iOutputBuffer.SetBytes(outputBufferBytes); 
    }
}

TBool CodecDsdDff::TrySeek(TUint aStreamId, TUint64 aSample)
{
    aSample &= kSampleBlockRoundingMask; // round down to block boundary

    TUint64 bytePos = (aSample * iChannelCount / 8);

    if (!iController->TrySeekTo(aStreamId, bytePos+iFileHeaderSizeBytes))
    {
        return false;
    }
    
    iAudioBytesRemaining = iAudioBytesTotal-bytePos;
    iTrackOffsetJiffies = ((TUint64)aSample * Jiffies::kPerSecond) / iSampleRate; 

    iInputBuffer.SetBytes(0);
    SendMsgDecodedStream(aSample);

    return true;
}


void CodecDsdDff::SendMsgDecodedStream(TUint64 aStartSample)
{
    iController->OutputDecodedStreamDsd(iSampleRate, iChannelCount, Brn("DsdDff"), iTrackLengthJiffies, aStartSample, DeriveProfile(iChannelCount));
}

void CodecDsdDff::ReadId(const Brx& aId)
{
    Bws<4> id;
    iController->Read(id, 4);
    if (id != aId)
    {
        THROW(CodecStreamCorrupt);
    }
}

TUint64 CodecDsdDff::ReadChunkHeader(const Brx& aExpectedId)
{
    
    TUint64 dataByteCount = 0;

    for(;;)
    {
        dataByteCount = ReadChunkHeader();

        if ( iInputBuffer.Bytes()<4 )
        {
            //Log::Print("CodecDsdDff::ReadChunkHeader(%.*s)  read less than 4 bytes - corrupt\n", PBUF(aExpectedId));
            THROW(CodecStreamCorrupt);
        }
        else if (iInputBuffer.Split(0, 4) == aExpectedId)
        {
            //Log::Print("CodecDsdDff::ReadChunkHeader(%.*s)  found chunk  dataByteCount= %d\n", PBUF(aExpectedId), dataByteCount);
            break;
        }
        else
        {
            //Log::Print("CodecDsdDff::ReadChunkHeader(%.*s)  discarding chunk %.*s  data\n", PBUF(aExpectedId), PBUF(iInputBuffer.Split(0, 4)));
            TUint64 reminingBytes = dataByteCount;
            while(reminingBytes>0)
            {
                iInputBuffer.SetBytes(0);
                iController->Read(iInputBuffer, (TUint)std::min(reminingBytes, (TUint64)kInputBufMaxBytes)); // read and discard unexpected chunk's data
                reminingBytes -= iInputBuffer.Bytes();
            }
        }
    }

    return dataByteCount;
}

TUint64 CodecDsdDff::ReadChunkHeader()
{
    // Header format:
    //
    // ID - 4 bytes
    // Chunk data size in bytes - 8 bytes 
    // Chunk data - n bytes (n=Chunk data size)

    iInputBuffer.SetBytes(0);
    iController->Read(iInputBuffer, 12);
    TUint64 dataByteCount = BeUint64At(iInputBuffer, 4);
    return dataByteCount;
}


TUint64 CodecDsdDff::BeUint64At(Brx& aBuf, TUint aOffset)
{
    TUint64 val = Converter::BeUint32At(aBuf, aOffset);
    val <<= 32;
    val += ((TUint64)Converter::BeUint32At(aBuf, aOffset+4));
    return val;
}


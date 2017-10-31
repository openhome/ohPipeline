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
    static const TUint kInputBufMaxBytes = 2*kBlockSize; //DecodedAudio::kMaxBytes;
    static const TUint kOutputBufMaxBytes = 2*kBlockSize; 

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
    Bws<kInputBufMaxBytes> iInputBuf; 
    Bws<kOutputBufMaxBytes> iOutputBuf; 
    TUint iChannelCount;
    TUint iSampleRate;
    TUint iBitDepth;
    TUint64 iAudioBytesTotal;
    TUint64 iAudioBytesRemaining;
    TUint64 iFileSize;
    TUint iBitRate;
    TUint64 iTrackStart;
    TUint64 iTrackOffset;
    TUint64 iTrackLengthJiffies;
    TUint64 iSampleCount;
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
    : CodecBase("DSDDFF", kCostLow)
{
    aMimeTypeList.Add("audio/dff");
    aMimeTypeList.Add("audio/x-dff");
    iOutputBuf.SetBytes(iOutputBuf.MaxBytes());
}


TBool CodecDsdDff::Recognise(const EncodedStreamInfo& aStreamInfo)
{
    if (aStreamInfo.RawPcm()) {
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

    iFileSize = ReadChunkHeader(Brn("FRM8")) + 12;

    ReadId(Brn("DSD "));

    // could also read ChunkSize here and check that it's a sensible value
    ProcessFverChunk();
    ProcessPropChunk();
    ProcessDsdChunk();
}


void CodecDsdDff::ProcessFverChunk()
{
    // version - (ChunkSize=4) bytes  example // 0x01050000 version 1.5.0.0 DSDIFF
    TUint64 chunkBytes = ReadChunkHeader(Brn("FVER"));

    if (chunkBytes != 4) // this chunk size always = 4
    {
        Log::Print("CodecDsdDff::ProcessFverChunk()  corrupt! \n");
        THROW(CodecStreamCorrupt);
    }
    iController->Read(iInputBuf, (TUint)chunkBytes); // read version string
}

void CodecDsdDff::ProcessDsdChunk()
{
    //Log::Print("CodecDsdDff::ProcessDsdChunk() \n");
    iAudioBytesTotal = ReadChunkHeader(Brn("DSD ")); // could be "DSD " or "DST "
    iAudioBytesRemaining = iAudioBytesTotal;
    //Log::Print("CodecDsdDff::ProcessDsdChunk()   iAudioBytesTotal=%lld \n", iAudioBytesTotal);
}

void CodecDsdDff::ProcessPropChunk()
{
    // container chunk  - contains a number of "local"" chunks

    TUint64 propChunkBytes = ReadChunkHeader(Brn("PROP"));
    if (propChunkBytes < 4)
    {
        THROW(CodecStreamCorrupt);
    }
    ReadId(Brn("SND "));

    propChunkBytes -= 4;

    TUint64 bytesRead = 0;

    while(bytesRead<propChunkBytes)
    {
        TUint64 localChunkDataBytes = ReadChunkHeader(); // reads 12 bytes
        iController->Read(iInputBuf, (TUint)localChunkDataBytes);

        bytesRead += 12+localChunkDataBytes;
        //Log::Print("CodecDsdDff::ProcessPropChunk() bytesRead= %lld \n", bytesRead);
        
        Brn localChunkId = iInputBuf.Split(0, 4);
        //Log::Print("CodecDsdDff::ProcessPropChunk() id= %.*s   localchunk bytes= %lld   bytesRead= %lld  propChunkBytes= %lld \n", PBUF(localChunkId), localChunkDataBytes, bytesRead, propChunkBytes);
        
        if (localChunkId == Brn("FS  "))
        {
            //Log::Print("CodecDsdDff::ProcessPropChunk()  FS \n");
            iSampleRate = Converter::BeUint32At(iInputBuf, 12);
        }
        else if (localChunkId == Brn("CHNL"))
        {
            //Log::Print("CodecDsdDff::ProcessPropChunk()  CHNL \n");
            iChannelCount = Converter::BeUint16At(iInputBuf, 12);
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
    iChannelCount = 0;
    iSampleCount = 0;
    iBitDepth = 0;
    iSampleRate = 0;
    iBitRate = 0;
    iAudioBytesTotal = 0;
    iAudioBytesRemaining = 0;
    iTrackStart = 0;
    iTrackOffset = 0;
    iTrackLengthJiffies = 0;

    ProcessFormChunk(); // Could throw CodecStreamEnded/CodecStreamCorrupt.

    iSampleCount = iAudioBytesTotal * 8 / 2; // *8 samples per byte, /2 stereo samples


    Log::Print("DSDDFF:\n");
    Log::Print("  iChannelCount = %u\n", iChannelCount);
    Log::Print("  iSampleRate = %u\n", iSampleRate);
    Log::Print("  iBitDepth = %u\n", iBitDepth);
    Log::Print("  iAudioBytesTotal = %llu\n", iAudioBytesTotal);
    Log::Print("  iAudioBytesRemaining = %llu\n", iAudioBytesRemaining);
    Log::Print("  iFileSize = %llu\n", iFileSize);
    Log::Print("  iBitRate = %u\n", iBitRate);
    Log::Print("  iTrackStart = %llu\n", iTrackStart);
    Log::Print("  iTrackOffset = %llu\n", iTrackOffset);
    Log::Print("  iTrackLengthJiffies = %llu\n", iTrackLengthJiffies);
    //Log::Print("  iFormatVersion = %u\n", iFormatVersion);
    //Log::Print("  iFormatId = %u\n", iFormatId);
    //Log::Print("  iChannelType = %u\n", iChannelType);
    Log::Print("  iSampleCount = %llu\n", iSampleCount);

    // now fake some values to get it through "PCM" stream validation in pipeline:
    iTrackLengthJiffies = iSampleCount * Jiffies::PerSample(iSampleRate);
    
    SendMsgDecodedStream(0);
}

void CodecDsdDff::Process()
{
    if (iAudioBytesRemaining == 0)
    {  // check for end of file
        //Log::Print("CodecDsdDff::Process()  CodecStreamEnded  \n");
        THROW(CodecStreamEnded);
    }

    iInputBuf.SetBytes(0);
    iController->Read(iInputBuf, iInputBuf.MaxBytes());

    TransferToOutputBuffer();

    iTrackOffset += iController->OutputAudioDsd(iOutputBuf, iChannelCount, iSampleRate, iTrackOffset);
    iAudioBytesRemaining -= iInputBuf.Bytes();
}

void CodecDsdDff::TransferToOutputBuffer()
{
    LOG(kCodec, ">CodecDsdDff::TransferToOutputBuffer()\n");
    const TByte* inPtr = iInputBuf.Ptr();
    TByte* oPtr = const_cast<TByte*>(iOutputBuf.Ptr());

    TUint loopCount = kBlockSize/2;

    for (TUint i = 0 ; i < loopCount ; ++i) {
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
}

TBool CodecDsdDff::TrySeek(TUint /*aStreamId*/, TUint64 /*aSample*/)
{
    return false;
}

void CodecDsdDff::SendMsgDecodedStream(TUint64 aStartSample)
{
    iController->OutputDecodedStreamDsd(iSampleRate, iChannelCount, Brn("Dsd"), iTrackLengthJiffies, aStartSample, DeriveProfile(iChannelCount));
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

        if ( iInputBuf.Bytes()<4 )
        {
            //Log::Print("CodecDsdDff::ReadChunkHeader(%.*s)  read less than 4 bytes - corrupt\n", PBUF(aExpectedId));
            THROW(CodecStreamCorrupt);
        }
        else if (iInputBuf.Split(0, 4) == aExpectedId)
        {
            //Log::Print("CodecDsdDff::ReadChunkHeader(%.*s)  found chunk  dataByteCount= %d\n", PBUF(aExpectedId), dataByteCount);
            break;
        }
        else
        {
            //Log::Print("CodecDsdDff::ReadChunkHeader(%.*s)  discarding chunk %.*s  data\n", PBUF(aExpectedId), PBUF(iInputBuf.Split(0, 4)));
            TUint64 reminingBytes = dataByteCount;
            while(reminingBytes>0)
            {
                iInputBuf.SetBytes(0);
                iController->Read(iInputBuf, (TUint)std::min(reminingBytes, (TUint64)kInputBufMaxBytes)); // read and discard unexpected chunk's data
                reminingBytes -= iInputBuf.Bytes();
            }
        }
    }

    return dataByteCount;
}

TUint64 CodecDsdDff::ReadChunkHeader()
{
    iInputBuf.SetBytes(0);
    iController->Read(iInputBuf, 12);
    TUint64 dataByteCount = BeUint64At(iInputBuf, 4);
    return dataByteCount;
}


TUint64 CodecDsdDff::BeUint64At(Brx& aBuf, TUint aOffset)
{
    TUint64 val = Converter::BeUint32At(aBuf, aOffset);
    val <<= 32;
    val += ((TUint64)Converter::BeUint32At(aBuf, aOffset+4));
    return val;
}


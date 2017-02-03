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

class CodecDsd : public CodecBase
{
public:
    CodecDsd(IMimeTypeList& aMimeTypeList);
    ~CodecDsd();
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
    static TUint64 LeUint64At(Brx& aBuf, TUint aOffset);
    void SendMsgDecodedStream();
    TBool StreamIsValid() const;

private:
    Bws<DecodedAudio::kMaxBytes + 40> iReadBuf; // +40 to accommodate a fragment of a following (10ch, 32-bit) sample
    TUint iChannelCount;
    TUint iSampleRate;
    TUint iBitDepth;
    TUint64 iAudioBytesTotal;
    TUint iAudioBytesRemaining;
    TUint64 iFileSize;
    TUint iBitRate;
    TUint64 iTrackStart;
    TUint64 iTrackOffset;
    TUint64 iTrackLengthJiffies;
    TUint iBlockSizePerChannel;
    TUint iFormatVersion;
    TUint iFormatId;
    TUint iChannelType;
    TUint64 iSampleCount;
};

} // namespace Codec
} // namespace Media
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Codec;

CodecBase* CodecFactory::NewDsd(IMimeTypeList& aMimeTypeList)
{ // static
    return new CodecDsd(aMimeTypeList);
}

CodecDsd::CodecDsd(IMimeTypeList& aMimeTypeList)
    : CodecBase("DSD", kCostLow)
{
    aMimeTypeList.Add("audio/dsd");
    aMimeTypeList.Add("audio/x-dsd");
    aMimeTypeList.Add("audio/x-dsf");
}

CodecDsd::~CodecDsd()
{

}

void CodecDsd::StreamInitialise()
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
    iTrackOffset = 0;
    iReadBuf.SetBytes(0);
}

void CodecDsd::Process()
{
    if (iAudioBytesRemaining == 0) {  // check for end of file unless continuous streaming - ie iFileSize == 0
        THROW(CodecStreamEnded);
    }

    iController->ReadNextMsg(iReadBuf);

    TUint bufBytes = iReadBuf.Bytes();
    bufBytes = std::min(bufBytes, iAudioBytesRemaining);
    Brn split = iReadBuf.Split(bufBytes);
    iReadBuf.SetBytes(bufBytes);

    iTrackOffset += iController->OutputAudioPcm(iReadBuf, iChannelCount, iSampleRate, iBitDepth, AudioDataEndian::Little, iTrackOffset);
    iAudioBytesRemaining -= iReadBuf.Bytes();
    iReadBuf.Replace(split);
}

TBool CodecDsd::TrySeek(TUint /*aStreamId*/, TUint64 /*aSample*/)
{
/*
    const TUint byteDepth = iBitDepth/8;
    const TUint64 bytePos = aSample * iChannelCount * byteDepth;

    // Some bounds checking.
    const TUint64 seekPosJiffies = Jiffies::PerSample(iSampleRate)*aSample;
    if (seekPosJiffies > iTrackLengthJiffies) {
        return false;
    }

    if (!iController->TrySeekTo(aStreamId, iTrackStart + bytePos)) {
        return false;
    }
    iTrackOffset = ((TUint64)aSample * Jiffies::kPerSecond) / iSampleRate;
    if(iFileSize != 0) {    // UI should not allow seeking within streamed audio, but check before updating track length anyhow
        iAudioBytesRemaining = iAudioBytesTotal - (TUint)(aSample * iChannelCount * byteDepth);
    }

    iReadBuf.SetBytes(0);
    SendMsgDecodedStream(aSample);
    return true;
*/
    return false;
}

TBool CodecDsd::Recognise(const EncodedStreamInfo& aStreamInfo)
{
    if (aStreamInfo.RawPcm())
    {
        return false;
    }

    return ReadChunkId(Brn("DSD "));
}

void CodecDsd::ProcessHeader()
{
    LOG(kMedia, "Wav::ProcessHeader()\n");

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


TBool CodecDsd::ReadChunkId(const Brx& aId)
{
    iReadBuf.SetBytes(0);
    iController->Read(iReadBuf, 4);
    if (iReadBuf.Bytes() < 4)
    {
        THROW(CodecStreamEnded); // could be a network error
    }

    return (iReadBuf == aId);
}

void CodecDsd::ProcessDsdChunk()
{
    //We shouldn't be in the wav codec unless this says 'DSD '
    //This isn't a track corrupt issue as it was previously checked by Recognise
    ASSERT(ReadChunkId(Brn("DSD ")));

    iController->Read(iReadBuf, 24);
    if (iReadBuf.Bytes() != 28)
    {
        THROW(CodecStreamEnded); // could be a network error
    }

    if(LeUint64At(iReadBuf, 4) != 28) //DSD chunk size must be 28
    {
        THROW(CodecStreamCorrupt);
    }

    iFileSize = LeUint64At(iReadBuf, 12);
}

void CodecDsd::ProcessFmtChunk()
{
    if(!ReadChunkId(Brn("fmt ")))
    {
        THROW(CodecStreamCorrupt);
    }

    iController->Read(iReadBuf, 8);
    if (iReadBuf.Bytes() < 12)
    {
        THROW(CodecStreamEnded); // could be a network error
    }

    TUint64 chunkBytes = LeUint64At(iReadBuf, 4);

    // Read in remainder of "fmt " chunk.
    iController->Read(iReadBuf, (TUint32)chunkBytes-12);
    if (iReadBuf.Bytes() != chunkBytes)
    {
        THROW(CodecStreamEnded);
    }

    iFormatVersion = Converter::LeUint32At(iReadBuf, 12);
    iFormatId = Converter::LeUint32At(iReadBuf, 16);
    iChannelType = Converter::LeUint32At(iReadBuf, 20);
    iChannelCount = Converter::LeUint32At(iReadBuf, 24);
    iSampleRate = Converter::LeUint32At(iReadBuf, 28);
    iBitDepth = Converter::LeUint32At(iReadBuf, 32);
    iSampleCount = LeUint64At(iReadBuf, 36);
    iBlockSizePerChannel = Converter::LeUint32At(iReadBuf, 44);
    iBitRate = iSampleRate * iBitDepth;
    //reserved = Converter::LeUint32At(iReadBuf, 48);

    if (!StreamIsValid())
    {
        THROW(CodecStreamCorrupt);
    }

//    iTrackStart += fmtChunkBytes + 8;
}

TBool CodecDsd::StreamIsValid() const
{
    if (iFileSize == 0)
    {
        return false;
    }

    if ( (iBitDepth!=1) || (iChannelCount != 2) || (iSampleRate == 0) )
    {
        return false;
    }

    if (iBlockSizePerChannel!=4096)
    {
        return false;
    }

    return true;
}


void CodecDsd::ProcessDataChunk()
{
    if(!ReadChunkId(Brn("data")))
    {
        THROW(CodecStreamCorrupt);
    }

    iController->Read(iReadBuf, 8);
    if (iReadBuf.Bytes() < 12)
    {
        THROW(CodecStreamEnded); // could be a network error
    }

    iAudioBytesTotal = (TUint32)LeUint64At(iReadBuf, 4)-12;

    iTrackLengthJiffies = (iSampleCount * Jiffies::kPerSecond) / iSampleRate;
}

void CodecDsd::ProcessMetadataChunk()
{

}

void CodecDsd::SendMsgDecodedStream()
{
    iController->OutputDecodedStreamDsd(iBitRate, iSampleRate, iTrackLengthJiffies);
}


TUint64 CodecDsd::LeUint64At(Brx& aBuf, TUint aOffset)
{
    TUint64 val = Converter::LeUint32At(aBuf, aOffset);
    val += ((TUint64)Converter::LeUint32At(aBuf, aOffset+4))<<32;
    return val;
}
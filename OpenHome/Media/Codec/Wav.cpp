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

class CodecWav : public CodecBase
{
public:
    CodecWav(IMimeTypeList& aMimeTypeList);
    ~CodecWav();
private: // from CodecBase
    TBool Recognise(const EncodedStreamInfo& aStreamInfo);
    void StreamInitialise();
    void Process();
    TBool TrySeek(TUint aStreamId, TUint64 aSample);
private:
    void ProcessHeader();
    void ProcessRiffChunk();
    void ProcessFmtChunk();
    void ProcessDataChunk();
    TUint FindChunk(const Brx& aChunkId);
    void SendMsgDecodedStream(TUint64 aStartSample);
    void WriteSamples(TByte*& aDest, TUint& aSamplesWritten, TUint aSamplesDest);
    void WriteSamples(TByte*& aDest, const TByte* aSrc, TUint aSamples);
    void ClearAudioEncoded();
private:
    Bws<DecodedAudio::kMaxBytes + 40> iReadBuf; // +40 to accommodate a fragment of a following (10ch, 32-bit) sample
    TUint iNumChannels;
    TUint iSampleRate;
    TUint iBitDepth;
    TUint iBitDepthSrc;
    TUint iAudioBytesTotal;
    TUint iAudioBytesRemaining;
    TUint iFileSize;
    TUint iBitRate;
    TUint iSampleBytesSrc;
    TUint iSampleBytesDest;
    TUint64 iTrackStart;
    TUint64 iTrackOffset;
    TUint64 iTrackLengthJiffies;
    Bws<40> iPartialSample;
    MsgAudioEncoded* iAudioEncoded;
    TUint iAudioEncodedBytesConsumed;
};

} // namespace Codec
} // namespace Media
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Codec;

CodecBase* CodecFactory::NewWav(IMimeTypeList& aMimeTypeList)
{ // static
    return new CodecWav(aMimeTypeList);
}



CodecWav::CodecWav(IMimeTypeList& aMimeTypeList)
    : CodecBase("WAV", kCostLow)
    , iAudioEncoded(nullptr)
{
    aMimeTypeList.Add("audio/wav");
    aMimeTypeList.Add("audio/wave");
    aMimeTypeList.Add("audio/x-wav");
}

CodecWav::~CodecWav()
{
    ClearAudioEncoded();
}

TBool CodecWav::Recognise(const EncodedStreamInfo& aStreamInfo)
{
    if (aStreamInfo.StreamFormat() != EncodedStreamInfo::Format::Encoded) {
        return false;
    }
    Bws<12> buf;
    iController->Read(buf, buf.MaxBytes());
    const TChar* ptr = reinterpret_cast<const TChar*>(buf.Ptr());
    if (buf.Bytes() == 12 && strncmp(ptr, "RIFF", 4) == 0 && strncmp(ptr+8, "WAVE", 4) == 0) {
        return true;
    }
#if 0 // debug helper
    Log::Print("CodecWav::Recognise() failed.  Was passed data \n  ");
    for (TUint i=0; i<buf.Bytes(); i++) {
        Log::Print(" %02x", buf[i]);
    }
    Log::Print("\n");
#endif
    return false;
}

void CodecWav::StreamInitialise()
{
    iNumChannels = 0;
    iSampleRate = 0;
    iBitDepth = 0;
    iBitDepthSrc = 0;
    iSampleBytesSrc = 0;
    iSampleBytesDest = 0;
    iAudioBytesTotal = 0;
    iAudioBytesRemaining = 0;
    iFileSize = 0;
    iTrackStart = 0;
    iTrackOffset = 0;
    iReadBuf.SetBytes(0);
    ClearAudioEncoded();
}

void CodecWav::Process()
{
    if (iNumChannels == 0) {
        ProcessHeader();
        SendMsgDecodedStream(0);
        iReadBuf.SetBytes(0);
    }
    else {
        if ((iAudioBytesRemaining == 0) && (iFileSize != 0)) {  // check for end of file unless continuous streaming - ie iFileSize == 0
            THROW(CodecStreamEnded);
        }
        TByte* dest;
        TUint samplesDest;
        iController->GetAudioBuf(dest, samplesDest);
        TUint samplesWritten = 0;
        if (iAudioEncoded != nullptr) {
            WriteSamples(dest, samplesWritten, samplesDest);
        }
        try {
            if (samplesWritten < samplesDest) {
                auto encoded = iController->ReadNextMsg();
                if (iAudioEncoded == nullptr || iAudioEncoded->Bytes() == iAudioEncodedBytesConsumed) {
                    iAudioEncodedBytesConsumed = 0;
                }
                else {
                    Bws<40> sampleBuf;
                    sampleBuf.Append(iAudioEncoded->AudioData().Ptr(iAudioEncoded->AudioDataOffset() + iAudioEncodedBytesConsumed),
                        iAudioEncoded->Bytes() - iAudioEncodedBytesConsumed);
                    const auto data2BytesRequired = iSampleBytesDest - sampleBuf.Bytes();
                    if (data2BytesRequired > encoded->Bytes()) {
                        THROW(CodecStreamCorrupt);
                    }
                    sampleBuf.Append(encoded->AudioData().Ptr(encoded->AudioDataOffset()), data2BytesRequired);
                    WriteSamples(dest, sampleBuf.Ptr(), 1);
                    iAudioEncodedBytesConsumed = data2BytesRequired;
                    samplesWritten++;
                }
                if (iAudioEncoded != nullptr) {
                    iAudioEncoded->RemoveRef();
                }
                iAudioEncoded = encoded;
            }

            if (samplesWritten < samplesDest) {
                WriteSamples(dest, samplesWritten, samplesDest - samplesWritten);
            }
        }
        catch (Exception&) {
            if (samplesWritten != 0) {
                iController->OutputAudioBuf(samplesWritten, iTrackOffset);
            }
            throw;
        }

        iController->OutputAudioBuf(samplesWritten, iTrackOffset);
    }
}

TBool CodecWav::TrySeek(TUint aStreamId, TUint64 aSample)
{
    const TUint64 bytePos = aSample * iSampleBytesSrc;

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
        iAudioBytesRemaining = iAudioBytesTotal - (TUint)(aSample * iSampleBytesSrc);
        // Truncate iAudioBytesRemaining to a sensible sample boundary.
        const TUint remaining = iAudioBytesRemaining % iSampleBytesSrc;
        iAudioBytesRemaining -= remaining;
    }

    iReadBuf.SetBytes(0);
    ClearAudioEncoded();
    SendMsgDecodedStream(aSample);
    return true;
}

void CodecWav::ProcessHeader()
{
    LOG(kMedia, "Wav::ProcessHeader()\n");

    // format of WAV header taken from https://ccrma.stanford.edu/courses/422/projects/WaveFormat/
    // More useful description of WAV file format: http://www-mmsp.ece.mcgill.ca/Documents/AudioFormats/WAVE/WAVE.html

    // We expect chunks in this order:
    // - RIFF chunk
    // - fmt chunk
    // - LIST/INFO chunk (optional)
    // - data chunk

    ProcessRiffChunk();
    ProcessFmtChunk();
    ProcessDataChunk();
}

void CodecWav::ProcessRiffChunk()
{
    iReadBuf.SetBytes(0);
    iController->Read(iReadBuf, 12);
    if (iReadBuf.Bytes() < 12) {
        THROW(CodecStreamEnded);
    }
    const TByte* header = iReadBuf.Ptr();

    //We shouldn't be in the wav codec unless this says 'RIFF'
    //This isn't a track corrupt issue as it was previously checked by Recognise
    ASSERT(strncmp((const TChar*)header, "RIFF", 4) == 0);

    // Get the file size
    iFileSize = (header[7]<<24) | (header[6]<<16) | (header[5]<<8) | header[4]; // file size of zero indicates a continuous stream

    //We shouldn't be in the wav codec unless this says 'WAVE'
    //This isn't a track corrupt issue as it was previously checked by Recognise
    ASSERT(strncmp((const TChar*)header+8, "WAVE", 4) == 0);

    iTrackStart += 12;
}

void CodecWav::ProcessFmtChunk()
{
    // Find "fmt " chunk (and get size).
    TUint fmtChunkBytes = FindChunk(Brn("fmt "));
    if (fmtChunkBytes != 16 && fmtChunkBytes != 18 && fmtChunkBytes != 40) {
        THROW(CodecStreamCorrupt);
    }

    // Read in remainder of "fmt " chunk.
    iReadBuf.SetBytes(0);
    iController->Read(iReadBuf, fmtChunkBytes);
    if (iReadBuf.Bytes() < fmtChunkBytes) {
        THROW(CodecStreamEnded);
    }

    // Parse "fmt " chunk.
    const TUint audioFormat = Converter::LeUint16At(iReadBuf, 0);
    // 0xfffe is WAVE_FORMAT_EXTENSIBLE, i.e., 24 bits or >2 channels.
    if (audioFormat != 0x01 && audioFormat != 0xfffe) {
        THROW(CodecStreamFeatureUnsupported);
    }

    iNumChannels = Converter::LeUint16At(iReadBuf, 2);
    iSampleRate = Converter::LeUint32At(iReadBuf, 4);
    const TUint byteRate = Converter::LeUint32At(iReadBuf, 8);
    iBitRate = byteRate * 8;
    //const TUint blockAlign = Converter::LeUint16At(iReadBuf, 12);
    iBitDepthSrc = Converter::LeUint16At(iReadBuf, 14);
    iBitDepth = std::min(iBitDepthSrc, iController->MaxBitDepth());
    // Calculate a sample boundary that will keep pipeline happy.
    iSampleBytesSrc = iNumChannels * (iBitDepthSrc / 8);
    iSampleBytesDest = iNumChannels * (iBitDepth / 8);

    if (iNumChannels == 0 || iSampleRate == 0 || iBitRate == 0
            || iBitDepth == 0 || iBitDepth % 8 != 0) {
        THROW(CodecStreamCorrupt);
    }

    iTrackStart += fmtChunkBytes + 8;
}

void CodecWav::ProcessDataChunk()
{
    // Find the "data" chunk.
    TUint dataChunkBytes = FindChunk(Brn("data"));

    if(iFileSize == 0) {        //streaming
        iAudioBytesTotal = 0;
    }
    else {
        iAudioBytesTotal = dataChunkBytes;
    }
    iAudioBytesRemaining = iAudioBytesTotal;
    // Truncate iAudioBytesRemaining to a sensible sample boundary.
    // This avoids scenario where files may have miscellaneous data beyond audio data, which could result in Process() call never removing any data from read buffer at end of audio data because iAudioBytesRemaining > 0 && iAudioBytesRemaining < iSampleBytesSrc, so it fills read buffer and requests more data on next call.
    const TUint remaining = iAudioBytesRemaining % iSampleBytesSrc;    // "fmt " chunk must come before "data" chunk, so iSampleBytesSrc should be initialised.
    iAudioBytesRemaining -= remaining;

    iTrackStart += 8;

    const TUint numSamples = iAudioBytesRemaining / iSampleBytesSrc;
    iTrackLengthJiffies = ((TUint64)numSamples * Jiffies::kPerSecond) / iSampleRate;
}

TUint CodecWav::FindChunk(const Brx& aChunkId)
{
    LOG(kCodec, "CodecWav::FindChunk: %.*s\n", PBUF(aChunkId));

    for (;;) {
        iReadBuf.SetBytes(0);
        iController->Read(iReadBuf, 8); //Read chunk id and chunk size
        if (iReadBuf.Bytes() < 8) {
            THROW(CodecStreamEnded);
        }
        TUint bytes = Converter::LeUint32At(iReadBuf, 4);
        bytes += (bytes % 2); // one byte padding if chunk size is odd

        if (Brn(iReadBuf.Ptr(), 4) == aChunkId) {
            return bytes;
        }
        else {
            // Discard remainder of chunk.
            TUint bytesRemaining = bytes;
            while (bytesRemaining > 0) {
                iReadBuf.SetBytes(0);

                TUint readBytes = bytesRemaining;
                if (readBytes > iReadBuf.MaxBytes()) {
                    readBytes = iReadBuf.MaxBytes();
                }
                iController->Read(iReadBuf, readBytes);

                // Check if all data was delivered. (If not, the stream ended.)
                if (iReadBuf.Bytes() < readBytes) {
                    THROW(CodecStreamEnded);
                }

                bytesRemaining -= readBytes;
            }

            iTrackStart += 8 + bytes;
        }
    }
}

void CodecWav::SendMsgDecodedStream(TUint64 aStartSample)
{
    iController->OutputDecodedStream(iBitRate, iBitDepth, iSampleRate, iNumChannels, Brn("WAV"), iTrackLengthJiffies, aStartSample, true, DeriveProfile(iNumChannels));
}

void CodecWav::WriteSamples(TByte*& aDest, TUint& aSamplesWritten, TUint aSamplesDest)
{
    const auto src = iAudioEncoded->AudioData().Ptr(iAudioEncoded->AudioDataOffset() + iAudioEncodedBytesConsumed);
    const auto srcRemaining = iAudioEncoded->Bytes() - iAudioEncodedBytesConsumed;
    const auto srcSamples = srcRemaining / iSampleBytesSrc;
    const auto samples = std::min(srcSamples, aSamplesDest);
    WriteSamples(aDest, src, samples);
    aSamplesWritten += samples;
    iAudioEncodedBytesConsumed += (samples * iSampleBytesSrc);
    if (iAudioEncoded->Bytes() == iAudioEncodedBytesConsumed) {
        ClearAudioEncoded();
    }
}

void CodecWav::WriteSamples(TByte*& aDest, const TByte* aSrc, TUint aSamples)
{
    const auto bytes = aSamples * iSampleBytesSrc;
    switch (iBitDepthSrc)
    {
    case 8:
        (void)memcpy(aDest, aSrc, bytes);
        aDest += bytes;
        break;
    case 16:
        for (TUint i = 0; i < bytes; i += 2) {
            *aDest++ = aSrc[i + 1];
            *aDest++ = aSrc[i];
        }
        break;
    case 24:
        for (TUint i = 0; i < bytes; i += 3) {
            *aDest++ = aSrc[i + 2];
            *aDest++ = aSrc[i + 1];
            *aDest++ = aSrc[i];
        }
        break;
    case 32:
        // FIXME - ask animator for its max bit depth rather than hard-coding core4 constraints
        for (TUint i = 0; i < bytes; i += 4) {
            *aDest++ = aSrc[i + 3];
            *aDest++ = aSrc[i + 2];
            *aDest++ = aSrc[i + 1];
            // discard least significant byte of aSrc if Animator is limited to 24-bit audio
            if (iBitDepth > 24) {
                *aDest++ = aSrc[i];
            }
        }
        break;
    }
}

void CodecWav::ClearAudioEncoded()
{
    if (iAudioEncoded != nullptr) {
        iAudioEncoded->RemoveRef();
        iAudioEncoded = nullptr;
    }
    iAudioEncodedBytesConsumed = 0;
}

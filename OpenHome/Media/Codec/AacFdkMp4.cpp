#include <OpenHome/Media/Codec/AacFdkBase.h>
#include <OpenHome/Media/Codec/Mpeg4.h>
#include <OpenHome/Media/Codec/CodecController.h>
#include <OpenHome/Media/Codec/CodecFactory.h>
#include <OpenHome/Media/Codec/Container.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Media/Debug.h>

namespace OpenHome {
namespace Media {
namespace Codec {

class CodecAacFdkMp4 : public CodecAacFdkBase
{
private:
    static const TUint kDefaultAscBytes = 2;
public:
    CodecAacFdkMp4(IMimeTypeList& aMimeTypeList);
    ~CodecAacFdkMp4();
private: // from CodecBase
    TBool Recognise(const EncodedStreamInfo& aStreamInfo);
    void StreamInitialise();
    void Process();
    TBool TrySeek(TUint aStreamId, TUint64 aSample);
    //void StreamCompleted();
private:
    void ProcessMpeg4();
    TUint SkipEsdsTag(IReader& aReader, TByte& aDescLen);
private:
    static const TUint kMaxRecogBytes = 6 * 1024; // copied from previous CodecController behaviour
    Bws<kMaxRecogBytes> iRecogBuf;
    SampleSizeTable iSampleSizeTable;
    SeekTable iSeekTable;
    TUint iCurrentCodecSample;  // Sample count is 32 bits in stsz box.
    Bwh iAudioSpecificConfig;
};

} //namespace Codec
} //namespace Media
} //namespace OpenHome


using namespace OpenHome;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Codec;

CodecBase* CodecFactory::NewAacFdkMp4(IMimeTypeList& aMimeTypeList)
{ // static
    return new CodecAacFdkMp4(aMimeTypeList);
}



// CodecAacFdkMp4

CodecAacFdkMp4::CodecAacFdkMp4(IMimeTypeList& aMimeTypeList)
    : CodecAacFdkBase("AAC", aMimeTypeList)
    , iAudioSpecificConfig(kDefaultAscBytes)
{
}

CodecAacFdkMp4::~CodecAacFdkMp4()
{
}

TBool CodecAacFdkMp4::Recognise(const EncodedStreamInfo& aStreamInfo)
{
    LOG(kCodec, "CodecAacFdkMp4::Recognise\n");

    if (aStreamInfo.StreamFormat() != EncodedStreamInfo::Format::Encoded) {
        return false;
    }
    iRecogBuf.SetBytes(0);
    iController->Read(iRecogBuf, iRecogBuf.MaxBytes());
    if (iRecogBuf.Bytes() >= 4) {
        if (Brn(iRecogBuf.Ptr(), 4) == Brn("mp4a")) {
            // FIXME - should also check codec type that is passed within esds to determine that it is definitely AAC and not another codec (e.g., MP3)
            LOG(kCodec, "CodecAacFdkMp4::Recognise aac mp4a\n");
            return true;
        }
    }
    return false;
}

TUint CodecAacFdkMp4::SkipEsdsTag(IReader& aReader, TByte& aDescLen)
{
    TUint skip = 0;
    TByte val = aReader.Read(1)[0];

    switch (val) {
        case 0x80:
        case 0x81:
        case 0xFE:
            skip = 3;
            break;
        default:
            skip = 0;
    }

    if (skip > 0) {
        aDescLen = aReader.Read(skip)[skip - 1];
    }
    else {
        aDescLen = val;
    }
    return skip + 1;
}

void CodecAacFdkMp4::StreamInitialise()
{
    LOG(kCodec, ">CodecAacFdkMp4::StreamInitialise\n");

    CodecAacFdkBase::StreamInitialise();

    iCurrentCodecSample = 0;
    iAudioSpecificConfig.SetBytes(0);

    // Use iInBuf for gathering initialisation data, as it doesn't need to be used for audio until Process() starts being called.
    Mpeg4Info info;
    try {
        CodecBufferedReader codecBufReader(*iController, iInBuf);
        Mpeg4InfoReader mp4Reader(codecBufReader);
        mp4Reader.Read(info);

        // // see http://wiki.multimedia.cx/index.php?title=Understanding_AAC for details
        // // or http://xhelmboyx.tripod.com/formats/mp4-layout.txt - search for 'esds'

        // // also see APar_Extract_esds_Info() in
        // //          http://m4sharp.googlecode.com/svn-history/r3/trunk/m4aSharp/m4aSharp/AP_AtomExtracts.cpp
        // // and
        // //          http://www.jthink.net/jaudiotagger/javadoc/org/jaudiotagger/audio/mp4/atom/Mp4EsdsBox.html
        // /*
        // EsdsBox ( stream specific description box), usually holds the Bitrate/No of Channels

        // It contains a number of (possibly optional?) sections (section 3 - 6) (containing optional filler) with differeent info in each section.

        // -> 4 bytes version/flags = 8-bit hex version + 24-bit hex flags (current = 0)

        // Section 3 -> 1 byte ES descriptor type tag = 8-bit hex value 0x03
        // -> 3 bytes optional extended descriptor type tag string = 3 * 8-bit hex value - types are 0x80,0x81,0xFE
        // -> 1 byte descriptor type length = 8-bit unsigned length
        // -> 2 bytes ES ID = 16-bit unsigned value
        // -> 1 byte stream priority = 8-bit unsigned value - Defaults to 16 and ranges from 0 through to 31

        // Section 4 -> 1 byte decoder config descriptor type tag = 8-bit hex value 0x04
        // -> 3 bytes optional extended descriptor type tag string = 3 * 8-bit hex value - types are 0x80,0x81,0xFE
        // -> 1 byte descriptor type length = 8-bit unsigned length *
        // -> 1 byte object type ID = 8-bit unsigned value
        // - type IDs are system v1 = 1 ; system v2 = 2
        // - type IDs are MPEG-4 video = 32 ; MPEG-4 AVC SPS = 33
        // - type IDs are MPEG-4 AVC PPS = 34 ; MPEG-4 audio = 64
        // - type IDs are MPEG-2 simple video = 96
        // - type IDs are MPEG-2 main video = 97
        // - type IDs are MPEG-2 SNR video = 98
        // - type IDs are MPEG-2 spatial video = 99
        // - type IDs are MPEG-2 high video = 100
        // - type IDs are MPEG-2 4:2:2 video = 101
        // - type IDs are MPEG-4 ADTS main = 102
        // - type IDs are MPEG-4 ADTS Low Complexity = 103
        // - type IDs are MPEG-4 ADTS Scalable Sampling Rate = 104
        // - type IDs are MPEG-2 ADTS = 105 ; MPEG-1 video = 106
        // - type IDs are MPEG-1 ADTS = 107 ; JPEG video = 108
        // - type IDs are private audio = 192 ; private video = 208
        // - type IDs are 16-bit PCM LE audio = 224 ; vorbis audio = 225
        // - type IDs are dolby v3 (AC3) audio = 226 ; alaw audio = 227
        // - type IDs are mulaw audio = 228 ; G723 ADPCM audio = 229
        // - type IDs are 16-bit PCM Big Endian audio = 230
        // - type IDs are Y'CbCr 4:2:0 (YV12) video = 240 ; H264 video = 241
        // - type IDs are H263 video = 242 ; H261 video = 243

        // -> 6 bits stream type = 3/4 byte hex value - type IDs are object descript. = 1 ; clock ref. = 2 - type IDs are scene descript. = 4 ; visual = 4 - type IDs are audio = 5 ; MPEG-7 = 6 ; IPMP = 7 - type IDs are OCI = 8 ; MPEG Java = 9 - type IDs are user private = 32
        // -> 1 bit upstream flag = 1/8 byte hex value
        // -> 1 bit reserved flag = 1/8 byte hex value set to 1
        // -> 3 bytes buffer size = 24-bit unsigned value
        // -> 4 bytes maximum bit rate = 32-bit unsigned value
        // -> 4 bytes average bit rate = 32-bit unsigned value

        // Section 5 -> 1 byte decoder specific descriptor type tag 8-bit hex value 0x05
        // -> 3 bytes optional extended descriptor type tag string = 3 * 8-bit hex value - types are 0x80,0x81,0xFE
        // -> 1 byte descriptor type length = 8-bit unsigned length
        // -> 1 byte Audio profile Id - 5 bits Profile Id - 3 bits Unknown
        // -> 8 bits other flags - 3 bits unknown - 2 bits is No of Channels - 3 bits unknown

        // Section 6
        // -> 1 byte SL config descriptor type tag = 8-bit hex value 0x06
        // -> 3 bytes optional extended descriptor type tag string = 3 * 8-bit hex value - types are 0x80,0x81,0xFE
        // -> 1 byte descriptor type length = 8-bit unsigned length
        // -> 1 byte SL value = 8-bit hex value set to 0x02

        // Valid Type ID seen for aac is "MPEG-4 audio" - not sure if any others are used so no checking for this is done
        // */
        iChannels = 0;

        TByte descTag, descLen, val;
        TUint bytesRead = 0;

        descTag = codecBufReader.Read(1)[0];
        bytesRead++;
        if (descTag == 3) {     // section 3
            bytesRead += SkipEsdsTag(codecBufReader, descLen);
            if (descLen != 0) {
                // skip es_id (2 bytes) and stream_priority (1 byte)
                descTag = codecBufReader.Read(4)[3];
                bytesRead += 4;
                if (descTag == 4) {     // section 4
                    bytesRead += SkipEsdsTag(codecBufReader, descLen);
                    if (descLen != 0) {
                        // 13 = obj type (1)     +
                        //      stream type (1)  +
                        //      buffer size (3)  +
                        //      max_bit_rate (4) +
                        //      avg_bit_rate (4)
                        Brn buf = codecBufReader.Read(13);
                        bytesRead += 13;
                        // extract bitrates
                        iBitrateMax = Converter::BeUint32At(buf, 5);
                        iBitrateAverage = Converter::BeUint32At(buf, 9);
                        descTag = codecBufReader.Read(1)[0];
                        bytesRead++;
                        if(descTag == 5) {     // section 5
                            bytesRead += SkipEsdsTag(codecBufReader, descLen);
                            if (descLen != 0) {
                                LOG(kCodec, "CodecAacFdkMp4::StreamInitialise AudioSpecificConfig bytes: %u\n", descLen);
                                Brn audioProfile = codecBufReader.Read(descLen);
                                bytesRead += audioProfile.Bytes();
                                iAudioSpecificConfig.Grow(audioProfile.Bytes());
                                iAudioSpecificConfig.Replace(audioProfile);
                                // skip audio profile id (1 byte)
                                val = audioProfile[1];
                                
                                iChannels = (val >> 3) & 0xf;  // FIXME - compare against iInfo.Channels() - or is that not valid?
                            }
                        }
                    }
                }
            }
        }
        // Skip any remaining bytes of the ES descriptor.
        // First check that we have not gone beyond its end
        if (info.StreamDescriptorBytes() < bytesRead) {
            THROW(CodecStreamCorrupt);
        }
        TUint skip = info.StreamDescriptorBytes() - bytesRead;

        if (skip > 0) {
            codecBufReader.Read(skip);
        }

        InitialiseDecoderMp4(iAudioSpecificConfig);

        // Read sample size table.
        ReaderBinary readerBin(codecBufReader);
        iSampleSizeTable.Clear();
        const TUint sampleCount = readerBin.ReadUintBe(4);
        iSampleSizeTable.Init(sampleCount);
        for (TUint i=0; i<sampleCount; i++) {
            const TUint sampleSize = readerBin.ReadUintBe(4);
            iSampleSizeTable.AddSampleSize(sampleSize);
        }

        // Read seek table.
        iSeekTable.Deinitialise();
        SeekTableInitialiser seekTableInitialiser(iSeekTable, codecBufReader);
        seekTableInitialiser.Init();
    }
    catch (MediaMpeg4FileInvalid&) {
        THROW(CodecStreamCorrupt);
    }
    catch (ReaderError&) {
        THROW(CodecStreamEnded);
    }

    iInBuf.SetBytes(0);

    iSampleRate = info.Timescale();
    iOutputSampleRate = iSampleRate;
    iBitDepth = info.BitDepth();
    //iChannels = iMp4->Channels();     // not valid !!!
    iSamplesTotal = info.Duration();

    if (iChannels == 0) {
        THROW(CodecStreamCorrupt);  // invalid for an audio file type
    }

    iTrackLengthJiffies = (iSamplesTotal * Jiffies::kPerSecond) / iSampleRate;
    iTrackOffset = 0;

    LOG(kCodec, "CodecAacFdkMp4::StreamInitialise iBitrateAverage %u, iBitDepth %u, iSampleRate: %u, iSamplesTotal %llu, iChannels %u, iTrackLengthJiffies %u\n", iBitrateAverage, iBitDepth, iOutputSampleRate, iSamplesTotal, iChannels, iTrackLengthJiffies);
    iController->OutputDecodedStream(iBitrateAverage, iBitDepth, iOutputSampleRate, iChannels, kCodecAac, iTrackLengthJiffies, 0, false, DeriveProfile(iChannels));
}

TBool CodecAacFdkMp4::TrySeek(TUint aStreamId, TUint64 aSample)
{
    LOG(kCodec, "CodecAacFdkMp4::TrySeek(%u, %llu)\n", aStreamId, aSample);

    TUint divisor = 1;
    if (iOutputSampleRate != iSampleRate
            && iOutputSampleRate > 0
            && iSampleRate > 0) {
        divisor = iOutputSampleRate / iSampleRate;
    }
    TUint64 seekTableInputSample = aSample / divisor;

    try {
        TUint64 codecSample = 0;
        // This alters seekTableInputSample to point to closest audio sample to aSample that can actually be seeked to.
        // codecSample is altered to be the "codec" sample containing that audio sample. This is nothing to do with audio samples in Hz.
        const TUint64 bytes = iSeekTable.Offset(seekTableInputSample, codecSample);     // find file offset relating to given audio sample
        LOG(kCodec, "CodecAacFdkMp4::Seek to sample: %llu, byte: %llu, codecSample: %llu\n", seekTableInputSample, bytes, codecSample);
        const TBool canSeek = iController->TrySeekTo(aStreamId, bytes);
        if (canSeek) {
            const TUint64 seekTableOutputSample = seekTableInputSample * divisor;
            iTotalSamplesOutput = aSample;
            iCurrentCodecSample = static_cast<TUint>(codecSample);
            iTrackOffset = (Jiffies::kPerSecond/iOutputSampleRate)*seekTableOutputSample;
            iInBuf.SetBytes(0);
            iOutBuf.SetBytes(0);
            iController->OutputDecodedStream(iBitrateAverage, iBitDepth, iOutputSampleRate, iChannels, kCodecAac, iTrackLengthJiffies, seekTableOutputSample, false, DeriveProfile(iChannels));
        }
        return canSeek;
    }
    catch (MediaMpeg4OutOfRange&) {
        LOG(kCodec, "CodecAacFdkMp4::TrySeek caught MediaMpeg4OutOfRange sample aStreamId: %u, aSample: %lld\n", aStreamId, aSample);
        return false;
    }
    catch (MediaMpeg4FileInvalid&) {
        LOG(kCodec, "CodecAacFdkMp4::TrySeek caught MediaMpeg4FileInvalid aStreamId: %u, aSample: %lld\n", aStreamId, aSample);
        return false;
    }
}

void CodecAacFdkMp4::Process()
{
    ProcessMpeg4();

    if (iNewStreamStarted) {
        THROW(CodecStreamStart);
    }
    if (iStreamEnded) {
        THROW(CodecStreamEnded);
    }
}

void CodecAacFdkMp4::ProcessMpeg4() 
{
    if (iCurrentCodecSample < iSampleSizeTable.Count()) {

        // Read in a single aac sample.
        iInBuf.SetBytes(0);

        try {
            LOG_TRACE(kCodec, "CodecAacFdkMp4::Process  iCurrentCodecSample: %u, size: %u, inBuf.MaxBytes(): %u\n", iCurrentCodecSample, iSampleSizeTable.SampleSize(iCurrentCodecSample), iInBuf.MaxBytes());
            TUint sampleSize = iSampleSizeTable.SampleSize(iCurrentCodecSample);
            iController->Read(iInBuf, sampleSize);
            LOG_TRACE(kCodec, "CodecAacFdkMp4::Process  read iInBuf.Bytes() = %u\n", iInBuf.Bytes());
            if (iInBuf.Bytes() < sampleSize) {
                THROW(CodecStreamEnded);
            }
            iCurrentCodecSample++;

            // Now decode and output
            DecodeFrame();
        }
        catch (CodecStreamStart&) {
            iNewStreamStarted = true;
            LOG(kCodec, "CodecAacFdkMp4::ProcessMpeg4 caught CodecStreamStart\n");
        }
        catch (CodecStreamEnded&) {
            iStreamEnded = true;
            LOG(kCodec, "CodecAacFdkMp4::ProcessMpeg4 caught CodecStreamEnded\n");
        }
    }
    else {
        iStreamEnded = true;
    }

    FlushOutput();
}

#ifdef CODEC_OPUS_ENABLED

#include <OpenHome/Media/Codec/CodecController.h>
#include <OpenHome/Media/Codec/CodecFactory.h>
#include <OpenHome/Media/Codec/Container.h>
#include <OpenHome/Media/Codec/Mpeg4.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Media/MimeTypeList.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/Ascii.h>

#include <opus.h>

namespace OpenHome {
namespace Media {
namespace Codec {

class CodecOpus : public CodecBase
{
public:
    CodecOpus(IMimeTypeList& aMimeTypeList);
    ~CodecOpus();
private: // from CodecBase
    TBool Recognise(const EncodedStreamInfo& aStreamInfo);
    void StreamInitialise();
    void Process();
    TBool TrySeek(TUint aStreamId, TUint64 aSample);
    void StreamCompleted();

private:
    void TryReadSizeTable(CodecBufferedReader&);
    void TryReadSeekTable(CodecBufferedReader&);
    TBool ValidateCodecInformation(const Brx&) const;

private:
    Brn iName;
    OpusDecoder* iDecoder;
    SampleSizeTable iSampleSizeTable;
    SeekTable iSeekTable;
    Bws<48000 * 2 * sizeof(TInt)> iInBuf;
    Bws<48000 * 2 * sizeof(TInt)> iDecodedBuf;

    TUint iSampleRate;
    TUint iChannelCount;
    TUint iBitDepth;
    TUint iBitRate; // (?)
    TUint iSamplesDecoded;
    TUint64 iTrackOffset;
    TUint64 iTrackLengthJiffies;

};

} // namespace Codec
} // namespace Media
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Codec;

CodecBase* CodecFactory::NewOpus(IMimeTypeList& aMimeTypeList)
{ // static
    return new CodecOpus(aMimeTypeList);
}

static const Brn kCodecOpus("Opus");

// @OpusConfig
// Opus Config is defined in: https://www.opus-codec.org/docs/opus_in_isobmff.html (Section: 4.3.2)
// This forms part of the dOps box.
// Currently we only support the most basic of forms, which doesn't provide any channel mapping information.
// Size:
// - Version              (1)
// - ChannelCount         (1)
// - PreSkip              (2)
// - SampleRate           (4)
// - OutputGain           (2)
// - ChannelMappingFamily (1)
//                         = 11 bytes
static const TUint kOpusConfigSize = 11;

#ifdef DEFINE_BIG_ENDIAN
static const AudioDataEndian kAudioEndianess = AudioDataEndian::Big;
#else
static const AudioDataEndian kAudioEndianess = AudioDataEndian::Little;
#endif


// CodecOpus
CodecOpus::CodecOpus(IMimeTypeList& aMimeTypeList)
    : CodecBase("Opus")
    , iName("Opus")
{
    int err = 0;
    iDecoder = opus_decoder_create(48000, 2, &err);
    ASSERT(iDecoder != nullptr);
    ASSERT(err == OPUS_OK)

    aMimeTypeList.Add("audio/x-opus");
}

CodecOpus::~CodecOpus()
{
    opus_decoder_destroy(iDecoder);
    iDecoder = nullptr;
}

TBool CodecOpus::Recognise(const EncodedStreamInfo& aStreamInfo)
{
    if (aStreamInfo.StreamFormat() != EncodedStreamInfo::Format::Encoded) {
        return false;
    }
    Bws<4> buf;
    iController->Read(buf, buf.MaxBytes());
    const TChar* ptr = reinterpret_cast<const TChar*>(buf.Ptr());
    if(buf.Bytes() >= 4) {
        if(strncmp(ptr, "Opus", 4) == 0) {
            return true;
        }
    }
    return false;
}

void CodecOpus::StreamInitialise()
{
    LOG(kCodec, "CodecOpus::StreamInitialise\n");

    iSampleRate = 0;
    iChannelCount = 0;
    iBitDepth = 0;
    iBitRate = 0;
    iTrackLengthJiffies = 0;
    iTrackOffset = 0;
    iSamplesDecoded = 0;

    // Use iInBuf for gathering initialisation data, as it doesn't need to be used for audio until Process() starts being called.
    Mpeg4Info info;
    Bws<kOpusConfigSize> config;

    try {
        CodecBufferedReader codecBufReader(*iController, iInBuf);
        Mpeg4InfoReader mp4Reader(codecBufReader);
        mp4Reader.Read(info);

        if (info.StreamDescriptorBytes() < kOpusConfigSize) {
            THROW(CodecStreamCorrupt);
        }

        config.Append(codecBufReader.Read(kOpusConfigSize));

        TryReadSizeTable(codecBufReader);
        TryReadSeekTable(codecBufReader);
    }
    catch (MediaMpeg4FileInvalid&) {
        THROW(CodecStreamCorrupt);
    }
    catch (ReaderError&) {
        THROW(CodecStreamEnded);
    }

    iSampleRate   = info.SampleRate();
    iChannelCount = info.Channels();
    iBitDepth     = info.BitDepth();

    const TUint bytesPerSample = iChannelCount * iBitDepth / 8;
    iBitRate                   = iSampleRate * bytesPerSample * 8;
    iTrackLengthJiffies        = (info.Duration() * Jiffies::kPerSecond) / info.Timescale();

    if (!ValidateCodecInformation(config)) {
        THROW(CodecStreamCorrupt);
    }


    // Use the information above to reinitialise our decoder object to the desired output...
    int result = opus_decoder_ctl(iDecoder, OPUS_RESET_STATE);
    if (result != OPUS_OK) {
        Log::Print("CodecOpus::StreamInitialise() - Failed to reset decoder state.\n");
        THROW(CodecStreamCorrupt);
    }

    result = opus_decoder_init(iDecoder, iSampleRate, iChannelCount);
    if (result != OPUS_OK) {
        Log::Print("CodecOpus::StreamInitialise() - Failed to configure decoder to output params: SR: %u, Channels: %u\n", iSampleRate, iChannelCount);
        THROW(CodecStreamCorrupt);
    }

    iController->OutputDecodedStream(iBitRate, iBitDepth, iSampleRate, iChannelCount, kCodecOpus, iTrackLengthJiffies, 0, false, DeriveProfile(iChannelCount));
}


void CodecOpus::Process()
{
    iInBuf.SetBytes(0);
    iDecodedBuf.SetBytes(0);

    if (iSamplesDecoded < iSampleSizeTable.Count()) {
        const TUint sampleSize = iSampleSizeTable.SampleSize(iSamplesDecoded);
        iController->Read(iInBuf, sampleSize);

        const TInt outputSamples = opus_decode(iDecoder, (const TByte*)iInBuf.Ptr(), iInBuf.Bytes(), (TInt16*)iDecodedBuf.Ptr(), iSampleRate * iChannelCount, 0);
        if (outputSamples <= 0) {
            THROW(CodecStreamCorrupt);
        }

        iDecodedBuf.SetBytes(outputSamples * sizeof(TInt16) * iChannelCount);

        iTrackOffset += iController->OutputAudioPcm(iDecodedBuf, iChannelCount, iSampleRate, iBitDepth, kAudioEndianess, iTrackOffset);

        iSamplesDecoded += 1;
    }
    else {
        // At this point we've consumed everything we possibly can from the given sample size table.
        // If this is fixed stream then we'll have completed all available data and subsequent reads will fail.
        // However, if we're in a fragmented stream, we'll need to re-initialise the size & seek tables
        // from the incoming fragment.

        try {
            CodecBufferedReader codecBufReader(*iController, iInBuf);

            TryReadSizeTable(codecBufReader);
            TryReadSeekTable(codecBufReader);

            // Reset this, as we're at the start of a new chunk! :)
            iSamplesDecoded = 0;
        }
        catch (MediaMpeg4FileInvalid&) {
            THROW(CodecStreamCorrupt);
        }
        catch (ReaderError&) {
            THROW(CodecStreamEnded);
        }
    }
}

TBool CodecOpus::TrySeek(TUint aStreamId, TUint64 aSample)
{
    (void)aStreamId;
    (void)aSample;

    return false;
}

void CodecOpus::StreamCompleted()
{
    (void)opus_decoder_ctl(iDecoder, OPUS_RESET_STATE);
}

void CodecOpus::TryReadSizeTable(CodecBufferedReader& aReader)
{
    // Read sample size table.
    ReaderBinary readerBin(aReader);

    iSampleSizeTable.Clear();

    const TUint sampleCount = readerBin.ReadUintBe(4);

    iSampleSizeTable.Init(sampleCount);

    for (TUint i=0; i<sampleCount; i++) {
        const TUint sampleSize = readerBin.ReadUintBe(4);
        iSampleSizeTable.AddSampleSize(sampleSize);
    }
}

void CodecOpus::TryReadSeekTable(CodecBufferedReader& aReader)
{
    iSeekTable.Deinitialise();

    SeekTableInitialiser seekTableInitialiser(iSeekTable, aReader);
    seekTableInitialiser.Init();
}

TBool CodecOpus::ValidateCodecInformation(const Brx& aCodecInfo) const
{
    TBool valid = true;
    ReaderBuffer configReader(aCodecInfo);

    // CodecInfo is defined as the contents of the dOps MPEG 4 box which is provided to initialise streams
    // Here, we mostly validate that the external MPEG container values match the description of the
    // initialisation data provided in the Opus stream.
    // See @OpusConfig

    ASSERT_VA(aCodecInfo.Bytes() == kOpusConfigSize, "%s", "CodecOpus::ValidateCodecInfo - Incorrect size. Expected %u, got: %u\n", kOpusConfigSize, aCodecInfo.Bytes());

    const TByte version = configReader.Read(1).At(0);
    if (version != 0) {
        Log::Print("CodecOpus::StreamInitialise() - Version (%u) != 0 -> Invalid track\n", version);
        valid = false;
    }

    const TUint reportedChannelCount = configReader.Read(1).At(0);
    if (reportedChannelCount != iChannelCount) {
        Log::Print("CodecOpus::StreamInitialise() - Codec reported differing number of channels (Container: %u, Codec: %u)\n", iChannelCount, reportedChannelCount);
        valid = false;
    }

    // PreSkip (Ignored)
    (void)configReader.Read(2);

    const TUint reportedSampleRate = Converter::BeUint32At(configReader.Read(4), 0);
    if (reportedSampleRate != iSampleRate) {
        Log::Print("CodecOpus::StreamInitialise() - Codec report a different sample rate (Container: %u, Codec: %u)\n", iSampleRate, reportedSampleRate);
        valid = false;
    }

    // OutputGain (Ignored)
    (void)configReader.Read(2);

    // ChannelMappingFamily
    // NOTE: We only support mappingFamily == 0. This means that no mapping information is provided
    //       It's likely we've never reached here as if the mappingFamily wasn't 0, the config size would be larger
    //       and so fail the length checks above.
    ASSERT_VA(configReader.Read(1).At(0) == 0, "%s", "CodecOpus::ValidateCodecInfo - Unknown / invalid ChannelMappingFamily. Expected: 0, got: %u\n");

    return valid;
}

#endif // CODEC_OPUS_ENABLED

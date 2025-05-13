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
    Bws<1024> iInBuf;
    Bws<48000 * 2 * sizeof(TInt)> iDecodedBuf;

    TUint iSampleRate;
    TUint iChannelCount;
    TUint iBitDepth;
    TUint iBitRate; // (?)
    TUint iSamplesDecoded;
    TUint64 iTrackOffset;
    TUint64 iTrackLengthJiffies;
    TUint64 iSamplesToSkip;

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

// NOTE: Our current Opus support is pretty based and only covers Opus audio encoded
//       in a fragmented MPEG stream. Opus has a slightly different format under this
//       scenario and so the 'Opus' and 'dOps' box are used to detect the required details
//       about the incoming audio. This particular flavour of Opus does not contain any
//       seek information and so this must be provided by other parts of the MPEG stream.
//       We are required to implement the decode & seek support manually without the aid
//       of any supporting libraries.
//
//       Standard .Opus files contain far more information with regards to playback and would
//       allow us to make use of the 'libOpusFile' library. This provides a standard Xiph
//       codec implementation (see Flac, Vorbis) which handles seeking and playback on our
//       behalf.
//
//       In the future, should we want to support raw .opus files, we'll need to reconsider
//       how this class is implemented in order to handle both cases of the audio format.
//       (Furthermore, libOpusFile also depends on Vorbis codecs so might not be compatible
//       with our current codec structure.)

CodecOpus::CodecOpus(IMimeTypeList& aMimeTypeList)
    : CodecBase("Opus")
    , iName("Opus")
{
    int err = 0;
    iDecoder = opus_decoder_create(48000, 2, &err);
    ASSERT(iDecoder != nullptr);
    ASSERT(err == OPUS_OK)

    aMimeTypeList.Add("audio/x-opus-mpeg");
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
        if(strncmp(ptr, "dOps", 4) == 0) {
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
    iSamplesToSkip = 0;

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


        // iSamplesToSkip > 0 means we've likely had to SEEK our way through the content.
        if (iSamplesToSkip > 0) {

            // If we've to skip more samples than the samples we've just decoded, don't output any audio
            // at all.
            if (iSamplesToSkip >= (TUint64)outputSamples) {
                iSamplesToSkip -= outputSamples;
            }
            else {
                const TUint samplesToSkip     = (TUint)iSamplesToSkip;
                const TUint remainingSamples  = (TUint)outputSamples - samplesToSkip;
                const TUint sampleSizeInBytes = sizeof(TInt16) * iChannelCount;

                const TByte* startingOffset = iDecodedBuf.Ptr() + (samplesToSkip * sampleSizeInBytes);
                const TUint  sampleBytes    = remainingSamples * sampleSizeInBytes;

                iSamplesToSkip = 0;

                const Brn audioToOutput(startingOffset, sampleBytes);
                iTrackOffset += iController->OutputAudioPcm(audioToOutput, iChannelCount, iSampleRate, iBitDepth, kAudioEndianess, iTrackOffset);
            }
        }
        else {
            iDecodedBuf.SetBytes(outputSamples * sizeof(TInt16) * iChannelCount);

            iTrackOffset += iController->OutputAudioPcm(iDecodedBuf, iChannelCount, iSampleRate, iBitDepth, kAudioEndianess, iTrackOffset);
        }

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

    Log::Print("CodecOpus::TrySeek - Seeking is not availabler\n");
    return false;

    // Seeking dOps files (Opus served under fragmented DASH)
    //
    // In order to correctly seek dOps, we require to do a 2 stage process
    // 1) Need to detect which fragment contains the seek position
    // 2) How many samples from the between of the fragment is the seek position.
    //
    // We use the SeekTable to detect if we are in a fragmented stream as the table
    // is encoded differently from non-fragmented stream. We use the table to get
    // each fragment duration to anchor ourselves to the correct place.
    //
    // After that, we know the remaining number of samples that we need to skip
    // from the start of the fragment to obtain the correct seek position


    // @FragmentedStreamSeeking
    // Disabled until suitable solution can be found for all codecs. E, May 2025

    /*
    // NOTE: We only support seeking Opus when it's part of a fragmented MPEG stream.
    if (!iSeekTable.IsFragmentedStream()) {
        LOG_ERROR(kCodec, "CodecOpus::TrySeek - ATTEMPTING TO SEEK A NON-FRAGMENTED FILE. This is not supported.\n");
        return false;
    }

    const TUint seekPositionSeconds = (TUint)(aSample / iSampleRate);

    TUint fragmentIndex   = 0;
    TUint accumulatedTime = 0;
    TUint64 samplesToSkip = aSample;

    for(fragmentIndex = 0; fragmentIndex < iSeekTable.ChunkCount(); fragmentIndex += 1) {
        const TUint segmentDuration = iSeekTable.SamplesPerChunk(fragmentIndex);

        const TBool segmentContainsSeekPosition = (accumulatedTime + segmentDuration) > seekPositionSeconds;
        if (segmentContainsSeekPosition) {
            break;
        }

        accumulatedTime += segmentDuration;

        const TUint64 segmentSamples = segmentDuration * iSampleRate;
        samplesToSkip -= segmentSamples;
    }

    Log::Print("CodecOpus::TrySeek - Seeking to fragment %u, skipping %lu samples from fragment.\n", fragmentIndex, samplesToSkip);

    iSamplesToSkip  = samplesToSkip;
    iSamplesDecoded = 0;

    iTrackOffset = (aSample * Jiffies::kPerSecond) / iSampleRate;

    iSampleSizeTable.Clear();
    iSeekTable.Deinitialise();

    if (iController->TrySeekTo(aStreamId, fragmentIndex)) {
        iController->OutputDecodedStream(iBitRate, iBitDepth, iSampleRate, iChannelCount, kCodecOpus, iTrackLengthJiffies, aSample, false, DeriveProfile(iChannelCount));
        iTrackOffset = aSample;

        return true;
    }

    return false;
    */
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

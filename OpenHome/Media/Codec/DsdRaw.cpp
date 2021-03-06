#include <OpenHome/Media/Codec/CodecController.h>
#include <OpenHome/Media/Codec/CodecFactory.h>
#include <OpenHome/Media/Codec/Container.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Media/Debug.h>

namespace OpenHome {
namespace Media {
namespace Codec {

class CodecDsdRaw : public CodecBase
{
public:
    CodecDsdRaw(TUint aSampleBlockWords, TUint aPaddingBytes);
    ~CodecDsdRaw();
private: // from CodecBase
    TBool Recognise(const EncodedStreamInfo& aStreamInfo) override;
    void StreamInitialise() override;
    void Process() override;
    TBool TrySeek(TUint aStreamId, TUint64 aSample) override;
private:
    TUint iSampleRate;
    TUint iNumChannels;
    const TUint iSampleBlockWords;
    const TUint iPaddingBytes;
    TUint64 iStartSample;
    TUint64 iTrackOffset;
    TUint64 iTrackLengthJiffies;
    BwsCodecName iCodecName;
};

} // namespace Codec
} // namespace Media
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Codec;

CodecBase* CodecFactory::NewDsdRaw(TUint aSampleBlockWords, TUint aPaddingBytes)
{ // static
    return new CodecDsdRaw(aSampleBlockWords, aPaddingBytes);
}


CodecDsdRaw::CodecDsdRaw(TUint aSampleBlockWords, TUint aPaddingBytes)
    : CodecBase("DSD", kCostVeryLow)
    , iSampleBlockWords(aSampleBlockWords)
    , iPaddingBytes(aPaddingBytes)
{
}

CodecDsdRaw::~CodecDsdRaw()
{
}

TBool CodecDsdRaw::Recognise(const EncodedStreamInfo& aStreamInfo)
{
    if (aStreamInfo.StreamFormat() != EncodedStreamInfo::Format::Dsd) {
        return false;
    }
    iSampleRate = aStreamInfo.SampleRate();
    iNumChannels = aStreamInfo.NumChannels();
    iStartSample = aStreamInfo.StartSample();
    iCodecName.Replace(aStreamInfo.CodecName());
    //Log::Print("CodecDsdRaw::Recognise iSampleRate %u, iNumChannels %u, iSampleBlockWords=%u, iStartSample %llu\n",
    //           iSampleRate, iNumChannels, iSampleBlockWords, iStartSample);
    return true;
}

void CodecDsdRaw::StreamInitialise()
{
    try {
        const TUint64 lenBytes = iController->StreamLength();
        const TUint64 numSamples = lenBytes * 8 / iNumChannels; // 1 bit per subsample
        iTrackLengthJiffies = numSamples * Jiffies::PerSample(iSampleRate);
        iTrackOffset = (((TUint64)iStartSample) * Jiffies::kPerSecond) / iSampleRate;
        SpeakerProfile spStereo;
        iController->OutputDecodedStreamDsd(iSampleRate, iNumChannels, iCodecName, iTrackLengthJiffies, iStartSample, spStereo);
    }
    catch (SampleRateInvalid&) {
        THROW(CodecStreamCorrupt);
    }
}

void CodecDsdRaw::Process()
{
    auto msg = iController->ReadNextMsg();
    iTrackOffset += iController->OutputAudioDsd(msg, iNumChannels, iSampleRate, iSampleBlockWords, iTrackOffset, iPaddingBytes);
}

TBool CodecDsdRaw::TrySeek(TUint /*aStreamId*/, TUint64 /*aSample*/)
{
    return false;
}

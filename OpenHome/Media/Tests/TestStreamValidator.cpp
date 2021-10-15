#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Media/Pipeline/StreamValidator.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Utils/AllocatorInfoLogger.h>
#include <OpenHome/Private/SuiteUnitTest.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Media/ClockPuller.h>

#include <vector>

using namespace OpenHome;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Media;

namespace OpenHome {
namespace Media {

class SuiteStreamValidator : public SuiteUnitTest
                           , private IPipelineElementDownstream
                           , private IMsgProcessor
                           , private IPipelineAnimator
                           , private IStreamHandler
{
    static const TUint kBitrate = 256;
    static const TUint kSampleRate = 44100;
    static const TUint kSampleRateDsd = 2822400;
    static const TUint kChannels = 2;
    static const SpeakerProfile kProfile;
    static const TUint kBitDepth = 16;
public:
    SuiteStreamValidator();
private: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    enum EMsgType
    {
        EMsgNone
       ,EMsgMode
       ,EMsgTrack
       ,EMsgDrain
       ,EMsgEncodedStream
       ,EMsgStreamSegment
       ,EMsgDelay
       ,EMsgMetaText
       ,EMsgStreamInterrupted
       ,EMsgHalt
       ,EMsgFlush
       ,EMsgWait
       ,EMsgDecodedStream
       ,EMsgBitRate
       ,EMsgAudioPcm
       ,EMsgAudioDsd
       ,EMsgSilence
       ,EMsgQuit
    };
private:
    void PushMsg(EMsgType aType);
    void StartStream();
private:
    void MsgsPassThrough();
    void SupportedRatePassesThrough();
    void UnsupportedRateStartsFlushing();
    void UnsupportedBitDepthStartsFlushing();
    void UnsupportedFormatStartsFlushing();
    void AudioNotPassedWhileFlushing();
    void MsgsPassWhileFlushing();
    void MsgsEndFlush();
    void ExpectedFlushConsumed();
private: // from IPipelineElementDownstream
    void Push(Msg* aMsg) override;
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgTrack* aMsg) override;
    Msg* ProcessMsg(MsgDrain* aMsg) override;
    Msg* ProcessMsg(MsgDelay* aMsg) override;
    Msg* ProcessMsg(MsgEncodedStream* aMsg) override;
    Msg* ProcessMsg(MsgStreamSegment* aMsg) override;
    Msg* ProcessMsg(MsgAudioEncoded* aMsg) override;
    Msg* ProcessMsg(MsgMetaText* aMsg) override;
    Msg* ProcessMsg(MsgStreamInterrupted* aMsg) override;
    Msg* ProcessMsg(MsgHalt* aMsg) override;
    Msg* ProcessMsg(MsgFlush* aMsg) override;
    Msg* ProcessMsg(MsgWait* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsg(MsgBitRate* aMsg) override;
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgAudioDsd* aMsg) override;
    Msg* ProcessMsg(MsgSilence* aMsg) override;
    Msg* ProcessMsg(MsgPlayable* aMsg) override;
    Msg* ProcessMsg(MsgQuit* aMsg) override;
private: // from IPipelineAnimator
    TUint PipelineAnimatorBufferJiffies() const override;
    TUint PipelineAnimatorDelayJiffies(AudioFormat aFormat, TUint aSampleRate, TUint aBitDepth, TUint aNumChannels) const override;
    TUint PipelineAnimatorDsdBlockSizeWords() const override;
    TUint PipelineAnimatorMaxBitDepth() const override;
    void PipelineAnimatorGetMaxSampleRates(TUint& aPcm, TUint& aDsd) const override;
private: // from IStreamHandler
    EStreamPlay OkToPlay(TUint aStreamId) override;
    TUint TrySeek(TUint aStreamId, TUint64 aOffset) override;
    TUint TryDiscard(TUint aJiffies) override;
    TUint TryStop(TUint aStreamId) override;
    void NotifyStarving(const Brx& aMode, TUint aStreamId, TBool aStarving) override;
private:
    MsgFactory* iMsgFactory;
    TrackFactory* iTrackFactory;
    AllocatorInfoLogger iInfoAggregator;
    StreamValidator* iStreamValidator;
    EMsgType iLastMsg;
    TUint iNextStreamId;
    TByte iAudioData[884]; // 884 => 5ms @ 44.1, 16-bit, stereo
    TUint64 iTrackOffsetTx;
    TBool iRateSupported;
    TBool iBitDepthSupported;
    TBool iFormatSupported;
    TUint iExpectedFlushId;
};

} // namespace Media
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Media;


const SpeakerProfile SuiteStreamValidator::kProfile(2);

SuiteStreamValidator::SuiteStreamValidator()
    : SuiteUnitTest("StreamValidator tests")
{
    AddTest(MakeFunctor(*this, &SuiteStreamValidator::MsgsPassThrough), "MsgsPassThrough");
    AddTest(MakeFunctor(*this, &SuiteStreamValidator::SupportedRatePassesThrough), "SupportedRatePassesThrough");
    AddTest(MakeFunctor(*this, &SuiteStreamValidator::UnsupportedRateStartsFlushing), "UnsupportedRateStartsFlushing");
    AddTest(MakeFunctor(*this, &SuiteStreamValidator::UnsupportedBitDepthStartsFlushing), "UnsupportedBitDepthStartsFlushing");
    AddTest(MakeFunctor(*this, &SuiteStreamValidator::UnsupportedFormatStartsFlushing), "UnsupportedFormatStartsFlushing");
    AddTest(MakeFunctor(*this, &SuiteStreamValidator::AudioNotPassedWhileFlushing), "AudioNotPassedWhileFlushing");
    AddTest(MakeFunctor(*this, &SuiteStreamValidator::MsgsPassWhileFlushing), "MsgsPassWhileFlushing");
    AddTest(MakeFunctor(*this, &SuiteStreamValidator::MsgsEndFlush), "MsgsEndFlush");
    AddTest(MakeFunctor(*this, &SuiteStreamValidator::ExpectedFlushConsumed), "ExpectedFlushConsumed");
}

void SuiteStreamValidator::Setup()
{
    MsgFactoryInitParams init;
    init.SetMsgDelayCount(2);
    init.SetMsgAudioPcmCount(6, 5);
    init.SetMsgDecodedStreamCount(2);
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
    iTrackFactory = new TrackFactory(iInfoAggregator, 3);
    iStreamValidator = new StreamValidator(*iMsgFactory, *this);
    iStreamValidator->SetAnimator(*this);
    iLastMsg = EMsgNone;
    iNextStreamId = 1;
    (void)memset(iAudioData, 0x7f, sizeof(iAudioData));
    iTrackOffsetTx = 0;
    iRateSupported = iBitDepthSupported = iFormatSupported = true;
    iExpectedFlushId = MsgFlush::kIdInvalid;
}

void SuiteStreamValidator::TearDown()
{
    delete iStreamValidator;
    delete iTrackFactory;
    delete iMsgFactory;
}

void SuiteStreamValidator::PushMsg(EMsgType aType)
{
    Msg* msg = nullptr;
    switch (aType)
    {
    case EMsgMode:
        msg = iMsgFactory->CreateMsgMode(Brn("dummyMode"));
        break;
    case EMsgTrack:
    {
        Track* track = iTrackFactory->CreateTrack(Brx::Empty(), Brx::Empty());
        msg = iMsgFactory->CreateMsgTrack(*track);
        track->RemoveRef();
    }
        break;
    case EMsgDrain:
        msg = iMsgFactory->CreateMsgDrain(Functor());
        break;
    case EMsgEncodedStream:
        msg = iMsgFactory->CreateMsgEncodedStream(Brx::Empty(), Brx::Empty(), 0, 0, iNextStreamId, false, true, Multiroom::Allowed, nullptr);
        break;
    case EMsgDelay:
        msg = iMsgFactory->CreateMsgDelay(Jiffies::kPerMs * 20);
        break;
    case EMsgMetaText:
        msg = iMsgFactory->CreateMsgMetaText(Brn("dummy metatext"));
        break;
    case EMsgStreamInterrupted:
        msg = iMsgFactory->CreateMsgStreamInterrupted();
        break;
    case EMsgHalt:
        msg = iMsgFactory->CreateMsgHalt();
        break;
    case EMsgFlush:
        msg = iMsgFactory->CreateMsgFlush(iExpectedFlushId);
        break;
    case EMsgWait:
        msg = iMsgFactory->CreateMsgWait();
        break;
    case EMsgDecodedStream:
        msg = iMsgFactory->CreateMsgDecodedStream(iNextStreamId++, kBitrate, kBitDepth, kSampleRate, kChannels, Brn("Dummy"), 0, 0, true, true, false, false, AudioFormat::Pcm, Multiroom::Allowed, kProfile, this, RampType::Sample);
        break;
    case EMsgBitRate:
        msg = iMsgFactory->CreateMsgBitRate(123);
        break;
    case EMsgAudioPcm:
    {
        Brn audioBuf(iAudioData, sizeof(iAudioData));
        MsgAudioPcm* msgPcm = iMsgFactory->CreateMsgAudioPcm(audioBuf, kChannels, kSampleRate, kBitDepth, AudioDataEndian::Little, iTrackOffsetTx);
        iTrackOffsetTx += msgPcm->Jiffies();
        msg = msgPcm;
    }
        break;
    case EMsgAudioDsd:
    {
        Brn audioBuf(iAudioData, sizeof(iAudioData));
        auto msgDsd = iMsgFactory->CreateMsgAudioDsd(audioBuf, kChannels, kSampleRateDsd, 1, iTrackOffsetTx, 0);
        iTrackOffsetTx += msgDsd->Jiffies();
        msg = msgDsd;
    }
    break;
    case EMsgSilence:
    {
        TUint size = Jiffies::kPerMs * 4;
        msg = iMsgFactory->CreateMsgSilence(size, kSampleRate, kBitDepth, kChannels);
    }
        break;
    case EMsgQuit:
        msg = iMsgFactory->CreateMsgQuit();
        break;
    case EMsgNone:
    default:
        ASSERTS();
        break;
    }
    static_cast<IPipelineElementDownstream*>(iStreamValidator)->Push(msg);
}

void SuiteStreamValidator::StartStream()
{
    EMsgType types[] = { EMsgMode, EMsgTrack, EMsgEncodedStream, EMsgDecodedStream };
    const size_t numElems = sizeof(types) / sizeof(types[0]);
    for (size_t i=0; i<numElems; i++) {
        PushMsg(types[i]);
    }
}

void SuiteStreamValidator::MsgsPassThrough()
{
    EMsgType types[] = { EMsgMode, EMsgTrack, EMsgDrain, EMsgEncodedStream, EMsgDelay,
                         EMsgMetaText, EMsgStreamInterrupted, EMsgHalt, EMsgFlush, EMsgWait, EMsgDecodedStream,
                         EMsgBitRate, EMsgAudioPcm, EMsgAudioDsd, EMsgSilence, EMsgQuit };
    const size_t numElems = sizeof(types) / sizeof(types[0]);
    for (size_t i=0; i<numElems; i++) {
        PushMsg(types[i]);
        TEST(iLastMsg == types[i]);
    }
}

void SuiteStreamValidator::SupportedRatePassesThrough()
{
    iRateSupported = true;
    EMsgType types[] = { EMsgDecodedStream, EMsgAudioPcm, EMsgSilence };
    const size_t numElems = sizeof(types) / sizeof(types[0]);
    for (size_t i=0; i<numElems; i++) {
        PushMsg(types[i]);
        TEST(iLastMsg == types[i]);
    }
}

void SuiteStreamValidator::UnsupportedRateStartsFlushing()
{
    iRateSupported = false;
    PushMsg(EMsgDecodedStream);
    TEST(iLastMsg == EMsgNone);
    TEST(iStreamValidator->iFlushing);
}

void SuiteStreamValidator::UnsupportedBitDepthStartsFlushing()
{
    iBitDepthSupported = false;
    PushMsg(EMsgDecodedStream);
    TEST(iLastMsg == EMsgNone);
    TEST(iStreamValidator->iFlushing);
}

void SuiteStreamValidator::UnsupportedFormatStartsFlushing()
{
    iFormatSupported = false;
    PushMsg(EMsgDecodedStream);
    TEST(iLastMsg == EMsgNone);
    TEST(iStreamValidator->iFlushing);
}

void SuiteStreamValidator::AudioNotPassedWhileFlushing()
{
    iRateSupported = false;
    PushMsg(EMsgDecodedStream);
    TEST(iStreamValidator->iFlushing);
    PushMsg(EMsgAudioPcm);
    TEST(iLastMsg == EMsgNone);
    PushMsg(EMsgSilence);
    TEST(iLastMsg == EMsgNone);
    PushMsg(EMsgAudioDsd);
    TEST(iLastMsg == EMsgNone);
}

void SuiteStreamValidator::MsgsPassWhileFlushing()
{
    iRateSupported = false;
    PushMsg(EMsgDecodedStream);
    TEST(iStreamValidator->iFlushing);
    EMsgType types[] = { EMsgEncodedStream, EMsgDelay, EMsgHalt, EMsgFlush, EMsgWait, EMsgQuit };
    const size_t numElems = sizeof(types) / sizeof(types[0]);
    for (size_t i=0; i<numElems; i++) {
        PushMsg(types[i]);
        TEST(iLastMsg == types[i]);
        TEST(iStreamValidator->iFlushing);
    }
}

void SuiteStreamValidator::MsgsEndFlush()
{
    iRateSupported = false;
    EMsgType types[] = { EMsgMode, EMsgTrack };
    const size_t numElems = sizeof(types) / sizeof(types[0]);
    for (size_t i=0; i<numElems; i++) {
        PushMsg(EMsgDecodedStream);
        TEST(iStreamValidator->iFlushing);
        PushMsg(types[i]);
        TEST(iLastMsg == types[i]);
        TEST(!iStreamValidator->iFlushing);
    }

    iLastMsg = EMsgNone;
    PushMsg(EMsgDecodedStream);
    TEST(iStreamValidator->iFlushing);
    TEST(iLastMsg == EMsgNone);
    iRateSupported = true;
    PushMsg(EMsgDecodedStream);
    TEST(!iStreamValidator->iFlushing);
    TEST(iLastMsg == EMsgDecodedStream);
}

void SuiteStreamValidator::ExpectedFlushConsumed()
{
    iRateSupported = false;
    PushMsg(EMsgDecodedStream);
    PushMsg(EMsgAudioPcm);
    TEST(iStreamValidator->iFlushing);
    TEST(iLastMsg == EMsgNone);
    PushMsg(EMsgFlush);
    TEST(iStreamValidator->iFlushing);
    TEST(iLastMsg == EMsgFlush);
    iExpectedFlushId = 42;
    iLastMsg = EMsgNone;
    PushMsg(EMsgFlush);
    TEST(iStreamValidator->iFlushing);
    TEST(iLastMsg == EMsgFlush);
}

void SuiteStreamValidator::Push(Msg* aMsg)
{
    aMsg = aMsg->Process(*this);
    aMsg->RemoveRef();
}

Msg* SuiteStreamValidator::ProcessMsg(MsgMode* aMsg)
{
    iLastMsg = EMsgMode;
    return aMsg;
}

Msg* SuiteStreamValidator::ProcessMsg(MsgTrack* aMsg)
{
    iLastMsg = EMsgTrack;
    return aMsg;
}

Msg* SuiteStreamValidator::ProcessMsg(MsgDrain* aMsg)
{
    iLastMsg = EMsgDrain;
    return aMsg;
}

Msg* SuiteStreamValidator::ProcessMsg(MsgDelay* aMsg)
{
    iLastMsg = EMsgDelay;
    return aMsg;
}

Msg* SuiteStreamValidator::ProcessMsg(MsgEncodedStream* aMsg)
{
    iLastMsg = EMsgEncodedStream;
    return aMsg;
}

Msg* SuiteStreamValidator::ProcessMsg(MsgStreamSegment* aMsg)
{
    iLastMsg = EMsgStreamSegment;
    return aMsg;
}

Msg* SuiteStreamValidator::ProcessMsg(MsgAudioEncoded* aMsg)
{
    ASSERTS();
    return aMsg;
}

Msg* SuiteStreamValidator::ProcessMsg(MsgMetaText* aMsg)
{
    iLastMsg = EMsgMetaText;
    return aMsg;
}

Msg* SuiteStreamValidator::ProcessMsg(MsgStreamInterrupted* aMsg)
{
    iLastMsg = EMsgStreamInterrupted;
    return aMsg;
}

Msg* SuiteStreamValidator::ProcessMsg(MsgHalt* aMsg)
{
    iLastMsg = EMsgHalt;
    return aMsg;
}

Msg* SuiteStreamValidator::ProcessMsg(MsgFlush* aMsg)
{
    iLastMsg = EMsgFlush;
    return aMsg;
}

Msg* SuiteStreamValidator::ProcessMsg(MsgWait* aMsg)
{
    iLastMsg = EMsgWait;
    return aMsg;
}

Msg* SuiteStreamValidator::ProcessMsg(MsgDecodedStream* aMsg)
{
    iLastMsg = EMsgDecodedStream;
    return aMsg;
}

Msg* SuiteStreamValidator::ProcessMsg(MsgBitRate* aMsg)
{
    iLastMsg = EMsgBitRate;
    return aMsg;
}

Msg* SuiteStreamValidator::ProcessMsg(MsgAudioPcm* aMsg)
{
    iLastMsg = EMsgAudioPcm;
    return aMsg;
}

Msg* SuiteStreamValidator::ProcessMsg(MsgAudioDsd* aMsg)
{
    iLastMsg = EMsgAudioDsd;
    return aMsg;
}

Msg* SuiteStreamValidator::ProcessMsg(MsgSilence* aMsg)
{
    iLastMsg = EMsgSilence;
    return aMsg;
}

Msg* SuiteStreamValidator::ProcessMsg(MsgPlayable* aMsg)
{
    ASSERTS();
    return aMsg;
}

Msg* SuiteStreamValidator::ProcessMsg(MsgQuit* aMsg)
{
    iLastMsg = EMsgQuit;
    return aMsg;
}

TUint SuiteStreamValidator::PipelineAnimatorBufferJiffies() const
{
    return 0;
}

TUint SuiteStreamValidator::PipelineAnimatorDelayJiffies(AudioFormat /*aFormat*/, TUint /*aSampleRate*/, TUint /*aBitDepth*/, TUint /*aNumChannels*/) const
{
    if (!iRateSupported) {
        THROW(SampleRateUnsupported);
    }
    if (!iBitDepthSupported) {
        THROW(BitDepthUnsupported);
    }
    if (!iFormatSupported) {
        THROW(FormatUnsupported);
    }
    return Jiffies::kPerMs * 5;
}

TUint SuiteStreamValidator::PipelineAnimatorDsdBlockSizeWords() const
{
    return 1;
}

TUint SuiteStreamValidator::PipelineAnimatorMaxBitDepth() const
{
    return 32;
}

void SuiteStreamValidator::PipelineAnimatorGetMaxSampleRates(TUint& aPcm, TUint& aDsd) const
{
    aPcm = 192000;
    aDsd = 5644800;
}

EStreamPlay SuiteStreamValidator::OkToPlay(TUint /*aStreamId*/)
{
    return ePlayNo;
}

TUint SuiteStreamValidator::TrySeek(TUint /*aStreamId*/, TUint64 /*aOffset*/)
{
    ASSERTS();
    return MsgFlush::kIdInvalid;
}

TUint SuiteStreamValidator::TryDiscard(TUint /*aJiffies*/)
{
    ASSERTS();
    return MsgFlush::kIdInvalid;
}

TUint SuiteStreamValidator::TryStop(TUint /*aStreamId*/)
{
    return iExpectedFlushId;
}

void SuiteStreamValidator::NotifyStarving(const Brx& /*aMode*/, TUint /*aStreamId*/, TBool /*aStarving*/)
{
    ASSERTS();
}



void TestStreamValidator()
{
    Runner runner("StreamValidator tests\n");
    runner.Add(new SuiteStreamValidator());
    runner.Run();
}

#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/SuiteUnitTest.h>
#include <OpenHome/Media/Pipeline/Skipper.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Utils/AllocatorInfoLogger.h>
#include <OpenHome/Media/Utils/ProcessorAudioUtils.h>

#include <list>
#include <limits.h>

using namespace OpenHome;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Media;

namespace OpenHome {
namespace Media {

class SuiteSkipper : public SuiteUnitTest, private IPipelineElementUpstream, private IStreamHandler, private IMsgProcessor
{
    static const TUint kRampDurationLong = Jiffies::kPerMs * 50; // shorter than production code but this is assumed to not matter
    static const TUint kRampDurationShort = Jiffies::kPerMs * 20;
    static const TUint kRampDuration = kRampDurationLong;
    static const TUint kExpectedFlushId = 5;
    static const TUint kSampleRate = 44100;
    static const TUint kNumChannels = 2;
    static const SpeakerProfile kProfile;
public:
    SuiteSkipper();
    ~SuiteSkipper();
private: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private: // from IPipelineElementUpstream
    Msg* Pull() override;
private: // from IStreamHandler
    EStreamPlay OkToPlay(TUint aStreamId) override;
    TUint TrySeek(TUint aStreamId, TUint64 aOffset) override;
    TUint TryDiscard(TUint aJiffies) override;
    TUint TryStop(TUint aStreamId) override;
    void NotifyStarving(const Brx& aMode, TUint aStreamId, TBool aStarving) override;
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
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgAudioDsd* aMsg) override;
    Msg* ProcessMsg(MsgSilence* aMsg) override;
    Msg* ProcessMsg(MsgPlayable* aMsg) override;
    Msg* ProcessMsg(MsgQuit* aMsg) override;
private:
    enum EMsgType
    {
        ENone
       ,EMsgMode
       ,EMsgTrack
       ,EMsgDrain
       ,EMsgDelay
       ,EMsgEncodedStream
       ,EMsgMetaText
       ,EMsgStreamInterrupted
       ,EMsgDecodedStream
       ,EMsgAudioPcm
       ,EMsgAudioDsd
       ,EMsgSilence
       ,EMsgHalt
       ,EMsgFlush
       ,EMsgWait
       ,EMsgQuit
    };
private:
    void PullNext();
    void PullNext(EMsgType aExpectedMsg);
    Msg* CreateTrack();
    Msg* CreateEncodedStream();
    Msg* CreateDecodedStream();
    Msg* CreateAudio();
    Msg* CreateAudioDsd();
    Msg* CreateSilence(TUint aJiffies);
    void TestAllMsgsPassWhileNotSkipping();
    void TestRemoveStreamRampAudioRampsDown();
    void TestRampDurationTakenFromMode();
    void TestRemoveStreamRampHaltDeliveredOnRampDown();
    void TestRemoveStreamRampMsgsPassDuringRamp();
    void TestRemoveStreamRampFewMsgsPassAfterRamp();
    void TestRemoveStreamRampNewStreamResets();
    void TestRemoveStreamNoRampFewMsgsPass();
    void TestTryRemoveInvalidStream();
    void TestTryRemoveRampValidStream();
    void TestTryRemoveNoRampValidStream();
    void TestSilenceEndsRamp();
    void TestRemoveAllFlushesMostMsgs();
private:
    AllocatorInfoLogger iInfoAggregator;
    TrackFactory* iTrackFactory;
    MsgFactory* iMsgFactory;
    Skipper* iSkipper;
    EMsgType iLastPulledMsg;
    TBool iRamping;
    TUint iStreamId;
    TUint64 iTrackOffset;
    TUint64 iJiffies;
    std::list<Msg*> iPendingMsgs;
    TUint iLastSubsample;
    TUint iNextStreamId;
    TUint iLastHaltId;
};

} // namespace Media
} // namespace OpenHome


const SpeakerProfile SuiteSkipper::kProfile(2);

static const TChar* kMsgTypes[] = {  "None"
                                    ,"Mode"
                                    ,"Track"
                                    ,"Drain"
                                    ,"Delay"
                                    ,"EncodedStream"
                                    ,"Metatext"
                                    ,"StreamInterrupted"
                                    ,"DecodedStream"
                                    ,"BitRate"
                                    ,"AudioPcm"
                                    ,"Silence"
                                    ,"Halt"
                                    ,"Flush"
                                    ,"Wait"
                                    ,"Quit"
                                  };

SuiteSkipper::SuiteSkipper()
    : SuiteUnitTest("Skipper")
{
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestAllMsgsPassWhileNotSkipping), "TestAllMsgsPassWhileNotSkipping");
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestRemoveStreamRampAudioRampsDown), "TestRemoveStreamRampAudioRampsDown");
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestRampDurationTakenFromMode), "TestRampDurationTakenFromMode");
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestRemoveStreamRampHaltDeliveredOnRampDown), "TestRemoveStreamRampHaltDeliveredOnRampDown");
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestRemoveStreamRampMsgsPassDuringRamp), "TestRemoveStreamRampMsgsPassDuringRamp");
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestRemoveStreamRampFewMsgsPassAfterRamp), "TestRemoveStreamRampFewMsgsPassAfterRamp");
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestRemoveStreamRampNewStreamResets), "TestRemoveStreamRampNewStreamResets");
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestRemoveStreamNoRampFewMsgsPass), "TestRemoveStreamNoRampFewMsgsPass");
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestTryRemoveInvalidStream), "TestTryRemoveInvalidStream");
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestTryRemoveRampValidStream), "TestTryRemoveRampValidStream");
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestTryRemoveNoRampValidStream), "TestTryRemoveNoRampValidStream");
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestSilenceEndsRamp), "TestSilenceEndsRamp");
    AddTest(MakeFunctor(*this, &SuiteSkipper::TestRemoveAllFlushesMostMsgs), "TestRemoveAllFlushesMostMsgs");
}

SuiteSkipper::~SuiteSkipper()
{
}

void SuiteSkipper::Setup()
{
    iTrackFactory = new TrackFactory(iInfoAggregator, 5);
    MsgFactoryInitParams init;
    init.SetMsgAudioPcmCount(52, 50);
    init.SetMsgSilenceCount(10);
    init.SetMsgDecodedStreamCount(3);
    init.SetMsgTrackCount(3);
    init.SetMsgEncodedStreamCount(3);
    init.SetMsgMetaTextCount(3);
    init.SetMsgHaltCount(2);
    init.SetMsgFlushCount(2);
    init.SetMsgWaitCount(2);
    init.SetMsgModeCount(2);
    init.SetMsgDelayCount(2);
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
    iSkipper = new Skipper(*iMsgFactory, *this, kRampDurationLong, kRampDurationShort);
    iStreamId = UINT_MAX;
    iTrackOffset = 0;
    iJiffies = 0;
    iRamping = false;
    iLastSubsample = 0xffffff;
    iNextStreamId = 1;
    iLastHaltId = MsgHalt::kIdInvalid;
}

void SuiteSkipper::TearDown()
{
    while (iPendingMsgs.size() > 0) {
        iPendingMsgs.front()->RemoveRef();
        iPendingMsgs.pop_front();
    }
    delete iSkipper;
    delete iMsgFactory;
    delete iTrackFactory;
}

Msg* SuiteSkipper::Pull()
{
    ASSERT(iPendingMsgs.size() > 0);
    Msg* msg = iPendingMsgs.front();
    iPendingMsgs.pop_front();
    return msg;
}

EStreamPlay SuiteSkipper::OkToPlay(TUint /*aStreamId*/)
{
    return ePlayNo;
}

TUint SuiteSkipper::TrySeek(TUint /*aStreamId*/, TUint64 /*aOffset*/)
{
    ASSERTS();
    return MsgFlush::kIdInvalid;
}

TUint SuiteSkipper::TryDiscard(TUint /*aJiffies*/)
{
    ASSERTS();
    return MsgFlush::kIdInvalid;
}

TUint SuiteSkipper::TryStop(TUint aStreamId)
{
    if (aStreamId == iStreamId) {
        return kExpectedFlushId;
    }
    return MsgFlush::kIdInvalid;
}

void SuiteSkipper::NotifyStarving(const Brx& /*aMode*/, TUint /*aStreamId*/, TBool /*aStarving*/)
{
}

Msg* SuiteSkipper::ProcessMsg(MsgMode* aMsg)
{
    iLastPulledMsg = EMsgMode;
    return aMsg;
}

Msg* SuiteSkipper::ProcessMsg(MsgTrack* aMsg)
{
    iLastPulledMsg = EMsgTrack;
    return aMsg;
}

Msg* SuiteSkipper::ProcessMsg(MsgDrain* aMsg)
{
    iLastPulledMsg = EMsgDrain;
    return aMsg;
}

Msg* SuiteSkipper::ProcessMsg(MsgDelay* aMsg)
{
    iLastPulledMsg = EMsgDelay;
    return aMsg;
}

Msg* SuiteSkipper::ProcessMsg(MsgEncodedStream* aMsg)
{
    iLastPulledMsg = EMsgEncodedStream;
    iStreamId = aMsg->StreamId();
    return aMsg;
}

Msg* SuiteSkipper::ProcessMsg(MsgStreamSegment* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuiteSkipper::ProcessMsg(MsgAudioEncoded* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuiteSkipper::ProcessMsg(MsgMetaText* aMsg)
{
    iLastPulledMsg = EMsgMetaText;
    return aMsg;
}

Msg* SuiteSkipper::ProcessMsg(MsgStreamInterrupted* aMsg)
{
    iLastPulledMsg = EMsgStreamInterrupted;
    return aMsg;
}

Msg* SuiteSkipper::ProcessMsg(MsgHalt* aMsg)
{
    iLastPulledMsg = EMsgHalt;
    iLastHaltId = aMsg->Id();
    return aMsg;
}

Msg* SuiteSkipper::ProcessMsg(MsgFlush* aMsg)
{
    iLastPulledMsg = EMsgFlush;
    return aMsg;
}

Msg* SuiteSkipper::ProcessMsg(MsgWait* aMsg)
{
    iLastPulledMsg = EMsgWait;
    return aMsg;
}

Msg* SuiteSkipper::ProcessMsg(MsgDecodedStream* aMsg)
{
    iLastPulledMsg = EMsgDecodedStream;
    return aMsg;
}

Msg* SuiteSkipper::ProcessMsg(MsgAudioPcm* aMsg)
{
    iLastPulledMsg = EMsgAudioPcm;
    iJiffies += aMsg->Jiffies();
    MsgPlayable* playable = aMsg->CreatePlayable();
    ProcessorPcmBufTest pcmProcessor;
    playable->Read(pcmProcessor);
    Brn buf(pcmProcessor.Buf());
    ASSERT(buf.Bytes() >= 6);
    const TByte* ptr = buf.Ptr();
    const TUint bytes = buf.Bytes();
    const TUint firstSubsample = (ptr[0]<<16) | (ptr[1]<<8) | ptr[2];

    if (iRamping) {
        TEST(firstSubsample <= iLastSubsample);
    }
    else {
        TEST(firstSubsample == 0x7f7f7f);
    }
        iLastSubsample = (ptr[bytes-3]<<16) | (ptr[bytes-2]<<8) | ptr[bytes-1];
    if (iRamping) {
        TEST(iLastSubsample < firstSubsample);
        iRamping = (iLastSubsample > 0);
    }
    else {
        TEST(firstSubsample == 0x7f7f7f);
    }

    return playable;
}

Msg* SuiteSkipper::ProcessMsg(MsgAudioDsd* aMsg)
{
    iLastPulledMsg = EMsgAudioDsd;
    iJiffies += aMsg->Jiffies();
    return aMsg;
}

Msg* SuiteSkipper::ProcessMsg(MsgSilence* aMsg)
{
    iLastPulledMsg = EMsgSilence;
    return aMsg;
}

Msg* SuiteSkipper::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuiteSkipper::ProcessMsg(MsgQuit* aMsg)
{
    iLastPulledMsg = EMsgQuit;
    return aMsg;
}

void SuiteSkipper::PullNext()
{
    Msg* msg = iSkipper->Pull();
    msg = msg->Process(*this);
    msg->RemoveRef();
}

void SuiteSkipper::PullNext(EMsgType aExpectedMsg)
{
    Msg* msg = iSkipper->Pull();
    msg = msg->Process(*this);
    msg->RemoveRef();
    if (iLastPulledMsg != aExpectedMsg) {
        Print("Expected %s, got %s\n", kMsgTypes[aExpectedMsg], kMsgTypes[iLastPulledMsg]);
    }
    TEST(iLastPulledMsg == aExpectedMsg);
}

Msg* SuiteSkipper::CreateTrack()
{
    Track* track = iTrackFactory->CreateTrack(Brx::Empty(), Brx::Empty());
    Msg* msg = iMsgFactory->CreateMsgTrack(*track);
    track->RemoveRef();
    return msg;
}

Msg* SuiteSkipper::CreateEncodedStream()
{
    return iMsgFactory->CreateMsgEncodedStream(Brx::Empty(), Brx::Empty(), 1<<21, 0, ++iNextStreamId, true, false, Multiroom::Allowed, this);
}

Msg* SuiteSkipper::CreateDecodedStream()
{
    return iMsgFactory->CreateMsgDecodedStream(iNextStreamId, 100, 24, kSampleRate, kNumChannels, Brn("notARealCodec"), 1LL<<38, 0, true, true, false, false, AudioFormat::Pcm, Multiroom::Allowed, kProfile, this, RampType::Sample);
}

Msg* SuiteSkipper::CreateAudio()
{
    static const TUint kDataBytes = 3 * 1024;
    TByte encodedAudioData[kDataBytes];
    (void)memset(encodedAudioData, 0x7f, kDataBytes);
    Brn encodedAudioBuf(encodedAudioData, kDataBytes);
    MsgAudioPcm* audio = iMsgFactory->CreateMsgAudioPcm(encodedAudioBuf, kNumChannels, kSampleRate, 24, AudioDataEndian::Little, iTrackOffset);
    iTrackOffset += audio->Jiffies();
    return audio;
}

Msg* SuiteSkipper::CreateAudioDsd()
{
    TByte audioData[128];
    (void)memset(audioData, 0x7f, sizeof audioData);
    Brn audioBuf(audioData, sizeof audioData);
    MsgAudioDsd* audio = iMsgFactory->CreateMsgAudioDsd(audioBuf, 2, 2822400, 2, iTrackOffset, 0);
    iTrackOffset += audio->Jiffies();
    return audio;
}

Msg* SuiteSkipper::CreateSilence(TUint aJiffies)
{
    TUint jiffies = aJiffies;
    return iMsgFactory->CreateMsgSilence(jiffies, kSampleRate, 24, kNumChannels);
}

void SuiteSkipper::TestAllMsgsPassWhileNotSkipping()
{
    iPendingMsgs.push_back(iMsgFactory->CreateMsgMode(Brx::Empty()));
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgDrain(Functor()));
    iPendingMsgs.push_back(iMsgFactory->CreateMsgDelay(0));
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    iPendingMsgs.push_back(iMsgFactory->CreateMsgStreamInterrupted());
    iPendingMsgs.push_back(CreateDecodedStream());
    iPendingMsgs.push_back(CreateAudio());
    iPendingMsgs.push_back(CreateAudioDsd());
    iPendingMsgs.push_back(CreateSilence(Jiffies::kPerMs * 3));
    iPendingMsgs.push_back(iMsgFactory->CreateMsgHalt());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(2));
    iPendingMsgs.push_back(iMsgFactory->CreateMsgWait());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgQuit());
    iPendingMsgs.push_back(CreateTrack());

    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    PullNext(EMsgDrain);
    PullNext(EMsgDelay);
    PullNext(EMsgEncodedStream);
    PullNext(EMsgMetaText);
    PullNext(EMsgStreamInterrupted);
    PullNext(EMsgDecodedStream);
    PullNext(EMsgAudioPcm);
    PullNext(EMsgAudioDsd);
    PullNext(EMsgSilence);
    PullNext(EMsgHalt);
    PullNext(EMsgFlush);
    PullNext(EMsgWait);
    PullNext(EMsgQuit);
    PullNext(EMsgTrack);
}

void SuiteSkipper::TestRemoveStreamRampAudioRampsDown()
{
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(CreateDecodedStream());
    iPendingMsgs.push_back(CreateAudio());

    for (TUint i=0; i<4; i++) {
        PullNext();
    }
    TEST(iLastPulledMsg == EMsgAudioPcm);

    TEST(iSkipper->TryRemoveStream(iStreamId, true));
    iRamping = true;
    iJiffies = 0;
    while (iRamping) {
        iPendingMsgs.push_back(CreateAudio());
        PullNext(EMsgAudioPcm);
    }
    TEST(iJiffies == iSkipper->iRampJiffies);
}

void SuiteSkipper::TestRampDurationTakenFromMode()
{
    ModeInfo info;
    info.SetRampDurations(true, false);
    TUint expectedRampJiffies = kRampDurationLong;
    ModeTransportControls transportControls;
    auto mode = iMsgFactory->CreateMsgMode(Brn("Mode"), info, nullptr, transportControls);
    iPendingMsgs.push_back(mode);
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(CreateDecodedStream());
    iPendingMsgs.push_back(CreateAudio());
    for (TUint i=0; i<5; i++) {
        PullNext();
    }
    TEST(iLastPulledMsg == EMsgAudioPcm);
    TEST(iSkipper->TryRemoveStream(iStreamId, true));
    iRamping = true;
    iJiffies = 0;
    while (iRamping) {
        iPendingMsgs.push_back(CreateAudio());
        PullNext(EMsgAudioPcm);
    }
    TEST(iJiffies == expectedRampJiffies);

    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(kExpectedFlushId)); // should be consumed by Skipper
    info.SetRampDurations(false, false);
    expectedRampJiffies = kRampDurationShort;
    mode = iMsgFactory->CreateMsgMode(Brn("Mode"), info, nullptr, transportControls);
    iPendingMsgs.push_back(mode);
    PullNext(EMsgHalt);
    PullNext(EMsgFlush);
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(CreateDecodedStream());
    iPendingMsgs.push_back(CreateAudio());
    for (TUint i=0; i<5; i++) {
        PullNext();
    }
    TEST(iLastPulledMsg == EMsgAudioPcm);
    TEST(iSkipper->TryRemoveStream(iStreamId, true));
    iRamping = true;
    iJiffies = 0;
    while (iRamping) {
        iPendingMsgs.push_back(CreateAudio());
        PullNext(EMsgAudioPcm);
    }
    TEST(iJiffies == expectedRampJiffies);
}

void SuiteSkipper::TestRemoveStreamRampHaltDeliveredOnRampDown()
{
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(CreateDecodedStream());
    iPendingMsgs.push_back(CreateAudio());
    for (TUint i=0; i<4; i++) {
        PullNext();
    }

    TEST(iSkipper->TryRemoveStream(iStreamId, true));
    iRamping = true;
    while (iRamping) {
        iPendingMsgs.push_back(CreateAudio());
        PullNext(EMsgAudioPcm);
    }
    PullNext(EMsgHalt);
}

void SuiteSkipper::TestRemoveStreamRampMsgsPassDuringRamp()
{
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(CreateDecodedStream());
    iPendingMsgs.push_back(CreateAudio());

    for (TUint i=0; i<4; i++) {
        PullNext();
    }
    TEST(iLastPulledMsg == EMsgAudioPcm);

    TEST(iSkipper->TryRemoveStream(iStreamId, true));
    iRamping = true;
    iPendingMsgs.push_back(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    PullNext(EMsgMetaText);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(2));
    PullNext(EMsgFlush);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgQuit());
    PullNext(EMsgQuit);
    iPendingMsgs.push_back(CreateTrack());
    PullNext(EMsgTrack);
}

void SuiteSkipper::TestRemoveStreamRampFewMsgsPassAfterRamp()
{
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(CreateDecodedStream());
    iPendingMsgs.push_back(CreateAudio());

    for (TUint i=0; i<4; i++) {
        PullNext();
    }
    TEST(iLastPulledMsg == EMsgAudioPcm);

    TEST(iSkipper->TryRemoveStream(iStreamId, true));
    iRamping = true;
    while (iRamping) {
        iPendingMsgs.push_back(CreateAudio());
        PullNext(EMsgAudioPcm);
    }
    PullNext(EMsgHalt);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    iPendingMsgs.push_back(iMsgFactory->CreateMsgMetaText(Brn("second")));
    iPendingMsgs.push_back(CreateAudio());
    iPendingMsgs.push_back(CreateSilence(Jiffies::kPerMs * 3));
    iPendingMsgs.push_back(iMsgFactory->CreateMsgHalt());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(kExpectedFlushId)); // should be consumed by Skipper
    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(kExpectedFlushId+1));
    PullNext(EMsgHalt);
    PullNext(EMsgFlush);
    PullNext(EMsgMetaText); // last Metatext pulled while flushing will be buffered and delivered later
    PullNext(EMsgFlush);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgWait());
    PullNext(EMsgWait);
    iPendingMsgs.push_back(CreateTrack());
    PullNext(EMsgTrack);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgQuit());
    PullNext(EMsgQuit);
}

void SuiteSkipper::TestRemoveStreamRampNewStreamResets()
{
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(CreateDecodedStream());
    iPendingMsgs.push_back(CreateAudio());

    for (TUint i=0; i<4; i++) {
        PullNext();
    }
    TEST(iLastPulledMsg == EMsgAudioPcm);

    TEST(iSkipper->TryRemoveStream(iStreamId, true));
    iRamping = true;
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgAudioPcm);
    iPendingMsgs.push_back(CreateEncodedStream());
    PullNext(EMsgEncodedStream);
    iRamping = false;
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgAudioPcm);
}

void SuiteSkipper::TestRemoveStreamNoRampFewMsgsPass()
{
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(CreateDecodedStream());
    iPendingMsgs.push_back(CreateAudio());

    for (TUint i=0; i<4; i++) {
        PullNext();
    }
    TEST(iLastPulledMsg == EMsgAudioPcm);

    TEST(iSkipper->TryRemoveStream(iStreamId, false));
    iPendingMsgs.push_back(CreateAudio());
    iPendingMsgs.push_back(CreateSilence(Jiffies::kPerMs * 3));
    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(kExpectedFlushId)); // should be consumed by Skipper
    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(kExpectedFlushId+1));
    PullNext(EMsgHalt);
    PullNext(EMsgFlush);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgWait());
    PullNext(EMsgFlush);
    PullNext(EMsgWait);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgQuit());
    PullNext(EMsgQuit);
    iPendingMsgs.push_back(CreateTrack());
    PullNext(EMsgTrack);
}

void SuiteSkipper::TestTryRemoveInvalidStream()
{
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(CreateDecodedStream());
    for (TUint i=0; i<3; i++) {
        PullNext();
    }

    TEST(!iSkipper->TryRemoveStream(iStreamId+1, true));
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgAudioPcm);
}

void SuiteSkipper::TestTryRemoveRampValidStream()
{
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(CreateDecodedStream());
    iPendingMsgs.push_back(CreateAudio());

    for (TUint i=0; i<4; i++) {
        PullNext();
    }
    TEST(iLastPulledMsg == EMsgAudioPcm);

    TEST(iSkipper->TryRemoveStream(iStreamId, true));
    iRamping = true;

    iPendingMsgs.push_back(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    PullNext(EMsgMetaText);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(2));
    PullNext(EMsgFlush);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgQuit());
    PullNext(EMsgQuit);

    while (iRamping) {
        iPendingMsgs.push_back(CreateAudio());
        PullNext(EMsgAudioPcm);
    }
    PullNext(EMsgHalt);
    iPendingMsgs.push_back(CreateAudio());
    iPendingMsgs.push_back(CreateSilence(Jiffies::kPerMs * 3));
    iPendingMsgs.push_back(iMsgFactory->CreateMsgHalt());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(kExpectedFlushId)); // should be consumed by Skipper
    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(kExpectedFlushId+1));
    PullNext(EMsgHalt);
    PullNext(EMsgFlush);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgWait());
    PullNext(EMsgFlush);
    PullNext(EMsgWait);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgQuit());
    PullNext(EMsgQuit);
    iPendingMsgs.push_back(CreateTrack());
    PullNext(EMsgTrack);
}

void SuiteSkipper::TestTryRemoveNoRampValidStream()
{
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(CreateDecodedStream());
    iPendingMsgs.push_back(CreateAudio());

    for (TUint i=0; i<4; i++) {
        PullNext();
    }
    TEST(iLastPulledMsg == EMsgAudioPcm);

    TEST(iSkipper->TryRemoveStream(iStreamId, false));
    iRamping = false;

    iPendingMsgs.push_back(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    iPendingMsgs.push_back(CreateAudio());
    iPendingMsgs.push_back(CreateSilence(Jiffies::kPerMs * 3));
    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(kExpectedFlushId)); // should be consumed by Skipper
    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(kExpectedFlushId+1));
    PullNext(EMsgHalt);
    PullNext(EMsgFlush);
    PullNext(EMsgMetaText);
    PullNext(EMsgFlush);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgWait());
    PullNext(EMsgWait);
    iPendingMsgs.push_back(iMsgFactory->CreateMsgQuit());
    PullNext(EMsgQuit);
    iPendingMsgs.push_back(CreateTrack());
    PullNext(EMsgTrack);
}

void SuiteSkipper::TestSilenceEndsRamp()
{
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(CreateDecodedStream());
    iPendingMsgs.push_back(CreateAudio());

    for (TUint i=0; i<4; i++) {
        PullNext();
    }
    TEST(iLastPulledMsg == EMsgAudioPcm);

    TEST(iSkipper->TryRemoveStream(iStreamId, true));
    iRamping = true;
    iJiffies = 0;
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgAudioPcm);
    iPendingMsgs.push_back(CreateSilence(Jiffies::kPerMs * 3));
    PullNext(EMsgHalt);
    iRamping = false;
    TEST(iJiffies < kRampDuration);
    iPendingMsgs.push_back(CreateAudio());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(kExpectedFlushId));
    PullNext(EMsgFlush);
    iPendingMsgs.push_back(CreateEncodedStream());
    PullNext(EMsgEncodedStream);
}

void SuiteSkipper::TestRemoveAllFlushesMostMsgs()
{
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(CreateDecodedStream());
    iPendingMsgs.push_back(CreateAudio());

    for (TUint i=0; i<4; i++) {
        PullNext();
    }
    TEST(iLastPulledMsg == EMsgAudioPcm);

    const TUint kHaltId = 42;
    iSkipper->RemoveAll(kHaltId, true);
    iRamping = true;
    iJiffies = 0;
    iPendingMsgs.push_back(CreateAudio());
    PullNext(EMsgAudioPcm);
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateEncodedStream());
    PullNext(EMsgHalt);
    TEST(iLastHaltId == MsgHalt::kIdNone);

    iPendingMsgs.push_back(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    iPendingMsgs.push_back(CreateDecodedStream());
    iPendingMsgs.push_back(CreateAudio());
    iPendingMsgs.push_back(CreateSilence(Jiffies::kPerMs));
    iPendingMsgs.push_back(iMsgFactory->CreateMsgHalt());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgFlush(2));
    iPendingMsgs.push_back(iMsgFactory->CreateMsgWait());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgMode(Brx::Empty()));
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgDrain(Functor()));
    iPendingMsgs.push_back(iMsgFactory->CreateMsgDelay(0));
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    iPendingMsgs.push_back(CreateDecodedStream());
    iPendingMsgs.push_back(CreateAudio());
    iPendingMsgs.push_back(CreateEncodedStream());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    iPendingMsgs.push_back(CreateDecodedStream());
    iPendingMsgs.push_back(CreateAudio());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgHalt(kHaltId));

    PullNext(EMsgHalt);
    PullNext(EMsgFlush);
    PullNext(EMsgMode);
    PullNext(EMsgDrain);
    PullNext(EMsgDelay);
    PullNext(EMsgHalt);
    TEST(iLastHaltId == kHaltId);
    TEST(iPendingMsgs.size() == 0);
}


void TestSkipper()
{
    Runner runner("Skipper tests\n");
    runner.Add(new SuiteSkipper());
    runner.Run();
}

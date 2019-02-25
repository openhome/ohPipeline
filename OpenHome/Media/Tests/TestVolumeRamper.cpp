#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/SuiteUnitTest.h>
#include <OpenHome/Media/Pipeline/VolumeRamper.h>
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

class SuiteVolumeRamper : public SuiteUnitTest
                        , private IPipelineElementUpstream
                        , private IMsgProcessor
                        , private IVolumeRamper
{
    static const TUint kExpectedFlushId = 5;
    static const TUint kSampleRate = 44100;
    static const TUint kNumChannels = 2;
    static const SpeakerProfile kProfile;
    static const TUint kVolumeMultiplierUninitialised = IVolumeRamper::kMultiplierFull + 1;
    static const TUint kRampDuration = Jiffies::kPerMs * 100;
    static const TUint kRampDurationDsd = Jiffies::kPerMs * 20; // CreateAudioDsd outputs relatively short msgs
public:
    SuiteVolumeRamper();
    ~SuiteVolumeRamper();
private: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private: // from IPipelineElementUpstream
    Msg* Pull() override;
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgTrack* aMsg) override;
    Msg* ProcessMsg(MsgDrain* aMsg) override;
    Msg* ProcessMsg(MsgDelay* aMsg) override;
    Msg* ProcessMsg(MsgEncodedStream* aMsg) override;
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
private: // from IVolumeRamper
    void ApplyVolumeMultiplier(TUint aValue) override;
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
    Msg* CreateDecodedStream();
    Msg* CreateAudio();
    Msg* CreateAudioDsd();
    Msg* ApplyRamp(MsgAudioDecoded* aAudio);
    void DrainCallback();
    void HaltCallback();
private:
    void TestMsgsPass();
    void TestMutesWhenHaltAcknowledged();
    void TestMutesWhenDrainAcknowledged();
    void TestNoMuteWhenAudioBeforeHaltAcknowledged();
    void TestUnmutesOnNonBypassAudio();
    void TestBypassRampsVolumeDownOnAudioRampDown();
    void TestBypassRampsVolumeUpOnAudioRampUp();
    void TestDsdRampsVolumeDownOnAudioRampDown();
private:
    AllocatorInfoLogger iInfoAggregator;
    TrackFactory* iTrackFactory;
    MsgFactory* iMsgFactory;
    VolumeRamper* iVolumeRamper;
    EMsgType iNextMsg;
    EMsgType iLastPulledMsg;
    TUint64 iTrackOffset;
    TBool iDrainAcknowledged;
    TBool iHaltAcknowledged;
    TBool iDeferDrainAcknowledgement;
    TBool iDeferHaltAcknowledgement;
    TBool iAnalogBypassEnable;
    AudioFormat iFormat;
    Ramp::EDirection iRampDirection;
    TUint iRampPos;
    TUint iRampRemaining;
    TUint iLastRampMultiplier;
    MsgDrain* iLastDrainMsg;
    MsgHalt* iLastHaltMsg;
};

} // namespace Media
} // namespace OpenHome


const SpeakerProfile SuiteVolumeRamper::kProfile(2);

SuiteVolumeRamper::SuiteVolumeRamper()
    : SuiteUnitTest("VolumeRamper")
{
    AddTest(MakeFunctor(*this, &SuiteVolumeRamper::TestMsgsPass), "TestMsgsPass");
    AddTest(MakeFunctor(*this, &SuiteVolumeRamper::TestMutesWhenHaltAcknowledged), "TestMutesWhenHaltAcknowledged");
    AddTest(MakeFunctor(*this, &SuiteVolumeRamper::TestMutesWhenDrainAcknowledged), "TestMutesWhenDrainAcknowledged");
    AddTest(MakeFunctor(*this, &SuiteVolumeRamper::TestNoMuteWhenAudioBeforeHaltAcknowledged), "TestNoMuteWhenAudioBeforeHaltAcknowledged");
    AddTest(MakeFunctor(*this, &SuiteVolumeRamper::TestUnmutesOnNonBypassAudio), "TestUnmutesOnNonBypassAudio");
    AddTest(MakeFunctor(*this, &SuiteVolumeRamper::TestBypassRampsVolumeDownOnAudioRampDown), "TestBypassRampsVolumeDownOnAudioRampDown");
    AddTest(MakeFunctor(*this, &SuiteVolumeRamper::TestBypassRampsVolumeUpOnAudioRampUp), "TestBypassRampsVolumeUpOnAudioRampUp");
    AddTest(MakeFunctor(*this, &SuiteVolumeRamper::TestDsdRampsVolumeDownOnAudioRampDown), "TestDsdRampsVolumeDownOnAudioRampDown");
}

SuiteVolumeRamper::~SuiteVolumeRamper()
{
}

void SuiteVolumeRamper::Setup()
{
    iTrackFactory = new TrackFactory(iInfoAggregator, 5);
    MsgFactoryInitParams init;
    init.SetMsgAudioPcmCount(2, 1);
    init.SetMsgAudioDsdCount(2);
    init.SetMsgDrainCount(2);
    init.SetMsgHaltCount(2);
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
    iVolumeRamper = new VolumeRamper(*iMsgFactory, *this);
    iVolumeRamper->SetVolumeRamper(*this);
    iNextMsg = iLastPulledMsg = ENone;
    iTrackOffset = 0;
    iDrainAcknowledged = iHaltAcknowledged = iDeferDrainAcknowledgement = iDeferHaltAcknowledgement = false;
    iAnalogBypassEnable = false;
    iFormat = AudioFormat::Pcm;
    iRampDirection = Ramp::ENone;
    iLastRampMultiplier = kVolumeMultiplierUninitialised;
    iLastDrainMsg = nullptr;
    iLastHaltMsg = nullptr;
}

void SuiteVolumeRamper::TearDown()
{
    delete iVolumeRamper;
    delete iMsgFactory;
    delete iTrackFactory;
}

Msg* SuiteVolumeRamper::Pull()
{
    switch (iNextMsg)
    {
    case EMsgMode:
        return iMsgFactory->CreateMsgMode(Brx::Empty());
    case EMsgDrain:
        return iMsgFactory->CreateMsgDrain(MakeFunctor(*this, &SuiteVolumeRamper::DrainCallback));
    case EMsgStreamInterrupted:
        return iMsgFactory->CreateMsgStreamInterrupted();
    case EMsgDecodedStream:
        return iMsgFactory->CreateMsgDecodedStream(1, 100, 24, kSampleRate, kNumChannels, Brn("notARealCodec"), 1LL<<38, 0, true, true, false, iAnalogBypassEnable, iFormat, Multiroom::Allowed, kProfile, nullptr);
    case EMsgAudioPcm:
        return CreateAudio();
    case EMsgAudioDsd:
        return CreateAudioDsd();
    case EMsgSilence:
    {
        TUint size = Jiffies::kPerMs * 3;
        return iMsgFactory->CreateMsgSilence(size, kSampleRate, 24, kNumChannels);
    }
    case EMsgHalt:
        return iMsgFactory->CreateMsgHalt(42, MakeFunctor(*this, &SuiteVolumeRamper::HaltCallback));
    case EMsgQuit:
        return iMsgFactory->CreateMsgQuit();
    default:
        ASSERTS();
    }
    return nullptr;
}

Msg* SuiteVolumeRamper::ProcessMsg(MsgMode* aMsg)
{
    iLastPulledMsg = EMsgMode;
    return aMsg;
}

Msg* SuiteVolumeRamper::ProcessMsg(MsgTrack* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuiteVolumeRamper::ProcessMsg(MsgDrain* aMsg)
{
    iLastPulledMsg = EMsgDrain;
    if (iDeferDrainAcknowledgement) {
        iLastDrainMsg = aMsg;
        return nullptr;
    }
    aMsg->ReportDrained();
    return aMsg;
}

Msg* SuiteVolumeRamper::ProcessMsg(MsgDelay* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuiteVolumeRamper::ProcessMsg(MsgEncodedStream* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuiteVolumeRamper::ProcessMsg(MsgAudioEncoded* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuiteVolumeRamper::ProcessMsg(MsgMetaText* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuiteVolumeRamper::ProcessMsg(MsgStreamInterrupted* aMsg)
{
    iLastPulledMsg = EMsgStreamInterrupted;
    return aMsg;
}

Msg* SuiteVolumeRamper::ProcessMsg(MsgHalt* aMsg)
{
    iLastPulledMsg = EMsgHalt;
    if (iDeferHaltAcknowledgement) {
        iLastHaltMsg = aMsg;
        return nullptr;
    }
    aMsg->ReportHalted();
    return aMsg;
}

Msg* SuiteVolumeRamper::ProcessMsg(MsgFlush* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuiteVolumeRamper::ProcessMsg(MsgWait* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuiteVolumeRamper::ProcessMsg(MsgDecodedStream* aMsg)
{
    iLastPulledMsg = EMsgDecodedStream;
    return aMsg;
}

Msg* SuiteVolumeRamper::ProcessMsg(MsgBitRate* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuiteVolumeRamper::ProcessMsg(MsgAudioPcm* aMsg)
{
    iLastPulledMsg = EMsgAudioPcm;
    return aMsg;
}

Msg* SuiteVolumeRamper::ProcessMsg(MsgAudioDsd* aMsg)
{
    iLastPulledMsg = EMsgAudioDsd;
    return aMsg;
}

Msg* SuiteVolumeRamper::ProcessMsg(MsgSilence* aMsg)
{
    iLastPulledMsg = EMsgSilence;
    return aMsg;
}

Msg* SuiteVolumeRamper::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuiteVolumeRamper::ProcessMsg(MsgQuit* aMsg)
{
    iLastPulledMsg = EMsgQuit;
    return aMsg;
}

void SuiteVolumeRamper::ApplyVolumeMultiplier(TUint aValue)
{
    iLastRampMultiplier = aValue;
}

void SuiteVolumeRamper::PullNext(EMsgType aExpectedMsg)
{
    iNextMsg = aExpectedMsg;
    Msg* msg = static_cast<IPipelineElementUpstream*>(iVolumeRamper)->Pull();
    msg = msg->Process(*this);
    if (msg != nullptr) {
        msg->RemoveRef();
    }
    if (iLastPulledMsg != aExpectedMsg) {
        static const TChar* types[] = {
              "None"
            , "MsgMode"
            , "MsgTrack"
            , "MsgDrain"
            , "MsgDelay"
            , "MsgEncodedStream"
            , "MsgMetaText"
            , "MsgStreamInterrupted"
            , "MsgDecodedStream"
            , "MsgAudioPcm"
            , "MsgAudioDsd"
            , "MsgSilence"
            , "MsgHalt"
            , "MsgFlush"
            , "MsgWait"
            , "MsgQuit" };
        Print("Expected %s, got %s\n", types[iLastPulledMsg], types[aExpectedMsg]);
    }
    TEST(iLastPulledMsg == aExpectedMsg);
}

Msg* SuiteVolumeRamper::CreateAudio()
{
    static const TUint kDataBytes = 3 * 1024;
    TByte encodedAudioData[kDataBytes];
    (void)memset(encodedAudioData, 0x7f, kDataBytes);
    Brn encodedAudioBuf(encodedAudioData, kDataBytes);
    MsgAudioPcm* audio = iMsgFactory->CreateMsgAudioPcm(encodedAudioBuf, kNumChannels, kSampleRate, 24, AudioDataEndian::Little, iTrackOffset);
    iTrackOffset += audio->Jiffies();

    return ApplyRamp(audio);
}

Msg* SuiteVolumeRamper::CreateAudioDsd()
{
    TByte audioData[512];
    (void)memset(audioData, 0x7f, sizeof audioData);
    Brn audioBuf(audioData, sizeof audioData);
    MsgAudioDsd* audio = iMsgFactory->CreateMsgAudioDsd(audioBuf, 2, 2822400, 2, iTrackOffset, 0);
    iTrackOffset += audio->Jiffies();

    return ApplyRamp(audio);
}

Msg* SuiteVolumeRamper::ApplyRamp(MsgAudioDecoded* aAudio)
{
    if (iRampDirection != Ramp::ENone) {
        if (iRampRemaining < aAudio->Jiffies()) {
            aAudio->Split(iRampRemaining)->RemoveRef();
        }
        MsgAudio* split = nullptr;
        iRampPos = aAudio->SetRamp(iRampPos, iRampRemaining, iRampDirection, split);
        ASSERT(split == nullptr);
        if (iRampRemaining == 0) {
            iRampDirection = Ramp::ENone;
        }
    }

    return aAudio;
}

void SuiteVolumeRamper::DrainCallback()
{
    iDrainAcknowledged = true;
}

void SuiteVolumeRamper::HaltCallback()
{
    iHaltAcknowledged = true;
}

void SuiteVolumeRamper::TestMsgsPass()
{
    const EMsgType msgs[] = { EMsgMode
                            , EMsgDrain
                            , EMsgStreamInterrupted
                            , EMsgDecodedStream
                            , EMsgAudioPcm
                            , EMsgAudioDsd
                            , EMsgSilence
                            , EMsgHalt
                            , EMsgQuit
        };
    const TUint count = sizeof(msgs) / sizeof(msgs[0]);
    for (TUint i=0; i<count; i++) {
        PullNext(msgs[i]);
    }
}

void SuiteVolumeRamper::TestMutesWhenHaltAcknowledged()
{
    TEST(!iHaltAcknowledged);
    TEST(iLastRampMultiplier == kVolumeMultiplierUninitialised);
    iDeferHaltAcknowledgement = true;
    PullNext(EMsgHalt);
    TEST(!iHaltAcknowledged);
    TEST(iLastRampMultiplier == kVolumeMultiplierUninitialised);
    iLastHaltMsg->ReportHalted();
    iLastHaltMsg->RemoveRef();
    TEST(iHaltAcknowledged);
    TEST(iLastRampMultiplier == IVolumeRamper::kMultiplierZero);
}

void SuiteVolumeRamper::TestMutesWhenDrainAcknowledged()
{
    TEST(!iDrainAcknowledged);
    TEST(iLastRampMultiplier == kVolumeMultiplierUninitialised);
    iDeferDrainAcknowledgement = true;
    PullNext(EMsgDrain);
    TEST(!iDrainAcknowledged);
    TEST(iLastRampMultiplier == kVolumeMultiplierUninitialised);
    iLastDrainMsg->ReportDrained();
    iLastDrainMsg->RemoveRef();
    TEST(iDrainAcknowledged);
    TEST(iLastRampMultiplier == IVolumeRamper::kMultiplierZero);
}

void SuiteVolumeRamper::TestNoMuteWhenAudioBeforeHaltAcknowledged()
{
/*
    iDeferHaltAcknowledgement = true;
    PullNext(EMsgHalt);
    TEST(!iHaltAcknowledged);
    TEST(iLastRampMultiplier == kVolumeMultiplierUninitialised);
    PullNext(EMsgAudioPcm);
    iLastHaltMsg->ReportHalted();
    iLastHaltMsg->RemoveRef();
    TEST(iHaltAcknowledged);
    TEST(iLastRampMultiplier == kVolumeMultiplierUninitialised);
*/
}

void SuiteVolumeRamper::TestUnmutesOnNonBypassAudio()
{
    PullNext(EMsgDecodedStream);
    PullNext(EMsgHalt);
    TEST(iHaltAcknowledged);
    TEST(iLastRampMultiplier == IVolumeRamper::kMultiplierZero);
    PullNext(EMsgAudioPcm);
    TEST(iLastRampMultiplier == IVolumeRamper::kMultiplierFull);
}

void SuiteVolumeRamper::TestBypassRampsVolumeDownOnAudioRampDown()
{
    iRampDirection = Ramp::EDown;
    iRampPos = Ramp::kMax;
    iRampRemaining = kRampDuration;
    iAnalogBypassEnable = true;
    PullNext(EMsgDecodedStream);
    TUint prevRampMultiplier = iLastRampMultiplier;
    do {
        PullNext(EMsgAudioPcm);
        TEST(prevRampMultiplier > iLastRampMultiplier);
        prevRampMultiplier = iLastRampMultiplier;

    } while (iRampRemaining > 0);
    PullNext(EMsgHalt);
    TEST(iLastRampMultiplier == IVolumeRamper::kMultiplierZero);
}

void SuiteVolumeRamper::TestBypassRampsVolumeUpOnAudioRampUp()
{
    iRampDirection = Ramp::EUp;
    iRampPos = Ramp::kMin;
    iRampRemaining = kRampDuration;
    iAnalogBypassEnable = true;
    PullNext(EMsgDecodedStream);
    PullNext(EMsgHalt);
    TUint prevRampMultiplier = iLastRampMultiplier;
    do {
        PullNext(EMsgAudioPcm);
        TEST(prevRampMultiplier < iLastRampMultiplier);
        prevRampMultiplier = iLastRampMultiplier;

    } while (iRampRemaining > 0);
    PullNext(EMsgAudioPcm);
    TEST(IVolumeRamper::kMultiplierFull - iLastRampMultiplier < IVolumeRamper::kMultiplierFull/8);
}

void SuiteVolumeRamper::TestDsdRampsVolumeDownOnAudioRampDown()
{
    iRampDirection = Ramp::EDown;
    iRampPos = Ramp::kMax;
    iRampRemaining = kRampDurationDsd;
    iAnalogBypassEnable = false;
    iFormat = AudioFormat::Dsd;
    PullNext(EMsgDecodedStream);
    TUint prevRampMultiplier = iLastRampMultiplier;
    do {
        PullNext(EMsgAudioDsd);
        TEST(prevRampMultiplier > iLastRampMultiplier);
        prevRampMultiplier = iLastRampMultiplier;

    } while (iRampRemaining > 0);
    PullNext(EMsgHalt);
    TEST(iLastRampMultiplier == IVolumeRamper::kMultiplierZero);
}


void TestVolumeRamper()
{
    Runner runner("Analog bypass ramper tests\n");
    runner.Add(new SuiteVolumeRamper());
    runner.Run();
}

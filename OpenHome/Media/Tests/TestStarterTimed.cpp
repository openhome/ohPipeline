#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/SuiteUnitTest.h>
#include <OpenHome/Media/Pipeline/StarterTimed.h>
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

class SuiteStarterTimed :
      public SuiteUnitTest
    , private IPipelineElementUpstream
    , private IMsgProcessor
    , private IAudioTime
    , private IPipelineAnimator
{
    static const TUint kRampDuration = Jiffies::kPerMs * 50; // shorter than production code but this is assumed to not matter
    static const TUint kExpectedFlushId = 5;
    static const TUint kSampleRate = 48000;
    static const TUint kNumChannels = 2;
    static const SpeakerProfile kProfile;
public:
    SuiteStarterTimed();
    ~SuiteStarterTimed();
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
private: // from IAudioTime
    void GetTickCount(TUint aSampleRate, TUint64& aTicks, TUint& aFrequency) const override;
    void SetTickCount(TUint64 aTicks) override;
    void TimerStartTimer(TUint /*aSampleRate*/, TUint64 /*aStartTime*/) override {}
    void TimerLogTime(const TChar* /*aId*/) override {}
private: // from IPipelineAnimator
    TUint PipelineAnimatorBufferJiffies() const override { return 0; }
    TUint PipelineAnimatorDelayJiffies(AudioFormat /*aFormat*/, TUint /*aSampleRate*/, TUint /*aBitDepth*/, TUint /*aNumChannels*/) const override { return 0; }
    TUint PipelineAnimatorDsdBlockSizeWords() const override { return 0; }
    TUint PipelineAnimatorMaxBitDepth() const override { return 0; }
    void PipelineAnimatorGetMaxSampleRates(TUint& /*aPcm*/, TUint& /*aDsd*/) const override {}
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
    void PullNext(EMsgType aExpectedMsg);
    Msg* CreateTrack();
    Msg* CreateDecodedStream();
    Msg* CreateAudio();
    Msg* CreateAudioDsd();
private:
    void TestMsgsPass();
    void TestStartStreamDisabled();
    void TestStartStreamStartPosInPast();
    void TestStartStreamStartPosInFuture();
private:
    AllocatorInfoLogger iInfoAggregator;
    TrackFactory* iTrackFactory;
    MsgFactory* iMsgFactory;
    StarterTimed* iStarterTimed;
    EMsgType iLastPulledMsg;
    TUint iStreamId;
    TUint64 iTrackOffset;
    TUint64 iJiffiesSilence;
    std::list<Msg*> iPendingMsgs;
    TUint iNextStreamId;
    TUint64 iNextReportedTime;
    TUint iClockFreq;
};

} // namespace Media
} // namespace OpenHome


const SpeakerProfile SuiteStarterTimed::kProfile(2);

SuiteStarterTimed::SuiteStarterTimed()
    : SuiteUnitTest("StartAt")
{
    AddTest(MakeFunctor(*this, &SuiteStarterTimed::TestMsgsPass), "TestMsgsPass");
    AddTest(MakeFunctor(*this, &SuiteStarterTimed::TestStartStreamDisabled), "TestStartStreamDisabled");
    AddTest(MakeFunctor(*this, &SuiteStarterTimed::TestStartStreamStartPosInPast), "TestStartStreamStartPosInPast");
    AddTest(MakeFunctor(*this, &SuiteStarterTimed::TestStartStreamStartPosInFuture), "TestStartStreamStartPosInFuture");
}

SuiteStarterTimed::~SuiteStarterTimed()
{
}

void SuiteStarterTimed::Setup()
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
    init.SetMsgModeCount(2);
    init.SetMsgDrainCount(2);
    init.SetMsgWaitCount(2);
    init.SetMsgDelayCount(2);
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
    iStarterTimed = new StarterTimed(*iMsgFactory, *this, *this);
    iStarterTimed->SetAnimator(*this);
    iStreamId = UINT_MAX;
    iTrackOffset = 0;
    iJiffiesSilence = 0;
    iNextStreamId = 1;
    iNextReportedTime = 0;
    iClockFreq = 0;
}

void SuiteStarterTimed::TearDown()
{
    while (iPendingMsgs.size() > 0) {
        iPendingMsgs.front()->RemoveRef();
        iPendingMsgs.pop_front();
    }
    delete iStarterTimed;
    delete iMsgFactory;
    delete iTrackFactory;
}

Msg* SuiteStarterTimed::Pull()
{
    ASSERT(iPendingMsgs.size() > 0);
    Msg* msg = iPendingMsgs.front();
    iPendingMsgs.pop_front();
    return msg;
}

Msg* SuiteStarterTimed::ProcessMsg(MsgMode* aMsg)
{
    iLastPulledMsg = EMsgMode;
    return aMsg;
}

Msg* SuiteStarterTimed::ProcessMsg(MsgTrack* aMsg)
{
    iLastPulledMsg = EMsgTrack;
    return aMsg;
}

Msg* SuiteStarterTimed::ProcessMsg(MsgDrain* aMsg)
{
    iLastPulledMsg = EMsgDrain;
    aMsg->ReportDrained();
    return aMsg;
}

Msg* SuiteStarterTimed::ProcessMsg(MsgDelay* aMsg)
{
    iLastPulledMsg = EMsgDelay;
    return aMsg;
}

Msg* SuiteStarterTimed::ProcessMsg(MsgEncodedStream* aMsg)
{
    iLastPulledMsg = EMsgEncodedStream;
    iStreamId = aMsg->StreamId();
    return aMsg;
}

Msg* SuiteStarterTimed::ProcessMsg(MsgStreamSegment* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuiteStarterTimed::ProcessMsg(MsgAudioEncoded* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuiteStarterTimed::ProcessMsg(MsgMetaText* aMsg)
{
    iLastPulledMsg = EMsgMetaText;
    return aMsg;
}

Msg* SuiteStarterTimed::ProcessMsg(MsgStreamInterrupted* aMsg)
{
    iLastPulledMsg = EMsgStreamInterrupted;
    return aMsg;
}

Msg* SuiteStarterTimed::ProcessMsg(MsgHalt* aMsg)
{
    iLastPulledMsg = EMsgHalt;
    aMsg->ReportHalted();
    return aMsg;
}

Msg* SuiteStarterTimed::ProcessMsg(MsgFlush* aMsg)
{
    iLastPulledMsg = EMsgFlush;
    return aMsg;
}

Msg* SuiteStarterTimed::ProcessMsg(MsgWait* aMsg)
{
    iLastPulledMsg = EMsgWait;
    return aMsg;
}

Msg* SuiteStarterTimed::ProcessMsg(MsgDecodedStream* aMsg)
{
    iLastPulledMsg = EMsgDecodedStream;
    return aMsg;
}

Msg* SuiteStarterTimed::ProcessMsg(MsgAudioPcm* aMsg)
{
    iLastPulledMsg = EMsgAudioPcm;
    return aMsg;
}

Msg* SuiteStarterTimed::ProcessMsg(MsgAudioDsd* aMsg)
{
    iLastPulledMsg = EMsgAudioDsd;
    return aMsg;
}

Msg* SuiteStarterTimed::ProcessMsg(MsgSilence* aMsg)
{
    iLastPulledMsg = EMsgSilence;
    iJiffiesSilence += aMsg->Jiffies();
    return aMsg;
}

Msg* SuiteStarterTimed::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuiteStarterTimed::ProcessMsg(MsgQuit* aMsg)
{
    iLastPulledMsg = EMsgQuit;
    return aMsg;
}

void SuiteStarterTimed::GetTickCount(TUint /*aSampleRate*/, TUint64& aTicks, TUint& aFrequency) const
{
    aTicks = iNextReportedTime;
    aFrequency = iClockFreq;
}

void SuiteStarterTimed::SetTickCount(TUint64 /*aTicks*/)
{
}

void SuiteStarterTimed::PullNext(EMsgType aExpectedMsg)
{
    Msg* msg = static_cast<IPipelineElementUpstream*>(iStarterTimed)->Pull();
    msg = msg->Process(*this);
    msg->RemoveRef();
    if (iLastPulledMsg != aExpectedMsg) {
        static const TChar* types[] ={
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
        Print("Expected %s, got %s\n", types[aExpectedMsg], types[iLastPulledMsg]);
    }
    TEST(iLastPulledMsg == aExpectedMsg);
}

Msg* SuiteStarterTimed::CreateTrack()
{
    Track* track = iTrackFactory->CreateTrack(Brx::Empty(), Brx::Empty());
    Msg* msg = iMsgFactory->CreateMsgTrack(*track);
    track->RemoveRef();
    return msg;
}

Msg* SuiteStarterTimed::CreateDecodedStream()
{
    return iMsgFactory->CreateMsgDecodedStream(iNextStreamId, 100, 24, kSampleRate, kNumChannels, Brn("notARealCodec"), 1LL<<38, 0, true, true, false, false, AudioFormat::Pcm, Multiroom::Allowed, kProfile, nullptr, RampType::Sample);
}

Msg* SuiteStarterTimed::CreateAudio()
{
    static const TUint kDataBytes = 3 * 1024;
    TByte encodedAudioData[kDataBytes];
    (void)memset(encodedAudioData, 0x7f, kDataBytes);
    Brn encodedAudioBuf(encodedAudioData, kDataBytes);
    MsgAudioPcm* audio = iMsgFactory->CreateMsgAudioPcm(encodedAudioBuf, kNumChannels, kSampleRate, 24, AudioDataEndian::Little, iTrackOffset);
    iTrackOffset += audio->Jiffies();
    return audio;
}

Msg* SuiteStarterTimed::CreateAudioDsd()
{
    TByte audioData[128];
    (void)memset(audioData, 0x7f, sizeof audioData);
    Brn audioBuf(audioData, sizeof audioData);
    MsgAudioDsd* audio = iMsgFactory->CreateMsgAudioDsd(audioBuf, 2, 2822400, 2, iTrackOffset, 0);
    iTrackOffset += audio->Jiffies();
    return audio;
}

void SuiteStarterTimed::TestMsgsPass()
{
    iPendingMsgs.push_back(iMsgFactory->CreateMsgMode(Brx::Empty()));
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgDrain(Functor()));
    iPendingMsgs.push_back(CreateDecodedStream());
    iPendingMsgs.push_back(CreateAudio());
    iPendingMsgs.push_back(CreateAudioDsd());
    TUint size = Jiffies::kPerMs * 3;
    iPendingMsgs.push_back(iMsgFactory->CreateMsgSilence(size, kSampleRate, 16, kNumChannels));
    iPendingMsgs.push_back(iMsgFactory->CreateMsgHalt());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgStreamInterrupted());
    iPendingMsgs.push_back(iMsgFactory->CreateMsgQuit());

    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    PullNext(EMsgDrain);
    PullNext(EMsgDecodedStream);
    PullNext(EMsgAudioPcm);
    PullNext(EMsgAudioDsd);
    PullNext(EMsgSilence);
    PullNext(EMsgHalt);
    PullNext(EMsgStreamInterrupted);
    PullNext(EMsgQuit);
}

void SuiteStarterTimed::TestStartStreamDisabled()
{
    iPendingMsgs.push_back(iMsgFactory->CreateMsgMode(Brx::Empty()));
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateDecodedStream());
    TUint size = Jiffies::kPerMs;
    iPendingMsgs.push_back(iMsgFactory->CreateMsgSilence(size, kSampleRate, 16, kNumChannels));
    iPendingMsgs.push_back(CreateAudio());

    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    PullNext(EMsgSilence);
    PullNext(EMsgAudioPcm);
}

void SuiteStarterTimed::TestStartStreamStartPosInPast()
{
    iPendingMsgs.push_back(iMsgFactory->CreateMsgMode(Brx::Empty()));
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateDecodedStream());
    TUint size = Jiffies::kPerMs;
    iPendingMsgs.push_back(iMsgFactory->CreateMsgSilence(size, kSampleRate, 16, kNumChannels));
    iPendingMsgs.push_back(CreateAudio());
    iNextReportedTime = 2000;
    iClockFreq = 1000000;
    iStarterTimed->StartAt(1000);

    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    PullNext(EMsgSilence);
    PullNext(EMsgAudioPcm);
}

void SuiteStarterTimed::TestStartStreamStartPosInFuture()
{
    iPendingMsgs.push_back(CreateTrack());
    iPendingMsgs.push_back(CreateDecodedStream());
    TUint size = Jiffies::kPerMs;
    iPendingMsgs.push_back(iMsgFactory->CreateMsgSilence(size, kSampleRate, 16, kNumChannels));
    iPendingMsgs.push_back(CreateAudio());
    iNextReportedTime = Jiffies::kPerMs * 2;
    iClockFreq = Jiffies::kPerSecond;
    const TUint kStartTime = Jiffies::kPerMs * 12;
    iStarterTimed->StartAt(kStartTime);

    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    const TUint64 expectedSilence = kStartTime - iNextReportedTime + Jiffies::kPerMs; // kPerMs for size of MsgSilence queued above
    while (iJiffiesSilence < expectedSilence) {
        PullNext(EMsgSilence);
    }
    TEST(iJiffiesSilence == expectedSilence);
    PullNext(EMsgAudioPcm);
    TEST(iJiffiesSilence == expectedSilence);
}



void TestStarterTimed()
{
    Runner runner("StartAt tests\n");
    runner.Add(new SuiteStarterTimed());
    runner.Run();
}

#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/SuiteUnitTest.h>
#include <OpenHome/Av/Songcast/SenderThread.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Utils/AllocatorInfoLogger.h>

#include <list>
#include <limits.h>

using namespace OpenHome;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Media;
using namespace OpenHome::Av;

namespace OpenHome {
namespace Av {

class SuiteSenderQueue : public SuiteUnitTest, private IMsgProcessor
{
    static const TUint kRampDuration = Jiffies::kPerMs * 20;
    static const TUint kExpectedFlushId = 5;
    static const TUint kExpectedSeekSeconds = 51;
    static const TUint kSampleRate = 44100;
    static const TUint kNumChannels = 2;
    static const SpeakerProfile kProfile;
    static const TUint kTrackDurationSeconds = 180;
public:
    SuiteSenderQueue();
private: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
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
       ,EMsgBitRate
       ,EMsgAudioPcm
       ,EMsgAudioDsd
       ,EMsgHalt
       ,EMsgFlush
       ,EMsgWait
       ,EMsgQuit
    };
private:
    void PullNext();
    void PullNext(EMsgType aExpectedMsg);
    Msg* CreateTrack(const Brx& aUri);
    Msg* CreateEncodedStream();
    Msg* CreateDecodedStream();
    Msg* CreateAudio();
private:
    void TestSingleAudioReplacedByStreamInterrupted();
    void TestMultipleAudioReplacedByStreamInterrupted();
    void TestMultipleAudioBlocks();
    void TestPrunesBeforeMode();
    void TestPrunesEarlierModeContent();
    void TestPrunesBeforeTrack();
    void TestPrunesEarlierTrack();
    void TestPrunesBeforeStream();
    void TestPrunesEarlierStream();
    void TestPrunesDuplicateDelayMetatextHalt();
    void TestPrunesAllAbove();
    void TestQueueElementsCanBeReused();
    void TestQueuePrunesWhenFull();
private:
    AllocatorInfoLogger iInfoAggregator;
    TrackFactory* iTrackFactory;
    MsgFactory* iMsgFactory;
    SenderMsgQueue* iQueue;
    EMsgType iLastPulledMsg;
    TBool iGenerateAudio;
    TUint iStreamId;
    TUint64 iStreamSampleStart;
    TUint64 iTrackLengthJiffies;
    TUint iLastMsgAudioSize;
    TUint64 iTrackOffset;
    TUint64 iJiffies;
    TUint64 iTrackOffsetPulled;
    TUint iLastSubsample;
    TUint iNextStreamId;
    BwsMode iLastMode;
    BwsTrackUri iLastTrackUri;
    TUint iLastDelayJiffies;
    Bws<MsgMetaText::kMaxBytes> iLastMetatext;
    TUint iLastHaltId;
    TUint iLastStreamInterruptedJiffies;
};

} // namespace Av
} // namespace OpenHome

const SpeakerProfile SuiteSenderQueue::kProfile(2);

SuiteSenderQueue::SuiteSenderQueue()
    : SuiteUnitTest("SenderQueue")
{
    AddTest(MakeFunctor(*this, &SuiteSenderQueue::TestSingleAudioReplacedByStreamInterrupted), "TestSingleAudioReplacedByStreamInterrupted");
    AddTest(MakeFunctor(*this, &SuiteSenderQueue::TestMultipleAudioReplacedByStreamInterrupted), "TestMultipleAudioReplacedByStreamInterrupted");
    AddTest(MakeFunctor(*this, &SuiteSenderQueue::TestMultipleAudioBlocks), "TestMultipleAudioBlocks");
    AddTest(MakeFunctor(*this, &SuiteSenderQueue::TestPrunesBeforeMode), "TestPrunesBeforeMode");
    AddTest(MakeFunctor(*this, &SuiteSenderQueue::TestPrunesEarlierModeContent), "TestPrunesEarlierModeContent");
    AddTest(MakeFunctor(*this, &SuiteSenderQueue::TestPrunesBeforeTrack), "TestPrunesBeforeTrack");
    AddTest(MakeFunctor(*this, &SuiteSenderQueue::TestPrunesEarlierTrack), "TestPrunesEarlierTrack");
    AddTest(MakeFunctor(*this, &SuiteSenderQueue::TestPrunesBeforeStream), "TestPrunesBeforeStream");
    AddTest(MakeFunctor(*this, &SuiteSenderQueue::TestPrunesEarlierStream), "TestPrunesEarlierStream");
    AddTest(MakeFunctor(*this, &SuiteSenderQueue::TestPrunesDuplicateDelayMetatextHalt), "TestPrunesDuplicateDelayMetatextHalt");
    AddTest(MakeFunctor(*this, &SuiteSenderQueue::TestPrunesAllAbove), "TestPrunesAllAbove");
    AddTest(MakeFunctor(*this, &SuiteSenderQueue::TestQueueElementsCanBeReused), "TestQueueElementsCanBeReused");
    AddTest(MakeFunctor(*this, &SuiteSenderQueue::TestQueuePrunesWhenFull), "TestQueuePrunesWhenFull");
}

void SuiteSenderQueue::Setup()
{
    iLastMode.Replace(Brx::Empty());
    iLastTrackUri.Replace(Brx::Empty());
    iNextStreamId = 0;
    iStreamId = 999; // any non-zero value would do
    iTrackOffset = 0LL;
    iLastDelayJiffies = 0;
    iLastMetatext.Replace(Brx::Empty());
    iLastHaltId = 0;
    iLastStreamInterruptedJiffies = 0;

    iTrackFactory = new TrackFactory(iInfoAggregator, 5);
    MsgFactoryInitParams init;
    init.SetMsgAudioPcmCount(21, 21); // TestQueuePrunesWhenFull requires this is higher than queue capacity
    init.SetMsgSilenceCount(10);
    init.SetMsgStreamInterruptedCount(5);
    init.SetMsgModeCount(3);
    init.SetMsgDecodedStreamCount(3);
    init.SetMsgTrackCount(3);
    init.SetMsgDelayCount(5);
    init.SetMsgMetaTextCount(5);
    init.SetMsgHaltCount(5);
    iMsgFactory = new MsgFactory(iInfoAggregator, init);

    iQueue = new SenderMsgQueue(*iMsgFactory, 20);
}

void SuiteSenderQueue::TearDown()
{
    delete iQueue;
    delete iMsgFactory;
    delete iTrackFactory;
}

Msg* SuiteSenderQueue::ProcessMsg(MsgMode* aMsg)
{
    iLastPulledMsg = EMsgMode;
    iLastMode.Replace(aMsg->Mode());
    return aMsg;
}

Msg* SuiteSenderQueue::ProcessMsg(MsgTrack* aMsg)
{
    iLastPulledMsg = EMsgTrack;
    iLastTrackUri.Replace(aMsg->Track().Uri());
    return aMsg;
}

Msg* SuiteSenderQueue::ProcessMsg(MsgDrain* aMsg)
{
    iLastPulledMsg = EMsgDrain;
    return aMsg;
}

Msg* SuiteSenderQueue::ProcessMsg(MsgDelay* aMsg)
{
    iLastPulledMsg = EMsgDelay;
    iLastDelayJiffies = aMsg->RemainingJiffies();
    return aMsg;
}

Msg* SuiteSenderQueue::ProcessMsg(MsgEncodedStream* aMsg)
{
    iLastPulledMsg = EMsgEncodedStream;
    return aMsg;
}

Msg* SuiteSenderQueue::ProcessMsg(MsgStreamSegment* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuiteSenderQueue::ProcessMsg(MsgAudioEncoded* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuiteSenderQueue::ProcessMsg(MsgMetaText* aMsg)
{
    iLastPulledMsg = EMsgMetaText;
    iLastMetatext.Replace(aMsg->MetaText());
    return aMsg;
}

Msg* SuiteSenderQueue::ProcessMsg(MsgStreamInterrupted* aMsg)
{
    iLastPulledMsg = EMsgStreamInterrupted;
    iLastStreamInterruptedJiffies = aMsg->Jiffies();
    return aMsg;
}

Msg* SuiteSenderQueue::ProcessMsg(MsgHalt* aMsg)
{
    iLastPulledMsg = EMsgHalt;
    iLastHaltId = aMsg->Id();
    return aMsg;
}

Msg* SuiteSenderQueue::ProcessMsg(MsgFlush* aMsg)
{
    iLastPulledMsg = EMsgFlush;
    return aMsg;
}

Msg* SuiteSenderQueue::ProcessMsg(MsgWait* aMsg)
{
    iLastPulledMsg = EMsgWait;
    return aMsg;
}

Msg* SuiteSenderQueue::ProcessMsg(MsgDecodedStream* aMsg)
{
    iLastPulledMsg = EMsgDecodedStream;
    auto stream = aMsg->StreamInfo();
    iStreamId = stream.StreamId();
    iStreamSampleStart = stream.SampleStart();
    iTrackOffsetPulled = iStreamSampleStart * Jiffies::PerSample(stream.SampleRate());
    return aMsg;
}

Msg* SuiteSenderQueue::ProcessMsg(MsgBitRate* aMsg)
{
    iLastPulledMsg = EMsgBitRate;
    return aMsg;
}

Msg* SuiteSenderQueue::ProcessMsg(MsgAudioPcm* aMsg)
{
    iLastPulledMsg = EMsgAudioPcm;
    iLastMsgAudioSize = aMsg->Jiffies();
    iJiffies += aMsg->Jiffies();
    return aMsg;
}

Msg* SuiteSenderQueue::ProcessMsg(MsgAudioDsd* aMsg)
{
    iLastPulledMsg = EMsgAudioPcm;
    return aMsg;
}

Msg* SuiteSenderQueue::ProcessMsg(MsgSilence* aMsg)
{
    return aMsg;
}

Msg* SuiteSenderQueue::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuiteSenderQueue::ProcessMsg(MsgQuit* aMsg)
{
    iLastPulledMsg = EMsgQuit;
    return aMsg;
}

void SuiteSenderQueue::PullNext()
{
    //auto msg = iQueue->Dequeue();
    //msg = msg->Process(*this);
    //msg->RemoveRef();
}

void SuiteSenderQueue::PullNext(EMsgType aExpectedMsg)
{
    auto msg = iQueue->Dequeue();
    msg = msg->Process(*this);
    msg->RemoveRef();
    TEST(iLastPulledMsg == aExpectedMsg);
}

Msg* SuiteSenderQueue::CreateTrack(const Brx& aUri)
{
    Track* track = iTrackFactory->CreateTrack(aUri, Brx::Empty());
    Msg* msg = iMsgFactory->CreateMsgTrack(*track);
    track->RemoveRef();
    return msg;
}

Msg* SuiteSenderQueue::CreateDecodedStream()
{
    const TUint64 sampleStart = iTrackOffset / Jiffies::PerSample(kSampleRate);
    return iMsgFactory->CreateMsgDecodedStream(++iNextStreamId, 100, 24, kSampleRate, kNumChannels, Brn("notARealCodec"), 12345678LL, sampleStart, true, false, false, false, AudioFormat::Pcm, Multiroom::Allowed, kProfile, nullptr, RampType::Sample);
}

Msg* SuiteSenderQueue::CreateAudio()
{
    static const TUint kDataBytes = 960;
    TByte encodedAudioData[kDataBytes];
    (void)memset(encodedAudioData, 0x7f, kDataBytes);
    Brn encodedAudioBuf(encodedAudioData, kDataBytes);
    auto audio = iMsgFactory->CreateMsgAudioPcm(encodedAudioBuf, kNumChannels, kSampleRate, 24, AudioDataEndian::Little, iTrackOffset);
    iTrackOffset += audio->Jiffies();
    return audio;
}

void SuiteSenderQueue::TestSingleAudioReplacedByStreamInterrupted()
{
    iQueue->Enqueue(CreateAudio());
    iQueue->Prune();
    PullNext(EMsgStreamInterrupted);
    TEST(iLastStreamInterruptedJiffies == iTrackOffset);
    TEST(iQueue->Count() == 0);
}

void SuiteSenderQueue::TestMultipleAudioReplacedByStreamInterrupted()
{
    iQueue->Enqueue(CreateAudio());
    iQueue->Enqueue(CreateAudio());
    iQueue->Enqueue(CreateAudio());
    iQueue->Prune();
    PullNext(EMsgStreamInterrupted);
    TEST(iLastStreamInterruptedJiffies == iTrackOffset);
    TEST(iQueue->Count() == 0);
}

void SuiteSenderQueue::TestMultipleAudioBlocks()
{
    iQueue->Enqueue(CreateAudio());
    const auto block1 = iTrackOffset;
    iQueue->Enqueue(iMsgFactory->CreateMsgDelay(3));
    iQueue->Enqueue(CreateAudio());
    iQueue->Enqueue(CreateAudio());
    iQueue->Enqueue(CreateAudio());
    const auto block2 = iTrackOffset - block1;
    iQueue->Enqueue(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    iQueue->Enqueue(CreateAudio());
    iQueue->Enqueue(CreateAudio());
    const auto block3 = iTrackOffset - (block2 + block1);
    iQueue->Prune();
    PullNext(EMsgStreamInterrupted);
    TEST(iLastStreamInterruptedJiffies == block1);
    PullNext(EMsgDelay);
    PullNext(EMsgStreamInterrupted);
    TEST(iLastStreamInterruptedJiffies == block2);
    PullNext(EMsgMetaText);
    PullNext(EMsgStreamInterrupted);
    TEST(iLastStreamInterruptedJiffies == block3);
    TEST(iQueue->Count() == 0);
}

void SuiteSenderQueue::TestPrunesBeforeMode()
{
    iQueue->Enqueue(iMsgFactory->CreateMsgDelay(3));
    iQueue->Enqueue(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    iQueue->Enqueue(CreateAudio());
    iQueue->Enqueue(iMsgFactory->CreateMsgHalt());
    const Brn kMode("mode1");
    iQueue->Enqueue(iMsgFactory->CreateMsgMode(kMode));
    iQueue->Prune();
    PullNext(EMsgStreamInterrupted);
    TEST(iLastStreamInterruptedJiffies == iTrackOffset);
    PullNext(EMsgMode);
    TEST(iQueue->Count() == 0);
}

void SuiteSenderQueue::TestPrunesEarlierModeContent()
{
    iQueue->Enqueue(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    iQueue->Enqueue(CreateAudio());
    const auto block1 = iTrackOffset;
    iQueue->Enqueue(iMsgFactory->CreateMsgHalt());
    const Brn kMode1("mode1");
    iQueue->Enqueue(iMsgFactory->CreateMsgMode(kMode1));

    iQueue->Enqueue(CreateTrack(Brx::Empty()));
    iQueue->Enqueue(CreateDecodedStream());
    iQueue->Enqueue(CreateAudio());
    iQueue->Enqueue(CreateAudio());
    iQueue->Enqueue(CreateAudio());
    const auto block2 = iTrackOffset - block1;
    const Brn kMode2("mode2");
    iQueue->Enqueue(iMsgFactory->CreateMsgMode(kMode2));
    iQueue->Prune();

    PullNext(EMsgStreamInterrupted);
    TEST(iLastStreamInterruptedJiffies == block1);
    PullNext(EMsgMode);
    TEST(iLastMode == kMode1);
    PullNext(EMsgStreamInterrupted);
    TEST(iLastStreamInterruptedJiffies == block2);
    PullNext(EMsgMode);
    TEST(iLastMode == kMode2);
    TEST(iQueue->Count() == 0);
}

void SuiteSenderQueue::TestPrunesBeforeTrack()
{
    iQueue->Enqueue(iMsgFactory->CreateMsgDelay(3));
    iQueue->Enqueue(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    iQueue->Enqueue(CreateAudio());
    iQueue->Enqueue(iMsgFactory->CreateMsgHalt());
    iQueue->Enqueue(CreateTrack(Brx::Empty()));
    iQueue->Prune();
    PullNext(EMsgDelay); // delays apply across tracks so should not be pruned
    PullNext(EMsgStreamInterrupted);
    TEST(iLastStreamInterruptedJiffies == iTrackOffset);
    PullNext(EMsgTrack);
    TEST(iQueue->Count() == 0);
}

void SuiteSenderQueue::TestPrunesEarlierTrack()
{
    iQueue->Enqueue(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    iQueue->Enqueue(CreateAudio());
    const auto block1 = iTrackOffset;
    iQueue->Enqueue(iMsgFactory->CreateMsgHalt());
    const Brn kUri1("uri1");
    iQueue->Enqueue(CreateTrack(kUri1));

    iQueue->Enqueue(CreateDecodedStream());
    iQueue->Enqueue(CreateAudio());
    iQueue->Enqueue(CreateAudio());
    iQueue->Enqueue(CreateAudio());
    const auto block2 = iTrackOffset - block1;
    const Brn kUri2("uri2");
    iQueue->Enqueue(CreateTrack(kUri2));
    iQueue->Prune();

    PullNext(EMsgStreamInterrupted);
    TEST(iLastStreamInterruptedJiffies == block1);
    PullNext(EMsgStreamInterrupted);
    TEST(iLastStreamInterruptedJiffies == block2);
    PullNext(EMsgTrack);
    TEST(iLastTrackUri == kUri2);
    TEST(iQueue->Count() == 0);
}

void SuiteSenderQueue::TestPrunesBeforeStream()
{
    iQueue->Enqueue(iMsgFactory->CreateMsgDelay(3));
    iQueue->Enqueue(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    iQueue->Enqueue(CreateAudio());
    iQueue->Enqueue(iMsgFactory->CreateMsgHalt());
    iQueue->Enqueue(CreateDecodedStream());
    iQueue->Prune();
    PullNext(EMsgDelay); // delays apply across streams so should not be pruned
    PullNext(EMsgStreamInterrupted);
    TEST(iLastStreamInterruptedJiffies == iTrackOffset);
    PullNext(EMsgDecodedStream);
    TEST(iQueue->Count() == 0);
}

void SuiteSenderQueue::TestPrunesEarlierStream()
{
    iQueue->Enqueue(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    iQueue->Enqueue(CreateAudio());
    const auto block1 = iTrackOffset;
    iQueue->Enqueue(iMsgFactory->CreateMsgHalt());
    iQueue->Enqueue(CreateDecodedStream());

    iQueue->Enqueue(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    iQueue->Enqueue(CreateAudio());
    iQueue->Enqueue(CreateAudio());
    iQueue->Enqueue(CreateAudio());
    const auto block2 = iTrackOffset - block1;
    iQueue->Enqueue(CreateDecodedStream());
    iQueue->Prune();

    PullNext(EMsgStreamInterrupted);
    TEST(iLastStreamInterruptedJiffies == block1);
    PullNext(EMsgStreamInterrupted);
    TEST(iLastStreamInterruptedJiffies == block2);
    PullNext(EMsgDecodedStream);
    TEST(iNextStreamId == iStreamId);
    TEST(iQueue->Count() == 0);
}

void SuiteSenderQueue::TestPrunesDuplicateDelayMetatextHalt()
{
    iQueue->Enqueue(iMsgFactory->CreateMsgHalt());
    iQueue->Enqueue(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    iQueue->Enqueue(iMsgFactory->CreateMsgDelay(3));
    iQueue->Enqueue(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    iQueue->Enqueue(iMsgFactory->CreateMsgHalt());
    iQueue->Enqueue(iMsgFactory->CreateMsgDelay(60));
    const TUint kDelay = 12345;
    const Brn kMetatext("meta");
    const TUint kHaltId = 42;
    iQueue->Enqueue(iMsgFactory->CreateMsgDelay(kDelay));
    iQueue->Enqueue(iMsgFactory->CreateMsgMetaText(kMetatext));
    iQueue->Enqueue(iMsgFactory->CreateMsgHalt(kHaltId));

    iQueue->Prune();
    PullNext(EMsgDelay);
    TEST(iLastDelayJiffies == kDelay);
    PullNext(EMsgMetaText);
    TEST(iLastMetatext == kMetatext);
    PullNext(EMsgHalt);
    TEST(iLastHaltId == kHaltId);
    TEST(iQueue->Count() == 0);
}

void SuiteSenderQueue::TestPrunesAllAbove()
{
    /*
        [Delay, Metatext, Audio, Audio, Audio, Halt,
         Mode, Delay, Track, DecodedStream, Audio, Delay,
                      Track, DecodedStream, Audio, Metatext, Audio, Audio]
        ->
        [StreamInterrupted, Mode, StreamInterrupted, Delay, Track,
         DecodedStream, StreamInterrupted, Metatext, StreamInterrupted]
    */
    iQueue->Enqueue(iMsgFactory->CreateMsgDelay(3));
    iQueue->Enqueue(iMsgFactory->CreateMsgMetaText(Brx::Empty()));
    iQueue->Enqueue(CreateAudio());
    iQueue->Enqueue(CreateAudio());
    iQueue->Enqueue(CreateAudio());
    const auto block1 = iTrackOffset;
    iQueue->Enqueue(iMsgFactory->CreateMsgHalt());
    const Brn kMode("mode");
    iQueue->Enqueue(iMsgFactory->CreateMsgMode(kMode));
    iQueue->Enqueue(iMsgFactory->CreateMsgDelay(300));
    iQueue->Enqueue(CreateTrack(Brx::Empty()));
    iQueue->Enqueue(CreateDecodedStream());
    iQueue->Enqueue(CreateAudio());
    const auto block2 = iTrackOffset - block1;
    const TUint kDelay = 54321;
    iQueue->Enqueue(iMsgFactory->CreateMsgDelay(kDelay));
    const Brn kUri("uri");
    iQueue->Enqueue(CreateTrack(kUri));
    iQueue->Enqueue(CreateDecodedStream());
    iQueue->Enqueue(CreateAudio());
    const auto block3 = iTrackOffset - (block1 + block2);
    const Brn kMetatext("meta");
    iQueue->Enqueue(iMsgFactory->CreateMsgMetaText(kMetatext));
    iQueue->Enqueue(CreateAudio());
    iQueue->Enqueue(CreateAudio());
    const auto block4 = iTrackOffset - (block1 + block2 + block3);

    iQueue->Prune();
    PullNext(EMsgStreamInterrupted);
    TEST(iLastStreamInterruptedJiffies == block1);
    PullNext(EMsgMode);
    TEST(iLastMode == kMode);
    PullNext(EMsgStreamInterrupted);
    TEST(iLastStreamInterruptedJiffies == block2);
    PullNext(EMsgDelay);
    TEST(iLastDelayJiffies == kDelay);
    PullNext(EMsgTrack);
    TEST(iLastTrackUri == kUri);
    PullNext(EMsgDecodedStream);
    TEST(iStreamId == iNextStreamId);
    PullNext(EMsgStreamInterrupted);
    TEST(iLastStreamInterruptedJiffies == block3);
    PullNext(EMsgMetaText);
    TEST(iLastMetatext == kMetatext);
    PullNext(EMsgStreamInterrupted);
    TEST(iLastStreamInterruptedJiffies == block4);
    TEST(iQueue->Count() == 0);
}

void SuiteSenderQueue::TestQueueElementsCanBeReused()
{
    const TUint count = iQueue->iFree.Slots() * 3;
    for (TUint i = 0; i < count; i++) {
        iQueue->Enqueue(CreateAudio());
        PullNext(EMsgAudioPcm);
    }
    TEST(iQueue->Count() == 0);
}

void SuiteSenderQueue::TestQueuePrunesWhenFull()
{
    const TUint count = iQueue->iFree.Slots();
    for (TUint i = 0; i < count; i++) {
        iQueue->Enqueue(CreateAudio());
    }
    const auto block = iTrackOffset;
    iQueue->Enqueue(CreateAudio());
    PullNext(EMsgStreamInterrupted);
    TEST(iLastStreamInterruptedJiffies == block);
    PullNext(EMsgAudioPcm);
    TEST(iQueue->Count() == 0);
}



void TestSenderQueue()
{
    Runner runner("SenderMsgQueue tests\n");
    runner.Add(new SuiteSenderQueue());
    runner.Run();
}

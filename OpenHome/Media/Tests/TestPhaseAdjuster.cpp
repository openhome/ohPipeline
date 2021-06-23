#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/SuiteUnitTest.h>
#include <OpenHome/Media/Pipeline/PhaseAdjuster.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/ClockPuller.h>
#include <OpenHome/Media/Utils/AllocatorInfoLogger.h>
#include <OpenHome/Media/Utils/ProcessorAudioUtils.h>
#include <OpenHome/Media/Pipeline/RampValidator.h>
#include <OpenHome/Media/Pipeline/DecodedAudioValidator.h>
#include <OpenHome/Media/Pipeline/StarvationRamper.h>

using namespace OpenHome;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Media;

namespace OpenHome {
namespace Media {

class SuitePhaseAdjuster
    : public SuiteUnitTest
    , private IPipelineElementUpstream
    , private IMsgProcessor
    , private IClockPuller
    , private IPipelineAnimator
    , private IStarvationRamper
{
private:
    static const TUint kDecodedAudioCount   = 16;
    static const TUint kMsgAudioPcmCount    = 17;
    static const TUint kMsgSilenceCount     = 1;

    static const TUint kMsgSilenceSize      = Jiffies::kPerMs;

    static const TUint kSampleRate          = 44100;
    static const TUint kNumChannels         = 2;

    static const TUint kRampDurationMin     = Jiffies::kPerMs * 50;
    static const TUint kRampDurationMax     = Jiffies::kPerMs * 500;
    static const TUint kRampDefault         = kRampDurationMax;

    static const TUint kDelayJiffies        = 8110080;
    static const TUint kDefaultAudioJiffies = 983040;

    static const SpeakerProfile kProfile;
    static const Brn kMode;
    static const Brn kModeSongcast;
public:
    SuitePhaseAdjuster();
    ~SuitePhaseAdjuster();
private: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private: // from IPipelineElementUpstream
    Msg* Pull() override;
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgDrain* aMsg) override;
    Msg* ProcessMsg(MsgTrack* aMsg) override;
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
private: // from IClockPuller
    void Update(TInt aDelta) override;
    void Start() override;
    void Stop() override;
private: // from IPipelineAnimator
    TUint PipelineAnimatorBufferJiffies() const override;
    TUint PipelineAnimatorDelayJiffies(AudioFormat aFormat, TUint aSampleRate, TUint aBitDepth, TUint aNumChannels) const override;
    TUint PipelineAnimatorDsdBlockSizeWords() const override;
    TUint PipelineAnimatorMaxBitDepth() const override;
private: // from IStarvationRamper
    void WaitForOccupancy(TUint aJiffies) override;
private:
    enum EMsgType
    {
        ENone,
        EMsgMode,
        EMsgModeSongcast,
        EMsgTrack,
        EMsgDrain,
        EMsgDelay,
        EMsgEncodedStream,
        EMsgMetaText,
        EMsgStreamInterrupted,
        EMsgDecodedStream,
        EMsgBitRate,
        EMsgAudioPcm,
        EMsgAudioDsd,
        EMsgSilence,
        EMsgHalt,
        EMsgFlush,
        EMsgWait,
        EMsgQuit
    };
    enum class ERampStatus
    {
        ENoRamp,
        ERampingUp,
        ERampComplete
    };
private:
    void PullNext();
    void PullNext(EMsgType aExpectedMsg);
    MsgAudio* CreateAudio(TUint aJiffies);
    void QueueAudio(TUint aJiffies);
    TBool PullPostDropDecodedStream();

    void TestAllMsgsPass();
    void TestSongcastNoMsgDelay();
    void TestSongcastReceiverInSync();

    void TestSongcastReceiverBehindMsgBoundary();
    void TestSongcastReceiverBehindMsgNonBoundary();
    void TestSongcastReceiverBehindMsgsBoundary();
    void TestSongcastReceiverBehindMsgsNonBoundary();

    void TestSongcastReceiverAhead();

    void TestSongcastDrain(); // Results in new delay being sent down. Applies where clock family changes.
    void TestAnimatorDelayConsidered();
private:
    MsgFactory* iMsgFactory;
    TrackFactory* iTrackFactory;
    PhaseAdjuster* iPhaseAdjuster;
    AllocatorInfoLogger iInfoAggregator;
    RampValidator* iRampValidator;
    DecodedAudioValidator* iDecodedAudioValidator;
    EMsgType iNextGeneratedMsg;
    EMsgType iLastMsg;
    TUint iJiffies;
    TUint iJiffiesAudioPcm;
    TUint iNumMsgsGenerated;
    TUint iAudioMsgSizeJiffies;
    TUint64 iTrackOffset;
    TBool iNextModeSupportsLatency;
    TUint iNextDelayAbsoluteJiffies;
    IClockPuller* iNextModeClockPuller;
    TUint64 iLastPulledStreamPos;
    TUint iNextDiscardFlushId;
    TUint64 iNextStreamSampleStart;
    TUint iStreamId;
    TUint iNextStreamId;
    TInt iBufferSize;
    MsgQueueLite iMsgQueue;
    ERampStatus iRampStatus;
    TUint iLastRampPos;
    TUint iAnimatorDelayJiffies;
};

} // namespace Media
} // namespace OpenHome


// SuitePhaseAdjuster

const TUint SuitePhaseAdjuster::kDecodedAudioCount;
const TUint SuitePhaseAdjuster::kMsgAudioPcmCount;
const TUint SuitePhaseAdjuster::kMsgSilenceCount;
const TUint SuitePhaseAdjuster::kMsgSilenceSize;
const TUint SuitePhaseAdjuster::kSampleRate;
const TUint SuitePhaseAdjuster::kNumChannels;
const TUint SuitePhaseAdjuster::kRampDurationMin;
const TUint SuitePhaseAdjuster::kRampDurationMax;
const TUint SuitePhaseAdjuster::kDelayJiffies;
const TUint SuitePhaseAdjuster::kDefaultAudioJiffies;

const SpeakerProfile SuitePhaseAdjuster::kProfile(2);
const Brn SuitePhaseAdjuster::kMode("TestMode");
const Brn SuitePhaseAdjuster::kModeSongcast("Receiver");

SuitePhaseAdjuster::SuitePhaseAdjuster()
    : SuiteUnitTest("SuitePhaseAdjuster")
{
    AddTest(MakeFunctor(*this, &SuitePhaseAdjuster::TestAllMsgsPass), "TestAllMsgsPass");
    AddTest(MakeFunctor(*this, &SuitePhaseAdjuster::TestSongcastNoMsgDelay), "TestSongcastNoMsgDelay");
    AddTest(MakeFunctor(*this, &SuitePhaseAdjuster::TestSongcastReceiverInSync), "TestSongcastReceiverInSync");
    AddTest(MakeFunctor(*this, &SuitePhaseAdjuster::TestSongcastReceiverBehindMsgBoundary), "TestSongcastReceiverBehindMsgBoundary");
    AddTest(MakeFunctor(*this, &SuitePhaseAdjuster::TestSongcastReceiverBehindMsgNonBoundary), "TestSongcastReceiverBehindMsgNonBoundary");
    AddTest(MakeFunctor(*this, &SuitePhaseAdjuster::TestSongcastReceiverBehindMsgsBoundary), "TestSongcastReceiverBehindMsgsBoundary");
    AddTest(MakeFunctor(*this, &SuitePhaseAdjuster::TestSongcastReceiverBehindMsgsNonBoundary), "TestSongcastReceiverBehindMsgsNonBoundary");
    AddTest(MakeFunctor(*this, &SuitePhaseAdjuster::TestSongcastReceiverAhead), "TestSongcastReceiverAhead");
    AddTest(MakeFunctor(*this, &SuitePhaseAdjuster::TestSongcastDrain), "TestSongcastDrain");
    AddTest(MakeFunctor(*this, &SuitePhaseAdjuster::TestAnimatorDelayConsidered), "TestAnimatorDelayConsidered");

    // AddTest(MakeFunctor(*this, &SuitePhaseAdjuster::), "");
}

SuitePhaseAdjuster::~SuitePhaseAdjuster()
{
}

void SuitePhaseAdjuster::Setup()
{
    MsgFactoryInitParams init;
    init.SetMsgAudioPcmCount(kMsgAudioPcmCount, kDecodedAudioCount);
    init.SetMsgSilenceCount(kMsgSilenceCount);
    init.SetMsgEncodedStreamCount(2);
    init.SetMsgDecodedStreamCount(2);
    init.SetMsgModeCount(2);
    init.SetMsgDelayCount(2);
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
    iTrackFactory = new TrackFactory(iInfoAggregator, 1);
    iPhaseAdjuster = new PhaseAdjuster(*iMsgFactory, *this, *this, kRampDurationMin, kRampDurationMax);
    iPhaseAdjuster->SetAnimator(*this);
    iRampValidator = new RampValidator(*iPhaseAdjuster, "RampValidator");
    iDecodedAudioValidator = new DecodedAudioValidator(*iRampValidator, "DecodedAudioValidator");
    iLastMsg = ENone;
    iJiffies = iJiffiesAudioPcm = 0;
    iNumMsgsGenerated = 0;
    iAudioMsgSizeJiffies = 0;
    iTrackOffset = 0;
    iNextModeSupportsLatency = true;
    iNextDelayAbsoluteJiffies = 0;
    iNextModeClockPuller = nullptr;
    iLastPulledStreamPos = 0;
    iNextDiscardFlushId = MsgFlush::kIdInvalid;
    iNextStreamSampleStart = 0;
    iStreamId = UINT_MAX;
    iNextStreamId = 0;
    iBufferSize = 0;
    iRampStatus = ERampStatus::ENoRamp;
    iLastRampPos = 0x7f7f;
    iAnimatorDelayJiffies = 0;
}

void SuitePhaseAdjuster::TearDown()
{
    iMsgQueue.Clear();
    delete iDecodedAudioValidator;
    delete iRampValidator;
    delete iPhaseAdjuster;
    delete iMsgFactory;
    delete iTrackFactory;
}

Msg* SuitePhaseAdjuster::Pull()
{
    iNumMsgsGenerated++;
    switch (iNextGeneratedMsg)
    {
    case EMsgAudioPcm:
    {
        if (!iMsgQueue.IsEmpty()) {
            return iMsgQueue.Dequeue();
        }
        else {
            return CreateAudio(kDefaultAudioJiffies);
        }
    }
    case EMsgAudioDsd:
    {
        TByte audioData[128];
        (void)memset(audioData, 0x7f, sizeof audioData);
        Brn audioBuf(audioData, sizeof audioData);
        MsgAudioDsd* audio = iMsgFactory->CreateMsgAudioDsd(audioBuf, 2, 2822400, 2, iTrackOffset, 0);
        iAudioMsgSizeJiffies = audio->Jiffies();
        iTrackOffset += iAudioMsgSizeJiffies;
        return audio;
    }
    case EMsgSilence:
    {
        TUint size = kMsgSilenceSize;
        auto silence = iMsgFactory->CreateMsgSilence(size, kSampleRate, 16, kNumChannels);
        silence->SetObserver(*this);
        return silence;
    }
    case EMsgDecodedStream:
        return iMsgFactory->CreateMsgDecodedStream(iNextStreamId++, 0, 8, 44100, 2, Brx::Empty(), 0, iNextStreamSampleStart, false, false, false, false, AudioFormat::Pcm, Multiroom::Allowed, kProfile, nullptr, RampType::Sample);
    case EMsgMode:
    {
        ModeInfo info;
        info.SetSupportsLatency(iNextModeSupportsLatency);
        ModeTransportControls transportControls;
        return iMsgFactory->CreateMsgMode(kMode, info, iNextModeClockPuller, transportControls);
    }
    case EMsgModeSongcast:
    {
        ModeInfo info;
        info.SetSupportsLatency(iNextModeSupportsLatency);
        ModeTransportControls transportControls;
        return iMsgFactory->CreateMsgMode(kModeSongcast, info, iNextModeClockPuller, transportControls);
    }
    case EMsgTrack:
    {
        Track* track = iTrackFactory->CreateTrack(Brx::Empty(), Brx::Empty());
        Msg* msg = iMsgFactory->CreateMsgTrack(*track);
        track->RemoveRef();
        return msg;
    }
    case EMsgDrain:
        return iMsgFactory->CreateMsgDrain(Functor());
    case EMsgDelay:
        iNextGeneratedMsg = EMsgSilence;
        return iMsgFactory->CreateMsgDelay(iNextDelayAbsoluteJiffies);
    case EMsgEncodedStream:
        return iMsgFactory->CreateMsgEncodedStream(Brn("http://1.2.3.4:5"), Brn("metatext"), 0, 0, 0, false, false, Multiroom::Allowed, nullptr);
    case EMsgMetaText:
        return iMsgFactory->CreateMsgMetaText(Brn("metatext"));
    case EMsgStreamInterrupted:
        return iMsgFactory->CreateMsgStreamInterrupted();
    case EMsgBitRate:
        return iMsgFactory->CreateMsgBitRate(100);
    case EMsgHalt:
        return iMsgFactory->CreateMsgHalt();
    case EMsgFlush:
        iNextGeneratedMsg = EMsgDecodedStream; // successful TryDiscard should result in a Flush then DecodedStream
        return iMsgFactory->CreateMsgFlush(iNextDiscardFlushId);
    case EMsgWait:
        return iMsgFactory->CreateMsgWait();
    case EMsgQuit:
        return iMsgFactory->CreateMsgQuit();
    default:
        ASSERTS();
        return nullptr;
    }
}

MsgAudio* SuitePhaseAdjuster::CreateAudio(TUint aJiffies)
{
    static const TUint kByteDepth = 2;
    const TUint samples = aJiffies / Jiffies::PerSample(kSampleRate);
    const TUint bytes = samples * kNumChannels * kByteDepth;
    Bwh encodedAudioBuf(bytes);
    encodedAudioBuf.SetBytes(encodedAudioBuf.MaxBytes());
    encodedAudioBuf.Fill(0x7f);
    MsgAudioPcm* audio = iMsgFactory->CreateMsgAudioPcm(encodedAudioBuf, kNumChannels, kSampleRate, 16, AudioDataEndian::Little, iTrackOffset);
    audio->SetObserver(*this);
    iAudioMsgSizeJiffies = audio->Jiffies();
    iTrackOffset += iAudioMsgSizeJiffies;
    return audio;
}

void SuitePhaseAdjuster::QueueAudio(TUint aJiffies)
{
    TUint remaining = aJiffies;
    while (remaining > 0) {
        TUint jiffies = kDefaultAudioJiffies;
        if (remaining < jiffies) {
            jiffies = remaining;
        }

        iMsgQueue.Enqueue(CreateAudio(jiffies));
        remaining -= jiffies;
    }
}

TBool SuitePhaseAdjuster::PullPostDropDecodedStream()
{
    iNextGeneratedMsg = EMsgAudioPcm;
    PullNext();
    return iLastMsg == EMsgDecodedStream;
}

Msg* SuitePhaseAdjuster::ProcessMsg(MsgMode* aMsg)
{
    if (aMsg->Mode() == Brn("Receiver")) {
        iLastMsg = EMsgModeSongcast;
    }
    else {
        iLastMsg = EMsgMode;
    }
    return aMsg;
}

Msg* SuitePhaseAdjuster::ProcessMsg(MsgTrack* aMsg)
{
    iLastMsg = EMsgTrack;
    return aMsg;
}

Msg* SuitePhaseAdjuster::ProcessMsg(MsgDrain* aMsg)
{
    iLastMsg = EMsgDrain;
    return aMsg;
}

Msg* SuitePhaseAdjuster::ProcessMsg(MsgDelay* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuitePhaseAdjuster::ProcessMsg(MsgEncodedStream* aMsg)
{
    iLastMsg = EMsgEncodedStream;
    return aMsg;
}

Msg* SuitePhaseAdjuster::ProcessMsg(MsgStreamSegment* /*aMsg*/)
{
    ASSERTS(); /* only expect to deal with decoded audio at this stage of the pipeline */
    return nullptr;
}

Msg* SuitePhaseAdjuster::ProcessMsg(MsgAudioEncoded* /*aMsg*/)
{
    ASSERTS(); /* only expect to deal with decoded audio at this stage of the pipeline */
    return nullptr;
}

Msg* SuitePhaseAdjuster::ProcessMsg(MsgMetaText* aMsg)
{
    iLastMsg = EMsgMetaText;
    return aMsg;
}

Msg* SuitePhaseAdjuster::ProcessMsg(MsgStreamInterrupted* aMsg)
{
    iLastMsg = EMsgStreamInterrupted;
    return aMsg;
}

Msg* SuitePhaseAdjuster::ProcessMsg(MsgHalt* aMsg)
{
    iLastMsg = EMsgHalt;
    return aMsg;
}

Msg* SuitePhaseAdjuster::ProcessMsg(MsgFlush* aMsg)
{
    iLastMsg = EMsgFlush;
    return aMsg;
}

Msg* SuitePhaseAdjuster::ProcessMsg(MsgWait* aMsg)
{
    iLastMsg = EMsgWait;
    return aMsg;
}

Msg* SuitePhaseAdjuster::ProcessMsg(MsgDecodedStream* aMsg)
{
    iLastMsg = EMsgDecodedStream;
    const auto info = aMsg->StreamInfo();
    iStreamId = info.StreamId();
    iLastPulledStreamPos = info.SampleStart() * Jiffies::PerSample(info.SampleRate());
    return aMsg;
}

Msg* SuitePhaseAdjuster::ProcessMsg(MsgBitRate* aMsg)
{
    iLastMsg = EMsgBitRate;
    return aMsg;
}

Msg* SuitePhaseAdjuster::ProcessMsg(MsgAudioPcm* aMsg)
{
    iLastMsg = EMsgAudioPcm;
    TUint jiffies = aMsg->Jiffies();
    iLastPulledStreamPos += jiffies;

    MsgPlayable* playable = aMsg->CreatePlayable();
    ProcessorPcmBufTest pcmProcessor;
    playable->Read(pcmProcessor);
    Brn buf(pcmProcessor.Buf());
    const TByte* ptr = buf.Ptr();
    const TInt firstSubsample = (ptr[0]<<8) | ptr[1];
    const TUint bytes = buf.Bytes();
    const TInt lastSubsample = (ptr[bytes-2]<<8) | ptr[bytes-1];
    playable->RemoveRef();

    if (iRampStatus == ERampStatus::ERampingUp) {
        TEST(firstSubsample == static_cast<TInt>(iLastRampPos));
        TEST(lastSubsample > firstSubsample);
        iLastRampPos = lastSubsample;

        if (iLastRampPos == 0x7f7f - 1) {
            iLastRampPos += 1;
            iRampStatus = ERampStatus::ERampComplete;
        }
    }
    else {
        TEST(firstSubsample == static_cast<TInt>(iLastRampPos));
        TEST(lastSubsample == firstSubsample);
    }

    iJiffies += jiffies;
    iJiffiesAudioPcm += jiffies;
    return nullptr;
}

Msg* SuitePhaseAdjuster::ProcessMsg(MsgAudioDsd* aMsg)
{
    iLastMsg = EMsgAudioDsd;
    iJiffies += aMsg->Jiffies();
    return aMsg;
}

Msg* SuitePhaseAdjuster::ProcessMsg(MsgSilence* aMsg)
{
    iLastMsg = EMsgSilence;
    iJiffies += aMsg->Jiffies();
    return aMsg;
}

Msg* SuitePhaseAdjuster::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    ASSERTS(); // MsgPlayable not expected at this stage of the pipeline
    return nullptr;
}

Msg* SuitePhaseAdjuster::ProcessMsg(MsgQuit* aMsg)
{
    iLastMsg = EMsgQuit;
    return aMsg;
}

void SuitePhaseAdjuster::Update(TInt aDelta)
{
    iBufferSize += aDelta;
    iPhaseAdjuster->Update(aDelta);
}

void SuitePhaseAdjuster::Start()
{
    ASSERTS();
}

void SuitePhaseAdjuster::Stop()
{
    ASSERTS();
}

TUint SuitePhaseAdjuster::PipelineAnimatorBufferJiffies() const
{
    return Jiffies::kPerMs;
}

TUint SuitePhaseAdjuster::PipelineAnimatorDelayJiffies(AudioFormat /*aFormat*/, TUint /*aSampleRate*/, TUint /*aBitDepth*/, TUint /*aNumChannels*/) const
{
    return iAnimatorDelayJiffies;
}

TUint SuitePhaseAdjuster::PipelineAnimatorDsdBlockSizeWords() const
{
    ASSERTS();
    return 4;
}

TUint SuitePhaseAdjuster::PipelineAnimatorMaxBitDepth() const
{
    ASSERTS();
    return 24;
}

void SuitePhaseAdjuster::WaitForOccupancy(TUint /*aJiffies*/)
{
    // FIXME - add tests for this being called
}

void SuitePhaseAdjuster::PullNext()
{
    Msg* msg = iDecodedAudioValidator->Pull();
    msg = msg->Process(*this);
    if (msg != nullptr) {
        msg->RemoveRef();
    }
}

void SuitePhaseAdjuster::PullNext(EMsgType aExpectedMsg)
{
    iNextGeneratedMsg = aExpectedMsg;
    PullNext();
    TEST(iLastMsg == aExpectedMsg);
    if (iLastMsg != aExpectedMsg) {
        static const TChar* kMsgTypes[] ={ "None"
                                         , "Mode"
                                         , "Mode (Songcast)"
                                         , "Track"
                                         , "Drain"
                                         , "Delay"
                                         , "EncodedStream"
                                         , "Metatext"
                                         , "StreamInterrupted"
                                         , "DecodedStream"
                                         , "BitRate"
                                         , "AudioPcm"
                                         , "Silence"
                                         , "Halt"
                                         , "Flush"
                                         , "Wait"
                                         , "Quit"
        };
        Print("Expected %s, got %s\n", kMsgTypes[aExpectedMsg], kMsgTypes[iLastMsg]);
    }
}

void SuitePhaseAdjuster::TestAllMsgsPass()
{
    /* 'AllMsgs' excludes encoded & playable audio - PhaseAdjuster is
       assumed only useful to the portion of the pipeline that deals in decoded
       audio */
    static const EMsgType msgs[] = { EMsgMode, EMsgTrack, EMsgDrain, EMsgEncodedStream,
                                     EMsgMetaText, EMsgStreamInterrupted, EMsgDecodedStream,
                                     EMsgBitRate, EMsgAudioPcm, EMsgAudioDsd, EMsgSilence,
                                     EMsgHalt, EMsgFlush, EMsgWait, EMsgQuit };
    for (TUint i=0; i<sizeof(msgs)/sizeof(msgs[0]); i++) {
        PullNext(msgs[i]);
    }
}

void SuitePhaseAdjuster::TestSongcastNoMsgDelay()
{
    iNextModeClockPuller = nullptr;

    PullNext(EMsgModeSongcast);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    // No MsgDelay, so no dropping should occur.

    iJiffies = 0;
    iMsgQueue.Enqueue(CreateAudio(kDefaultAudioJiffies)); // Phase error is a single MsgAudioPcm (983040 jiffies in this case).
    PullNext(EMsgAudioPcm);
    TEST(iJiffies == kDefaultAudioJiffies);
}

void SuitePhaseAdjuster::TestSongcastReceiverInSync()
{
    iNextModeClockPuller = nullptr;

    PullNext(EMsgModeSongcast);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    TEST(iLastPulledStreamPos == 0);
    iNextGeneratedMsg = EMsgDelay;
    iNextDelayAbsoluteJiffies = kDelayJiffies;
    iJiffies = 0;
    PullNext(); // Phase adjuster consumes delay.

    while (iJiffies < kDelayJiffies) {
        PullNext(EMsgSilence);
    }
    TEST(iJiffies == kDelayJiffies);

    // After all silence has been output want exactly the same amount of audio queued up us the delay value.
    // This would mean the receiver is playing in sync with the sender.
    QueueAudio(kDelayJiffies);
    const auto offset = iTrackOffset;
    const auto bufferedAudio = iBufferSize;

    iJiffies = 0;
    PullNext(EMsgAudioPcm);
    TEST(iJiffies == kDefaultAudioJiffies);
    TEST(iTrackOffset == offset); // No more msgs than those in queue have been created/pulled.
    TEST(iBufferSize == bufferedAudio - static_cast<TInt>(kDefaultAudioJiffies)); // Should have only released 1 full msg (i.e., phase adjuster shouldn't have dropped any).
}

void SuitePhaseAdjuster::TestSongcastReceiverBehindMsgBoundary()
{
    iNextModeClockPuller = nullptr;

    PullNext(EMsgModeSongcast);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    TEST(iLastPulledStreamPos == 0);
    iNextGeneratedMsg = EMsgDelay;
    iNextDelayAbsoluteJiffies = kDelayJiffies;
    iJiffies = 0;
    PullNext(); // Phase adjuster consumes delay.

    while (iJiffies < kDelayJiffies) {
        PullNext(EMsgSilence);
    }
    TEST(iJiffies == kDelayJiffies);

    // Queue up exact number of delay jiffies of MsgAudioPcm.
    QueueAudio(kDelayJiffies);
    iMsgQueue.Enqueue(CreateAudio(kDefaultAudioJiffies)); // Buffer occupancy is now 1 MsgAudioPcm (983040 jiffies in this case) behind where it should be.
    const auto offset = iTrackOffset;
    const auto bufferedAudio = iBufferSize;
    iJiffies = 0;

    TEST(PullPostDropDecodedStream());
    TEST(iLastPulledStreamPos == kDefaultAudioJiffies);
    iRampStatus = ERampStatus::ERampingUp;
    iLastRampPos = 0;
    PullNext(EMsgAudioPcm);
    TEST(iJiffies == kDefaultAudioJiffies);
    TEST(iTrackOffset == offset); // Should match audio jiffies inserted into pipeline.
    TEST(iBufferSize == bufferedAudio - 2 * static_cast<TInt>(kDefaultAudioJiffies)); // Should have released 2 msgs worth of audio (1 full msg dropped, 1 full msg returned).

    PullNext(EMsgAudioPcm);
    PullNext(EMsgAudioPcm);
    PullNext(EMsgAudioPcm);
    TEST(iRampStatus == ERampStatus::ERampComplete);
}

void SuitePhaseAdjuster::TestSongcastReceiverBehindMsgNonBoundary()
{
    iNextModeClockPuller = nullptr;

    PullNext(EMsgModeSongcast);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    TEST(iLastPulledStreamPos == 0);
    iNextGeneratedMsg = EMsgDelay;
    iNextDelayAbsoluteJiffies = kDelayJiffies;
    iJiffies = 0;
    PullNext(); // Phase adjuster consumes delay.

    // Pull delay silence through.
    while (iJiffies < kDelayJiffies) {
        PullNext(EMsgSilence);
    }
    TEST(iJiffies == kDelayJiffies);

    QueueAudio(kDelayJiffies);
    iMsgQueue.Enqueue(CreateAudio(kDefaultAudioJiffies / 2)); // Buffer occupancy is now 0.5 MsgAudioPcm (983040 jiffies in this case) behind where it should be.
    const auto offset = iTrackOffset;
    const auto bufferedAudio = iBufferSize;
    iJiffies = 0;

    TEST(PullPostDropDecodedStream());
    TEST(iLastPulledStreamPos == kDefaultAudioJiffies / 2);
    iRampStatus = ERampStatus::ERampingUp;
    iLastRampPos = 0;
    PullNext(EMsgAudioPcm);
    TEST(iJiffies == kDefaultAudioJiffies / 2);
    TEST(iTrackOffset == offset); // No more msgs than those in queue have been created/pulled.
    TEST(iBufferSize == bufferedAudio - static_cast<TInt>(kDefaultAudioJiffies)); // Pulling through next MsgAudioPcm should have resulted in 1 msgs worth of audio being released (0.5 msg dropped, remaining 0.5 msg returned).

    PullNext(EMsgAudioPcm);
    PullNext(EMsgAudioPcm);
    PullNext(EMsgAudioPcm);
    TEST(iRampStatus == ERampStatus::ERampComplete);
}

void SuitePhaseAdjuster::TestSongcastReceiverBehindMsgsBoundary()
{
    iNextModeClockPuller = nullptr;

    PullNext(EMsgModeSongcast);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    TEST(iLastPulledStreamPos == 0);
    iNextGeneratedMsg = EMsgDelay;
    iNextDelayAbsoluteJiffies = kDelayJiffies;
    iJiffies = 0;
    PullNext(); // Phase adjuster consumes delay.

    while (iJiffies < kDelayJiffies) {
        PullNext(EMsgSilence);
    }
    TEST(iJiffies == kDelayJiffies);

    QueueAudio(kDelayJiffies);
    iMsgQueue.Enqueue(CreateAudio(kDefaultAudioJiffies));
    iMsgQueue.Enqueue(CreateAudio(kDefaultAudioJiffies)); // Buffer occupancy is now 2 MsgAudioPcm (2 * 983040 jiffies in this case) behind where it should be.
    const auto bufferedAudio = iBufferSize;
    const auto offset = iTrackOffset;
    iJiffies = 0;

    TEST(PullPostDropDecodedStream());
    TEST(iLastPulledStreamPos == 2 * kDefaultAudioJiffies);
    iRampStatus = ERampStatus::ERampingUp;
    iLastRampPos = 0;
    PullNext(EMsgAudioPcm);
    TEST(iJiffies == kDefaultAudioJiffies);
    TEST(iTrackOffset == offset); // No more msgs than those in queue have been created/pulled.
    TEST(iBufferSize == bufferedAudio - 3 * static_cast<TInt>(kDefaultAudioJiffies)); // Pulling through next MsgAudioPcm should have resulted in 3 msgs worth of audio being released (2 full msgs dropped, 1 full msg returned).

    PullNext(EMsgAudioPcm);
    PullNext(EMsgAudioPcm);
    PullNext(EMsgAudioPcm);
    TEST(iRampStatus == ERampStatus::ERampComplete);
}

void SuitePhaseAdjuster::TestSongcastReceiverBehindMsgsNonBoundary()
{
    iNextModeClockPuller = nullptr;

    PullNext(EMsgModeSongcast);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    TEST(iLastPulledStreamPos == 0);
    iNextGeneratedMsg = EMsgDelay;
    iNextDelayAbsoluteJiffies = kDelayJiffies;
    iJiffies = 0;
    PullNext(); // Phase adjuster consumes delay.

    while (iJiffies < kDelayJiffies) {
        PullNext(EMsgSilence);
    }
    TEST(iJiffies == kDelayJiffies);

    QueueAudio(kDelayJiffies);
    iMsgQueue.Enqueue(CreateAudio(kDefaultAudioJiffies));
    iMsgQueue.Enqueue(CreateAudio(kDefaultAudioJiffies / 2)); // Buffer occupancy is now 2 MsgAudioPcm (2 * 983040 jiffies in this case) behind where it should be.
    const auto offset = iTrackOffset;
    const auto bufferedAudio = iBufferSize;
    iJiffies = 0;

    TEST(PullPostDropDecodedStream());
    TEST(iLastPulledStreamPos == kDefaultAudioJiffies + kDefaultAudioJiffies / 2);
    iRampStatus = ERampStatus::ERampingUp;
    iLastRampPos = 0;
    PullNext(EMsgAudioPcm);
    TEST(iJiffies == kDefaultAudioJiffies / 2);
    TEST(iTrackOffset == offset); // Should have create 3 MsgAudioPcm, but first 2.5 should have been dropped by phase adjuster.
    TEST(iBufferSize == bufferedAudio - 2 * static_cast<TInt>(kDefaultAudioJiffies)); // Pulling through next MsgAudioPcm should have resulted in 2 msgs worth of audio being released (1.5 msgs dropped, 0.5 msg remainder returned).

    PullNext(EMsgAudioPcm);
    PullNext(EMsgAudioPcm);
    PullNext(EMsgAudioPcm);
    TEST(iRampStatus == ERampStatus::ERampComplete);
}

void SuitePhaseAdjuster::TestSongcastReceiverAhead()
{
    iNextModeClockPuller = nullptr;

    PullNext(EMsgModeSongcast);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    TEST(iLastPulledStreamPos == 0);
    iNextGeneratedMsg = EMsgDelay;
    iNextDelayAbsoluteJiffies = kDelayJiffies;
    iJiffies = 0;
    PullNext(); // Phase adjuster consumes delay.

    while (iJiffies < kDelayJiffies) {
        PullNext(EMsgSilence);
    }
    TEST(iJiffies == kDelayJiffies);

    // After all silence has been output want less audio queued up than the delay value.
    // This would mean the receiver is playing audio ahead of the sender.
    QueueAudio(kDelayJiffies - kDefaultAudioJiffies);
    const auto offset = iTrackOffset;
    const auto bufferedAudio = iBufferSize;
    iJiffies = 0;

    PullNext(EMsgAudioPcm);
    TEST(iJiffies == kDefaultAudioJiffies);
    TEST(iTrackOffset == offset);
    TEST(iBufferSize == bufferedAudio - static_cast<TInt>(kDefaultAudioJiffies)); // Should have only released 1 full msg (i.e., phase adjuster shouldn't have dropped any).
}

void SuitePhaseAdjuster::TestSongcastDrain()
{
    iNextModeClockPuller = nullptr;

    PullNext(EMsgModeSongcast);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    TEST(iLastPulledStreamPos == 0);
    iNextGeneratedMsg = EMsgDelay;
    iNextDelayAbsoluteJiffies = kDelayJiffies;
    iJiffies = 0;
    PullNext(); // Phase adjuster consumes delay.

    while (iJiffies < kDelayJiffies) {
        PullNext(EMsgSilence);
    }
    TEST(iJiffies == kDelayJiffies);

    // Queue up exact number of delay jiffies of MsgAudioPcm.
    QueueAudio(kDelayJiffies);
    iMsgQueue.Enqueue(CreateAudio(kDefaultAudioJiffies)); // Buffer occupancy is now 1 MsgAudioPcm (983040 jiffies in this case) behind where it should be.
    auto offset = iTrackOffset;
    auto bufferedAudio = iBufferSize;
    iJiffies = 0;

    TEST(PullPostDropDecodedStream());
    TEST(iLastPulledStreamPos == kDefaultAudioJiffies);
    iRampStatus = ERampStatus::ERampingUp;
    iLastRampPos = 0;
    PullNext(EMsgAudioPcm);
    TEST(iJiffies == kDefaultAudioJiffies);
    TEST(iTrackOffset == offset); // Should match audio jiffies inserted into pipeline.
    TEST(iBufferSize == bufferedAudio - 2 * static_cast<TInt>(kDefaultAudioJiffies)); // Should have released 2 msgs worth of audio (1 full msg dropped, 1 full msg returned.

    PullNext(EMsgAudioPcm);
    PullNext(EMsgAudioPcm);
    PullNext(EMsgAudioPcm);
    TEST(iRampStatus == ERampStatus::ERampComplete);

    iMsgQueue.Clear();
    iTrackOffset = 0;
    PullNext(EMsgDrain);
    PullNext(EMsgDecodedStream);
    TEST(iLastPulledStreamPos == 0);
    // Now output a drain followed by a new delay. Phase adjuster should restart adjustment.
    iNextGeneratedMsg = EMsgDelay;
    iNextDelayAbsoluteJiffies = kDelayJiffies;
    iJiffies = 0;
    PullNext(); // Phase adjuster consumes delay.

    while (iJiffies < kDelayJiffies) {
        PullNext(EMsgSilence);
    }
    TEST(iJiffies == kDelayJiffies);

    // Queue up exact number of delay jiffies of MsgAudioPcm.
    QueueAudio(kDelayJiffies);
    iMsgQueue.Enqueue(CreateAudio(kDefaultAudioJiffies)); // Buffer occupancy is now 1 MsgAudioPcm (983040 jiffies in this case) behind where it should be.
    offset = iTrackOffset;
    bufferedAudio = iBufferSize;
    iJiffies = 0;

    TEST(PullPostDropDecodedStream());
    TEST(iLastPulledStreamPos == kDefaultAudioJiffies);
    iRampStatus = ERampStatus::ERampingUp;
    iLastRampPos = 0;
    PullNext(EMsgAudioPcm);
    TEST(iJiffies == kDefaultAudioJiffies);
    TEST(iTrackOffset == offset); // Should match audio jiffies inserted into pipeline.
    TEST(iBufferSize == bufferedAudio - 2 * static_cast<TInt>(kDefaultAudioJiffies)); // Should have released 2 msgs worth of audio (1 full msg dropped, 1 full msg returned).

    PullNext(EMsgAudioPcm);
    PullNext(EMsgAudioPcm);
    PullNext(EMsgAudioPcm);
    TEST(iRampStatus == ERampStatus::ERampComplete);
}

void SuitePhaseAdjuster::TestAnimatorDelayConsidered()
{
    iNextModeClockPuller = nullptr;
    iAnimatorDelayJiffies = Jiffies::kPerMs;

    PullNext(EMsgModeSongcast);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    TEST(iLastPulledStreamPos == 0);
    iNextGeneratedMsg = EMsgDelay;
    iNextDelayAbsoluteJiffies = kDelayJiffies;
    iJiffies = 0;
    PullNext(); // Phase adjuster consumes delay.

    while (iJiffies < kDelayJiffies) {
        PullNext(EMsgSilence);
    }
    TEST(iJiffies == kDelayJiffies);

    QueueAudio(kDelayJiffies);
    TEST(PullPostDropDecodedStream());

    TUint pos = iAnimatorDelayJiffies;
    Jiffies::RoundDown(pos, 44100);
    TEST(iLastPulledStreamPos == pos);
}



void TestPhaseAdjuster()
{
    Runner runner("Songcast phase adjuster tests\n");
    runner.Add(new SuitePhaseAdjuster());
    runner.Run();
}

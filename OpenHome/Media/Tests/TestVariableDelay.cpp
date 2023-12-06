#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/SuiteUnitTest.h>
#include <OpenHome/Media/Pipeline/VariableDelay.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/ClockPuller.h>
#include <OpenHome/Media/Utils/AllocatorInfoLogger.h>
#include <OpenHome/Media/Utils/ProcessorAudioUtils.h>
#include <OpenHome/Media/Pipeline/RampValidator.h>
#include <OpenHome/Media/Pipeline/DecodedAudioValidator.h>

#include <string.h>
#include <limits.h>
#include <vector>

using namespace OpenHome;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Media;

namespace OpenHome {
namespace Media {

class SuiteVariableDelay : public SuiteUnitTest
                         , protected IPipelineElementUpstream
                         , private IMsgProcessor
                         , private IStreamHandler
                         , protected IClockPuller
{
    static const TUint kDecodedAudioCount = 2;
    static const TUint kMsgAudioPcmCount  = 3;
    static const TUint kMsgSilenceCount   = 1;

    static const TUint kMsgSilenceSize = Jiffies::kPerMs;

    static const TUint kSampleRate  = 44100;
    static const TUint kNumChannels = 2;
    static const SpeakerProfile kProfile;

    static const Brn kMode;
protected:
    static const TUint kRampDuration = Jiffies::kPerMs * 20;
public:
    ~SuiteVariableDelay();
protected:
    SuiteVariableDelay(const TChar* aId);
protected: // from SuiteUnitTest
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
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgAudioDsd* aMsg) override;
    Msg* ProcessMsg(MsgSilence* aMsg) override;
    Msg* ProcessMsg(MsgPlayable* aMsg) override;
    Msg* ProcessMsg(MsgQuit* aMsg) override;
private: // from IStreamHandler
    EStreamPlay OkToPlay(TUint aStreamId) override;
    TUint TrySeek(TUint aStreamId, TUint64 aOffset) override;
    TUint TryDiscard(TUint aJiffies) override;
    TUint TryStop(TUint aStreamId) override;
    void NotifyStarving(const Brx& aMode, TUint aStreamId, TBool aStarving) override;
private: // from IClockPuller
    void Update(TInt aDelta) override;
    void Start() override;
    void Stop() override;
protected:
    enum EMsgType
    {
        ENone,
        EMsgMode,
        EMsgTrack,
        EMsgDrain,
        EMsgDelay,
        EMsgEncodedStream,
        EMsgMetaText,
        EMsgStreamInterrupted,
        EMsgDecodedStream,
        EMsgAudioPcm,
        EMsgAudioDsd,
        EMsgSilence,
        EMsgHalt,
        EMsgFlush,
        EMsgWait,
        EMsgQuit
    };
protected:
    virtual void DoSetup() = 0;
    void PullNext();
    void PullNext(EMsgType aExpectedMsg);
private:
    MsgAudio* CreateAudio();
protected:
    MsgFactory* iMsgFactory;
    TrackFactory* iTrackFactory;
    AllocatorInfoLogger iInfoAggregator;
    VariableDelayBase* iVariableDelay;
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
    TUint iLastPulledDelay;
    TUint64 iLastPulledStreamPos;
    TUint iNextDiscardFlushId;
    TUint64 iNextStreamSampleStart;
    TUint iClockPullStartCount;
    TUint iClockPullStopCount;
    TUint iStreamId;
    TUint iNextStreamId;
    TInt iBufferSize;
};

class SuiteVariableDelayLeft : public SuiteVariableDelay, private IVariableDelayObserver
{
    static const TUint kDownstreamDelay = 30 * Jiffies::kPerMs;
public:
    SuiteVariableDelayLeft();
private: // from SuiteVariableDelay
    void DoSetup() override;
private: // from IVariableDelayObserver
    void NotifyDelayApplied(TUint aJiffies) override;
private:
    void TestAllMsgsPass();
    void TestDelayFromRunning();
    void TestDelayFromStarting();
    void TestReduceDelayFromRunning();
    void TestChangeDelayWhileRampingDown();
    void TestChangeDelayWhileRampingUp();
    void TestNoSilenceInjectedBeforeDecodedStream();
    void TestDelayAppliedAfterDrain();
    void TestDelayShorterThanDownstream();
    void TestReportsDelayToObserver();
    void TestUpstreamDiscardWhenDelayReduced();
private:
    TUint iDelayAppliedJiffies;
};

class SuiteVariableDelayRight : public SuiteVariableDelay, private IPipelineAnimator
{
    static const TUint kMinDelay = 10 * Jiffies::kPerMs;
public:
    SuiteVariableDelayRight();
private: // from SuiteVariableDelay
    void DoSetup() override;
private: // from IPipelineAnimator
    TUint PipelineAnimatorBufferJiffies() const override;
    TUint PipelineAnimatorDelayJiffies(AudioFormat aFormat, TUint aSampleRate, TUint aBitDepth, TUint aNumChannels) const override;
    TUint PipelineAnimatorDsdBlockSizeWords() const override;
    TUint PipelineAnimatorMaxBitDepth() const override;
    void PipelineAnimatorGetMaxSampleRates(TUint& aPcm, TUint& aDsd) const override;
    void PipelineAnimatorNotifyAudioReceived() override {}
private:
    void TestDelayShorterThanMinimum();
    void TestAnimatorCalledOnStreamChange();
    void TestClockPuller();
private:
    TUint iAnimatorDelayJiffies;
    mutable TUint iNumAnimatorDelayJiffiesCalls;
};

} // namespace Media
} // namespace OpenHome


// SuiteVariableDelay

const Brn SuiteVariableDelay::kMode("VariableDelayMode");
const SpeakerProfile SuiteVariableDelay::kProfile(2);

SuiteVariableDelay::SuiteVariableDelay(const TChar* aId)
    : SuiteUnitTest(aId)
{
}

SuiteVariableDelay::~SuiteVariableDelay()
{
}

void SuiteVariableDelay::Setup()
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
    DoSetup();
    ASSERT(iVariableDelay != nullptr);
    iRampValidator = new RampValidator(*iVariableDelay, "RampValidator");
    iDecodedAudioValidator = new DecodedAudioValidator(*iRampValidator, "DecodedAudioValidator");
    iLastMsg = ENone;
    iJiffies = iJiffiesAudioPcm = 0;
    iNumMsgsGenerated = 0;
    iAudioMsgSizeJiffies = 0;
    iTrackOffset = 0;
    iNextModeSupportsLatency = true;
    iNextDelayAbsoluteJiffies = 0;
    iNextModeClockPuller = nullptr;
    iLastPulledDelay = 0;
    iLastPulledStreamPos = 0;
    iNextDiscardFlushId = MsgFlush::kIdInvalid;
    iNextStreamSampleStart = 0;
    iClockPullStartCount = iClockPullStopCount = 0;
    iStreamId = UINT_MAX;
    iNextStreamId = 0;
    iBufferSize = 0;
}

void SuiteVariableDelay::TearDown()
{
    TEST(iBufferSize == 0);
    delete iDecodedAudioValidator;
    delete iRampValidator;
    delete iVariableDelay;
    delete iMsgFactory;
    delete iTrackFactory;
}

Msg* SuiteVariableDelay::Pull()
{
    iNumMsgsGenerated++;
    switch (iNextGeneratedMsg)
    {
    case EMsgAudioPcm:
        return CreateAudio();
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
        return iMsgFactory->CreateMsgSilence(size, kSampleRate, 16, kNumChannels);
    }
    case EMsgDecodedStream:
        return iMsgFactory->CreateMsgDecodedStream(iNextStreamId++, 0, 8, 44100, 2, Brx::Empty(), 0, iNextStreamSampleStart, false, false, false, false, AudioFormat::Pcm, Multiroom::Allowed, kProfile, this, RampType::Sample);
    case EMsgMode:
    {
        ModeInfo info;
        info.SetLatencyMode(iNextModeSupportsLatency ? Latency::Internal : Latency::NotSupported);
        ModeTransportControls transportControls;
        return iMsgFactory->CreateMsgMode(kMode, info, iNextModeClockPuller, transportControls);
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
        iNextGeneratedMsg = EMsgAudioPcm;
        return iMsgFactory->CreateMsgDelay(iNextDelayAbsoluteJiffies);
    case EMsgEncodedStream:
        return iMsgFactory->CreateMsgEncodedStream(Brn("http://1.2.3.4:5"), Brn("metatext"), 0, 0, 0, false, false, Multiroom::Allowed, nullptr);
    case EMsgMetaText:
        return iMsgFactory->CreateMsgMetaText(Brn("metatext"));
    case EMsgStreamInterrupted:
        return iMsgFactory->CreateMsgStreamInterrupted();
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

MsgAudio* SuiteVariableDelay::CreateAudio()
{
    static const TUint kDataBytes = 3 * 1024;
    TByte encodedAudioData[kDataBytes];
    (void)memset(encodedAudioData, 0x7f, kDataBytes);
    Brn encodedAudioBuf(encodedAudioData, kDataBytes);
    MsgAudioPcm* audio = iMsgFactory->CreateMsgAudioPcm(encodedAudioBuf, kNumChannels, kSampleRate, 16, AudioDataEndian::Little, iTrackOffset);
    iAudioMsgSizeJiffies = audio->Jiffies();
    iTrackOffset += iAudioMsgSizeJiffies;
    return audio;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgMode* aMsg)
{
    iLastMsg = EMsgMode;
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgTrack* aMsg)
{
    iLastMsg = EMsgTrack;
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgDrain* aMsg)
{
    iLastMsg = EMsgDrain;
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgDelay* aMsg)
{
    iLastMsg = EMsgDelay;
    iLastPulledDelay = aMsg->RemainingJiffies();
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgEncodedStream* aMsg)
{
    iLastMsg = EMsgEncodedStream;
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgStreamSegment* /*aMsg*/)
{
    ASSERTS(); /* only expect to deal with decoded audio at this stage of the pipeline */
    return nullptr;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgAudioEncoded* /*aMsg*/)
{
    ASSERTS(); /* only expect to deal with decoded audio at this stage of the pipeline */
    return nullptr;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgMetaText* aMsg)
{
    iLastMsg = EMsgMetaText;
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgStreamInterrupted* aMsg)
{
    iLastMsg = EMsgStreamInterrupted;
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgHalt* aMsg)
{
    iLastMsg = EMsgHalt;
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgFlush* aMsg)
{
    iLastMsg = EMsgFlush;
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgWait* aMsg)
{
    iLastMsg = EMsgWait;
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgDecodedStream* aMsg)
{
    iLastMsg = EMsgDecodedStream;
    const auto info = aMsg->StreamInfo();
    iStreamId = info.StreamId();
    iLastPulledStreamPos = info.SampleStart() * Jiffies::PerSample(info.SampleRate());
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgAudioPcm* aMsg)
{
    iLastMsg = EMsgAudioPcm;
    TUint jiffies = aMsg->Jiffies();
    iLastPulledStreamPos += jiffies;

    MsgPlayable* playable = aMsg->CreatePlayable();
    ProcessorPcmBufTest pcmProcessor;
    playable->Read(pcmProcessor);
    Brn buf(pcmProcessor.Buf());
    playable->RemoveRef();
    const TByte* ptr = buf.Ptr();
    const TInt firstSubsample = (ptr[0]<<8) | ptr[1];
    const TUint bytes = buf.Bytes();
    const TInt lastSubsample = (ptr[bytes-2]<<8) | ptr[bytes-1];

    switch (iVariableDelay->iStatus)
    {
    case VariableDelayBase::ERampingDown:
        TEST(firstSubsample > lastSubsample);
        break;
    case VariableDelayBase::ERampingUp:
        if (iVariableDelay->iPendingStream == nullptr) { // non-null => this msg is the end of a ramp down
            TEST(firstSubsample < lastSubsample);
        }
        break;
    case VariableDelayBase::ERampedDown:
        break;
    case VariableDelayBase::ERunning:
        if (iJiffies >= kRampDuration) {
            TEST(firstSubsample == lastSubsample);
        }
        break;
    case VariableDelayBase::EStarting:
        TEST(firstSubsample == lastSubsample);
        break;
    }
    iJiffies += jiffies;
    iJiffiesAudioPcm += jiffies;
    return nullptr;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgAudioDsd* aMsg)
{
    iLastMsg = EMsgAudioDsd;
    iJiffies += aMsg->Jiffies();
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgSilence* aMsg)
{
    iLastMsg = EMsgSilence;
    iJiffies += aMsg->Jiffies();
    return aMsg;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    ASSERTS(); // MsgPlayable not expected at this stage of the pipeline
    return nullptr;
}

Msg* SuiteVariableDelay::ProcessMsg(MsgQuit* aMsg)
{
    iLastMsg = EMsgQuit;
    return aMsg;
}

EStreamPlay SuiteVariableDelay::OkToPlay(TUint /*aStreamId*/)
{
    ASSERTS();
    return ePlayNo;
}

TUint SuiteVariableDelay::TrySeek(TUint /*aStreamId*/, TUint64 /*aOffset*/)
{
    ASSERTS();
    return MsgFlush::kIdInvalid;
}

TUint SuiteVariableDelay::TryDiscard(TUint aJiffies)
{
    if (iNextDiscardFlushId == MsgFlush::kIdInvalid) {
        return MsgFlush::kIdInvalid;
    }
    iTrackOffset += aJiffies;
    iNextStreamSampleStart = iTrackOffset / Jiffies::PerSample(kSampleRate);
    return iNextDiscardFlushId;
}

TUint SuiteVariableDelay::TryStop(TUint /*aStreamId*/)
{
    ASSERTS();
    return MsgFlush::kIdInvalid;
}

void SuiteVariableDelay::NotifyStarving(const Brx& /*aMode*/, TUint /*aStreamId*/, TBool /*aStarving*/)
{
    ASSERTS();
}

void SuiteVariableDelay::Update(TInt aDelta)
{
    iBufferSize += aDelta;
}

void SuiteVariableDelay::Start()
{
    iClockPullStartCount++;
}

void SuiteVariableDelay::Stop()
{
    iClockPullStopCount++;
}

void SuiteVariableDelay::PullNext()
{
    Msg* msg = iDecodedAudioValidator->Pull();
    msg = msg->Process(*this);
    if (msg != nullptr) {
        msg->RemoveRef();
    }
}

void SuiteVariableDelay::PullNext(EMsgType aExpectedMsg)
{
    iNextGeneratedMsg = aExpectedMsg;
    PullNext();
    TEST(iLastMsg == aExpectedMsg);
    if (iLastMsg != aExpectedMsg) {
        static const TChar* kMsgTypes[] ={ "None"
                                         , "Mode"
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


// SuiteVariableDelayLeft

SuiteVariableDelayLeft::SuiteVariableDelayLeft()
    : SuiteVariableDelay("VariableDelayLeft")
{
    AddTest(MakeFunctor(*this, &SuiteVariableDelayLeft::TestAllMsgsPass), "TestAllMsgsPass");
    AddTest(MakeFunctor(*this, &SuiteVariableDelayLeft::TestDelayFromRunning), "TestDelayFromRunning");
    AddTest(MakeFunctor(*this, &SuiteVariableDelayLeft::TestDelayFromStarting), "TestDelayFromStarting");
    AddTest(MakeFunctor(*this, &SuiteVariableDelayLeft::TestReduceDelayFromRunning), "TestReduceDelayFromRunning");
    AddTest(MakeFunctor(*this, &SuiteVariableDelayLeft::TestChangeDelayWhileRampingDown), "TestChangeDelayWhileRampingDown");
    AddTest(MakeFunctor(*this, &SuiteVariableDelayLeft::TestChangeDelayWhileRampingUp), "TestChangeDelayWhileRampingUp");
    AddTest(MakeFunctor(*this, &SuiteVariableDelayLeft::TestNoSilenceInjectedBeforeDecodedStream), "TestNoSilenceInjectedBeforeDecodedStream");
    AddTest(MakeFunctor(*this, &SuiteVariableDelayLeft::TestDelayAppliedAfterDrain), "TestDelayAppliedAfterDrain");
    AddTest(MakeFunctor(*this, &SuiteVariableDelayLeft::TestDelayShorterThanDownstream), "TestDelayShorterThanDownstream");
    AddTest(MakeFunctor(*this, &SuiteVariableDelayLeft::TestReportsDelayToObserver), "TestReportsDelayToObserver");
    AddTest(MakeFunctor(*this, &SuiteVariableDelayLeft::TestUpstreamDiscardWhenDelayReduced), "TestUpstreamDiscardWhenDelayReduced");
}

void SuiteVariableDelayLeft::DoSetup()
{
    iVariableDelay = new VariableDelayLeft(*iMsgFactory, *this, kRampDuration, kDownstreamDelay);
    static_cast<VariableDelayLeft*>(iVariableDelay)->SetObserver(*this);
    iDelayAppliedJiffies = UINT_MAX;
}

void SuiteVariableDelayLeft::NotifyDelayApplied(TUint aJiffies)
{
    iDelayAppliedJiffies = aJiffies;
}

void SuiteVariableDelayLeft::TestAllMsgsPass()
{
    /* 'AllMsgs' excludes encoded & playable audio - VariableDelay is assumed only
       useful to the portion of the pipeline that deals in decoded audio */
    static const EMsgType msgs[] = { EMsgMode, EMsgTrack, EMsgDrain, EMsgEncodedStream,
                                     EMsgMetaText, EMsgStreamInterrupted, EMsgDecodedStream,
                                     EMsgAudioPcm, EMsgAudioDsd, EMsgSilence,
                                     EMsgHalt, EMsgFlush, EMsgWait, EMsgDelay, EMsgQuit };
    for (TUint i=0; i<sizeof(msgs)/sizeof(msgs[0]); i++) {
        PullNext(msgs[i]);
    }
}

void SuiteVariableDelayLeft::TestDelayFromRunning()
{
    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    TEST(iVariableDelay->iStatus == VariableDelayBase::EStarting);
    PullNext(EMsgAudioPcm);
    static const TUint kDelay = 60 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay;
    PullNext(EMsgDelay);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERampingDown);

    iJiffies = 0;
    do {
        PullNext(EMsgAudioPcm);
    } while (iVariableDelay->iStatus == VariableDelayBase::ERampingDown);
    TEST(iJiffies == kRampDuration);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERampedDown);

    iJiffies = 0;
    while (iJiffies < kDelay - kDownstreamDelay) {
        PullNext(EMsgSilence);
    }
    TEST(iJiffies == kDelay - kDownstreamDelay);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERampingUp);

    iJiffies = 0;
    do {
        PullNext(EMsgAudioPcm);
    } while (iVariableDelay->iStatus == VariableDelayBase::ERampingUp);
    TEST(iJiffies == kRampDuration);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERunning);
}

void SuiteVariableDelayLeft::TestDelayFromStarting()
{
    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    TEST(iVariableDelay->iStatus == VariableDelayBase::EStarting);
    static const TUint kDelay = 60 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay;
    PullNext(EMsgDelay);
    TEST(iVariableDelay->iStatus == VariableDelayBase::EStarting);

    iJiffies = 0;
    iNextGeneratedMsg = EMsgAudioPcm;
    while (iJiffies < kDelay - kDownstreamDelay) {
        PullNext();
    }
    TEST(iJiffies == kDelay - kDownstreamDelay);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERunning);
    PullNext(EMsgAudioPcm);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERunning);
}

void SuiteVariableDelayLeft::TestReduceDelayFromRunning()
{
    TestDelayFromStarting();
    static const TUint kDelay = 40 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay;
    while (!iVariableDelay->iQueue.IsEmpty()) {
        PullNext();
    }
    PullNext(EMsgDelay);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERampingDown);

    iJiffies = 0;
    do {
        PullNext(EMsgAudioPcm);
    } while (iVariableDelay->iStatus == VariableDelayBase::ERampingDown);
    TEST(iJiffies == kRampDuration);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERampedDown);

    iJiffies = 0;
    const TUint64 prevOffset = iTrackOffset;
    const TUint queuedAudio = (TUint)iTrackOffset - iJiffiesAudioPcm;
    iNextGeneratedMsg = EMsgAudioPcm;
    PullNext();
    TEST(iLastMsg == EMsgDecodedStream);
    PullNext(EMsgAudioPcm);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERampingUp);

    do {
        PullNext(EMsgAudioPcm);
    } while (iVariableDelay->iStatus == VariableDelayBase::ERampingUp);
    TEST(iJiffies == kRampDuration);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERunning);
    while (!iVariableDelay->iQueue.IsEmpty()) {
        PullNext();
    }
    TUint audioGenerated = (TUint)(iTrackOffset - prevOffset);
    TEST(audioGenerated - iJiffies + queuedAudio == 20 * Jiffies::kPerMs);
}

void SuiteVariableDelayLeft::TestChangeDelayWhileRampingDown()
{
    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    TEST(iVariableDelay->iStatus == VariableDelayBase::EStarting);
    PullNext(EMsgAudioPcm);
    static const TUint kDelay = 60 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay;
    PullNext(EMsgDelay);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERampingDown);

    iJiffies = 0;
    PullNext(EMsgAudioPcm);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERampingDown);
    static const TUint kDelay2 = 50 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay2;
    PullNext(EMsgDelay);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERampingDown);
    do {
        PullNext(EMsgAudioPcm);
    } while (iVariableDelay->iStatus == VariableDelayBase::ERampingDown);
    TEST(iJiffies == kRampDuration);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERampedDown);
}

void SuiteVariableDelayLeft::TestChangeDelayWhileRampingUp()
{
    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    TEST(iVariableDelay->iStatus == VariableDelayBase::EStarting);
    PullNext(EMsgAudioPcm);
    static const TUint kDelay = 60 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay;
    PullNext(EMsgDelay);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERampingDown);

    iJiffies = 0;
    do {
        PullNext(EMsgAudioPcm);
    } while (iVariableDelay->iStatus == VariableDelayBase::ERampingDown);
    TEST(iJiffies == kRampDuration);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERampedDown);

    iJiffies = 0;
    while (iJiffies < kDelay - kDownstreamDelay) {
        PullNext(EMsgSilence);
    }
    TEST(iJiffies == kDelay - kDownstreamDelay);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERampingUp);
    PullNext(EMsgAudioPcm);
    static const TUint kDelay2 = 70 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay2;
    const TUint remainingRamp = iVariableDelay->iRemainingRampSize;
    PullNext(EMsgDelay);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERampingDown);
    TEST(iVariableDelay->iRemainingRampSize == iVariableDelay->iRampDuration - remainingRamp);
}

void SuiteVariableDelayLeft::TestNoSilenceInjectedBeforeDecodedStream()
{
    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    PullNext(EMsgDrain);
    static const TUint kDelay = 150 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay;
    PullNext(EMsgDelay);
    PullNext(EMsgTrack);
}

void SuiteVariableDelayLeft::TestDelayAppliedAfterDrain()
{
    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    TEST(iVariableDelay->iStatus == VariableDelayBase::EStarting);
    static const TUint kDelay = 40 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay;
    PullNext(EMsgDelay);
    TEST(iVariableDelay->iStatus == VariableDelayBase::EStarting);

    iJiffies = 0;
    iNextGeneratedMsg = EMsgAudioPcm;
    while (iJiffies < kDelay - kDownstreamDelay) {
        PullNext();
    }
    PullNext(EMsgAudioPcm);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERunning);
    PullNext(EMsgDrain);
    iNextGeneratedMsg = EMsgSilence;
    PullNext();
    TEST(iLastMsg == EMsgSilence);
    iNextGeneratedMsg = EMsgAudioPcm;
    iJiffies = 0;
    while (iJiffies < kDelay - kDownstreamDelay) {
        PullNext();
        TEST(iLastMsg == EMsgSilence);
    }
    TEST(iJiffies == kDelay - kDownstreamDelay);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERampingUp);
    PullNext(EMsgAudioPcm);
}

void SuiteVariableDelayLeft::TestDelayShorterThanDownstream()
{
    PullNext(EMsgMode);
    static const TUint kDelay = 40 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay;
    PullNext(EMsgDelay);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    iNextGeneratedMsg = EMsgAudioPcm;
    do {
        PullNext();
    } while (iLastMsg == EMsgSilence);
    PullNext(EMsgAudioPcm);

    iNextDelayAbsoluteJiffies = kDownstreamDelay - Jiffies::kPerMs;
    PullNext(EMsgDelay);
    TEST(iLastPulledDelay == iNextDelayAbsoluteJiffies);
    if (iLastPulledDelay != iNextDelayAbsoluteJiffies) {
        Print("Expected %ums, got %u\n", Jiffies::ToMs(iNextDelayAbsoluteJiffies), Jiffies::ToMs(iLastPulledDelay));
    }
    // we previously had some delay applied.  iVariableDelay should ramp down, discard then ramp up.  No silence should be output
    iNextGeneratedMsg = EMsgAudioPcm;
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERampingDown);
    TBool pulledDecodedStream = false;
    TBool tryPullDecodedStream = false;
    while (iVariableDelay->iStatus != VariableDelayBase::ERunning) {
        PullNext();
        if (iVariableDelay->iStatus == VariableDelayBase::ERampingUp && !pulledDecodedStream) {
            if (!tryPullDecodedStream) {
                tryPullDecodedStream = true;
            }
            else {
                TEST(iLastMsg == EMsgDecodedStream);
                pulledDecodedStream = true;
                iJiffies = 0;
            }
        }
        else {
            TEST(iLastMsg == EMsgAudioPcm);
            if (iLastMsg != EMsgAudioPcm) {
                Print("Expected %u, got %u\n", EMsgAudioPcm, iLastMsg);
            }
        }
    }
    PullNext(EMsgAudioPcm); // pull any msg fragment remaining following ramp up

    // set another delay lower than downstream.  iVariableDelay is already at zero delay so remain in state Running
    iNextDelayAbsoluteJiffies -= Jiffies::kPerMs;
    PullNext(EMsgDelay);
    TEST(iLastPulledDelay == iNextDelayAbsoluteJiffies);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERunning);
    PullNext(EMsgAudioPcm);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERunning);
}

void SuiteVariableDelayLeft::TestReportsDelayToObserver()
{
    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);

    static const TUint kDelay = kDownstreamDelay + 15 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay;
    PullNext(EMsgDelay);
    do {
        PullNext();
    } while (iLastMsg == EMsgSilence);
    TEST(iLastMsg == EMsgAudioPcm);
    TEST(iDelayAppliedJiffies == kDelay - kDownstreamDelay);
}

void SuiteVariableDelayLeft::TestUpstreamDiscardWhenDelayReduced()
{
    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    PullNext(EMsgAudioPcm);
    static const TUint kDelay = 100 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay;
    PullNext(EMsgDelay);

    iJiffies = 0;
    do {
        PullNext(EMsgAudioPcm);
    } while (iVariableDelay->iStatus == VariableDelayBase::ERampingDown);
    iJiffies = 0;
    do {
        PullNext();
    } while (iLastMsg == EMsgSilence);
    iJiffies = 0;
    do {
        PullNext(EMsgAudioPcm);
    } while (iVariableDelay->iStatus == VariableDelayBase::ERampingUp);
    while (!iVariableDelay->iQueue.IsEmpty()) {
        PullNext();
    }

    static const TUint kDelayReduction = 20 * Jiffies::kPerMs;
    static const TUint kDelay2 = kDelay - kDelayReduction;
    iNextDelayAbsoluteJiffies = kDelay2;
    iNextDiscardFlushId = 42;
    PullNext(EMsgDelay);

    iJiffies = 0;
    do {
        PullNext(EMsgAudioPcm);
    } while (iVariableDelay->iStatus == VariableDelayBase::ERampingDown);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERampedDown);
    iNextGeneratedMsg = EMsgFlush;
    PullNext();
    TEST(iLastMsg == EMsgFlush);
    PullNext();
    TEST(iLastMsg == EMsgDecodedStream);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERampingUp);
    iJiffies = 0;
    do {
        PullNext(EMsgAudioPcm);
    } while (iVariableDelay->iStatus == VariableDelayBase::ERampingUp);
    TEST(iVariableDelay->iStatus == VariableDelayBase::ERunning);
}


// SuiteVariableDelayRight

SuiteVariableDelayRight::SuiteVariableDelayRight()
    : SuiteVariableDelay("VariableDelayRight")
{
    AddTest(MakeFunctor(*this, &SuiteVariableDelayRight::TestDelayShorterThanMinimum), "TestDelayShorterThanMinimum");
    AddTest(MakeFunctor(*this, &SuiteVariableDelayRight::TestAnimatorCalledOnStreamChange), "TestAnimatorCalledOnStreamChange");
    AddTest(MakeFunctor(*this, &SuiteVariableDelayRight::TestClockPuller), "TestClockPuller");
}

void SuiteVariableDelayRight::DoSetup()
{
    auto variableDelay = new VariableDelayRight(*iMsgFactory, *this, kRampDuration, kMinDelay);
    variableDelay->SetAnimator(*this);
    iVariableDelay = variableDelay;
    iAnimatorDelayJiffies = 0;
    iNumAnimatorDelayJiffiesCalls = 0;
}

TUint SuiteVariableDelayRight::PipelineAnimatorBufferJiffies() const
{
    ASSERTS();
    return 0;
}

TUint SuiteVariableDelayRight::PipelineAnimatorDelayJiffies(AudioFormat /*aFormat*/, TUint /*aSampleRate*/, TUint /*aBitDepth*/, TUint /*aNumChannels*/) const
{
    iNumAnimatorDelayJiffiesCalls++;
    return iAnimatorDelayJiffies;
}

TUint SuiteVariableDelayRight::PipelineAnimatorDsdBlockSizeWords() const
{
    return 1;
}

TUint SuiteVariableDelayRight::PipelineAnimatorMaxBitDepth() const
{
    return 24;
}

void SuiteVariableDelayRight::PipelineAnimatorGetMaxSampleRates(TUint& aPcm, TUint& aDsd) const
{
    aPcm = 192000;
    aDsd = 5644800;
}

void SuiteVariableDelayRight::TestDelayShorterThanMinimum()
{
    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    static const TUint kDelay = kMinDelay - Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay;
    PullNext(EMsgDelay);
    while (iJiffies < kMinDelay) {
        PullNext();
        TEST(iLastMsg == EMsgSilence);
    }
    PullNext(EMsgAudioPcm);
    TEST(iLastMsg == EMsgAudioPcm);
}

void SuiteVariableDelayRight::TestAnimatorCalledOnStreamChange()
{
    TEST(iNumAnimatorDelayJiffiesCalls == 0);
    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    PullNext(EMsgDecodedStream);
    TEST(iNumAnimatorDelayJiffiesCalls == 1);

    static const TUint kDelay = kMinDelay - Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay;
    PullNext(EMsgDelay);
    while (iJiffies < kMinDelay) {
        PullNext();
        TEST(iLastMsg == EMsgSilence);
    }
    PullNext(EMsgAudioPcm);
    TEST(iLastMsg == EMsgAudioPcm);

    TEST(iNumAnimatorDelayJiffiesCalls == 1);
}

void SuiteVariableDelayRight::TestClockPuller()
{
    iNextModeClockPuller = this;
    PullNext(EMsgMode);
    PullNext(EMsgTrack);
    TEST(iClockPullStopCount == 0);
    PullNext(EMsgDecodedStream);
    TEST(iClockPullStartCount == 0);
    TEST(iClockPullStopCount == 1);

    static const TUint kDelay = 20 * Jiffies::kPerMs;
    iNextDelayAbsoluteJiffies = kDelay;
    PullNext(EMsgDelay);
    for (;;) {
        PullNext();
        if (iLastMsg != EMsgSilence) {
            break;
        }
        TEST(iClockPullStopCount == 2);
    }
    TEST(iClockPullStartCount == 1);
    TEST(iClockPullStopCount == 2);
}


void TestVariableDelay()
{
    Runner runner("Variable delay tests\n");
    runner.Add(new SuiteVariableDelayLeft());
    runner.Add(new SuiteVariableDelayRight());
    runner.Run();
}

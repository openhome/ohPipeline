#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/SuiteUnitTest.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Media/Pipeline/Pipeline.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Pipeline/VolumeRamper.h>
#include <OpenHome/Media/Pipeline/MuterVolume.h>
#include <OpenHome/Media/Pipeline/StarterTimed.h>
#include <OpenHome/Media/PipelineObserver.h>
#include <OpenHome/Media/Utils/AllocatorInfoLogger.h>
#include <OpenHome/ThreadPool.h>

using namespace OpenHome;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Media;

namespace OpenHome {
namespace Media {

class SuitePipelineConfig : public Suite
                          , private IMsgProcessor
                          , private IStreamPlayObserver
                          , private ISeekRestreamer
                          , private IUrlBlockWriter
                          , private IPipelineAnimator
                          , private IVolumeRamper
{
public:
    SuitePipelineConfig(Environment& aEnv);
    ~SuitePipelineConfig();
private: // from Suite
    void Test() override;
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
private: // from IStreamPlayObserver
    void NotifyTrackFailed(TUint aTrackId) override;
    void NotifyStreamPlayStatus(TUint aTrackId, TUint aStreamId, EStreamPlay aStatus) override;
private: // from ISeekRestreamer
    TUint SeekRestream(const Brx& aMode, TUint aTrackId) override;
private: // from IUrlBlockWriter
    TBool TryGet(IWriter& aWriter, const Brx& aUrl, TUint64 aOffset, TUint aBytes) override;
private: // from IPipelineAnimator
    TUint PipelineAnimatorBufferJiffies() const override;
    TUint PipelineAnimatorDelayJiffies(AudioFormat aFormat, TUint aSampleRate, TUint aBitDepth, TUint aNumChannels) const override;
    TUint PipelineAnimatorDsdBlockSizeWords() const override;
    TUint PipelineAnimatorMaxBitDepth() const override;
    void PipelineAnimatorGetMaxSampleRates(TUint& aPcm, TUint& aDsd) const override;
private: // from IVolumeRamper
    void ApplyVolumeMultiplier(TUint aValue) override;
private:
    enum EMsgType
    {
        ENone
       ,EMsgMode
       ,EMsgDrain
       ,EMsgDecodedStream
       ,EMsgHalt
       ,EMsgPlayable
       ,EMsgQuit
    };
private:
    void RunTest(PipelineInitParams* aInitParams);
    void PullNext(EMsgType aExpectedMsg);
    void TimerCallback();
    Msg* CreateMsgSilence();
private:
    void TestMsgDrainFollowsHalt();
    void TestBlocksWaitingForDrainResponse();
    void TestDrainAfterStarvation();
    void TestOneDrainAfterHaltAndStarvation();
private:
    AllocatorInfoLogger iInfoAggregator;
    TrackFactory* iTrackFactory;
    MsgFactory* iMsgFactory;
    AudioTimeCpu* iAudioTime;
    NullPipelineObserver iPipelineObserver;
    EMsgType iLastPulledMsg;
    VolumeRamperStub iVolumeRamper;

};

} // namespace Media
} // namespace OpenHome


SuitePipelineConfig::SuitePipelineConfig(Environment& aEnv)
    : Suite("PipelineConfig")
{
    iTrackFactory = new TrackFactory(iInfoAggregator, 1);
    iAudioTime = new AudioTimeCpu(aEnv);
    MsgFactoryInitParams init;
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
}

SuitePipelineConfig::~SuitePipelineConfig()
{
    delete iMsgFactory;
    delete iTrackFactory;
    delete iAudioTime;
}

void SuitePipelineConfig::Test()
{
    EPipelineSupportElements elems[] = { EPipelineSupportElementsMandatory,
                                         EPipelineSupportElementsLogger,
                                         EPipelineSupportElementsDecodedAudioValidator,
                                         EPipelineSupportElementsRampValidator,
                                         EPipelineSupportElementsValidatorMinimal,
                                         EPipelineSupportElementsAudioDumper,
                                         };
    const TUint num_elems = sizeof(elems) / sizeof(elems[0]);
    for (TUint i=0; i<num_elems; i++) {
        auto initParams = PipelineInitParams::New();
        initParams->SetSupportElements(elems[i]);
        RunTest(initParams);
    }

    auto initParams = PipelineInitParams::New();
    if (initParams->Muter() == PipelineInitParams::MuterImpl::eRampSamples) {
        initParams->SetMuter(PipelineInitParams::MuterImpl::eRampVolume);
    }
    else {
        initParams->SetMuter(PipelineInitParams::MuterImpl::eRampSamples);
    }
    RunTest(initParams);
}

void SuitePipelineConfig::RunTest(PipelineInitParams* aInitParams)
{
    Pipeline* pipeline = new Pipeline(aInitParams, iInfoAggregator, *iTrackFactory, iPipelineObserver, *this, *this, *this, *iAudioTime);
    pipeline->Start(*this, iVolumeRamper);
    pipeline->Push(iMsgFactory->CreateMsgQuit());
    Msg* msg = pipeline->Pull();
    msg = msg->Process(*this);
    msg->RemoveRef();
    TEST(iLastPulledMsg == EMsgQuit);
    delete pipeline;
}

Msg* SuitePipelineConfig::ProcessMsg(MsgMode* aMsg)
{
    iLastPulledMsg = EMsgMode;
    return aMsg;
}

Msg* SuitePipelineConfig::ProcessMsg(MsgTrack* aMsg)               { ASSERTS(); return aMsg; }

Msg* SuitePipelineConfig::ProcessMsg(MsgDrain* aMsg)
{
    iLastPulledMsg = EMsgDrain;
    aMsg->ReportDrained();
    return aMsg;
}

Msg* SuitePipelineConfig::ProcessMsg(MsgDelay* aMsg)               { ASSERTS(); return aMsg; }
Msg* SuitePipelineConfig::ProcessMsg(MsgEncodedStream* aMsg)       { ASSERTS(); return aMsg; }
Msg* SuitePipelineConfig::ProcessMsg(MsgStreamSegment* aMsg)       { ASSERTS(); return aMsg; }
Msg* SuitePipelineConfig::ProcessMsg(MsgAudioEncoded* aMsg)        { ASSERTS(); return aMsg; }
Msg* SuitePipelineConfig::ProcessMsg(MsgMetaText* aMsg)            { ASSERTS(); return aMsg; }
Msg* SuitePipelineConfig::ProcessMsg(MsgStreamInterrupted* aMsg)   { ASSERTS(); return aMsg; }

Msg* SuitePipelineConfig::ProcessMsg(MsgHalt* aMsg)
{
    iLastPulledMsg = EMsgHalt;
    aMsg->ReportHalted();
    return aMsg;
}

Msg* SuitePipelineConfig::ProcessMsg(MsgFlush* aMsg)               { ASSERTS(); return aMsg; }
Msg* SuitePipelineConfig::ProcessMsg(MsgWait* aMsg)                { ASSERTS(); return aMsg; }

Msg* SuitePipelineConfig::ProcessMsg(MsgDecodedStream* aMsg)
{
    iLastPulledMsg = EMsgDecodedStream;
    return aMsg;
}

Msg* SuitePipelineConfig::ProcessMsg(MsgAudioPcm* aMsg)            { ASSERTS(); return aMsg; }
Msg* SuitePipelineConfig::ProcessMsg(MsgAudioDsd* aMsg)            { ASSERTS(); return aMsg; }
Msg* SuitePipelineConfig::ProcessMsg(MsgSilence* aMsg)             { ASSERTS(); return aMsg; }

Msg* SuitePipelineConfig::ProcessMsg(MsgPlayable* aMsg)
{
    iLastPulledMsg = EMsgPlayable;
    return aMsg;
}

Msg* SuitePipelineConfig::ProcessMsg(MsgQuit* aMsg)
{
    iLastPulledMsg = EMsgQuit;
    return aMsg;
}

void SuitePipelineConfig::NotifyTrackFailed(TUint /*aTrackId*/)
{
}

void SuitePipelineConfig::NotifyStreamPlayStatus(TUint /*aTrackId*/, TUint /*aStreamId*/, EStreamPlay /*aStatus*/)
{
}

TUint SuitePipelineConfig::SeekRestream(const Brx& /*aMode*/, TUint /*aTrackId*/)
{
    return MsgFlush::kIdInvalid;
}

TBool SuitePipelineConfig::TryGet(IWriter& /*aWriter*/, const Brx& /*aUrl*/, TUint64 /*aOffset*/, TUint /*aBytes*/)
{
    return false;
}

TUint SuitePipelineConfig::PipelineAnimatorBufferJiffies() const
{
    return 0;
}

TUint SuitePipelineConfig::PipelineAnimatorDelayJiffies(AudioFormat /*aFormat*/, TUint /*aSampleRate*/, TUint /*aBitDepth*/, TUint /*aNumChannels*/) const
{
    return 0;
}

TUint SuitePipelineConfig::PipelineAnimatorDsdBlockSizeWords() const
{
    return 1;
}

TUint SuitePipelineConfig::PipelineAnimatorMaxBitDepth() const
{
    return 24;
}

void SuitePipelineConfig::PipelineAnimatorGetMaxSampleRates(TUint& aPcm, TUint& aDsd) const
{
    aPcm = 192000;
    aDsd = 5644800;
}

void SuitePipelineConfig::ApplyVolumeMultiplier(TUint /*aValue*/)
{
}


void TestPipelineConfig(Environment& aEnv)
{
    Runner runner("Pipeline configuration tests\n");
    runner.Add(new SuitePipelineConfig(aEnv));
    runner.Run();
}

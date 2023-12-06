#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Media/Pipeline/PreDriver.h>
#include <OpenHome/Media/Pipeline/StarterTimed.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Utils/AllocatorInfoLogger.h>
#include <OpenHome/Media/Utils/ProcessorAudioUtils.h>
#include <OpenHome/Media/ClockPuller.h>

#include <string.h>
#include <vector>

using namespace OpenHome;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Media;

namespace OpenHome {
namespace Media {

class SuitePreDriver : public Suite, private IPipelineElementUpstream, private IMsgProcessor
{
    static const TUint kMsgFormatCount = 2;

    static const TUint kSampleRate  = 44100;
public:
    SuitePreDriver();
    ~SuitePreDriver();
    void Test() override;
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
private:
    enum EMsgType
    {
        ENone
       ,EMsgMode
       ,EMsgTrack
       ,EMsgDrain
       ,EMsgMetaText
       ,EMsgEncodedStream
       ,EMsgStreamInterrupted
       ,EMsgHalt
       ,EMsgFlush
       ,EMsgWait
       ,EMsgDecodedStream
       ,EMsgAudioPcm
       ,EMsgAudioDsd
       ,EMsgSilence
       ,EMsgPlayable
       ,EMsgQuit
    };
private:
    MsgAudioPcm* CreateAudio();
    MsgAudioDsd* CreateAudioDsd();
private:
    MsgFactory* iMsgFactory;
    TrackFactory* iTrackFactory;
    AllocatorInfoLogger iInfoAggregator;
    PreDriver* iPreDriver;
    ClockPullerMock iClockPuller;
    EMsgType iNextGeneratedMsg;
    std::vector<EMsgType> iNextGeneratedMsgs;
    EMsgType iLastMsg;
    TUint64 iTrackOffset;
    TUint iSampleRate;
    TUint iBitDepth;
    TUint iNumChannels;
    SpeakerProfile iProfile;
    TUint iAudioMsgSizeJiffies;
    TUint iNextMsgSilenceSize;
    TBool iNextModePullable;
    Optional<IAudioTime> iAudioTimeOpt;
};

} // namespace Media
} // namespace OpenHome


// SuitePreDriver

SuitePreDriver::SuitePreDriver()
    : Suite("Pre-Driver tests")
    , iLastMsg(ENone)
    , iTrackOffset(0)
    , iNumChannels(2)
{
    MsgFactoryInitParams init;
    init.SetMsgAudioPcmCount(10, 10);
    init.SetMsgSilenceCount(10);
    init.SetMsgPlayableCount(10, 1, 10);
    init.SetMsgDecodedStreamCount(kMsgFormatCount);
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
    iTrackFactory = new TrackFactory(iInfoAggregator, 1);
    MsgAudioPcm* audio = CreateAudio();
    iAudioMsgSizeJiffies = audio->Jiffies();
    audio->RemoveRef();
    iNextMsgSilenceSize = iAudioMsgSizeJiffies;
    iPreDriver = new PreDriver(*this, iAudioTimeOpt);
    iNextModePullable = false;
}

SuitePreDriver::~SuitePreDriver()
{
    delete iPreDriver;
    delete iMsgFactory;
    delete iTrackFactory;
}

void SuitePreDriver::Test()
{
    iSampleRate = 44100;
    iBitDepth = 16;

    // Send Format msg; check it is passed on.
    iNextGeneratedMsg = EMsgDecodedStream;
    Msg* msg = iPreDriver->Pull();
    msg = msg->Process(*this);
    msg->RemoveRef();
    TEST(iLastMsg == EMsgDecodedStream);

    // Send Audio; check it is passed on.
    iNextGeneratedMsg = EMsgAudioPcm;
    iPreDriver->Pull()->Process(*this)->RemoveRef();
    TEST(iLastMsg == EMsgPlayable);

    // Send DSD; check it is passed on.
    iNextGeneratedMsg = EMsgAudioDsd;
    iPreDriver->Pull()->Process(*this)->RemoveRef();
    TEST(iLastMsg == EMsgPlayable);

    // Send Silence; check it is passed on.
    iNextGeneratedMsg = EMsgSilence;
    iPreDriver->Pull()->Process(*this)->RemoveRef();
    TEST(iLastMsg == EMsgPlayable);

    // Send Quit; check it is passed on.
    iNextGeneratedMsg = EMsgQuit;
    iPreDriver->Pull()->Process(*this)->RemoveRef();
    TEST(iLastMsg == EMsgQuit);

    // Send Mode; check it is passed on.
    iNextGeneratedMsg = EMsgMode;
    iNextModePullable = true;
    iPreDriver->Pull()->Process(*this)->RemoveRef();
    TEST(iLastMsg == EMsgMode);

    // Send Drain; check it is passed on
    iNextGeneratedMsg = EMsgDrain;
    iPreDriver->Pull()->Process(*this)->RemoveRef();
    TEST(iLastMsg == EMsgDrain);

    // Send Format with same sample rate + bit depth + no. channels.  Check it isn't passed on (we move on to Silence instead).
    iNextGeneratedMsg = EMsgDecodedStream;
    iPreDriver->Pull()->Process(*this)->RemoveRef();
    TEST(iLastMsg == EMsgPlayable);

    // Send Halt; check it is passed on.
    iNextGeneratedMsg = EMsgHalt;
    iPreDriver->Pull()->Process(*this)->RemoveRef();
    TEST(iLastMsg == EMsgHalt);

    // Send StreamInterrupted; check it is consumed and the msg that follows is passed on
    iNextGeneratedMsg = EMsgStreamInterrupted;
    iPreDriver->Pull()->Process(*this)->RemoveRef();
    TEST(iLastMsg == EMsgPlayable);

    // Send Audio then Format with different sample rate.
    iNextGeneratedMsg = EMsgAudioPcm;
    iPreDriver->Pull()->Process(*this)->RemoveRef();
    TEST(iLastMsg == EMsgPlayable);
    iSampleRate = 48000;
    iNextGeneratedMsg = EMsgDecodedStream;
    iPreDriver->Pull()->Process(*this)->RemoveRef();
    TEST(iLastMsg == EMsgDecodedStream);

    // Mode (non-pullable) -> Mode (pullable) - duplicate DecodedStream suppressed
    iNextGeneratedMsg = EMsgMode;
    iNextModePullable = false;
    iPreDriver->Pull()->Process(*this)->RemoveRef();
    TEST(iLastMsg == EMsgMode);
    iSampleRate = 44100;
    iNextGeneratedMsg = EMsgDecodedStream;
    iPreDriver->Pull()->Process(*this)->RemoveRef();
    TEST(iLastMsg == EMsgDecodedStream);
    iNextModePullable = true;
    iNextGeneratedMsg = EMsgMode;
    iPreDriver->Pull()->Process(*this)->RemoveRef();
    TEST(iLastMsg == EMsgMode);
    iNextGeneratedMsg = EMsgDecodedStream;
    iPreDriver->Pull()->Process(*this)->RemoveRef();
    TEST(iLastMsg == EMsgPlayable);

    // Mode (pullable) -> Mode (non-pullable) - duplicate DecodedStream passed on
    iNextGeneratedMsg = EMsgMode;
    iNextModePullable = false;
    iPreDriver->Pull()->Process(*this)->RemoveRef();
    TEST(iLastMsg == EMsgMode);
    iNextGeneratedMsg = EMsgDecodedStream;
    iPreDriver->Pull()->Process(*this)->RemoveRef();
    TEST(iLastMsg == EMsgDecodedStream);

    // Mode, DecodedStream, Mode (identical), DecodedStream (identical) - duplicate Mode + DecodedStream pruned
    iNextModePullable = true;
    iNextGeneratedMsgs.push_back(EMsgMode);
    iSampleRate = 96000;
    iNextGeneratedMsgs.push_back(EMsgDecodedStream);
    iNextGeneratedMsgs.push_back(EMsgMode);
    iNextGeneratedMsgs.push_back(EMsgDecodedStream);
    iPreDriver->Pull()->Process(*this)->RemoveRef();
    TEST(iLastMsg == EMsgMode);
    iPreDriver->Pull()->Process(*this)->RemoveRef();
    TEST(iLastMsg == EMsgDecodedStream);
    iPreDriver->Pull()->Process(*this)->RemoveRef();
    TEST(iLastMsg == EMsgPlayable);
}

Msg* SuitePreDriver::Pull()
{
    auto nextMsg = iNextGeneratedMsg;
    if (iNextGeneratedMsgs.size() > 0) {
        nextMsg = iNextGeneratedMsgs[0];
        iNextGeneratedMsgs.erase(iNextGeneratedMsgs.begin(), iNextGeneratedMsgs.begin()+1);
    }
    switch (nextMsg)
    {
    case EMsgMode:
    {
        ModeInfo info;
        ModeTransportControls transportControls;
        IClockPuller* puller = iNextModePullable ? &iClockPuller : nullptr;
        return iMsgFactory->CreateMsgMode(puller? Brn("dummyMode1") : Brn("dummyMode2"), info, puller, transportControls);
    }
    case EMsgDrain:
        return iMsgFactory->CreateMsgDrain(Functor());
    case EMsgEncodedStream:
        iNextGeneratedMsg = EMsgAudioPcm;
        return iMsgFactory->CreateMsgEncodedStream(Brn("http://1.2.3.4:5"), Brn("metatext"), 0, 0, 0, false, false, Multiroom::Allowed, nullptr);
    case EMsgMetaText:
        iNextGeneratedMsg = EMsgAudioPcm;
        return iMsgFactory->CreateMsgMetaText(Brn("metatext"));
    case EMsgStreamInterrupted:
        iNextGeneratedMsg = EMsgAudioPcm;
        return iMsgFactory->CreateMsgStreamInterrupted();
    case EMsgHalt:
        return iMsgFactory->CreateMsgHalt();
    case EMsgFlush:
        return iMsgFactory->CreateMsgFlush(1);
    case EMsgWait:
        return iMsgFactory->CreateMsgWait();
    case EMsgDecodedStream:
        iNextGeneratedMsg = EMsgSilence;
        return iMsgFactory->CreateMsgDecodedStream(0, 128000, iBitDepth, iSampleRate, iNumChannels, Brn("dummy codec"), (TUint64)1<<31, 0, false, false, false, false, AudioFormat::Pcm, Multiroom::Allowed, iProfile, nullptr, RampType::Sample);
    case EMsgAudioPcm:
        return CreateAudio();
    case EMsgAudioDsd:
        return CreateAudioDsd();
    case EMsgSilence:
    {
        TUint size = iNextMsgSilenceSize;
        return iMsgFactory->CreateMsgSilence(size, iSampleRate, iBitDepth, iNumChannels);
    }
    case EMsgQuit:
        return iMsgFactory->CreateMsgQuit();
    default:
    case ENone:
    case EMsgPlayable:
        ASSERTS();
        return nullptr;
    }
}

MsgAudioPcm* SuitePreDriver::CreateAudio()
{
    static const TUint kDataBytes = 3 * 1024;
    TByte encodedAudioData[kDataBytes];
    (void)memset(encodedAudioData, 0xff, kDataBytes);
    Brn encodedAudioBuf(encodedAudioData, kDataBytes);
    MsgAudioPcm* audio = iMsgFactory->CreateMsgAudioPcm(encodedAudioBuf, iNumChannels, kSampleRate, 16, AudioDataEndian::Little, iTrackOffset);
    iTrackOffset += audio->Jiffies();
    return audio;
}

MsgAudioDsd* SuitePreDriver::CreateAudioDsd()
{
    TByte audioData[128];
    (void)memset(audioData, 0x7f, sizeof audioData);
    Brn audioBuf(audioData, sizeof audioData);
    MsgAudioDsd* audio = iMsgFactory->CreateMsgAudioDsd(audioBuf, 2, 2822400, 2, iTrackOffset, 0);
    iTrackOffset += audio->Jiffies();
    return audio;
}

Msg* SuitePreDriver::ProcessMsg(MsgMode* aMsg)
{
    iLastMsg = EMsgMode;
    return aMsg;
}

Msg* SuitePreDriver::ProcessMsg(MsgTrack* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuitePreDriver::ProcessMsg(MsgDrain* aMsg)
{
    iLastMsg = EMsgDrain;
    return aMsg;
}

Msg* SuitePreDriver::ProcessMsg(MsgDelay* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuitePreDriver::ProcessMsg(MsgEncodedStream* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuitePreDriver::ProcessMsg(MsgStreamSegment* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuitePreDriver::ProcessMsg(MsgAudioEncoded* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuitePreDriver::ProcessMsg(MsgMetaText* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuitePreDriver::ProcessMsg(MsgStreamInterrupted* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuitePreDriver::ProcessMsg(MsgHalt* aMsg)
{
    iLastMsg = EMsgHalt;
    return aMsg;
}

Msg* SuitePreDriver::ProcessMsg(MsgFlush* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuitePreDriver::ProcessMsg(MsgWait* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuitePreDriver::ProcessMsg(MsgDecodedStream* aMsg)
{
    TEST(aMsg->StreamInfo().BitDepth() == iBitDepth);
    TEST(aMsg->StreamInfo().SampleRate() == iSampleRate);
    TEST(aMsg->StreamInfo().NumChannels() == iNumChannels);
    iLastMsg = EMsgDecodedStream;
    return aMsg;
}

Msg* SuitePreDriver::ProcessMsg(MsgAudioPcm* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuitePreDriver::ProcessMsg(MsgAudioDsd* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuitePreDriver::ProcessMsg(MsgSilence* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SuitePreDriver::ProcessMsg(MsgPlayable* aMsg)
{
    iLastMsg = EMsgPlayable;
    return aMsg;
}

Msg* SuitePreDriver::ProcessMsg(MsgQuit* aMsg)
{
    iLastMsg = EMsgQuit;
    return aMsg;
}



void TestPreDriver()
{
    Runner runner("Pre-Driver tests\n");
    runner.Add(new SuitePreDriver());
    runner.Run();
}

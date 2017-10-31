#include <OpenHome/Media/Pipeline/PreDriver.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/ClockPuller.h>
#include <OpenHome/Media/Debug.h>

using namespace OpenHome;
using namespace OpenHome::Media;

// PreDriver

const TUint PreDriver::kSupportedMsgTypes =   eMode
                                            | eDrain
                                            | eStreamInterrupted
                                            | eHalt
                                            | eDecodedStream
                                            | eAudioPcm
                                            | eAudioDsd
                                            | eSilence
                                            | eQuit;

PreDriver::PreDriver(IPipelineElementUpstream& aUpstreamElement)
    : PipelineElement(kSupportedMsgTypes)
    , iUpstreamElement(aUpstreamElement)
    , iSampleRate(0)
    , iBitDepth(0)
    , iNumChannels(0)
    , iFormat(AudioFormat::Undefined)
    , iShutdownSem("PDSD", 0)
    , iSilenceSinceLastAudio(0)
    , iSilenceSinceAudio(false)
    , iModeHasPullableClock(false)
    , iQuit(false)
{
}

PreDriver::~PreDriver()
{
    iShutdownSem.Wait();
}

Msg* PreDriver::Pull()
{
    Msg* msg;
    do {
        msg = iUpstreamElement.Pull();
        ASSERT(msg != nullptr);
        const TBool silenceSincePcm = iSilenceSinceAudio;
        msg = msg->Process(*this);
        if (silenceSincePcm && !iSilenceSinceAudio) {
            const TUint ms = Jiffies::ToMs(iSilenceSinceLastAudio);
            iSilenceSinceLastAudio = 0;
            LOG(kPipeline, "PreDriver: silence since last audio - %ums\n", ms);
        }
        if (iQuit) {
            iShutdownSem.Signal();
        }

    } while (msg == nullptr);
    return msg;
}

Msg* PreDriver::ProcessMsg(MsgMode* aMsg)
{
    if (iModeHasPullableClock) {
        /* if we're changing from a mode that used a pullable clock, make sure the next
           DecodedStream is passed on.  Without this, we'd risk leaving the new mode
           playing at a skewed clock rate set by the previous clock puller. */
        iSampleRate = iBitDepth = iNumChannels = 0;
    }
    iModeHasPullableClock = aMsg->ClockPullers().Enabled();
    return aMsg;
}

Msg* PreDriver::ProcessMsg(MsgDrain* aMsg)
{
    iSilenceSinceLastAudio = 0;
    iSilenceSinceAudio = false;
    return aMsg;
}

Msg* PreDriver::ProcessMsg(MsgStreamInterrupted* aMsg)
{
    aMsg->RemoveRef();
    return nullptr;
}

Msg* PreDriver::ProcessMsg(MsgDecodedStream* aMsg)
{
    const DecodedStreamInfo& stream = aMsg->StreamInfo();
    if (stream.SampleRate()  == iSampleRate  &&
        stream.BitDepth()    == iBitDepth    &&
        stream.NumChannels() == iNumChannels &&
        stream.Format()      == iFormat) {
        // no change in format.  Discard this msg
        aMsg->RemoveRef();
        return nullptr;
    }
    iSampleRate = stream.SampleRate();
    iBitDepth = stream.BitDepth();
    iNumChannels = stream.NumChannels();
    iFormat = stream.Format();

    return aMsg;
}

Msg* PreDriver::ProcessMsg(MsgAudioPcm* aMsg)
{
    iSilenceSinceAudio = false;
    return aMsg->CreatePlayable();
}

Msg* PreDriver::ProcessMsg(MsgAudioDsd* aMsg)
{
    iSilenceSinceAudio = false;
    return aMsg->CreatePlayable();
}

Msg* PreDriver::ProcessMsg(MsgSilence* aMsg)
{
    iSilenceSinceAudio = true;
    iSilenceSinceLastAudio += aMsg->Jiffies();
    return aMsg->CreatePlayable();
}

Msg* PreDriver::ProcessMsg(MsgQuit* aMsg)
{
    iQuit = true;
    return aMsg;
}

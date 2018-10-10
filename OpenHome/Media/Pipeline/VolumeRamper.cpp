#include <OpenHome/Media/Pipeline/VolumeRamper.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Av/Debug.h>

using namespace OpenHome;
using namespace OpenHome::Media;

const TUint VolumeRamper::kSupportedMsgTypes =   eMode
                                               | eDrain
                                               | eStreamInterrupted
                                               | eHalt
                                               | eDecodedStream
                                               | eAudioPcm
                                               | eAudioDsd
                                               | eSilence
                                               | eQuit;

VolumeRamper::VolumeRamper(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstream)
    : PipelineElement(kSupportedMsgTypes)
    , iMsgFactory(aMsgFactory)
    , iUpstream(aUpstream)
    , iLock("MPMT")
    , iVolumeRamper(nullptr)
    , iMsgDrain(nullptr)
    , iMsgHalt(nullptr)
    , iHalting(false)
    , iHalted(false)
    , iEnabled(false)
{
}

VolumeRamper::~VolumeRamper()
{
    if (iMsgDrain != nullptr) {
        iMsgDrain->RemoveRef();
    }
    if (iMsgHalt != nullptr) {
        iMsgHalt->RemoveRef();
    }
}

void VolumeRamper::SetVolumeRamper(IVolumeRamper& aVolumeRamper)
{
    iVolumeRamper = &aVolumeRamper;
}

Msg* VolumeRamper::Pull()
{
    Msg* msg = iUpstream.Pull();
    AutoMutex _(iLock);
    iHalting = false;
    msg = msg->Process(*this);
    return msg;
}

Msg* VolumeRamper::ProcessMsg(MsgDrain* aMsg)
{
    ASSERT(iMsgDrain == nullptr);
    iHalting = true;
    iMsgDrain = aMsg;
    return iMsgFactory.CreateMsgDrain(MakeFunctor(*this, &VolumeRamper::Drained));
}

Msg* VolumeRamper::ProcessMsg(MsgHalt* aMsg)
{
    ASSERT(iMsgHalt == nullptr);
    iHalting = true;
    iMsgHalt = aMsg;
    return iMsgFactory.CreateMsgHalt(aMsg->Id(), MakeFunctor(*this, &VolumeRamper::Halted));
}

Msg* VolumeRamper::ProcessMsg(MsgDecodedStream* aMsg)
{
    auto& stream = aMsg->StreamInfo();
    iEnabled = (stream.AnalogBypass() || stream.Format() == AudioFormat::Dsd);
    return aMsg;
}

Msg* VolumeRamper::ProcessMsg(MsgAudioPcm* aMsg)
{
    ProcessAudio(aMsg);
    return aMsg;
}

Msg* VolumeRamper::ProcessMsg(MsgAudioDsd* aMsg)
{
    ProcessAudio(aMsg);
    return aMsg;
}

Msg* VolumeRamper::ProcessMsg(MsgSilence* aMsg)
{
    if (iEnabled) {
        iVolumeRamper->ApplyVolumeMultiplier(IVolumeRamper::kMultiplierZero);
    }
    return aMsg;
}

void VolumeRamper::ProcessAudio(MsgAudioDecoded* aMsg)
{
    if (iEnabled) {
        const TUint rampMultiplier = aMsg->MedianRampMultiplier();
        iVolumeRamper->ApplyVolumeMultiplier(rampMultiplier);
    }
    else if (iHalted) {
        LOG(kVolume, "VolumeRamper::ProcessAudio() iHalted rampMultiplier: %u\n", IVolumeRamper::kMultiplierFull);
        iHalted = false;
        iVolumeRamper->ApplyVolumeMultiplier(IVolumeRamper::kMultiplierFull);
    }
}

void VolumeRamper::Drained()
{
    AutoMutex _(iLock);
    CheckForHalted();
    ASSERT(iMsgDrain != nullptr);
    iMsgDrain->ReportDrained();
    iMsgDrain->RemoveRef();
    iMsgDrain = nullptr;
}

void VolumeRamper::Halted()
{
    AutoMutex _(iLock);
    CheckForHalted();
    ASSERT(iMsgHalt != nullptr);
    iMsgHalt->ReportHalted();
    iMsgHalt->RemoveRef();
    iMsgHalt = nullptr;
}

void VolumeRamper::CheckForHalted()
{
    if (iHalting) {
        LOG(kVolume, "VolumeRamper::CheckForHalted iHalting, rampMultiplier: %u\n", IVolumeRamper::kMultiplierZero);
        iHalted = true;
        iVolumeRamper->ApplyVolumeMultiplier(IVolumeRamper::kMultiplierZero);
    }
}

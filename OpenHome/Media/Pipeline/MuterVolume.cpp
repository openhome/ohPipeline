#include <OpenHome/Media/Pipeline/MuterVolume.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Functor.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Media;

const TUint MuterVolume::kSupportedMsgTypes =   eMode
                                              | eTrack
                                              | eDrain
                                              | eEncodedStream
                                              | eMetatext
                                              | eStreamInterrupted
                                              | eHalt
                                              | eDecodedStream
                                              | eAudioPcm
                                              | eAudioDsd
                                              | eSilence
                                              | eQuit;
const TUint MuterVolume::kJiffiesUntilMute = 10 * Jiffies::kPerMs;

MuterVolume::MuterVolume(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstream)
    : PipelineElement(kSupportedMsgTypes)
    , iMsgFactory(aMsgFactory)
    , iUpstream(aUpstream)
    , iVolumeMuter(nullptr)
    , iLock("MPMT")
    , iSemMuted("MPMT", 0)
    , iState(State::eRunning)
    , iMsgHalt(nullptr)
    , iHalted(true)
{
}

MuterVolume::~MuterVolume()
{
    if (iMsgHalt != nullptr) {
        iMsgHalt->RemoveRef();
    }
}

void MuterVolume::Start(IVolumeMuterStepped& aVolumeMuter)
{
    AutoMutex _(iLock);
    iVolumeMuter = &aVolumeMuter;
    if (iState == State::eMuted) {
        iVolumeMuter->SetMuted();
    }
}

void MuterVolume::Mute()
{
    LOG(kPipeline, "> MuterVolume::Mute\n");
    TBool block = false;
    {
        AutoMutex _(iLock);
        if (iVolumeMuter == nullptr) { // not yet Start()ed
            iState = State::eMuted;
        }
        else {
            switch (iState)
            {
            case State::eMutingRamp:
            case State::eMutingWait:
            case State::eMuted:
                break;
            case State::eRunning:
            case State::eUnmutingRamp:
                if (iHalted) {
                    iState = State::eMuted;
                    iVolumeMuter->SetMuted();
                }
                else if (iVolumeMuter->BeginMute() == IVolumeMuterStepped::Status::eComplete) {
                    iState = State::eMuted;
                }
                else {
                    iState = State::eMutingRamp;
                    block = true;
                }
                break;
            }

            if (block) {
                (void)iSemMuted.Clear();
            }
        }
    }
    if (block) {
        iSemMuted.Wait();
    }
    LOG(kPipeline, "< MuterVolume::Mute (block=%u)\n", block);
}

void MuterVolume::Unmute()
{
    LOG(kPipeline, "MuterVolume::Unmute\n");
    AutoMutex _(iLock);
    if (iVolumeMuter == nullptr) { // not yet Start()ed
        iState = State::eRunning;
    }
    else {
        switch (iState)
        {
        case State::eRunning:
        case State::eUnmutingRamp:
            break;
        case State::eMutingRamp:
        case State::eMutingWait:
            iSemMuted.Signal();
            // fall through
        case State::eMuted:
            if (iHalted) {
                iState = State::eRunning;
                iVolumeMuter->SetUnmuted();
            }
            else if (iVolumeMuter->BeginUnmute() == IVolumeMuterStepped::Status::eComplete) {
                iState = State::eRunning;
            }
            else {
                iState = State::eUnmutingRamp;
            }
            break;
        }
    }
}

Msg* MuterVolume::Pull()
{
    Msg* msg = iUpstream.Pull();
    iLock.Wait();
    msg = msg->Process(*this);
    iLock.Signal();
    return msg;
}

Msg* MuterVolume::ProcessMsg(MsgHalt* aMsg)
{
    ASSERT(iMsgHalt == nullptr);
    iMsgHalt = aMsg;
    return iMsgFactory.CreateMsgHalt(aMsg->Id(), MakeFunctor(*this, &MuterVolume::PipelineHalted));
}

Msg* MuterVolume::ProcessMsg(MsgAudioPcm* aMsg)
{
    iHalted = false;
    ProcessAudio(aMsg);
    return aMsg;
}

Msg* MuterVolume::ProcessMsg(MsgAudioDsd* aMsg)
{
    iHalted = false;
    ProcessAudio(aMsg);
    return aMsg;
}

Msg* MuterVolume::ProcessMsg(MsgSilence* aMsg)
{
    ProcessAudio(aMsg);
    return aMsg;
}

void MuterVolume::ProcessAudio(MsgAudio* aMsg)
{
    const auto jiffies = aMsg->Jiffies();
    switch (iState)
    {
    case State::eMutingRamp:
        if (iVolumeMuter->StepMute(jiffies) == IVolumeMuterStepped::Status::eComplete) {
            iState = State::eMutingWait;
            iJiffiesUntilMute = kJiffiesUntilMute;
        }
        break;
    case State::eUnmutingRamp:
        if (iVolumeMuter->StepUnmute(jiffies) == IVolumeMuterStepped::Status::eComplete) {
            iState = State::eRunning;
        }
        break;
    case State::eMutingWait:
        if (iJiffiesUntilMute > jiffies) {
            iJiffiesUntilMute -= jiffies;
        }
        else {
            iJiffiesUntilMute = 0;
            iState = State::eMuted;
            iSemMuted.Signal();
        }
        break;
    default:
        break;
    }
}

void MuterVolume::PipelineHalted()
{
    AutoMutex _(iLock);
    iHalted = true;
    iJiffiesUntilMute = 0;
    iSemMuted.Signal();
    switch (iState)
    {
    case State::eRunning:
        break;
    case State::eMutingRamp:
    case State::eMutingWait:
        iState = State::eMuted;
        iVolumeMuter->SetMuted();
        break;
    case State::eUnmutingRamp:
        iState = State::eRunning;
        iVolumeMuter->SetUnmuted();
        break;
    case State::eMuted:
        break;
    }

    ASSERT(iMsgHalt != nullptr);
    iMsgHalt->ReportHalted();
    iMsgHalt->RemoveRef();
    iMsgHalt = nullptr;
}

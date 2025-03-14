#include <OpenHome/Media/Pipeline/Brancher.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Types.h>

using namespace OpenHome;
using namespace OpenHome::Media;


// Branch

IBranch* Branch::Create(
    IPipelineElementDownstream& aDownstream,
    IBranchPreProcessor*        aPreProcessor,
    IBranchEnableProcessor*     aEnableProcessor,
    IBranchPostProcessor*       aPostProcessor)
{
    return new Branch(aDownstream, aPreProcessor, aEnableProcessor, aPostProcessor);
}

Branch::Branch(
    IPipelineElementDownstream& aDownstream,
    IBranchPreProcessor*        aPreProcessor,
    IBranchEnableProcessor*     aEnableProcessor,
    IBranchPostProcessor*       aPostProcessor)

    : iDownstream(aDownstream)
    , iPreProcessor(aPreProcessor)
    , iEnableProcessor(aEnableProcessor)
    , iPostProcessor(aPostProcessor)
{
}

void Branch::Push(Msg* aMsg)
{
    iDownstream.Push(aMsg);
}

Msg* Branch::PreProcess(Msg* aMsg)
{
    if (iPreProcessor != nullptr) {
        return aMsg->Process(*iPreProcessor);
    }
    return aMsg;

}

Msg* Branch::PostProcess(Msg* aMsg)
{
    if (iPostProcessor != nullptr) {
        return aMsg->Process(*iPostProcessor);
    }
    return aMsg;
}

TBool Branch::ProcessEnable(Msg* aMsg)
{
    if (iEnableProcessor != nullptr) {
        return iEnableProcessor->Peek(*aMsg);
    }
    return true;
}

// Brancher

Brancher::Brancher(IPipelineElementUpstream& aUpstream, const Brx& aId, EPriority aPriority)
    : iUpstream(aUpstream)
    , iId(aId)
    , iPriority(aPriority)
    , iBranch(nullptr)
    , iEnabled(false)
    , iLock("BCHR")
{
}

const Brx& Brancher::Id() const
{
    return iId;
}

IBrancher::EPriority Brancher::Priority() const
{
    return iPriority;
}

void Brancher::SetBranch(IBranch& aBranch)
{
    AutoMutex amx(iLock);
    iBranch = &aBranch;
}

void Brancher::SetEnabled(TBool aEnable)
{
    AutoMutex amx(iLock);
    iEnabled = aEnable;
}

Msg* Brancher::Pull()
{
    Msg* msg = iUpstream.Pull();
    if (iBranch == nullptr) {
        return msg;
    }
    msg = iBranch->PreProcess(msg);
    if (!IsEnabledLocked(*msg)) { // TODO: Not thread safe
        return msg;
    }

    AutoMutex amx(iLock);

    Msg* copy = MsgCloner::NewRef(*msg);
    iBranch->Push(copy);
     
    return iBranch->PostProcess(msg);   
}

TBool Brancher::IsEnabledLocked(Msg& aMsg)
{
    if (!iEnabled) {
        return false;
    }
    if (iBranch == nullptr) {
        return false;
    }
    return iBranch->ProcessEnable(&aMsg);    
}


// Brancher::MsgCloner

Msg* Brancher::MsgCloner::NewRef(Msg& aMsg)
{
    MsgCloner self;
    return aMsg.Process(self);
    
}

Brancher::MsgCloner::MsgCloner()
{
}

Msg* Brancher::MsgCloner::ProcessMsg(MsgMode* aMsg)
{
    aMsg->AddRef();
    return aMsg;
}

Msg* Brancher::MsgCloner::ProcessMsg(MsgTrack* aMsg)
{
    aMsg->AddRef();
    return aMsg;
}

Msg* Brancher::MsgCloner::ProcessMsg(MsgDrain* aMsg)
{
    aMsg->AddRef();
    return aMsg;
}

Msg* Brancher::MsgCloner::ProcessMsg(MsgDelay* aMsg)
{
    aMsg->AddRef();
    return aMsg;
}

Msg* Brancher::MsgCloner::ProcessMsg(MsgEncodedStream* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* Brancher::MsgCloner::ProcessMsg(MsgStreamSegment* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* Brancher::MsgCloner::ProcessMsg(MsgAudioEncoded* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* Brancher::MsgCloner::ProcessMsg(MsgMetaText* aMsg)
{
    aMsg->AddRef();
    return aMsg;
}

Msg* Brancher::MsgCloner::ProcessMsg(MsgStreamInterrupted* aMsg)
{
    aMsg->AddRef();
    return aMsg;
}

Msg* Brancher::MsgCloner::ProcessMsg(MsgHalt* aMsg)
{
    aMsg->AddRef();
    return aMsg;
}

Msg* Brancher::MsgCloner::ProcessMsg(MsgFlush* aMsg)
{
    aMsg->AddRef();
    return aMsg;
}

Msg* Brancher::MsgCloner::ProcessMsg(MsgWait* aMsg)
{
    aMsg->AddRef();
    return aMsg;
}

Msg* Brancher::MsgCloner::ProcessMsg(MsgDecodedStream* aMsg)
{
    aMsg->AddRef();
    return aMsg;
}

Msg* Brancher::MsgCloner::ProcessMsg(MsgAudioPcm* aMsg)
{
    return aMsg->Clone();
}

Msg* Brancher::MsgCloner::ProcessMsg(MsgAudioDsd* aMsg)
{
    return aMsg->Clone();
}

Msg* Brancher::MsgCloner::ProcessMsg(MsgSilence* aMsg)
{
    return aMsg->Clone();
}

Msg* Brancher::MsgCloner::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* Brancher::MsgCloner::ProcessMsg(MsgQuit* aMsg)
{
    aMsg->AddRef();
    return aMsg;
}


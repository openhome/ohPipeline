#include <OpenHome/Media/Pipeline/Drainer.h>
#include <OpenHome/Types.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Media/Debug.h>

#include <atomic>

using namespace OpenHome;
using namespace OpenHome::Media;

const TUint DrainerBase::kSupportedMsgTypes =   eMode
                                              | eTrack
                                              | eDrain
                                              | eDelay
                                              | eEncodedStream
                                              | eAudioEncoded
                                              | eMetatext
                                              | eStreamInterrupted
                                              | eHalt
                                              | eFlush
                                              | eWait
                                              | eDecodedStream
                                              | eBitRate
                                              | eAudioPcm
                                              | eAudioDsd
                                              | eSilence
                                              | eQuit;

DrainerBase::DrainerBase(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstream)
    : PipelineElement(kSupportedMsgTypes)
    , iMsgFactory(aMsgFactory)
    , iGenerateDrainMsg(false)
    , iUpstream(aUpstream)
    , iSem("DRAI", 0)
    , iPending(nullptr)
    , iWaitForDrained(false)
{
    ASSERT(iGenerateDrainMsg.is_lock_free());
}

DrainerBase::~DrainerBase()
{
    if (iPending != nullptr) {
        iPending->RemoveRef();
    }
}

Msg* DrainerBase::Pull()
{
    if (iWaitForDrained) {
        iSem.Wait();
        iWaitForDrained = false; // no synchronisation required - is only accessed in this function
    }
    {
        if (iGenerateDrainMsg.load()) {
            iGenerateDrainMsg.store(false);
            iWaitForDrained = true;
            return iMsgFactory.CreateMsgDrain(MakeFunctor(iSem, &Semaphore::Signal));
        }
    }
    Msg* msg;
    if (iPending == nullptr) {
        msg = iUpstream.Pull();
    }
    else {
        msg = iPending;
        iPending = nullptr;
    }
    {
        /* iUpstream.Pull() has unbounded duration.  If NotifyStarving() was
           called during this time, we should drain the pipeline before passing
           on the next msg. */
        if (iGenerateDrainMsg.load()) {
            iGenerateDrainMsg.store(false);
            iWaitForDrained = true;
            iPending = msg;
            return iMsgFactory.CreateMsgDrain(MakeFunctor(iSem, &Semaphore::Signal));
        }
        msg = msg->Process(*this);
    }
    return msg;
}


// DrainerLeft

DrainerLeft::DrainerLeft(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstream)
    : DrainerBase(aMsgFactory, aUpstream)
    , iStreamHandler(nullptr)
{
}

Msg* DrainerLeft::ProcessMsg(MsgEncodedStream* aMsg)
{
    iStreamHandler = aMsg->StreamHandler();
    auto msg = iMsgFactory.CreateMsgEncodedStream(aMsg, this);
    aMsg->RemoveRef();
    return msg;
}

EStreamPlay DrainerLeft::OkToPlay(TUint aStreamId)
{
    return iStreamHandler.load()->OkToPlay(aStreamId);
}

TUint DrainerLeft::TrySeek(TUint aStreamId, TUint64 aOffset)
{
    return iStreamHandler.load()->TrySeek(aStreamId, aOffset);
}

TUint DrainerLeft::TryDiscard(TUint aJiffies)
{
    return iStreamHandler.load()->TryDiscard(aJiffies);
}

TUint DrainerLeft::TryStop(TUint aStreamId)
{
    return iStreamHandler.load()->TryStop(aStreamId);
}

void DrainerLeft::NotifyStarving(const Brx& aMode, TUint aStreamId, TBool aStarving)
{
    if (aStarving) {
        LOG(kPipeline, "DrainerLeft enabled (NotifyStarving)\n");
        iGenerateDrainMsg.store(true);
    }
    auto streamHandler = iStreamHandler.load();
    if (streamHandler != nullptr) {
        streamHandler->NotifyStarving(aMode, aStreamId, aStarving);
    }
}


// DrainerRight

DrainerRight::DrainerRight(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstream)
    : DrainerBase(aMsgFactory, aUpstream)
{
}

Msg* DrainerRight::ProcessMsg(MsgHalt* aMsg)
{
    LOG(kPipeline, "DrainerRight enabled (MsgHalt)\n");
    iGenerateDrainMsg.store(true);
    return aMsg;
}

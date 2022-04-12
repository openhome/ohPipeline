#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Standard.h>

#include <atomic>

namespace OpenHome {
namespace Media {

class DrainerBase : public PipelineElement, public IPipelineElementUpstream, private INonCopyable
{
    static const TUint kSupportedMsgTypes;
protected:
    DrainerBase(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstream);
    ~DrainerBase();
private: // from IPipelineElementUpstream
    Msg* Pull() override;
protected:
    MsgFactory& iMsgFactory;
    std::atomic<TBool> iGenerateDrainMsg;
private:
    IPipelineElementUpstream& iUpstream;
    Semaphore iSem;
    Msg* iPending;
    TBool iWaitForDrained;
};

class DrainerLeft : public DrainerBase, private IStreamHandler
{
    friend class SuiteDrainer;
public:
    DrainerLeft(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstream);
private: // from PipelineElement
    Msg* ProcessMsg(MsgEncodedStream* aMsg) override;
private: // from IStreamHandler
    EStreamPlay OkToPlay(TUint aStreamId) override;
    TUint TrySeek(TUint aStreamId, TUint64 aOffset) override;
    TUint TryDiscard(TUint aJiffies) override;
    TUint TryStop(TUint aStreamId) override;
    void NotifyStarving(const Brx& aMode, TUint aStreamId, TBool aStarving) override;
private:
    std::atomic<IStreamHandler*> iStreamHandler;
};

class DrainerRight : public DrainerBase
{
    friend class SuiteDrainer;
public:
    DrainerRight(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstream);
private: // from PipelineElement
    Msg* ProcessMsg(MsgHalt* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
private:
    TUint iSampleRate;
    TUint iBitDepth;
    TUint iNumChannels;
};

} // namespace Media
} // namespace OpenHome


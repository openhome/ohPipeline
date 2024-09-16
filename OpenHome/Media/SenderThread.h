#pragma once

#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Fifo.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Thread.h>

#include <atomic>

namespace OpenHome {
    class ThreadFunctor;
namespace Media {

class ISenderMsgPruner : public IMsgProcessor
{
public:
    virtual TBool IsComplete() const = 0;
};

class SenderMsgQueue
{
    friend class SuiteSenderQueue;
public:
    SenderMsgQueue(MsgFactory& aFactory, TUint aMaxCount);
    ~SenderMsgQueue();
    void Enqueue(Msg* aMsg);
    Msg* Dequeue();
private:
    class Element
    {
    public:
        Element();
        void Reset();
    public:
        Msg* iMsg;
        Element* iNext;
    };
private:
    TUint Count() const;
    void Prune();
    void Process(ISenderMsgPruner& aProcessor, Element*& aPrev, Element*& aElem, Element*& aNext);
    void HandleMsgRemoved(Element* aPrev, Element* aElem, Element* aNext);
private:
    MsgFactory& iFactory;
    FifoLiteDynamic<Element*> iFree;
    Element* iHead;
    Element* iTail;
    TUint iCount;
};

class SenderThread : public IPipelineElementDownstream
                   , private IMsgProcessor
                   , private INonCopyable
{
    static const TUint kMaxMsgBacklog = 100;
public:
    SenderThread(
        IPipelineElementDownstream& aDownstream,
        const TChar*                aId,
        MsgFactory&                 aFactory,
        TUint                       aThreadPriority,
        TUint                       aMaxMsgBacklog = kMaxMsgBacklog); // asserts if ever exceeded
    ~SenderThread();
private: // from IPipelineElementDownstream
    void Push(Msg* aMsg) override;
private:
    void Run();
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
    IPipelineElementDownstream& iDownstream;
    ThreadFunctor* iThread;
    Mutex iLock;
    SenderMsgQueue iQueue;
    Semaphore iShutdownSem;
    TBool iQuit;
};

} // namespace Media
} // namespace OpenHome

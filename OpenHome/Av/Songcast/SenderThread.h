#pragma once

#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Fifo.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Thread.h>

#include <atomic>

namespace OpenHome {
    class ThreadFunctor;
namespace Av {

class ISongcastMsgPruner : public Media::IMsgProcessor
{
public:
    virtual TBool IsComplete() const = 0;
};

class SenderMsgQueue
{
    friend class SuiteSenderQueue;
public:
    SenderMsgQueue(Media::MsgFactory& aFactory, TUint aMaxCount);
    ~SenderMsgQueue();
    void Enqueue(Media::Msg* aMsg);
    Media::Msg* Dequeue();
private:
    class Element
    {
    public:
        Element();
        void Reset();
    public:
        Media::Msg* iMsg;
        Element* iNext;
    };
private:
    TUint Count() const;
    void Prune();
    void Process(ISongcastMsgPruner& aProcessor, Element*& aPrev, Element*& aElem, Element*& aNext);
    void HandleMsgRemoved(Element* aPrev, Element* aElem, Element* aNext);
private:
    Media::MsgFactory& iFactory;
    FifoLiteDynamic<Element*> iFree;
    Element* iHead;
    Element* iTail;
    TUint iCount;
};

class SenderThread : public Media::IPipelineElementDownstream
                   , private Media::IMsgProcessor
                   , private INonCopyable
{
    static const TUint kMaxMsgBacklog; // asserts if ever exceeded
public:
    SenderThread(Media::IPipelineElementDownstream& aDownstream,
                 Media::MsgFactory& aFactory,
                 TUint aThreadPriority);
    ~SenderThread();
private: // from Media::IPipelineElementDownstream
    void Push(Media::Msg* aMsg) override;
private:
    void Run();
private: // from Media::IMsgProcessor
    Media::Msg* ProcessMsg(Media::MsgMode* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgTrack* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgDrain* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgDelay* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgEncodedStream* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgAudioEncoded* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgMetaText* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgStreamInterrupted* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgHalt* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgFlush* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgWait* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgDecodedStream* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgBitRate* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgAudioPcm* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgAudioDsd* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgSilence* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgPlayable* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgQuit* aMsg) override;
private:
    Media::IPipelineElementDownstream& iDownstream;
    ThreadFunctor* iThread;
    Mutex iLock;
    SenderMsgQueue iQueue;
    Semaphore iShutdownSem;
    TBool iQuit;
};

} // namespace Av
} // namespace OpenHome

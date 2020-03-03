#include <OpenHome/Av/Songcast/SenderThread.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Debug-ohMediaPlayer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Printer.h>

#include <array>
#include <vector>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;


class ProcessorMsgAudioPrune : public IMsgProcessor
{
public:
    ProcessorMsgAudioPrune();
    TUint DiscardedJiffies() const;
private:
    Msg* Discard(MsgAudio* aAudio);
    void EndDiscardBlock();
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgTrack* aMsg) override;
    Msg* ProcessMsg(MsgDrain* aMsg) override;
    Msg* ProcessMsg(MsgDelay* aMsg) override;
    Msg* ProcessMsg(MsgEncodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioEncoded* aMsg) override;
    Msg* ProcessMsg(MsgMetaText* aMsg) override;
    Msg* ProcessMsg(MsgStreamInterrupted* aMsg) override;
    Msg* ProcessMsg(MsgHalt* aMsg) override;
    Msg* ProcessMsg(MsgFlush* aMsg) override;
    Msg* ProcessMsg(MsgWait* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsg(MsgBitRate* aMsg) override;
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgAudioDsd* aMsg) override;
    Msg* ProcessMsg(MsgSilence* aMsg) override;
    Msg* ProcessMsg(MsgPlayable* aMsg) override;
    Msg* ProcessMsg(MsgQuit* aMsg) override;
private:
    TUint iDiscardedJiffies;
};


// ProcessorCount

class ProcessorCount : public IMsgProcessor
{
public:
    ProcessorCount()
        : iCountMode(0)
        , iCountTrack(0)
        , iCountDelay(0)
        , iCountMetaText(0)
        , iCountHalt(0)
        , iCountStream(0)
    {}
    TUint CountMode() const     { return iCountMode; }
    TUint CountTrack() const    { return iCountTrack; }
    TUint CountDelay() const    { return iCountDelay; }
    TUint CountMetaText() const { return iCountMetaText; }
    TUint CountHalt() const     { return iCountHalt; }
    TUint CountStream() const { return iCountStream; }
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override                 { iCountMode++; return aMsg; }
    Msg* ProcessMsg(MsgTrack* aMsg) override                { if (aMsg->StartOfStream()) { iCountTrack++; } return aMsg; }
    Msg* ProcessMsg(MsgDrain* aMsg) override                { return aMsg; }
    Msg* ProcessMsg(MsgDelay* aMsg) override                { iCountDelay++; return aMsg; }
    Msg* ProcessMsg(MsgEncodedStream* aMsg) override        { ASSERTS(); return aMsg; }
    Msg* ProcessMsg(MsgAudioEncoded* aMsg) override         { ASSERTS(); return aMsg; }
    Msg* ProcessMsg(MsgMetaText* aMsg) override             { iCountMetaText++; return aMsg; }
    Msg* ProcessMsg(MsgStreamInterrupted* aMsg) override    { return aMsg; }
    Msg* ProcessMsg(MsgHalt* aMsg) override                 { iCountHalt++; return aMsg; }
    Msg* ProcessMsg(MsgFlush* aMsg) override                { return aMsg; }
    Msg* ProcessMsg(MsgWait* aMsg) override                 { return aMsg; }
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override        { iCountStream++; return aMsg; }
    Msg* ProcessMsg(MsgBitRate* aMsg) override              { return aMsg; }
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override             { return aMsg; }
    Msg* ProcessMsg(MsgAudioDsd* aMsg) override             { return aMsg; }
    Msg* ProcessMsg(MsgSilence* aMsg) override              { return aMsg; }
    Msg* ProcessMsg(MsgPlayable* aMsg) override             { ASSERTS(); return aMsg; }
    Msg* ProcessMsg(MsgQuit* aMsg) override                 { return aMsg; }
private:
    TUint iCountMode;
    TUint iCountTrack;
    TUint iCountDelay;
    TUint iCountMetaText;
    TUint iCountHalt;
    TUint iCountStream;
};

class ProcessorMode : public ISongcastMsgPruner
{
public:
    ProcessorMode(TUint& aCountMode, TUint& aCountTrack, TUint& aCountDelay,
                 TUint& aCountMetaText, TUint& aCountHalt, TUint& aCountStream);
public: // from ISongcastMsgPruner
    TBool IsComplete() const override;
private:
    Msg* RemoveIfNotLatestMode(Msg* aMsg);
    Msg* RemoveIfNotLatestMode(Msg* aMsg, TUint& aCount);
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgTrack* aMsg) override;
    Msg* ProcessMsg(MsgDrain* aMsg) override;
    Msg* ProcessMsg(MsgDelay* aMsg) override;
    Msg* ProcessMsg(MsgEncodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioEncoded* aMsg) override;
    Msg* ProcessMsg(MsgMetaText* aMsg) override;
    Msg* ProcessMsg(MsgStreamInterrupted* aMsg) override;
    Msg* ProcessMsg(MsgHalt* aMsg) override;
    Msg* ProcessMsg(MsgFlush* aMsg) override;
    Msg* ProcessMsg(MsgWait* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsg(MsgBitRate* aMsg) override;
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgAudioDsd* aMsg) override;
    Msg* ProcessMsg(MsgSilence* aMsg) override;
    Msg* ProcessMsg(MsgPlayable* aMsg) override;
    Msg* ProcessMsg(MsgQuit* aMsg) override;
private:
    TUint& iCountMode;
    TUint& iCountTrack;
    TUint& iCountDelay;
    TUint& iCountMetaText;
    TUint& iCountHalt;
    TUint& iCountStream;
};

class ProcessorTrack : public ISongcastMsgPruner
{
public:
    ProcessorTrack(TUint& aCountTrack, TUint& aCountMetaText,
                   TUint& aCountHalt, TUint& aCountStream);
public: // from ISongcastMsgPruner
    TBool IsComplete() const override;
private:
    Msg* RemoveIfNotLatestTrack(Msg* aMsg);
    Msg* RemoveIfNotLatestTrack(Msg* aMsg, TUint& aCount);
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgTrack* aMsg) override;
    Msg* ProcessMsg(MsgDrain* aMsg) override;
    Msg* ProcessMsg(MsgDelay* aMsg) override;
    Msg* ProcessMsg(MsgEncodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioEncoded* aMsg) override;
    Msg* ProcessMsg(MsgMetaText* aMsg) override;
    Msg* ProcessMsg(MsgStreamInterrupted* aMsg) override;
    Msg* ProcessMsg(MsgHalt* aMsg) override;
    Msg* ProcessMsg(MsgFlush* aMsg) override;
    Msg* ProcessMsg(MsgWait* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsg(MsgBitRate* aMsg) override;
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgAudioDsd* aMsg) override;
    Msg* ProcessMsg(MsgSilence* aMsg) override;
    Msg* ProcessMsg(MsgPlayable* aMsg) override;
    Msg* ProcessMsg(MsgQuit* aMsg) override;
private:
    TUint& iCountTrack;
    TUint& iCountMetaText;
    TUint& iCountHalt;
    TUint& iCountStream;
};

class ProcessorStream: public ISongcastMsgPruner
{
public:
    ProcessorStream( TUint& aCountMetaText, TUint& aCountHalt, TUint& aCountStream);
public: // from ISongcastMsgPruner
    TBool IsComplete() const override;
private:
    Msg* RemoveIfNotLatestStream(Msg* aMsg);
    Msg* RemoveIfNotLatestStream(Msg* aMsg, TUint& aCount);
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgTrack* aMsg) override;
    Msg* ProcessMsg(MsgDrain* aMsg) override;
    Msg* ProcessMsg(MsgDelay* aMsg) override;
    Msg* ProcessMsg(MsgEncodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioEncoded* aMsg) override;
    Msg* ProcessMsg(MsgMetaText* aMsg) override;
    Msg* ProcessMsg(MsgStreamInterrupted* aMsg) override;
    Msg* ProcessMsg(MsgHalt* aMsg) override;
    Msg* ProcessMsg(MsgFlush* aMsg) override;
    Msg* ProcessMsg(MsgWait* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsg(MsgBitRate* aMsg) override;
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgAudioDsd* aMsg) override;
    Msg* ProcessMsg(MsgSilence* aMsg) override;
    Msg* ProcessMsg(MsgPlayable* aMsg) override;
    Msg* ProcessMsg(MsgQuit* aMsg) override;
private:
    TUint& iCountMetaText;
    TUint& iCountHalt;
    TUint& iCountStream;
};

class ProcessorDelayMetaTextHalt : public ISongcastMsgPruner
{
public:
    ProcessorDelayMetaTextHalt(TUint& aCountDelay, TUint& aCountMetaText, TUint& aCountHalt);
public: // from ISongcastMsgPruner
    TBool IsComplete() const override;
private:
    Msg* RemoveIfNotLatest(Msg* aMsg, TUint& aCount);
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgTrack* aMsg) override;
    Msg* ProcessMsg(MsgDrain* aMsg) override;
    Msg* ProcessMsg(MsgDelay* aMsg) override;
    Msg* ProcessMsg(MsgEncodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioEncoded* aMsg) override;
    Msg* ProcessMsg(MsgMetaText* aMsg) override;
    Msg* ProcessMsg(MsgStreamInterrupted* aMsg) override;
    Msg* ProcessMsg(MsgHalt* aMsg) override;
    Msg* ProcessMsg(MsgFlush* aMsg) override;
    Msg* ProcessMsg(MsgWait* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsg(MsgBitRate* aMsg) override;
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgAudioDsd* aMsg) override;
    Msg* ProcessMsg(MsgSilence* aMsg) override;
    Msg* ProcessMsg(MsgPlayable* aMsg) override;
    Msg* ProcessMsg(MsgQuit* aMsg) override;
private:
    TUint& iCountDelay;
    TUint& iCountMetaText;
    TUint& iCountHalt;
};


// ProcessorMsgAudioPrune

ProcessorMsgAudioPrune::ProcessorMsgAudioPrune()
    : iDiscardedJiffies(0)
{
}
TUint ProcessorMsgAudioPrune::DiscardedJiffies() const
{
    return iDiscardedJiffies;
}
Msg* ProcessorMsgAudioPrune::Discard(MsgAudio* aAudio)
{
    iDiscardedJiffies += aAudio->Jiffies();
    aAudio->RemoveRef();
    return nullptr;
}
void ProcessorMsgAudioPrune::EndDiscardBlock()
{
    iDiscardedJiffies = 0;
}
Msg* ProcessorMsgAudioPrune::ProcessMsg(MsgMode* aMsg)              { EndDiscardBlock(); return aMsg; }
Msg* ProcessorMsgAudioPrune::ProcessMsg(MsgTrack* aMsg)             { EndDiscardBlock(); return aMsg; }
Msg* ProcessorMsgAudioPrune::ProcessMsg(MsgDrain* aMsg)             { EndDiscardBlock(); return aMsg; }
Msg* ProcessorMsgAudioPrune::ProcessMsg(MsgDelay* aMsg)             { EndDiscardBlock(); return aMsg; }
Msg* ProcessorMsgAudioPrune::ProcessMsg(MsgEncodedStream* aMsg)     { EndDiscardBlock(); return aMsg; }
Msg* ProcessorMsgAudioPrune::ProcessMsg(MsgAudioEncoded* aMsg)      { EndDiscardBlock(); return aMsg; }
Msg* ProcessorMsgAudioPrune::ProcessMsg(MsgMetaText* aMsg)          { EndDiscardBlock(); return aMsg; }
Msg* ProcessorMsgAudioPrune::ProcessMsg(MsgStreamInterrupted* aMsg) { EndDiscardBlock(); return aMsg; }
Msg* ProcessorMsgAudioPrune::ProcessMsg(MsgHalt* aMsg)              { EndDiscardBlock(); return aMsg; }
Msg* ProcessorMsgAudioPrune::ProcessMsg(MsgFlush* aMsg)             { EndDiscardBlock(); return aMsg; }
Msg* ProcessorMsgAudioPrune::ProcessMsg(MsgWait* aMsg)              { EndDiscardBlock(); return aMsg; }
Msg* ProcessorMsgAudioPrune::ProcessMsg(MsgDecodedStream* aMsg)     { EndDiscardBlock(); return aMsg; }
Msg* ProcessorMsgAudioPrune::ProcessMsg(MsgBitRate* aMsg)           { EndDiscardBlock(); return aMsg; }
Msg* ProcessorMsgAudioPrune::ProcessMsg(MsgAudioPcm* aMsg)          { return Discard(aMsg); }
Msg* ProcessorMsgAudioPrune::ProcessMsg(MsgAudioDsd* aMsg)          { return Discard(aMsg); }
Msg* ProcessorMsgAudioPrune::ProcessMsg(MsgSilence* aMsg)           { return Discard(aMsg); }
Msg* ProcessorMsgAudioPrune::ProcessMsg(MsgPlayable* aMsg)          { ASSERTS(); return aMsg; }
Msg* ProcessorMsgAudioPrune::ProcessMsg(MsgQuit* aMsg)              { EndDiscardBlock(); return aMsg; }


// ProcessorMode

ProcessorMode::ProcessorMode(TUint& aCountMode, TUint& aCountTrack, TUint& aCountDelay,
                             TUint& aCountMetaText, TUint& aCountHalt, TUint& aCountStream)
    : iCountMode(aCountMode)
    , iCountTrack(aCountTrack)
    , iCountDelay(aCountDelay)
    , iCountMetaText(aCountMetaText)
    , iCountHalt(aCountHalt)
    , iCountStream(aCountStream)
{
}
TBool ProcessorMode::IsComplete() const
{
    return iCountMode == 0;
}
Msg* ProcessorMode::RemoveIfNotLatestMode(Msg* aMsg)
{
    if (!IsComplete()) {
        aMsg->RemoveRef();
        return nullptr;
    }
    return aMsg;
}
Msg* ProcessorMode::RemoveIfNotLatestMode(Msg* aMsg, TUint& aCount)
{
    auto msg = RemoveIfNotLatestMode(aMsg);
    if (msg == nullptr) {
        --aCount;
    }
    return msg;
}
Msg* ProcessorMode::ProcessMsg(MsgMode* aMsg)
{
    --iCountMode;
    return aMsg;
}
Msg* ProcessorMode::ProcessMsg(MsgTrack* aMsg)              { return RemoveIfNotLatestMode(aMsg, iCountTrack); }
Msg* ProcessorMode::ProcessMsg(MsgDrain* aMsg)              { return RemoveIfNotLatestMode(aMsg); }
Msg* ProcessorMode::ProcessMsg(MsgDelay* aMsg)              { return RemoveIfNotLatestMode(aMsg, iCountDelay); }
Msg* ProcessorMode::ProcessMsg(MsgEncodedStream* aMsg)      { return RemoveIfNotLatestMode(aMsg); }
Msg* ProcessorMode::ProcessMsg(MsgAudioEncoded* aMsg)       { ASSERTS(); return RemoveIfNotLatestMode(aMsg); }
Msg* ProcessorMode::ProcessMsg(MsgMetaText* aMsg)           { return RemoveIfNotLatestMode(aMsg, iCountMetaText); }
Msg* ProcessorMode::ProcessMsg(MsgStreamInterrupted* aMsg)  { return aMsg; }
Msg* ProcessorMode::ProcessMsg(MsgHalt* aMsg)               { return RemoveIfNotLatestMode(aMsg, iCountHalt); }
Msg* ProcessorMode::ProcessMsg(MsgFlush* aMsg)              { return RemoveIfNotLatestMode(aMsg); }
Msg* ProcessorMode::ProcessMsg(MsgWait* aMsg)               { return RemoveIfNotLatestMode(aMsg); }
Msg* ProcessorMode::ProcessMsg(MsgDecodedStream* aMsg)      { return RemoveIfNotLatestMode(aMsg, iCountStream); }
Msg* ProcessorMode::ProcessMsg(MsgBitRate* aMsg)            { return RemoveIfNotLatestMode(aMsg); }
Msg* ProcessorMode::ProcessMsg(MsgAudioPcm* aMsg)           { ASSERTS(); return RemoveIfNotLatestMode(aMsg); }
Msg* ProcessorMode::ProcessMsg(MsgAudioDsd* aMsg)           { ASSERTS(); return RemoveIfNotLatestMode(aMsg); }
Msg* ProcessorMode::ProcessMsg(MsgSilence* aMsg)            { ASSERTS(); return RemoveIfNotLatestMode(aMsg); }
Msg* ProcessorMode::ProcessMsg(MsgPlayable* aMsg)           { ASSERTS(); return RemoveIfNotLatestMode(aMsg); }
Msg* ProcessorMode::ProcessMsg(MsgQuit* aMsg)               { return aMsg; }


// ProcessorTrack

ProcessorTrack::ProcessorTrack(TUint& aCountTrack, TUint& aCountMetaText,
                               TUint& aCountHalt, TUint& aCountStream)
    : iCountTrack(aCountTrack)
    , iCountMetaText(aCountMetaText)
    , iCountHalt(aCountHalt)
    , iCountStream(aCountStream)
{
}
TBool ProcessorTrack::IsComplete() const
{
    return iCountTrack == 0;
}
Msg* ProcessorTrack::RemoveIfNotLatestTrack(Msg* aMsg)
{
    if (!IsComplete()) {
        aMsg->RemoveRef();
        return nullptr;
    }
    return aMsg;
}
Msg* ProcessorTrack::RemoveIfNotLatestTrack(Msg* aMsg, TUint& aCount)
{
    auto msg = RemoveIfNotLatestTrack(aMsg);
    if (msg == nullptr) {
        --aCount;
    }
    return msg;
}
Msg* ProcessorTrack::ProcessMsg(MsgMode* aMsg)              { return aMsg; }
Msg* ProcessorTrack::ProcessMsg(MsgTrack* aMsg)
{
    if (aMsg->StartOfStream() && --iCountTrack!= 0) {
        aMsg->RemoveRef();
        return nullptr;
    }
    return aMsg;
}
Msg* ProcessorTrack::ProcessMsg(MsgDrain* aMsg)             { return RemoveIfNotLatestTrack(aMsg); }
Msg* ProcessorTrack::ProcessMsg(MsgDelay* aMsg)             { return aMsg; }
Msg* ProcessorTrack::ProcessMsg(MsgEncodedStream* aMsg)     { return RemoveIfNotLatestTrack(aMsg); }
Msg* ProcessorTrack::ProcessMsg(MsgAudioEncoded* aMsg)      { ASSERTS(); return RemoveIfNotLatestTrack(aMsg); }
Msg* ProcessorTrack::ProcessMsg(MsgMetaText* aMsg)          { return RemoveIfNotLatestTrack(aMsg, iCountMetaText); }
Msg* ProcessorTrack::ProcessMsg(MsgStreamInterrupted* aMsg) { return aMsg; }
Msg* ProcessorTrack::ProcessMsg(MsgHalt* aMsg)              { return RemoveIfNotLatestTrack(aMsg, iCountHalt); }
Msg* ProcessorTrack::ProcessMsg(MsgFlush* aMsg)             { return RemoveIfNotLatestTrack(aMsg); }
Msg* ProcessorTrack::ProcessMsg(MsgWait* aMsg)              { return RemoveIfNotLatestTrack(aMsg); }
Msg* ProcessorTrack::ProcessMsg(MsgDecodedStream* aMsg)     { return RemoveIfNotLatestTrack(aMsg, iCountStream); }
Msg* ProcessorTrack::ProcessMsg(MsgBitRate* aMsg)           { return RemoveIfNotLatestTrack(aMsg); }
Msg* ProcessorTrack::ProcessMsg(MsgAudioPcm* aMsg)          { ASSERTS(); return RemoveIfNotLatestTrack(aMsg); }
Msg* ProcessorTrack::ProcessMsg(MsgAudioDsd* aMsg)          { ASSERTS(); return RemoveIfNotLatestTrack(aMsg); }
Msg* ProcessorTrack::ProcessMsg(MsgSilence* aMsg)           { ASSERTS(); return RemoveIfNotLatestTrack(aMsg); }
Msg* ProcessorTrack::ProcessMsg(MsgPlayable* aMsg)          { ASSERTS(); return RemoveIfNotLatestTrack(aMsg); }
Msg* ProcessorTrack::ProcessMsg(MsgQuit* aMsg)              { return aMsg; }



// ProcessorStream

ProcessorStream::ProcessorStream(TUint& aCountMetaText, TUint& aCountHalt, TUint& aCountStream)
    : iCountMetaText(aCountMetaText)
    , iCountHalt(aCountHalt)
    , iCountStream(aCountStream)
{
}
TBool ProcessorStream::IsComplete() const
{
    return iCountStream == 0;
}
Msg* ProcessorStream::RemoveIfNotLatestStream(Msg* aMsg)
{
    if (!IsComplete()) {
        aMsg->RemoveRef();
        return nullptr;
    }
    return aMsg;
}
Msg* ProcessorStream::RemoveIfNotLatestStream(Msg* aMsg, TUint& aCount)
{
    auto msg = RemoveIfNotLatestStream(aMsg);
    if (msg == nullptr) {
        --aCount;
    }
    return msg;
}
Msg* ProcessorStream::ProcessMsg(MsgMode* aMsg)                 { return aMsg; }
Msg* ProcessorStream::ProcessMsg(MsgTrack* aMsg)                { return aMsg; }
Msg* ProcessorStream::ProcessMsg(MsgDrain* aMsg)                { return RemoveIfNotLatestStream(aMsg); }
Msg* ProcessorStream::ProcessMsg(MsgDelay* aMsg)                { return aMsg; }
Msg* ProcessorStream::ProcessMsg(MsgEncodedStream* aMsg)        { return aMsg; }
Msg* ProcessorStream::ProcessMsg(MsgAudioEncoded* aMsg)         { ASSERTS(); return RemoveIfNotLatestStream(aMsg); }
Msg* ProcessorStream::ProcessMsg(MsgMetaText* aMsg)             { return RemoveIfNotLatestStream(aMsg, iCountMetaText); }
Msg* ProcessorStream::ProcessMsg(MsgStreamInterrupted* aMsg)    { return aMsg; }
Msg* ProcessorStream::ProcessMsg(MsgHalt* aMsg)                 { return RemoveIfNotLatestStream(aMsg, iCountHalt); }
Msg* ProcessorStream::ProcessMsg(MsgFlush* aMsg)                { return RemoveIfNotLatestStream(aMsg); }
Msg* ProcessorStream::ProcessMsg(MsgWait* aMsg)                 { return RemoveIfNotLatestStream(aMsg); }
Msg* ProcessorStream::ProcessMsg(MsgDecodedStream* aMsg)
{
    if (--iCountStream != 0) {
        aMsg->RemoveRef();
        return nullptr;
    }
    return aMsg;
}
Msg* ProcessorStream::ProcessMsg(MsgBitRate* aMsg)              { return RemoveIfNotLatestStream(aMsg); }
Msg* ProcessorStream::ProcessMsg(MsgAudioPcm* aMsg)             { ASSERTS(); return RemoveIfNotLatestStream(aMsg); }
Msg* ProcessorStream::ProcessMsg(MsgAudioDsd* aMsg)             { ASSERTS(); return RemoveIfNotLatestStream(aMsg); }
Msg* ProcessorStream::ProcessMsg(MsgSilence* aMsg)              { ASSERTS(); return RemoveIfNotLatestStream(aMsg); }
Msg* ProcessorStream::ProcessMsg(MsgPlayable* aMsg)             { ASSERTS(); return RemoveIfNotLatestStream(aMsg); }
Msg* ProcessorStream::ProcessMsg(MsgQuit* aMsg)                 { return aMsg; }


// ProcessorDelayMetaTextHalt

ProcessorDelayMetaTextHalt::ProcessorDelayMetaTextHalt(TUint& aCountDelay, TUint& aCountMetaText, TUint& aCountHalt)
    : iCountDelay(aCountDelay)
    , iCountMetaText(aCountMetaText)
    , iCountHalt(aCountHalt)
{
}
TBool ProcessorDelayMetaTextHalt::IsComplete() const
{
    return iCountDelay <= 1 && iCountMetaText <= 1 && iCountHalt <= 1;
}
Msg* ProcessorDelayMetaTextHalt::RemoveIfNotLatest(Msg* aMsg, TUint& aCount)
{
    if (aCount > 1) {
        --aCount;
        aMsg->RemoveRef();
        return nullptr;
    }
    return aMsg;
}
Msg* ProcessorDelayMetaTextHalt::ProcessMsg(MsgMode* aMsg)              { return aMsg; }
Msg* ProcessorDelayMetaTextHalt::ProcessMsg(MsgTrack* aMsg)             { return aMsg; }
Msg* ProcessorDelayMetaTextHalt::ProcessMsg(MsgDrain* aMsg)             { return aMsg; }
Msg* ProcessorDelayMetaTextHalt::ProcessMsg(MsgDelay* aMsg)             { return RemoveIfNotLatest(aMsg, iCountDelay); }
Msg* ProcessorDelayMetaTextHalt::ProcessMsg(MsgEncodedStream* aMsg)     { return aMsg; }
Msg* ProcessorDelayMetaTextHalt::ProcessMsg(MsgAudioEncoded* aMsg)      { return aMsg; }
Msg* ProcessorDelayMetaTextHalt::ProcessMsg(MsgMetaText* aMsg)          { return RemoveIfNotLatest(aMsg, iCountMetaText); }
Msg* ProcessorDelayMetaTextHalt::ProcessMsg(MsgStreamInterrupted* aMsg) { return aMsg; }
Msg* ProcessorDelayMetaTextHalt::ProcessMsg(MsgHalt* aMsg)              { return RemoveIfNotLatest(aMsg, iCountHalt); }
Msg* ProcessorDelayMetaTextHalt::ProcessMsg(MsgFlush* aMsg)             { return aMsg; }
Msg* ProcessorDelayMetaTextHalt::ProcessMsg(MsgWait* aMsg)              { return aMsg; }
Msg* ProcessorDelayMetaTextHalt::ProcessMsg(MsgDecodedStream* aMsg)     { return aMsg; }
Msg* ProcessorDelayMetaTextHalt::ProcessMsg(MsgBitRate* aMsg)           { return aMsg; }
Msg* ProcessorDelayMetaTextHalt::ProcessMsg(MsgAudioPcm* aMsg)          { return aMsg; }
Msg* ProcessorDelayMetaTextHalt::ProcessMsg(MsgAudioDsd* aMsg)          { return aMsg; }
Msg* ProcessorDelayMetaTextHalt::ProcessMsg(MsgSilence* aMsg)           { return aMsg; }
Msg* ProcessorDelayMetaTextHalt::ProcessMsg(MsgPlayable* aMsg)          { return aMsg; }
Msg* ProcessorDelayMetaTextHalt::ProcessMsg(MsgQuit* aMsg)              { return aMsg; }


// SenderMsgQueue::Element

SenderMsgQueue::Element::Element()
{
    Reset();
}

void SenderMsgQueue::Element::Reset()
{
    iMsg = nullptr;
    iNext = nullptr;
}


// SenderMsgQueue

SenderMsgQueue::SenderMsgQueue(MsgFactory& aFactory, TUint aMaxCount)
    : iFactory(aFactory)
    , iFree(aMaxCount)
    , iHead(nullptr)
    , iTail(nullptr)
    , iCount(0)
{
    for (TUint i = 0; i < aMaxCount; i++) {
        iFree.Write(new Element());
    }
}

SenderMsgQueue::~SenderMsgQueue()
{
    auto elem = iHead;
    while (elem != nullptr) {
        auto next = elem->iNext;
        elem->iMsg->RemoveRef();
        delete elem;
        elem = next;
    }
    const auto count = iFree.SlotsUsed();
    for (TUint i = 0; i < count; i++) {
        delete iFree.Read();
    }
}

void SenderMsgQueue::Enqueue(Msg* aMsg)
{
    ASSERT(aMsg != nullptr);
    if (iCount == iFree.Slots()) {
        Prune();
    }
    auto elem = iFree.Read();
    elem->iMsg = aMsg;
    if (iHead == nullptr) {
        iHead = elem;
    }
    else {
        iTail->iNext = elem;
    }
    iTail = elem;
    iCount++;
}

Msg* SenderMsgQueue::Dequeue()
{
    if (iHead == nullptr) {
        return nullptr;
    }
    auto elem = iHead;
    iHead = elem->iNext;
    iCount--;
    auto msg = elem->iMsg;
    elem->Reset();
    iFree.Write(elem);
    if (iHead == nullptr) {
        iTail = nullptr;
    }
    return msg;
}

TUint SenderMsgQueue::Count() const
{
    return iCount;
}

void SenderMsgQueue::Prune()
{
    LOG_INFO(kPipeline, "WARNING: Songcast sender - SenderMsgQueue::Prune() discarding audio\n")
    ProcessorMsgAudioPrune audioPruner;
    auto elem = iHead;
    ASSERT(elem != nullptr); // why are we being asked to prune if the queue is empty?
    Element* prev = nullptr;
    auto next = elem->iNext;
    for (;;) {
        const auto prevDiscarded = audioPruner.DiscardedJiffies();
        elem->iMsg = elem->iMsg->Process(audioPruner);
        const auto discarded = audioPruner.DiscardedJiffies();
        if ((prevDiscarded > 0 && discarded == 0) || (next == nullptr && discarded > 0)) {
            const auto jiffies = discarded == 0 ? prevDiscarded : discarded;
            auto newElem = iFree.Read();
            newElem->iMsg = iFactory.CreateMsgStreamInterrupted(jiffies);
            if (prev == nullptr) {
                iHead = newElem;
            }
            else {
                prev->iNext = newElem;
            }
            if (elem->iMsg == nullptr) {
                elem->Reset();
                iFree.Write(elem);
                newElem->iNext = next;
                if (newElem->iNext == nullptr) {
                    iTail = newElem;
                }
            }
            else {
                ++iCount;
                newElem->iNext = elem;
            }
            prev = newElem;
        }
        else if (elem->iMsg == nullptr) {
            HandleMsgRemoved(prev, elem, next);
        }

        if (elem->iMsg != nullptr) {
            prev = elem;
        }
        elem = next;
        if (next == nullptr) {
            break;
        }
        next = elem->iNext;
    }

    ProcessorCount procCount;
    elem = iHead;
    while (elem != nullptr) {
        (void)elem->iMsg->Process(procCount);
        elem = elem->iNext;
    }

    auto modeCount = procCount.CountMode();
    auto trackCount = procCount.CountTrack();
    auto delayCount = procCount.CountDelay();
    auto metatextCount = procCount.CountMetaText();
    auto haltCount = procCount.CountHalt();
    auto streamCount = procCount.CountStream();

    ProcessorMode procMode(modeCount, trackCount, delayCount, metatextCount, haltCount, streamCount);
    elem = iHead;
    prev = nullptr;
    next = elem == nullptr? nullptr : elem->iNext;
    Process(procMode, prev, elem, next);
    // continue from where procMode left off
    ProcessorTrack procTrack(trackCount, metatextCount, haltCount, streamCount);
    Process(procTrack, prev, elem, next);
    // continue from where procTrack left off
    ProcessorStream procStream(metatextCount, haltCount, streamCount);
    Process(procStream, prev, elem, next);

    // prune duplicates for a few remaining msgs from entire queue
    elem = iHead;
    prev = nullptr;
    next = elem == nullptr ? nullptr : elem->iNext;
    ProcessorDelayMetaTextHalt procDmh(delayCount, metatextCount, haltCount);
    Process(procDmh, prev, elem, next);
}

void SenderMsgQueue::Process(ISongcastMsgPruner& aProcessor, Element*& aPrev, Element*& aElem, Element*& aNext)
{
    for (; !aProcessor.IsComplete();) {
        aElem->iMsg = aElem->iMsg->Process(aProcessor);
        if (aElem->iMsg != nullptr) {
            aPrev = aElem;
        }
        else {
            HandleMsgRemoved(aPrev, aElem, aNext);
        }
        if (aNext == nullptr) {
            break;
        }
        aElem = aNext;
        aNext = aElem->iNext;
    }
}

void SenderMsgQueue::HandleMsgRemoved(Element* aPrev, Element* aElem, Element* aNext)
{
    aElem->Reset();
    iFree.Write(aElem);
    if (aPrev == nullptr) {
        iHead = aNext;
    }
    else {
        aPrev->iNext = aNext;
    }
    if (aNext == nullptr) {
        iTail = aPrev;
    }
    --iCount;
}


// SenderThread

const TUint SenderThread::kMaxMsgBacklog = 100;

SenderThread::SenderThread(IPipelineElementDownstream& aDownstream,
                           MsgFactory& aFactory,
                           TUint aThreadPriority)
    : iDownstream(aDownstream)
    , iLock("SCST")
    , iQueue(aFactory, kMaxMsgBacklog)
    , iShutdownSem("SGSN", 0)
    , iQuit(false)
{
    iThread = new ThreadFunctor("SongcastSender", MakeFunctor(*this, &SenderThread::Run), aThreadPriority);
    iThread->Start();
}

SenderThread::~SenderThread()
{
    iShutdownSem.Wait();
    delete iThread;
}

void SenderThread::Push(Msg* aMsg)
{
    AutoMutex _(iLock);
    iQueue.Enqueue(aMsg);
    iThread->Signal();
}

void SenderThread::Run()
{
    do {
        iThread->Wait();
        iLock.Wait();
        auto msg = iQueue.Dequeue();
        iLock.Signal();
        if (msg != nullptr) { // may be null after the queue has been pruned
            msg = msg->Process(*this);
            iDownstream.Push(msg);
        }
    } while (!iQuit);
    iShutdownSem.Signal();
}

Msg* SenderThread::ProcessMsg(MsgMode* aMsg)              { return aMsg; }
Msg* SenderThread::ProcessMsg(MsgTrack* aMsg)             { return aMsg; }
Msg* SenderThread::ProcessMsg(MsgDrain* aMsg)             { return aMsg; }
Msg* SenderThread::ProcessMsg(MsgDelay* aMsg)             { return aMsg; }
Msg* SenderThread::ProcessMsg(MsgEncodedStream* aMsg)     { return aMsg; }
Msg* SenderThread::ProcessMsg(MsgAudioEncoded* aMsg)      { return aMsg; }
Msg* SenderThread::ProcessMsg(MsgMetaText* aMsg)          { return aMsg; }
Msg* SenderThread::ProcessMsg(MsgStreamInterrupted* aMsg) { return aMsg; }
Msg* SenderThread::ProcessMsg(MsgHalt* aMsg)              { return aMsg; }
Msg* SenderThread::ProcessMsg(MsgFlush* aMsg)             { return aMsg; }
Msg* SenderThread::ProcessMsg(MsgWait* aMsg)              { return aMsg; }
Msg* SenderThread::ProcessMsg(MsgDecodedStream* aMsg)     { return aMsg; }
Msg* SenderThread::ProcessMsg(MsgBitRate* aMsg)           { return aMsg; }
Msg* SenderThread::ProcessMsg(MsgAudioPcm* aMsg)          { return aMsg; }
Msg* SenderThread::ProcessMsg(MsgAudioDsd* aMsg)          { return aMsg; }
Msg* SenderThread::ProcessMsg(MsgSilence* aMsg)           { return aMsg; }
Msg* SenderThread::ProcessMsg(MsgPlayable* aMsg)          { return aMsg; }
Msg* SenderThread::ProcessMsg(MsgQuit* aMsg)
{
    iQuit = true;
    return aMsg;
}

#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Thread.h>

namespace OpenHome {
namespace Media {

class Flusher : public IPipelineElementUpstream, private IMsgProcessor, private INonCopyable
{
public:
    Flusher(IPipelineElementUpstream& aUpstream, const TChar* aId);
    ~Flusher();
    void DiscardUntilHalt(TUint aId);
    void DiscardUntilFlush(TUint aId);
public: // from IPipelineElementUpstream
    Msg* Pull() override;
private:
    inline TBool IsFlushing() const;
    Msg* ProcessFlushable(Msg* aMsg);
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
    Mutex iLock;
    IPipelineElementUpstream& iUpstream;
    const TChar* iId;
    TUint iTargetHaltId;
    TUint iTargetFlushId;
    TUint iLastHaltId;
    TUint iLastFlushId;
    Msg* iPendingMetatext;
};

} // namespace Media
} // namespace OpenHome


#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/PipelineObserver.h>
#include <OpenHome/Media/Pipeline/Msg.h>

#include <atomic>

namespace OpenHome {
namespace Media {

/*
Element which reports state changes in pipeline.
Is passive - it reports on Msgs but doesn't create/destroy/edit them.
*/

class IPipelineElementObserverThread;

class Reporter : public PipelineElement, public IPipelineElementUpstream, private INonCopyable
{
    static const TUint kSupportedMsgTypes;
    static const Brn kNullMetaText;
    static const TUint kTrackNotifyDelayMs = 10;
public:
    Reporter(IPipelineElementUpstream& aUpstreamElement, IPipelineObserver& aObserver, IPipelineElementObserverThread& aObserverThread);
    virtual ~Reporter();
    void SetPipelineState(EPipelineState aState);
public: // from IPipelineElementUpstream
    Msg* Pull() override;
private: // IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgTrack* aMsg) override;
    Msg* ProcessMsg(MsgMetaText* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgAudioDsd* aMsg) override;
private:
    void ProcessAudio(MsgAudioDecoded* aMsg);
    void EventCallback();
private:
    IPipelineElementUpstream& iUpstreamElement;
    IPipelineObserver& iObserver;
    IPipelineElementObserverThread& iObserverThread;
    TUint iEventId;
    std::atomic<MsgMode*> iMsgMode;
    std::atomic<MsgTrack*> iMsgTrack;
    std::atomic<MsgDecodedStream*> iMsgDecodedStreamInfo;
    std::atomic<MsgMetaText*> iMsgMetaText;
    std::atomic<TUint> iSeconds;
    TUint iPrevSeconds;
    TUint iJiffies; // Fraction of a second
    //TBool iNotifyTime;
    std::atomic<EPipelineState> iPipelineState;
    EPipelineState iPrevPipelineState;
    //TBool iNotifyPipelineState;
};

} // namespace Media
} // namespace OpenHome


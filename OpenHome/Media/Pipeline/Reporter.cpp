#include <OpenHome/Media/Pipeline/Reporter.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Pipeline/ElementObserver.h>

#include <atomic>

using namespace OpenHome;
using namespace OpenHome::Media;

// Reporter

const TUint Reporter::kSupportedMsgTypes =   eMode
                                           | eTrack
                                           | eDrain
                                           | eDelay
                                           | eMetatext
                                           | eStreamInterrupted
                                           | eHalt
                                           | eFlush
                                           | eWait
                                           | eDecodedStream
                                           | eAudioPcm
                                           | eAudioDsd
                                           | eSilence
                                           | eQuit;

const Brn Reporter::kNullMetaText("");

Reporter::Reporter(IPipelineElementUpstream& aUpstreamElement, IPipelineObserver& aObserver, IPipelineElementObserverThread& aObserverThread)
    : PipelineElement(kSupportedMsgTypes)
    , iUpstreamElement(aUpstreamElement)
    , iObserver(aObserver)
    , iObserverThread(aObserverThread)
    , iMsgMode(nullptr)
    , iMsgTrack(nullptr)
    , iMsgDecodedStreamInfo(nullptr)
    , iMsgMetaText(nullptr)
    , iSeconds(0)
    , iPrevSeconds(UINT_MAX)
    , iJiffies(0)
    , iPipelineState(EPipelineStopped)
    , iPrevPipelineState(EPipelineStateCount)
{
    /* Older versions of gcc have partial support for atomics, not including is_lock_free for pointer types
    ASSERT(iMsgMode.is_lock_free());
    ASSERT(iMsgTrack.is_lock_free());
    ASSERT(iMsgDecodedStreamInfo.is_lock_free());
    ASSERT(iMsgMetaText.is_lock_free());
    */
    ASSERT(iSeconds.is_lock_free());
    ASSERT(iPipelineState.is_lock_free());
    iEventId = iObserverThread.Register(MakeFunctor(*this, &Reporter::EventCallback));
}

Reporter::~Reporter()
{
    if (iMsgMode.load() != nullptr) {
        iMsgMode.load()->RemoveRef();
    }
    if (iMsgTrack.load() != nullptr) {
        iMsgTrack.load()->RemoveRef();
    }
    if (iMsgDecodedStreamInfo.load() != nullptr) {
        iMsgDecodedStreamInfo.load()->RemoveRef();
    }
    if (iMsgMetaText.load() != nullptr) {
        iMsgMetaText.load()->RemoveRef();
    }
}

void Reporter::SetPipelineState(EPipelineState aState)
{
    iPipelineState.store(aState);
    iObserverThread.Schedule(iEventId);
}

Msg* Reporter::Pull()
{
    Msg* msg = iUpstreamElement.Pull();
    (void)msg->Process(*this);
    return msg;
}

Msg* Reporter::ProcessMsg(MsgMode* aMsg)
{
    auto prevMetatext = iMsgMetaText.exchange(nullptr);
    if (prevMetatext != nullptr) {
        prevMetatext->RemoveRef();
    }
    auto prevStream = iMsgDecodedStreamInfo.exchange(nullptr);
    if (prevStream != nullptr) {
        prevStream->RemoveRef();
    }
    iSeconds.store(0);
    auto prevTrack = iMsgTrack.exchange(nullptr);
    if (prevTrack != nullptr) {
        prevTrack->RemoveRef();
    }
    aMsg->AddRef();
    auto prevMode = iMsgMode.exchange(aMsg);
    if (prevMode != nullptr) {
        prevMode->RemoveRef();
    }
    iObserverThread.Schedule(iEventId);
    return aMsg;
}

Msg* Reporter::ProcessMsg(MsgTrack* aMsg)
{
    if (aMsg->StartOfStream()) {
        auto prevMetatext = iMsgMetaText.exchange(nullptr);
        if (prevMetatext != nullptr) {
            prevMetatext->RemoveRef();
        }
        auto prevStream = iMsgDecodedStreamInfo.exchange(nullptr);
        if (prevStream != nullptr) {
            prevStream->RemoveRef();
        }
        iSeconds.store(0);
    }
    aMsg->AddRef();
    auto prevTrack = iMsgTrack.exchange(aMsg);
    if (prevTrack != nullptr) {
        prevTrack->RemoveRef();
    }
    iObserverThread.Schedule(iEventId);
    return aMsg;
}

Msg* Reporter::ProcessMsg(MsgMetaText* aMsg)
{
    aMsg->AddRef();
    auto prevMetatext = iMsgMetaText.exchange(aMsg);
    if (prevMetatext != nullptr) {
        prevMetatext->RemoveRef();
    }
    iObserverThread.Schedule(iEventId);
    return aMsg;
}

Msg* Reporter::ProcessMsg(MsgDecodedStream* aMsg)
{
    const DecodedStreamInfo& streamInfo = aMsg->StreamInfo();
    TUint64 jiffies = (streamInfo.SampleStart() * Jiffies::kPerSecond) / streamInfo.SampleRate();
    iSeconds.store((TUint)(jiffies / Jiffies::kPerSecond));
    iJiffies = jiffies % Jiffies::kPerSecond;
    aMsg->AddRef();
    auto prevStream = iMsgDecodedStreamInfo.exchange(aMsg);
    if (prevStream != nullptr) {
        prevStream->RemoveRef();
    }
    iObserverThread.Schedule(iEventId);
    return aMsg;
}

Msg* Reporter::ProcessMsg(MsgAudioPcm* aMsg)
{
    ProcessAudio(aMsg);
    return aMsg;
}

Msg* Reporter::ProcessMsg(MsgAudioDsd* aMsg)
{
    ProcessAudio(aMsg);
    return aMsg;
}

void Reporter::ProcessAudio(MsgAudioDecoded* aMsg)
{
    TBool reportChange = false;
    iJiffies += aMsg->Jiffies();
    while (iJiffies > Jiffies::kPerSecond) {
        reportChange = true;
        iSeconds++;
        iJiffies -= Jiffies::kPerSecond;
    }
    if (reportChange) {
        iObserverThread.Schedule(iEventId);
    }
}

void Reporter::EventCallback()
{
    auto msgMode = iMsgMode.exchange(nullptr);
    auto msgTrack = iMsgTrack.exchange(nullptr);
    auto msgStream = iMsgDecodedStreamInfo.exchange(nullptr);;
    auto msgMetatext = iMsgMetaText.exchange(nullptr);;
    const TUint seconds = iSeconds.load();
    const EPipelineState pipelineState = iPipelineState.load();

    if (msgMode != nullptr) {
        iObserver.NotifyMode(msgMode->Mode(), msgMode->Info(), msgMode->TransportControls());
        msgMode->RemoveRef();
    }
    if (msgTrack != nullptr) {
        iObserver.NotifyTrack(msgTrack->Track(), msgTrack->StartOfStream());
        msgTrack->RemoveRef();
    }
    if (msgStream != nullptr) {
        iObserver.NotifyStreamInfo(msgStream->StreamInfo());
        msgStream->RemoveRef();
    }
    if (msgMetatext != nullptr) {
        iObserver.NotifyMetaText(msgMetatext->MetaText());
        msgMetatext->RemoveRef();
    }
    if (iPrevSeconds != seconds) {
        iPrevSeconds = seconds;
        iObserver.NotifyTime(seconds);
    }
    if (iPrevPipelineState != pipelineState) {
        iPrevPipelineState = pipelineState;
        iObserver.NotifyPipelineState(pipelineState);
    }
}

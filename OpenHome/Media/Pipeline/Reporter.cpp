#include <OpenHome/Media/Pipeline/Reporter.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Pipeline/ElementObserver.h>

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
                                           | eBitRate
                                           | eAudioPcm
                                           | eAudioDsd
                                           | eSilence
                                           | eQuit;

const Brn Reporter::kNullMetaText("");

Reporter::Reporter(IPipelineElementUpstream& aUpstreamElement, IPipelineObserver& aObserver, IPipelineElementObserverThread& aObserverThread)
    : PipelineElement(kSupportedMsgTypes)
    , iLock("RPTR")
    , iUpstreamElement(aUpstreamElement)
    , iObserver(aObserver)
    , iObserverThread(aObserverThread)
    , iMsgMode(nullptr)
    , iMsgTrack(nullptr)
    , iMsgDecodedStreamInfo(nullptr)
    , iMsgMetaText(nullptr)
    , iSeconds(0)
    , iJiffies(0)
    , iNotifyTime(false)
    , iPipelineState(EPipelineStopped)
    , iNotifyPipelineState(false)
{
    iEventId = iObserverThread.Register(MakeFunctor(*this, &Reporter::EventCallback));
}

Reporter::~Reporter()
{
    if (iMsgMode != nullptr) {
        iMsgMode->RemoveRef();
    }
    if (iMsgTrack != nullptr) {
        iMsgTrack->RemoveRef();
    }
    if (iMsgDecodedStreamInfo != nullptr) {
        iMsgDecodedStreamInfo->RemoveRef();
    }
    if (iMsgMetaText != nullptr) {
        iMsgMetaText->RemoveRef();
    }
}

void Reporter::SetPipelineState(EPipelineState aState)
{
    AutoMutex amx(iLock);
    iPipelineState = aState;
    iNotifyPipelineState = true;
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
    AutoMutex amx(iLock);
    iMode.Replace(aMsg->Mode());
    if (iMsgMode != nullptr) {
        iMsgMode->RemoveRef();
    }
    iMsgMode = aMsg;
    iMsgMode->AddRef();
    if (iMsgTrack != nullptr) {
        iMsgTrack->RemoveRef();
        iMsgTrack = nullptr;
    }
    if (iMsgDecodedStreamInfo != nullptr) {
        iMsgDecodedStreamInfo->RemoveRef();
        iMsgDecodedStreamInfo = nullptr;
    }
    if (iMsgMetaText != nullptr) {
        iMsgMetaText->RemoveRef();
        iMsgMetaText = nullptr;
    }
    iNotifyTime = false;
    iObserverThread.Schedule(iEventId);
    return aMsg;
}

Msg* Reporter::ProcessMsg(MsgTrack* aMsg)
{
    AutoMutex amx(iLock);
    if (iMsgTrack != nullptr) {
        iMsgTrack->RemoveRef();
    }
    iMsgTrack = aMsg;
    iMsgTrack->AddRef();
    if (aMsg->StartOfStream()) {
        if (iMsgDecodedStreamInfo != nullptr) {
            iMsgDecodedStreamInfo->RemoveRef();
            iMsgDecodedStreamInfo = nullptr;
        }
        if (iMsgMetaText != nullptr) {
            iMsgMetaText->RemoveRef();
            iMsgMetaText = nullptr;
        }
        iNotifyTime = false;
    }
    iObserverThread.Schedule(iEventId);
    return aMsg;
}

Msg* Reporter::ProcessMsg(MsgMetaText* aMsg)
{
    AutoMutex amx(iLock);
    if (iMsgMetaText != nullptr) {
        iMsgMetaText->RemoveRef();
    }
    iMsgMetaText = aMsg;
    iMsgMetaText->AddRef();
    iObserverThread.Schedule(iEventId);
    return aMsg;
}

Msg* Reporter::ProcessMsg(MsgDecodedStream* aMsg)
{
    AutoMutex amx(iLock);
    const DecodedStreamInfo& streamInfo = aMsg->StreamInfo();
    TUint64 jiffies = (streamInfo.SampleStart() * Jiffies::kPerSecond) / streamInfo.SampleRate();
    iSeconds = (TUint)(jiffies / Jiffies::kPerSecond);
    iJiffies = jiffies % Jiffies::kPerSecond;
    if (iMsgDecodedStreamInfo != nullptr) {
        iMsgDecodedStreamInfo->RemoveRef();
    }
    iMsgDecodedStreamInfo = aMsg;
    iMsgDecodedStreamInfo->AddRef();
    iNotifyTime = true;
    iObserverThread.Schedule(iEventId);
    return aMsg;
}

Msg* Reporter::ProcessMsg(MsgBitRate* aMsg)
{
    // FIXME report change in bit rate
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
    AutoMutex amx(iLock);
    TBool reportChange = false;
    iJiffies += aMsg->Jiffies();
    while (iJiffies > Jiffies::kPerSecond) {
        reportChange = true;
        iSeconds++;
        iJiffies -= Jiffies::kPerSecond;
    }
    if (reportChange) {
        iNotifyTime= true;
        iObserverThread.Schedule(iEventId);
    }
}

void Reporter::EventCallback()
{
    iLock.Wait();
    // Mode.
    MsgMode* msgMode = iMsgMode;
    iMsgMode = nullptr;
    // Track.
    MsgTrack* msgTrack = iMsgTrack;
    iMsgTrack = nullptr;
    // Stream info.
    MsgDecodedStream* msgStream = iMsgDecodedStreamInfo;
    iMsgDecodedStreamInfo = nullptr;
    MsgMetaText* msgMetatext = iMsgMetaText;
    iMsgMetaText = nullptr;
    // Time.
    const TUint seconds = iSeconds;
    const TBool notifyTime = iNotifyTime;
    iNotifyTime = false;
    // Pipeline state.
    const EPipelineState pipelineState = iPipelineState;
    const TBool notifyPipelineState = iNotifyPipelineState;
    iNotifyPipelineState = false;
    iLock.Signal();

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
    if (notifyTime) {
        iObserver.NotifyTime(seconds);
    }
    if (notifyPipelineState) {
        iObserver.NotifyPipelineState(pipelineState);
    }
}

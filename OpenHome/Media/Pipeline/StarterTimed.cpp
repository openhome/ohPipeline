#include <OpenHome/Media/Pipeline/StarterTimed.h>
#include <OpenHome/Types.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Env.h>
#include <OpenHome/OsWrapper.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Media/Debug.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Media;

const TUint StarterTimed::kSupportedMsgTypes =   eMode
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

const TUint StarterTimed::kMaxSilenceJiffies = Jiffies::kPerMs * 5;

StarterTimed::StarterTimed(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstream, IAudioTime& aAudioTime)
    : PipelineElement(kSupportedMsgTypes)
    , iMsgFactory(aMsgFactory)
    , iUpstream(aUpstream)
    , iAudioTime(aAudioTime)
    , iLock("STim")
    , iStartTicks(0)
    , iPipelineDelayJiffies(0)
    , iSampleRate(0)
    , iBitDepth(0)
    , iNumChannels(0)
    , iPending(nullptr)
    , iJiffiesRemaining(0)
    , iStartingStream(false)
{
}

StarterTimed::~StarterTimed()
{
    if (iPending != nullptr) {
        iPending->RemoveRef();
    }
}

void StarterTimed::StartAt(TUint64 aTime)
{
    AutoMutex _(iLock);
    iStartTicks = aTime;
}

Msg* StarterTimed::Pull()
{
    Msg* msg = nullptr;
    do {
        if (iJiffiesRemaining != 0) {
            TUint jiffies = std::min(iJiffiesRemaining, kMaxSilenceJiffies);
            msg = iMsgFactory.CreateMsgSilence(jiffies, iSampleRate, iBitDepth, iNumChannels);
            if (iJiffiesRemaining < kMaxSilenceJiffies) {
                iJiffiesRemaining = 0; // CreateMsgSilence rounds to nearest sample so jiffies>iJiffiesRemaining is possible in the final call
            }
            else {
                iJiffiesRemaining -= jiffies;
            }
        }
        else if (iPending != nullptr) {
            msg = iPending;
            iPending = nullptr;
        }
        else {
            msg = iUpstream.Pull();
            msg = msg->Process(*this);
        }
    } while (msg == nullptr);
    return msg;
}

Msg* StarterTimed::ProcessMsg(MsgDelay* aMsg)
{
    iPipelineDelayJiffies = aMsg->TotalJiffies();
    return aMsg;
}

Msg* StarterTimed::ProcessMsg(MsgDecodedStream* aMsg)
{
    const auto& info = aMsg->StreamInfo();
    iSampleRate = info.SampleRate();
    iBitDepth = info.BitDepth();
    iNumChannels = info.NumChannels();
    iStartingStream = true;
    return aMsg;
}

Msg* StarterTimed::ProcessMsg(MsgSilence* aMsg)
{
    TUint64 startTicks = 0;
    if (iStartingStream) {
        AutoMutex _(iLock);
        startTicks = iStartTicks;
        iStartTicks = 0;
        iStartingStream = false;
    }

    if (startTicks > 0) {
        TUint64 ticksNow;
        TUint freq;
        iAudioTime.GetTickCount(iSampleRate, ticksNow, freq);

        if (startTicks > ticksNow) {
            TUint64 delayTicks = startTicks - ticksNow;
            iJiffiesRemaining = 0;
            while (delayTicks > freq) {
                iJiffiesRemaining += Jiffies::kPerSecond;
                delayTicks -= freq;
            }
            iJiffiesRemaining += (TUint)((delayTicks * Jiffies::kPerSecond) / freq);
            iJiffiesRemaining -= iPipelineDelayJiffies; // iPipelineDelayJiffies will already be applied by other pipeline elements
            LOG(kMedia, "StartAt: delay jiffies=%u (%ums)\n", iJiffiesRemaining, Jiffies::ToMs(iJiffiesRemaining));
            iPending = aMsg;
            return nullptr;
        }
    }

    return aMsg;
}


// AudioTimeCpu

AudioTimeCpu::AudioTimeCpu(Environment& aEnv)
    : iOsCtx(aEnv.OsCtx())
{
}

void AudioTimeCpu::GetTickCount(TUint /*aSampleRate*/, TUint64& aTicks, TUint& aFrequency) const
{
    aTicks = Os::TimeInUs(iOsCtx);
    static const TUint kUsTicksPerSecond = 1000000;
    aFrequency = kUsTicksPerSecond;
}

#include <OpenHome/Media/Pipeline/StarvationRamper.h>
#include <OpenHome/Types.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/FlywheelRamper.h>
#include <OpenHome/Media/Pipeline/ElementObserver.h>
#include <OpenHome/Media/Debug.h>
//#include <OpenHome/Private/Timer.h>
//#include <OpenHome/Net/Private/Globals.h>

#include <algorithm>
#include <atomic>

using namespace OpenHome;
using namespace OpenHome::Media;

class FlywheelPlayableCreator : private IMsgProcessor
{
public:
    FlywheelPlayableCreator();
    MsgPlayable* CreatePlayable(Msg* aAudio);
private: // from IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override                 { ASSERTS(); return aMsg; }
    Msg* ProcessMsg(MsgTrack* aMsg) override                { ASSERTS(); return aMsg; }
    Msg* ProcessMsg(MsgDrain* aMsg) override                { ASSERTS(); return aMsg; }
    Msg* ProcessMsg(MsgDelay* aMsg) override                { ASSERTS(); return aMsg; }
    Msg* ProcessMsg(MsgEncodedStream* aMsg) override        { ASSERTS(); return aMsg; }
    Msg* ProcessMsg(MsgStreamSegment* aMsg) override        { ASSERTS(); return aMsg; }
    Msg* ProcessMsg(MsgAudioEncoded* aMsg) override         { ASSERTS(); return aMsg; }
    Msg* ProcessMsg(MsgMetaText* aMsg) override             { ASSERTS(); return aMsg; }
    Msg* ProcessMsg(MsgStreamInterrupted* aMsg) override    { ASSERTS(); return aMsg; }
    Msg* ProcessMsg(MsgHalt* aMsg) override                 { ASSERTS(); return aMsg; }
    Msg* ProcessMsg(MsgFlush* aMsg) override                { ASSERTS(); return aMsg; }
    Msg* ProcessMsg(MsgWait* aMsg) override                 { ASSERTS(); return aMsg; }
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override        { ASSERTS(); return aMsg; }
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgAudioDsd* aMsg) override             { ASSERTS(); return aMsg; }
    Msg* ProcessMsg(MsgSilence* aMsg) override;
    Msg* ProcessMsg(MsgPlayable* aMsg) override             { ASSERTS(); return aMsg; }
    Msg* ProcessMsg(MsgQuit* aMsg) override                 { ASSERTS(); return aMsg; }
private:
    MsgPlayable* iPlayable;
};


FlywheelPlayableCreator::FlywheelPlayableCreator()
    : iPlayable(nullptr)
{
}

MsgPlayable* FlywheelPlayableCreator::CreatePlayable(Msg* aAudio)
{
    iPlayable = nullptr;
    (void)aAudio->Process(*this);
    return iPlayable;
}

Msg* FlywheelPlayableCreator::ProcessMsg(MsgAudioPcm* aMsg)
{
    aMsg->ClearRamp();
    iPlayable = aMsg->CreatePlayable();
    return nullptr;
}

Msg* FlywheelPlayableCreator::ProcessMsg(MsgSilence* aMsg)
{
    aMsg->ClearRamp();
    iPlayable = aMsg->CreatePlayable();
    return nullptr;
}


// FlywheelInput

FlywheelInput::FlywheelInput(TUint aMaxJiffies)
{
    const TUint minJiffiesPerSample = Jiffies::PerSample(kMaxSampleRate);
    const TUint numSamples = (aMaxJiffies + minJiffiesPerSample - 1) / minJiffiesPerSample;
    const TUint channelBytes = numSamples * kSubsampleBytes;
    const TUint bytes = channelBytes * kMaxChannels;
    iPtr = new TByte[bytes];
}

FlywheelInput::~FlywheelInput()
{
    delete[] iPtr;
}

const Brx& FlywheelInput::Prepare(MsgQueueLite& aQueue, TUint aJiffies, TUint aSampleRate, TUint /*aBitDepth*/, TUint aNumChannels)
{
    ASSERT(aNumChannels <= kMaxChannels);
    const TUint numSamples = aJiffies / Jiffies::PerSample(aSampleRate);
    const TUint channelBytes = numSamples * kSubsampleBytes;
    TByte* p = iPtr;
    for (TUint i=0; i<aNumChannels; i++) {
        iChannelPtr[i] = p;
        p += channelBytes;
    }

    FlywheelPlayableCreator playableCreator;
    while (!aQueue.IsEmpty()) {
        MsgPlayable* playable = playableCreator.CreatePlayable(aQueue.Dequeue());
        playable->Read(*this);
        playable->RemoveRef();
    }

    const TUint bytes = channelBytes * aNumChannels;
    iBuf.Set(iPtr, bytes);
    return iBuf;
}

void FlywheelInput::BeginBlock()
{
}

inline void FlywheelInput::AppendSubsample8(TByte*& aDest, const TByte*& aSrc)
{
    *aDest++ = *aSrc++;
    *aDest++ = 0;
    *aDest++ = 0;
    *aDest++ = 0;
}

inline void FlywheelInput::AppendSubsample16(TByte*& aDest, const TByte*& aSrc)
{
    *aDest++ = *aSrc++;
    *aDest++ = *aSrc++;
    *aDest++ = 0;
    *aDest++ = 0;
}

inline void FlywheelInput::AppendSubsample24(TByte*& aDest, const TByte*& aSrc)
{
    *aDest++ = *aSrc++;
    *aDest++ = *aSrc++;
    *aDest++ = *aSrc++;
    *aDest++ = 0;
}

inline void FlywheelInput::AppendSubsample32(TByte*& aDest, const TByte*& aSrc)
{
    *aDest++ = *aSrc++;
    *aDest++ = *aSrc++;
    *aDest++ = *aSrc++;
    *aDest++ = *aSrc++;
}

void FlywheelInput::ProcessFragment(const Brx& aData, TUint aNumChannels, TUint aSubsampleBytes)
{
    DoProcessFragment(aData, aNumChannels, aSubsampleBytes);
}

void FlywheelInput::ProcessSilence(const Brx& aData, TUint aNumChannels, TUint aSubsampleBytes)
{
    DoProcessFragment(aData, aNumChannels, aSubsampleBytes);
}

void FlywheelInput::DoProcessFragment(const Brx& aData, TUint aNumChannels, TUint aSubsampleBytes)
{
    const TByte* src = aData.Ptr();
    const TUint numSubsamples = aData.Bytes() / aSubsampleBytes;
    const TUint numSamples = numSubsamples / aNumChannels;
    for (TUint i=0; i<numSamples; i++) {
        for (TUint j=0; j<aNumChannels; j++) {
            switch (aSubsampleBytes)
            {
            case 1:
                AppendSubsample8(iChannelPtr[j], src);
                break;
            case 2:
                AppendSubsample16(iChannelPtr[j], src);
                break;
            case 3:
                AppendSubsample24(iChannelPtr[j], src);
                break;
            case 4:
                AppendSubsample32(iChannelPtr[j], src);
                break;
            default:
                ASSERTS();
                break;
            }
        }
    }
}

void FlywheelInput::EndBlock()
{
}

void FlywheelInput::Flush()
{
}


// RampGenerator

RampGenerator::RampGenerator(MsgFactory& aMsgFactory, TUint aInputJiffies, TUint aRampJiffies, TUint aThreadPriority)
    : iMsgFactory(aMsgFactory)
    , iRampJiffies(aRampJiffies)
    , iSem("FWRG", 0)
    , iRecentAudio(nullptr)
    , iSampleRate(0)
    , iNumChannels(0)
    , iBitDepth(0)
    , iCurrentRampValue(Ramp::kMax)
    , iRemainingRampSize(0)
{
    ASSERT(iActive.is_lock_free());
    iActive.store(false);

    iFlywheelRamper = new FlywheelRamperManager(*this, aInputJiffies, aRampJiffies);

    const TUint minJiffiesPerSample = Jiffies::PerSample(kMaxSampleRate);
    const TUint numSamples = (FlywheelRamperManager::kMaxOutputJiffiesBlockSize + minJiffiesPerSample - 1) / minJiffiesPerSample;
    const TUint channelBytes = numSamples * kSubsampleBytes;
    const TUint bytes = channelBytes * kMaxChannels;
    iFlywheelAudio = new Bwh(bytes);

    iThread = new ThreadFunctor("FlywheelRamper",
                                MakeFunctor(*this, &RampGenerator::FlywheelRamperThread),
                                aThreadPriority);
    iThread->Start();
}

RampGenerator::~RampGenerator()
{
    ASSERT(iQueue.IsEmpty());
    delete iThread;
    delete iFlywheelAudio;
    delete iFlywheelRamper;
}

void RampGenerator::Start(const Brx& aRecentAudio, TUint aSampleRate, TUint aNumChannels, TUint aBitDepth, TUint aCurrentRampValue)
{
    iRecentAudio = &aRecentAudio;
    iSampleRate = aSampleRate;
    iNumChannels = aNumChannels;
    iBitDepth = aBitDepth;
    iCurrentRampValue = aCurrentRampValue;
    const TUint genSampleCount = Jiffies::ToSamples(iRampJiffies, iSampleRate);
    iRemainingRampSize = Jiffies::PerSample(iSampleRate) * genSampleCount;
    (void)iSem.Clear();
    iActive.store(true);
    iThread->Signal();
}

TBool RampGenerator::TryGetAudio(Msg*& aMsg)
{
    if (!iActive.load() && iQueue.IsEmpty()) {
        return false;
    }
    iSem.Wait();
    if (!iActive.load() && iQueue.IsEmpty()) {
        return false;
    }
    aMsg = iQueue.Dequeue();
    return true;
}

void RampGenerator::FlywheelRamperThread()
{
    try {
        for (;;) {
            iThread->Wait();
            iFlywheelRamper->Ramp(*iRecentAudio, iSampleRate, iNumChannels);
            iActive.store(false);
            iSem.Signal();
        }
    }
    catch (ThreadKill&) {
    }
}

void RampGenerator::BeginBlock()
{
    iFlywheelAudio->SetBytes(0);
}

void RampGenerator::ProcessFragment(const Brx& aData, TUint /*aNumChannels*/, TUint /*aSubsampleBytes*/)
{
    const TUint subsamples = aData.Bytes() / 4;
    const TByte* src = aData.Ptr();
    TByte* dest = const_cast<TByte*>(iFlywheelAudio->Ptr() + iFlywheelAudio->Bytes());
    switch (iBitDepth)
    {
    case 8:
        for (TUint i = 0; i<subsamples; i++) {
            *dest++ = *src++;
            src++;
            src++;
            src++;
        }
        break;
    case 16:
        for (TUint i = 0; i<subsamples; i++) {
            *dest++ = *src++;
            *dest++ = *src++;
            src++;
            src++;
        }
        break;
    case 24:
        for (TUint i = 0; i<subsamples; i++) {
            *dest++ = *src++;
            *dest++ = *src++;
            *dest++ = *src++;
            src++;
        }
        break;
    case 32:
        // Pipeline only guarantees to support up to 24-bit audio so discard least significant byte
        for (TUint i = 0; i<subsamples; i++) {
            *dest++ = *src++;
            *dest++ = *src++;
            *dest++ = *src++;
            *dest++ = 0;
            src++;
        }
        break;
    default:
        ASSERTS();
        break;
    }
    iFlywheelAudio->SetBytes(iFlywheelAudio->Bytes() + (subsamples * (iBitDepth/8)));
    //Log::Print("++ RampGenerator::ProcessFragment numSamples=%u\n", aData.Bytes() / 8);
#if 0
    if (aNumChannels == 2) {
        const TByte* p = aData.Ptr();
        static const TUint stride = 8;
        const TUint samples = aData.Bytes() / stride;
        ASSERT(aData.Bytes() % stride == 0);
        for (TUint i=0; i<samples; i++) {
            TByte b[stride];
            for (TUint j=0; j<stride; j++) {
                b[j] = *p++;
            }
            Log::Print("  %02x%02x%02x%02x  %02x%02x%02x%02x\n", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7]);
        }
    }
#endif
}

void RampGenerator::ProcessSilence(const Brx& /*aData*/, TUint /*aNumChannels*/, TUint /*aSubsampleBytes*/)
{
    // FlywheelRamper outputs 32-bit fragments only
    ASSERTS();
}

void RampGenerator::EndBlock()
{
    auto audio = iMsgFactory.CreateMsgAudioPcm(*iFlywheelAudio, iNumChannels, iSampleRate, iBitDepth, AudioDataEndian::Big, MsgAudioPcm::kTrackOffsetInvalid);
    if (iCurrentRampValue == Ramp::kMin) {
        audio->SetMuted();
    }
    else {
        MsgAudio* split;
        iCurrentRampValue = audio->SetRamp(iCurrentRampValue, iRemainingRampSize, Ramp::EDown, split);
        ASSERT(split == nullptr);
    }
    iQueue.Enqueue(audio);
    iSem.Signal();
}

void RampGenerator::Flush()
{
    ASSERTS();
}


// StarvationRamper

const TUint StarvationRamper::kTrainingJiffies    = Jiffies::kPerMs * 1;
const TUint StarvationRamper::kRampDownJiffies    = Jiffies::kPerMs * 20;
const TUint StarvationRamper::kMaxAudioOutJiffies = Jiffies::kPerMs * 5;

StarvationRamper::StarvationRamper(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstream,
                                   IStarvationRamperObserver& aObserver,
                                   IPipelineElementObserverThread& aObserverThread, TUint aSizeJiffies,
                                   TUint aThreadPriority, TUint aRampUpSize, TUint aMaxStreamCount)
    : iMsgFactory(aMsgFactory)
    , iUpstream(aUpstream)
    , iObserver(aObserver)
    , iObserverThread(aObserverThread)
    , iMaxJiffies(aSizeJiffies)
    , iThreadPriorityFlywheelRamper(aThreadPriority)
    , iThreadPriorityStarvationRamper(iThreadPriorityFlywheelRamper-1)
    , iRampUpJiffies(aRampUpSize)
    , iMaxStreamCount(aMaxStreamCount)
    , iLock("SRM1")
    , iSem("SRM2", 0)
    , iFlywheelInput(kTrainingJiffies)
    , iRecentAudioJiffies(0)
    , iStreamHandler(nullptr)
    , iState(State::Halted)
    , iStarving(false)
    , iExit(false)
    , iStartDrain(false)
    , iDraining(false)
    , iStreamId(IPipelineIdProvider::kStreamIdInvalid)
    , iSampleRate(0)
    , iBitDepth(0)
    , iNumChannels(0)
    , iFormat(AudioFormat::Undefined)
    , iCurrentRampValue(Ramp::kMin)
    , iRemainingRampSize(0)
    , iTargetFlushId(MsgFlush::kIdInvalid)
    , iLastPulledAudioRampValue(Ramp::kMax)
    , iTrackStreamCount(0)
    , iDrainCount(0)
    , iHaltCount(0)
    , iStartOccupancyJiffies(0)
    , iSemStartOccupancy("SRM3", 0)
    , iLastEventBuffering(false)
{
    ASSERT(iEventBuffering.is_lock_free());
    iEventId = iObserverThread.Register(MakeFunctor(*this, &StarvationRamper::EventCallback));
    iEventBuffering.store(false); // ensure SetBuffering call below detects a state change
    SetBuffering(true);

    iRampGenerator = new RampGenerator(aMsgFactory, kTrainingJiffies, kRampDownJiffies, iThreadPriorityFlywheelRamper);
    iPullerThread = new ThreadFunctor("StarvationRamper",
                                      MakeFunctor(*this, &StarvationRamper::PullerThread),
                                      iThreadPriorityStarvationRamper);
    iPullerThread->Start();
}

StarvationRamper::~StarvationRamper()
{
    delete iPullerThread;
    delete iRampGenerator;
}

void StarvationRamper::Flush(TUint aId)
{
    AutoMutex _(iLock);
    iTargetFlushId = aId;
    iCurrentRampValue = Ramp::kMax;
    iRemainingRampSize = kRampDownJiffies;
    iState = State::RampingDown;
}

void StarvationRamper::DrainAllAudio()
{
    iStartDrain.store(true);
}

TUint StarvationRamper::SizeInJiffies() const
{
    return Jiffies();
}

TUint StarvationRamper::ThreadPriorityFlywheelRamper() const
{
    return iThreadPriorityFlywheelRamper;
}

TUint StarvationRamper::ThreadPriorityStarvationRamper() const
{
    return iThreadPriorityStarvationRamper;
}

inline TBool StarvationRamper::IsFull() const
{
    return (Jiffies() >= iMaxJiffies || DecodedStreamCount() == iMaxStreamCount);
}

void StarvationRamper::PullerThread()
{
    do {
        Msg* msg = iUpstream.Pull();
        iLock.Wait();
        DoEnqueue(msg);
        TBool isFull = IsFull();
        if (isFull) {
            iSem.Clear();
        }
        const auto startOccupancy = iStartOccupancyJiffies.load();
        const auto triggerStart = startOccupancy > 0 && Jiffies() >= startOccupancy;
        iLock.Signal();
        if (triggerStart) {
            iSemStartOccupancy.Signal();
        }
        if (isFull) {
            iSem.Wait();
        }
    } while (!iExit);
}

void StarvationRamper::StartFlywheelRamp()
{
    LOG(kPipeline, "StarvationRamper::StartFlywheelRamp()\n");
//    const TUint startTime = Time::Now(*gEnv);
    if (iRecentAudioJiffies > kTrainingJiffies) {
        TInt excess = iRecentAudioJiffies - kTrainingJiffies;
        while (excess > 0) {
            MsgAudio* audio = static_cast<MsgAudio*>(iRecentAudio.Dequeue());
            if (audio->Jiffies() > (TUint)excess) {
                MsgAudio* remaining = audio->Split((TUint)excess);
                iRecentAudio.EnqueueAtHead(remaining);
            }
            const TUint msgJiffies = audio->Jiffies();
            excess -= msgJiffies;
            iRecentAudioJiffies -= msgJiffies;
            audio->RemoveRef();
        }
    }
    else {
        TInt remaining = kTrainingJiffies - iRecentAudioJiffies;
        while (remaining > 0) {
            TUint size = std::min((TUint)remaining, kMaxAudioOutJiffies);
            auto silence = iMsgFactory.CreateMsgSilence(size, iSampleRate, iBitDepth, iNumChannels);
            iRecentAudio.EnqueueAtHead(silence);
            size = silence->Jiffies(); // original size may have been rounded to a sample boundary
            remaining -= size;
            iRecentAudioJiffies += size;
        }
    }

    const Brx& recentSamples = iFlywheelInput.Prepare(iRecentAudio, iRecentAudioJiffies, iSampleRate, iBitDepth, iNumChannels);
//    const TUint prepEnd = Time::Now(*gEnv);
    iRecentAudioJiffies = 0;
    ASSERT(iRecentAudio.IsEmpty());

    TUint rampStart = iCurrentRampValue;
    /*if (rampStart == Ramp::kMax) {
        rampStart = iLastPulledAudioRampValue;
    }*/
    iRampGenerator->Start(recentSamples, iSampleRate, iNumChannels, iBitDepth, rampStart);
//    const TUint flywheelEnd = Time::Now(*gEnv);
    iState = State::FlywheelRamping;
//    Log::Print("StarvationRamper::StartFlywheelRamp rampStart=%08x, prepTime=%ums, flywheelTime=%ums\n", rampStart, prepEnd - startTime, flywheelEnd - prepEnd);

    iStarving = true;
    iStreamHandler->NotifyStarving(iMode, iStreamId, true);
}

void StarvationRamper::NewStream()
{
    iState = State::Starting;
    iRecentAudio.Clear();
    iRecentAudioJiffies = 0;
    iStreamId = IPipelineIdProvider::kStreamIdInvalid;
    iLastPulledAudioRampValue = Ramp::kMax;
}

void StarvationRamper::ProcessAudioOut(MsgAudio* aMsg)
{
    if (iStarving) {
        iStarving = false;
        iStreamHandler->NotifyStarving(iMode, iStreamId, false);
    }

    if (iFormat == AudioFormat::Dsd) {
        // following code prepares for later use of FlywheelRamper.
        // This isn't available for DSD so prep can be skipped.
        return;
    }

    iLastPulledAudioRampValue = aMsg->Ramp().End();

    auto clone = aMsg->Clone();
    iRecentAudio.Enqueue(clone);
    iRecentAudioJiffies += clone->Jiffies();
    if (iRecentAudioJiffies > kTrainingJiffies && iRecentAudio.NumMsgs() > 1) {
        MsgAudio* audio = static_cast<MsgAudio*>(iRecentAudio.Dequeue());
        iRecentAudioJiffies -= audio->Jiffies();
        if (iRecentAudioJiffies >= kTrainingJiffies) {
            audio->RemoveRef();
        }
        else {
            iRecentAudio.EnqueueAtHead(audio);
            iRecentAudioJiffies += audio->Jiffies();
        }
    }
}

void StarvationRamper::ApplyRamp(MsgAudioDecoded* aMsg)
{
    if (aMsg->Jiffies() > iRemainingRampSize) {
        MsgAudio* remaining = aMsg->Split(iRemainingRampSize);
        EnqueueAtHead(remaining);
    }
    MsgAudio* split;
    auto direction = iState == State::RampingUp ? Ramp::EUp : Ramp::EDown;
    iCurrentRampValue = aMsg->SetRamp(iCurrentRampValue, iRemainingRampSize, direction, split);
    if (split != nullptr) {
        EnqueueAtHead(split);
    }
    if (iRemainingRampSize == 0) {
        if (iState == State::RampingUp) {
            iState = State::Running;
        }
        else if (iTargetFlushId != MsgFlush::kIdInvalid) {
            iState = State::Flushing;
        }
        else {
            iState = State::FlywheelRamping; // FIXME - cheat to have a Halt generated but
                                             // not processed before start moves to RampingUp
        }
    }
}

void StarvationRamper::SetBuffering(TBool aBuffering)
{
    const TBool prev = iEventBuffering.exchange(aBuffering);
    if (prev != aBuffering) {
        iObserverThread.Schedule(iEventId);
    }
}

void StarvationRamper::EventCallback()
{
    const TBool buffering = iEventBuffering.load();
    if (buffering != iLastEventBuffering) {
        iObserver.NotifyStarvationRamperBuffering(buffering);
        iLastEventBuffering = buffering;
    }
}

Msg* StarvationRamper::Pull()
{
    {
        const auto startOccupancy = iStartOccupancyJiffies.load();
        if (startOccupancy > 0 &&
            iDrainCount.load() == 0 && iHaltCount.load() == 0) {
            if (Jiffies() < startOccupancy) {
                iSemStartOccupancy.Wait();
            }
            iStartOccupancyJiffies.store(0);
            iAudioOutSinceLastStartOccupancy = 0;
        }
    }

    if (IsEmpty() || iStartDrain.load()) {
        SetBuffering(true);
        if (iStartDrain.load()) {
            iStartDrain.store(false);
            iDraining.store(true);
        }
        if ((iState == State::Running ||
            (iState == State::RampingUp && iCurrentRampValue != Ramp::kMin)) && !iExit) {
            StartFlywheelRamp();
        }
    }

    Msg* msg = nullptr;
    do {
        if (iRampGenerator->TryGetAudio(msg)) {
            return msg;
        }
        else if (iState == State::FlywheelRamping) {
            iState = State::RampingUp;
            iCurrentRampValue = Ramp::kMin;
            iRemainingRampSize = iRampUpJiffies;
            return iMsgFactory.CreateMsgHalt();
        }

        const TBool wasFlushing = iState == State::Flushing;
        msg = DoDequeue(true);
        iLock.Wait();
        if (!IsFull()) {
            iSem.Signal();
        }
        iLock.Signal();
        if (wasFlushing && iState == State::Flushing && msg != nullptr) {
            msg->RemoveRef();
            msg = nullptr;
        }
    } while (msg == nullptr);
    return msg;
}

void StarvationRamper::ProcessMsgIn(MsgTrack* /*aMsg*/)
{
    iTrackStreamCount++;
}

void StarvationRamper::ProcessMsgIn(MsgDrain* /*aMsg*/)
{
    iDrainCount++;
    iSemStartOccupancy.Signal();
}

void StarvationRamper::ProcessMsgIn(MsgDelay* aMsg)
{
    iMaxJiffies = std::max(aMsg->RemainingJiffies(), 140 * Jiffies::kPerMs);
}

void StarvationRamper::ProcessMsgIn(MsgHalt* /*aMsg*/)
{
    iHaltCount++;
    iSemStartOccupancy.Signal();
}

void StarvationRamper::ProcessMsgIn(MsgDecodedStream* /*aMsg*/)
{
    iTrackStreamCount++;
}

void StarvationRamper::ProcessMsgIn(MsgQuit* /*aMsg*/)
{
    iExit = true;
}

Msg* StarvationRamper::ProcessMsgOut(MsgMode* aMsg)
{
    NewStream();
    iMode.Replace(aMsg->Mode());
    return aMsg;
}

Msg* StarvationRamper::ProcessMsgOut(MsgTrack* aMsg)
{
    NewStream();
    iTrackStreamCount--;
    aMsg->RemoveRef();
    return nullptr;
}

Msg* StarvationRamper::ProcessMsgOut(MsgDrain* aMsg)
{
    iDrainCount--;
    iDraining.store(false);
    if (iState == State::Running ||
        (iState == State::RampingUp && iCurrentRampValue != Ramp::kMin)) {
        EnqueueAtHead(aMsg);
        SetBuffering(true);
        StartFlywheelRamp();
        return nullptr;
    }
    return aMsg;
}

Msg* StarvationRamper::ProcessMsgOut(MsgMetaText* aMsg)
{
    aMsg->RemoveRef();
    return nullptr;
}

Msg* StarvationRamper::ProcessMsgOut(MsgHalt* aMsg)
{
    // set Halted state on both entry and exit of this msg
    // ...on entry to avoid us starting a ramp down before outputting a Halt
    // ...on exit in case Halted state from entry was reset by outputting Audio
    iState = State::Halted;
    iHaltCount--;
    return aMsg;
}

Msg* StarvationRamper::ProcessMsgOut(MsgFlush* aMsg)
{
    const auto id = aMsg->Id();
    aMsg->RemoveRef();
    if (iTargetFlushId != MsgFlush::kIdInvalid && id == iTargetFlushId) {
        if (iState == State::RampingDown) {
            StartFlywheelRamp();
        }
        else if (iState == State::Flushing) {
            iState = State::Halted;
            iTargetFlushId = MsgFlush::kIdInvalid;
            return iMsgFactory.CreateMsgHalt();
        }
    }
    return nullptr;
}

Msg* StarvationRamper::ProcessMsgOut(MsgWait* aMsg)
{
    aMsg->RemoveRef();
    return nullptr;
}

Msg* StarvationRamper::ProcessMsgOut(MsgDecodedStream* aMsg)
{
    NewStream();
    iTrackStreamCount--;

    auto streamInfo = aMsg->StreamInfo();
    iStreamId = streamInfo.StreamId();
    iStreamHandler = streamInfo.StreamHandler();
    iSampleRate = streamInfo.SampleRate();
    iBitDepth = streamInfo.BitDepth();
    iNumChannels = streamInfo.NumChannels();
    iFormat = streamInfo.Format();
    iCurrentRampValue = Ramp::kMax;
    return aMsg;
}

Msg* StarvationRamper::ProcessMsgOut(MsgAudioPcm* aMsg)
{
    ++iAudioOutSinceLastStartOccupancy;
    if (iDraining.load()) {
        aMsg->RemoveRef();
        return nullptr;
    }
    if (iState == State::Starting || iState == State::Halted) {
        iState = State::Running;
    }

    if (aMsg->Jiffies() > kMaxAudioOutJiffies) {
        auto split = aMsg->Split(kMaxAudioOutJiffies);
        EnqueueAtHead(split);
    }

    if ((iState == State::RampingUp || iState == State::RampingDown) && iRemainingRampSize > 0) {
        if (aMsg->Jiffies() > iRemainingRampSize) {
            MsgAudio* remaining = aMsg->Split(iRemainingRampSize);
            EnqueueAtHead(remaining);
        }
        MsgAudio* split;
        auto direction = iState == State::RampingUp ? Ramp::EUp : Ramp::EDown;
        iCurrentRampValue = aMsg->SetRamp(iCurrentRampValue, iRemainingRampSize, direction, split);
        if (split != nullptr) {
            EnqueueAtHead(split);
        }
        if (iRemainingRampSize == 0) {
            if (iState == State::RampingUp) {
                iState = State::Running;
            }
            else {
                iState = State::Flushing;
            }
        }
    }

    ProcessAudioOut(aMsg);
    SetBuffering(false);

    return aMsg;
}

Msg* StarvationRamper::ProcessMsgOut(MsgAudioDsd* aMsg)
{
    if (iDraining.load()) {
        aMsg->RemoveRef();
        return nullptr;
    }
    if (iState == State::Starting || iState == State::Halted) {
        iState = State::Running;
    }

    if (aMsg->Jiffies() > kMaxAudioOutJiffies) {
        auto split = aMsg->Split(kMaxAudioOutJiffies);
        EnqueueAtHead(split);
    }

    switch (iState)
    {
    case State::Running:
        if (Jiffies() <= kRampDownJiffies &&
            iHaltCount.load() == 0 && iTrackStreamCount.load() == 0) {
            iState = State::RampingDown;
            iCurrentRampValue = Ramp::kMax;
            iRemainingRampSize = aMsg->Jiffies() + Jiffies();
            ApplyRamp(aMsg);
        }
        break;
    case State::RampingDown:
        ApplyRamp(aMsg);
        if (iState == State::FlywheelRamping) {
            iStarving = true;
            iStreamHandler->NotifyStarving(iMode, iStreamId, true);
        }
        break;
    case State::RampingUp:
        if (Jiffies() <= kRampDownJiffies &&
            iHaltCount.load() == 0 && iTrackStreamCount.load() == 0) {
            // less audio than would be required for an emergency ramp from Running
            // ...ramp back down to silence immediately
            if (iCurrentRampValue == Ramp::kMin) {
                aMsg->SetMuted();
            }
            else {
                iState = State::RampingDown;
                // leave iCurrentRampValue unchanged
                iRemainingRampSize = aMsg->Jiffies() + Jiffies(); // ramp down over all remaining audio
                ApplyRamp(aMsg);
            }
        }
        else {
            ApplyRamp(aMsg);
        }
        break;
    default:
        break;
    }

    ProcessAudioOut(aMsg);
    SetBuffering(false);

    return aMsg;
}

Msg* StarvationRamper::ProcessMsgOut(MsgSilence* aMsg)
{
    if (iDraining.load()) {
        aMsg->RemoveRef();
        return nullptr;
    }
    if (iState == State::Halted) {
        iState = State::Starting;
    }
    if (aMsg->Jiffies() > kMaxAudioOutJiffies) {
        auto split = aMsg->Split(kMaxAudioOutJiffies);
        EnqueueAtHead(split);
    }
    ProcessAudioOut(aMsg);
    return aMsg;
}

void StarvationRamper::WaitForOccupancy(TUint aJiffies)
{
    if (iDrainCount.load() > 0 || iHaltCount.load() > 0) {
        return;
    }
    (void)iSemStartOccupancy.Clear();
    iStartOccupancyJiffies.store(aJiffies);
}

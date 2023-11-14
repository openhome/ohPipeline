// AllocatorBase

inline const TChar* AllocatorBase::Name() const
{
    return iName;
}


// Allocated

inline TUint Allocated::RefCount() const
{
    return iRefCount.load();
}

// Jiffies

inline TUint Jiffies::ToMs(TUint aJiffies)
{ // static
    return aJiffies / kPerMs;
}
inline TUint Jiffies::ToMs(TUint64 aJiffies)
{ // static
    return static_cast<TUint>(aJiffies / kPerMs);
}
inline TUint Jiffies::ToSamples(TUint aJiffies, TUint aSampleRate)
{ // static
    return aJiffies / PerSample(aSampleRate);
}
inline TUint64 Jiffies::ToSamples(TUint64 aJiffies, TUint aSampleRate)
{ // static
    return aJiffies / PerSample(aSampleRate);
}


// Ramp

inline TUint Ramp::Start() const
{
    return iStart;
}
inline TUint Ramp::End() const
{
    return iEnd;
}
inline Ramp::EDirection Ramp::Direction() const
{
    return iDirection;
}
inline TBool Ramp::IsEnabled() const
{
    return iEnabled;
}


// ModeInfo

inline ModeInfo::ModeInfo()
{
    Clear();
}
inline ModeInfo::ModeInfo(Latency aLatencyMode)
{
    Clear();
    iLatencyMode = aLatencyMode;
}
inline void ModeInfo::SetLatencyMode(Latency aLatencyMode)
{
    iLatencyMode = aLatencyMode;
}
inline void ModeInfo::SetSupportsPause(TBool aSupportsPause)
{
    iSupportsPause = aSupportsPause;
}
inline void ModeInfo::SetSupportsNextPrev(TBool aSupportsNext, TBool aSupportsPrev)
{
    iSupportsNext = aSupportsNext;
    iSupportsPrev = aSupportsPrev;
}
inline void ModeInfo::SetSupportsRepeatRandom(TBool aSupportsRepeat, TBool aSupportsRandom)
{
    iSupportsRepeat  = aSupportsRepeat;
    iSupportsRandom  = aSupportsRandom;
}
inline void ModeInfo::SetRampDurations(TBool aPauseResume, TBool aSkip)
{
    iRampPauseResumeLong = aPauseResume;
    iRampSkipLong        = aSkip;
}
inline Latency ModeInfo::LatencyMode() const
{
    return iLatencyMode;
}
inline TBool ModeInfo::SupportsPause() const
{
    return iSupportsPause;
}
inline TBool ModeInfo::SupportsNext() const
{
    return iSupportsNext;
}
inline TBool ModeInfo::SupportsPrev() const
{
    return iSupportsPrev;
}
inline TBool ModeInfo::SupportsRepeat() const
{
    return iSupportsRepeat;
}
inline TBool ModeInfo::SupportsRandom() const
{
    return iSupportsRandom;
}
inline TBool ModeInfo::RampPauseResumeLong() const
{
    return iRampPauseResumeLong;
}
inline TBool ModeInfo::RampSkipLong() const
{
    return iRampSkipLong;
}


// ModeTransportControls

inline void ModeTransportControls::SetPlay(Functor aPlay)
{
    iPlay = aPlay;
}
inline void ModeTransportControls::SetPause(Functor aPause)
{
    iPause = aPause;
}
inline void ModeTransportControls::SetStop(Functor aStop)
{
    iStop = aStop;
}
inline void ModeTransportControls::SetNext(Functor aNext)
{
    iNext = aNext;
}
inline void ModeTransportControls::SetPrev(Functor aPrev)
{
    iPrev = aPrev;
}
inline void ModeTransportControls::SetSeek(FunctorGeneric<TUint> aSeek)
{
    iSeek = aSeek;
}
inline Functor ModeTransportControls::Play() const
{
    return iPlay;
}
inline Functor ModeTransportControls::Pause() const
{
    return iPause;
}
inline Functor ModeTransportControls::Stop() const
{
    return iStop;
}
inline Functor ModeTransportControls::Next() const
{
    return iNext;
}
inline Functor ModeTransportControls::Prev() const
{
    return iPrev;
}
inline FunctorGeneric<TUint> ModeTransportControls::Seek() const
{
    return iSeek;
}


// MsgAudioEncoded, MsgAudioPcm

#ifdef TIMESTAMP_LOGGING_ENABLE
inline void MsgAudioEncoded::AddLogPoint(const TChar* aId)
{
    iAudioData->SetTimestamp(aId);
}
inline void MsgAudioPcm::AddLogPoint(const TChar* aId)
{
    iAudioData->SetTimestamp(aId);
}
#else
inline void MsgAudioEncoded::AddLogPoint(const TChar* /*aId*/)
{
}
inline void MsgAudioPcm::AddLogPoint(const TChar* /*aId*/)
{
}
#endif


// DecodedStreamInfo

inline TUint DecodedStreamInfo::StreamId() const
{
    return iStreamId;
}
inline TUint DecodedStreamInfo::BitRate() const
{
    return iBitRate;
}
inline TUint DecodedStreamInfo::BitDepth() const
{
    return iBitDepth;
}
inline TUint DecodedStreamInfo::SampleRate() const
{
    return iSampleRate;
}
inline TUint DecodedStreamInfo::NumChannels() const
{
    return iNumChannels;
}
inline const Brx& DecodedStreamInfo::CodecName() const
{
    return iCodecName;
}
inline TUint64 DecodedStreamInfo::TrackLength() const
{
    return iTrackLength;
}
inline TUint64 DecodedStreamInfo::SampleStart() const
{
    return iSampleStart;
}
inline TBool DecodedStreamInfo::Lossless() const
{
    return iLossless;
}
inline TBool DecodedStreamInfo::Seekable() const
{
    return iSeekable;
}
inline TBool DecodedStreamInfo::Live() const
{
    return iLive;
}
inline TBool DecodedStreamInfo::AnalogBypass() const
{
    return iAnalogBypass;
}
inline Media::Multiroom DecodedStreamInfo::Multiroom() const
{
    return iMultiroom;
}
inline AudioFormat DecodedStreamInfo::Format() const
{
    return iFormat;
}
inline const SpeakerProfile& DecodedStreamInfo::Profile() const
{
    return iProfile;
}
inline IStreamHandler* DecodedStreamInfo::StreamHandler() const
{
    return iStreamHandler;
}
inline RampType DecodedStreamInfo::Ramp() const
{
    return iRamp;
}


// MsgQueueLite

inline void MsgQueueLite::Enqueue(Msg* aMsg)
{
    DoEnqueue(aMsg);
}
inline Msg* MsgQueueLite::Dequeue()
{
    return DoDequeue();
}
inline void MsgQueueLite::EnqueueAtHead(Msg* aMsg)
{
    DoEnqueueAtHead(aMsg);
}
inline TBool MsgQueueLite::IsEmpty() const
{
    return MsgQueueBase::IsEmpty();
}
inline void MsgQueueLite::Clear()
{
    MsgQueueBase::DoClear();
}
inline TUint MsgQueueLite::NumMsgs() const
{
    return MsgQueueBase::NumMsgs();
}


// MsgFactoryInitParams

inline MsgFactoryInitParams::MsgFactoryInitParams()
    : iMsgModeCount(1)
    , iMsgTrackCount(1)
    , iMsgDrainCount(1)
    , iMsgDelayCount(1)
    , iMsgEncodedStreamCount(1)
    , iMsgStreamSegmentCount(1)
    , iEncodedAudioCount(1)
    , iMsgAudioEncodedCount(1)
    , iMsgMetaTextCount(1)
    , iMsgStreamInterruptedCount(1)
    , iMsgHaltCount(1)
    , iMsgFlushCount(1)
    , iMsgWaitCount(1)
    , iMsgDecodedStreamCount(1)
    , iMsgBitRateCount(1)
    , iDecodedAudioCount(1)
    , iMsgAudioPcmCount(1)
    , iMsgAudioDsdCount(1)
    , iMsgSilenceCount(1)
    , iMsgPlayablePcmCount(1)
    , iMsgPlayableDsdCount(1)
    , iMsgPlayableSilenceCount(1)
    , iMsgQuitCount(1)
{
}
inline void MsgFactoryInitParams::SetMsgModeCount(TUint aCount)
{
    iMsgModeCount = aCount;
}
inline void MsgFactoryInitParams::SetMsgTrackCount(TUint aCount)
{
    iMsgTrackCount = aCount;
}
inline void MsgFactoryInitParams::SetMsgDrainCount(TUint aCount)
{
    iMsgDrainCount = aCount;
}
inline void MsgFactoryInitParams::SetMsgDelayCount(TUint aCount)
{
    iMsgDelayCount = aCount;
}
inline void MsgFactoryInitParams::SetMsgEncodedStreamCount(TUint aCount)
{
    iMsgEncodedStreamCount = aCount;
}
inline void MsgFactoryInitParams::SetMsgStreamSegmentCount(TUint aCount)
{
    iMsgStreamSegmentCount = aCount;
}
inline void MsgFactoryInitParams::SetMsgAudioEncodedCount(TUint aCount, TUint aEncodedAudioCount)
{
    iMsgAudioEncodedCount = aCount;
    iEncodedAudioCount = aEncodedAudioCount;
}
inline void MsgFactoryInitParams::SetMsgMetaTextCount(TUint aCount)
{
    iMsgMetaTextCount = aCount;
}
inline void MsgFactoryInitParams::SetMsgStreamInterruptedCount(TUint aCount)
{
    iMsgStreamInterruptedCount = aCount;
}
inline void MsgFactoryInitParams::SetMsgHaltCount(TUint aCount)
{
    iMsgHaltCount = aCount;
}
inline void MsgFactoryInitParams::SetMsgFlushCount(TUint aCount)
{
    iMsgFlushCount = aCount;
}
inline void MsgFactoryInitParams::SetMsgWaitCount(TUint aCount)
{
    iMsgWaitCount = aCount;
}
inline void MsgFactoryInitParams::SetMsgDecodedStreamCount(TUint aCount)
{
    iMsgDecodedStreamCount = aCount;
}
inline void MsgFactoryInitParams::SetMsgBitRateCount(TUint aCount)
{
    iMsgBitRateCount = aCount;
}
inline void MsgFactoryInitParams::SetMsgAudioPcmCount(TUint aCount, TUint aDecodedAudioCount)
{
    iMsgAudioPcmCount = aCount;
    iDecodedAudioCount = aDecodedAudioCount;
}
inline void MsgFactoryInitParams::SetMsgAudioDsdCount(TUint aCount)
{
    iMsgAudioDsdCount = aCount;
}
inline void MsgFactoryInitParams::SetMsgSilenceCount(TUint aCount)
{
    iMsgSilenceCount = aCount;
}
inline void MsgFactoryInitParams::SetMsgPlayableCount(TUint aPcmCount, TUint aDsdCount, TUint aSilenceCount)
{
    iMsgPlayablePcmCount = aPcmCount;
    iMsgPlayableDsdCount = aDsdCount;
    iMsgPlayableSilenceCount = aSilenceCount;
}
inline void MsgFactoryInitParams::SetMsgQuitCount(TUint aCount)
{
    iMsgQuitCount = aCount;
}


// MsgFactory

inline TUint MsgFactory::AllocatorModeCount() const
{
    return iAllocatorMsgMode.CellsUsed();
}
inline TUint MsgFactory::AllocatorTrackCount() const
{
    return iAllocatorMsgTrack.CellsUsed();
}
inline TUint MsgFactory::AllocatorDrainCount() const
{
    return iAllocatorMsgDrain.CellsUsed();
}
inline TUint MsgFactory::AllocatorDelayCount() const
{
    return iAllocatorMsgDelay.CellsUsed();
}
inline TUint MsgFactory::AllocatorEncodedStreamCount() const
{
    return iAllocatorMsgEncodedStream.CellsUsed();
}
inline TUint MsgFactory::AllocatorStreamSegmentCount() const
{
    return iAllocatorMsgStreamSegment.CellsUsed();
}
inline TUint MsgFactory::AllocatorAudioDataCount() const
{
    return iAllocatorAudioData.CellsUsed();
}
inline TUint MsgFactory::AllocatorAudioEncodedCount() const
{
    return iAllocatorMsgAudioEncoded.CellsUsed();
}
inline TUint MsgFactory::AllocatorMetaTextCount() const
{
    return iAllocatorMsgMetaText.CellsUsed();
}
inline TUint MsgFactory::AllocatorStreamInterruptedCount() const
{
    return iAllocatorMsgStreamInterrupted.CellsUsed();
}
inline TUint MsgFactory::AllocatorHaltCount() const
{
    return iAllocatorMsgHalt.CellsUsed();
}
inline TUint MsgFactory::AllocatorFlushCount() const
{
    return iAllocatorMsgFlush.CellsUsed();
}
inline TUint MsgFactory::AllocatorWaitCount() const
{
    return iAllocatorMsgWait.CellsUsed();
}
inline TUint MsgFactory::AllocatorDecodedStreamCount() const
{
    return iAllocatorMsgDecodedStream.CellsUsed();
}
inline TUint MsgFactory::AllocatorBitRateCount() const
{
    return iAllocatorMsgBitRate.CellsUsed();
}
inline TUint MsgFactory::AllocatorAudioPcmCount() const
{
    return iAllocatorMsgAudioPcm.CellsUsed();
}
inline TUint MsgFactory::AllocatorAudioDsdCount() const
{
    return iAllocatorMsgAudioDsd.CellsUsed();
}
inline TUint MsgFactory::AllocatorSilenceCount() const
{
    return iAllocatorMsgSilence.CellsUsed();
}
inline TUint MsgFactory::AllocatorPlayablePcmCount() const
{
    return iAllocatorMsgPlayablePcm.CellsUsed();
}
inline TUint MsgFactory::AllocatorPlayableDsdCount() const
{
    return iAllocatorMsgPlayableDsd.CellsUsed();
}
inline TUint MsgFactory::AllocatorPlayableSilenceCount() const
{
    return iAllocatorMsgPlayableSilence.CellsUsed();
}
inline TUint MsgFactory::AllocatorPlayableSilenceDsdCount() const
{
    return iAllocatorMsgPlayableSilenceDsd.CellsUsed();
}
inline TUint MsgFactory::AllocatorQuitCount() const
{
    return iAllocatorMsgQuit.CellsUsed();
}

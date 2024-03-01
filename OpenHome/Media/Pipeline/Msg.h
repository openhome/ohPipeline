#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Private/Fifo.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/InfoProvider.h>
#include <OpenHome/Optional.h>

#include <limits.h>
#include <atomic>

EXCEPTION(SampleRateInvalid);
EXCEPTION(SampleRateUnsupported);
EXCEPTION(BitDepthUnsupported);
EXCEPTION(FormatUnsupported);

#undef TIMESTAMP_LOGGING_ENABLE

namespace OpenHome {
namespace Media {

class Allocated;

class AllocatorBase : private IInfoProvider
{
public:
    ~AllocatorBase();
    void Free(Allocated* aPtr);
    TUint CellsTotal() const;
    TUint CellBytes() const;
    TUint CellsUsed() const;
    TUint CellsUsedMax() const;
    void GetStats(TUint& aCellsTotal, TUint& aCellBytes, TUint& aCellsUsed, TUint& aCellsUsedMax) const;
    inline const TChar* Name() const;
    static const Brn kQueryMemory;
protected:
    AllocatorBase(const TChar* aName, TUint aNumCells, TUint aCellBytes, IInfoAggregator& aInfoAggregator);
    Allocated* DoAllocate();
private:
    Allocated* Read();
private: // from IInfoProvider
    void QueryInfo(const Brx& aQuery, IWriter& aWriter);
protected:
    FifoLiteDynamic<Allocated*> iFree;
private:
    mutable Mutex iLock;
    const TChar* iName;
    const TUint iCellsTotal;
    const TUint iCellBytes;
    TUint iCellsUsed;
    TUint iCellsUsedMax;
};

template <class T> class Allocator : public AllocatorBase
{
public:
    Allocator(const TChar* aName, TUint aNumCells, IInfoAggregator& aInfoAggregator);
    virtual ~Allocator();
    T* Allocate();
};

template <class T> Allocator<T>::Allocator(const TChar* aName, TUint aNumCells, IInfoAggregator& aInfoAggregator)
    : AllocatorBase(aName, aNumCells, sizeof(T), aInfoAggregator)
{
    for (TUint i=0; i<aNumCells; i++) {
        iFree.Write(new T(*this));
    }
}

template <class T> Allocator<T>::~Allocator()
{
}

template <class T> T* Allocator<T>::Allocate()
{
    return static_cast<T*>(DoAllocate());
}

class Logger;

class Allocated
{
    friend class AllocatorBase;
    friend class SuiteAllocator;
    friend class Logger;
public:
    void AddRef();
    void RemoveRef();
    inline TUint RefCount() const;
protected:
    Allocated(AllocatorBase& aAllocator);
protected:
    virtual ~Allocated();
private:
    virtual void Clear();
protected:
    AllocatorBase& iAllocator;
private:
    std::atomic<TUint> iRefCount;
};

enum class AudioDataEndian
{
    Invalid,
    Little,
    Big
};

class AudioData : public Allocated
{
public:
    static const TUint kMaxBytes = 9216; // max of 8k (DSD), 2ms/6ch/192/32 and 5ms/2ch/192/24
                                         // (latter for Songcast, supporting earliest receiver)
                                         // ...rounded up to allow full utilisation for 16, 24
                                         // and 32-bit audio
public:
    AudioData(AllocatorBase& aAllocator);
    const TByte* Ptr(TUint aOffsetBytes) const;
    TUint Bytes() const;
    TByte* PtrW();
    void SetBytes(TUint aBytes);
#ifdef TIMESTAMP_LOGGING_ENABLE
    void SetTimestamp(const TChar* aId);
    TBool TryLogTimestamps();
#endif
private: // from Allocated
    void Clear() override;
protected:
    Bws<kMaxBytes> iData;
#ifdef TIMESTAMP_LOGGING_ENABLE
private:
    class Timestamp
    {
    public:
        Timestamp();
        void Reset();
        void Set(const TChar* aId, TUint64 aTimestamp);
        TBool TryLog();
    private:
        const TChar* iId;
        TUint64 iTimestamp;
    };
    static const TUint kMaxTimestamps = 20;
    Timestamp iTimestamps[kMaxTimestamps];
    TUint iNextTimestampIndex;
    OsContext* iOsCtx;
#endif // TIMESTAMP_LOGGING_ENABLE
};

class EncodedAudio : public AudioData
{
    friend class MsgFactory;
public:
    TUint Append(const Brx& aData); // returns number of bytes appended
    TUint Append(const Brx& aData, TUint aMaxBytes); // returns number of bytes appended
private:
    EncodedAudio(AllocatorBase& aAllocator);
    void Construct(const Brx& aData);
    TUint DoAppend(const Brx& aData, TUint aMaxBytes);
};

class DecodedAudio : public AudioData
{
    friend class MsgFactory;
public:
    static const TUint kMaxNumChannels = 8;
public:
    void Aggregate(DecodedAudio& aDecodedAudio);
    void SetBytes(TUint aBytes);
private:
    DecodedAudio(AllocatorBase& aAllocator);
    void ConstructPcm(const Brx& aData, TUint aBitDepth, AudioDataEndian aEndian);
    void ConstructDsd(const Brx& aData);
    void Construct();
    static void CopyToBigEndian16(const Brx& aData, TByte* aDest);
    static void CopyToBigEndian24(const Brx& aData, TByte* aDest);
    static void CopyToBigEndian32(const Brx& aData, TByte* aDest);
};

/**
 * Provides the pipeline's unit of timing.
 *
 * A single sample at any supported rate is representable as an integer number of jiffies.
 */
class Jiffies
{
public:
    static const TUint kPerSecond = 56448000; // lcm(384000, 352800)
    static const TUint kPerMs = kPerSecond / 1000;
public:
    static TBool IsValidSampleRate(TUint aSampleRate);
    static TUint PerSample(TUint aSampleRate);
    static TUint ToBytes(TUint& aJiffies, TUint aJiffiesPerSample, TUint aNumChannels, TUint aBitsPerSubsample);
    static TUint ToBytesSampleBlock(TUint& aJiffies, TUint aJiffiesPerSample, TUint aNumChannels, TUint aBitsPerSubsample, TUint aSamplesPerBlock);
    static void RoundDown(TUint& aJiffies, TUint aSampleRate);
    static void RoundUp(TUint& aJiffies, TUint aSampleRate);
    static void RoundDownNonZeroSampleBlock(TUint& aJiffies, TUint aSampleBlockJiffies);
    static TUint ToSongcastTime(TUint aJiffies, TUint aSampleRate);
    static TUint64 FromSongcastTime(TUint64 aSongcastTime, TUint aSampleRate);
    static inline TUint ToMs(TUint aJiffies);
    static inline TUint ToMs(TUint64 aJiffies);
    static inline TUint ToSamples(TUint aJiffies, TUint aSampleRate);
    inline static TUint64 ToSamples(TUint64 aJiffies, TUint aSampleRate);
    static TUint SongcastTicksPerSecond(TUint aSampleRate);
private:
    //Number of jiffies per sample
    static const TUint kJiffies7350     = kPerSecond / 7350;
    static const TUint kJiffies8000     = kPerSecond / 8000;
    static const TUint kJiffies11025    = kPerSecond / 11025;
    static const TUint kJiffies12000    = kPerSecond / 12000;
    static const TUint kJiffies14700    = kPerSecond / 14700;
    static const TUint kJiffies16000    = kPerSecond / 16000;
    static const TUint kJiffies22050    = kPerSecond / 22050;
    static const TUint kJiffies24000    = kPerSecond / 24000;
    static const TUint kJiffies29400    = kPerSecond / 29400;
    static const TUint kJiffies32000    = kPerSecond / 32000;
    static const TUint kJiffies44100    = kPerSecond / 44100;
    static const TUint kJiffies48000    = kPerSecond / 48000;
    static const TUint kJiffies88200    = kPerSecond / 88200;
    static const TUint kJiffies96000    = kPerSecond / 96000;
    static const TUint kJiffies176400   = kPerSecond / 176400;
    static const TUint kJiffies192000   = kPerSecond / 192000;
    static const TUint kJiffies352800   = kPerSecond / 352800;
    static const TUint kJiffies384000   = kPerSecond / 384000;
    static const TUint kJiffies2822400  = kPerSecond / 2822400; // DSD only from here on
    static const TUint kJiffies5644800  = kPerSecond / 5644800;
    static const TUint kJiffies11289600 = kPerSecond / 11289600;

    static const TUint kSongcastTicksPerSec44k = 44100 * 256;
    static const TUint kSongcastTicksPerSec48k = 48000 * 256;
public:
    static const TUint kMaxJiffiesPerSample = kJiffies7350; // Jiffies for lowest supported sample rate
};

class IMsgProcessor;

class Msg : public Allocated
{
    friend class MsgQueueBase;
public:
    virtual Msg* Process(IMsgProcessor& aProcessor) = 0;
protected:
    Msg(AllocatorBase& aAllocator);
private:
    Msg* iNextMsg;
};

class Ramp
{
    friend class SuiteRamp;
public:
    static const TUint kMax = 1<<14;
    static const TUint kMin = 0;
    enum EDirection
    {
        ENone
       ,EUp
       ,EDown
       ,EMute
    };
public:
    Ramp();
    void Reset();
    TBool Set(TUint aStart, TUint aFragmentSize, TUint aRemainingDuration, EDirection aDirection, Ramp& aSplit, TUint& aSplitPos); // returns true iff aSplit is set
    void SetMuted();
    Ramp Split(TUint aNewSize, TUint aCurrentSize);
    inline TUint Start() const;
    inline TUint End() const;
    inline EDirection Direction() const;
    inline TBool IsEnabled() const;
private:
    void SelectLowerRampPoints(TUint aRequestedStart, TUint aRequestedEnd);
    void Validate(const TChar* aId);
    TBool DoValidate();
private:
    TUint iStart;
    TUint iEnd;
    EDirection iDirection;
    TBool iEnabled;
    TUint iAttenuation;
};

class RampApplicator : private INonCopyable
{
    static const TUint kFullRampSpan;
public:
    RampApplicator(const Media::Ramp& aRamp);
    TUint Start(const Brx& aData, TUint aBitDepth, TUint aNumChannels); // returns number of samples
    void GetNextSample(TByte* aDest);
    static TUint MedianMultiplier(const Media::Ramp& aRamp);
private:
    const Media::Ramp& iRamp;
    const TByte* iPtr;
    TUint iBitDepth;
    TUint iNumChannels;
    TInt iNumSamples;
    TInt iTotalRamp;
    TInt iLoopCount;
};

class MsgFactory;


static const TUint kModeMaxBytes          = 32;
static const TUint kTrackUriMaxBytes      = 1024;
static const TUint kTrackMetaDataMaxBytes = 5 * 1024;
static const TUint kMaxCodecNameBytes     = 32;

typedef Bws<kModeMaxBytes>          BwsMode;
typedef Bws<kTrackUriMaxBytes>      BwsTrackUri;
typedef Bws<kTrackMetaDataMaxBytes> BwsTrackMetaData;
typedef Bws<kMaxCodecNameBytes>     BwsCodecName;

enum class Latency
{
    NotSupported,
    Internal,
    External
};

class Track : public Allocated
{
    friend class TrackFactory;
public:
    static const TUint kIdNone = 0;
public:
    Track(AllocatorBase& aAllocator);
    const Brx& Uri() const;
    const Brx& MetaData() const;
    TUint Id() const;
private:
    void Initialise(const Brx& aUri, const Brx& aMetaData, TUint aId);
private: // from Allocated
    void Clear() override;
private:
    BwsTrackUri iUri;
    BwsTrackMetaData iMetaData;
    TUint iId;
};

class MsgMode;
class ModeInfo
{
private:
    friend class MsgMode;
public:
    inline ModeInfo();
    inline ModeInfo(Latency aLatencyMode);
    inline void SetLatencyMode(Latency aLatencyMode);
    inline void SetSupportsPause(TBool aSupportsPause);
    inline void SetSupportsNextPrev(TBool aSupportsNext, TBool aSupportsPrev);
    inline void SetSupportsRepeatRandom(TBool aSupportsRepeat, TBool aSupportsRandom);
    inline void SetRampDurations(TBool aPauseResumeLong, TBool aSkipLong);
    inline Latency LatencyMode() const;
    inline TBool SupportsPause() const;
    inline TBool SupportsNext() const;
    inline TBool SupportsPrev() const;
    inline TBool SupportsRepeat() const;
    inline TBool SupportsRandom() const;
    inline TBool RampPauseResumeLong() const;
    inline TBool RampSkipLong() const;
private:
    void Clear();
private:
    Latency iLatencyMode;
    TBool iSupportsPause;
    TBool iSupportsNext;
    TBool iSupportsPrev;
    TBool iSupportsRepeat;
    TBool iSupportsRandom;
    TBool iRampPauseResumeLong;
    TBool iRampSkipLong;
};

class IClockPuller;

class ModeTransportControls
{
    friend class MsgMode;
public:
    ModeTransportControls();
    inline void SetPlay(Functor aPlay);
    inline void SetPause(Functor aPause);
    inline void SetStop(Functor aStop);
    inline void SetNext(Functor aNext);
    inline void SetPrev(Functor aPrev);
    inline void SetSeek(FunctorGeneric<TUint> aSeek);
    inline Functor Play() const;
    inline Functor Pause() const;
    inline Functor Stop() const;
    inline Functor Next() const;
    inline Functor Prev() const;
    inline FunctorGeneric<TUint> Seek() const;
private:
    void Clear();
private:
    Functor iPlay;
    Functor iPause;
    Functor iStop;
    Functor iNext;
    Functor iPrev;
    FunctorGeneric<TUint> iSeek;
};

class MsgMode : public Msg
{
    friend class MsgFactory;
public:
    MsgMode(AllocatorBase& aAllocator);
    const Brx& Mode() const;
    const ModeInfo& Info() const;
    Optional<IClockPuller> ClockPuller() const;
    const ModeTransportControls& TransportControls() const;
private:
    void Initialise(const Brx& aMode, const ModeInfo& aInfo,
                    Optional<IClockPuller> aClockPuller,
                    const ModeTransportControls& aTransportControls);
private: // from Msg
    void Clear() override;
    Msg* Process(IMsgProcessor& aProcessor) override;
private:
    BwsMode iMode;
    ModeInfo iInfo;
    Optional<IClockPuller> iClockPuller;
    ModeTransportControls iTransportControls;
};

class MsgTrack : public Msg
{
    friend class MsgFactory;
public:
    static const TUint kMaxUriBytes = 1024;
public:
    MsgTrack(AllocatorBase& aAllocator);
    Media::Track& Track() const;
    TBool StartOfStream() const;
private:
    void Initialise(Media::Track& aTrack, TBool aStartOfStream);
private: // from Msg
    void Clear() override;
    Msg* Process(IMsgProcessor& aProcessor) override;
private:
    Media::Track* iTrack;
    TBool iStartOfStream;
};

class MsgDrain : public Msg
{
    friend class MsgFactory;
public:
    MsgDrain(AllocatorBase& aAllocator);
    void ReportDrained();
    TUint Id() const;
private:
    void Initialise(TUint aId, Functor aCallback);
private: // from Msg
    void Clear() override;
    Msg* Process(IMsgProcessor& aProcessor) override;
private:
    Functor iCallback;
    TUint iId;
    TBool iCallbackPending;
};

class MsgDelay : public Msg
{
    friend class MsgFactory;
public:
    MsgDelay(AllocatorBase& aAllocator);
    TUint RemainingJiffies() const;
    TUint TotalJiffies() const;
private:
    void Initialise(TUint aTotalJiffies);
    void Initialise(TUint aRemainingJiffies, TUint aTotalJiffies);
private: // from Msg
    void Clear() override;
    Msg* Process(IMsgProcessor& aProcessor) override;
private:
    TUint iRemainingJiffies;
    TUint iTotalJiffies;
};

class SpeakerProfile
{
public:
    SpeakerProfile(TUint aNumFronts = 2);
    SpeakerProfile(TUint aNumFronts, TUint aNumSurrounds, TUint aNumSubs);

    TUint NumFronts() const;
    TUint NumSurrounds() const;
    TUint NumSubs() const;
    const TChar* ToString() const;

    TBool operator==(const SpeakerProfile& aOther) const;
    TBool operator!=(const SpeakerProfile& aOther) const;

    const SpeakerProfile& operator=(const SpeakerProfile& aOther);

private:
    static const TUint kMaxNameLen = 10;
    TUint iNumFronts;
    TUint iNumSurrounds;
    TUint iNumSubs;
    Bws<kMaxNameLen> iName;
};

class IStreamHandler;

class PcmStreamInfo
{
public:
    PcmStreamInfo();
    void Set(TUint aBitDepth, TUint aSampleRate, TUint aNumChannels, AudioDataEndian aEndian, const SpeakerProfile& aProfile, TUint64 aStartSample = 0);
    void SetAnalogBypass();
    void SetCodec(const Brx& aCodecName, TBool aLossless);
    void Clear();
    TUint BitDepth() const;
    TUint SampleRate() const;
    TUint NumChannels() const;
    AudioDataEndian Endian() const;
    const SpeakerProfile& Profile() const;
    TUint64 StartSample() const;
    TBool AnalogBypass() const;
    const Brx& CodecName() const;
    TBool Lossless() const;
    void operator=(const PcmStreamInfo &);
    operator TBool() const;
private:
    TUint iBitDepth;
    TUint iSampleRate;
    TUint iNumChannels;
    AudioDataEndian iEndian;
    SpeakerProfile iProfile;
    TUint64 iStartSample;
    TBool iAnalogBypass;
    BwsCodecName iCodecName;
    TBool iLossless;
};

class DsdStreamInfo
{
public:
    DsdStreamInfo();
    void Set(TUint aSampleRate, TUint aNumChannels, TUint aSampleBlockWords, TUint64 aStartSample = 0);
    void SetCodec(const Brx& aCodecName);
    void Clear();
    TUint SampleRate() const;
    TUint SampleBlockWords() const;
    TUint NumChannels() const;
    TUint64 StartSample() const;
    const Brx& CodecName() const;
    void operator=(const DsdStreamInfo &);
    operator TBool() const;
private:
    TUint iSampleRate;
    TUint iNumChannels;
    TUint iSampleBlockWords;
    TUint64 iStartSample;
    BwsCodecName iCodecName;
};

class MsgMetaText : public Msg
{
    friend class MsgFactory;
public:
    static const TUint kMaxBytes = 4 * 1024;
public:
    MsgMetaText(AllocatorBase& aAllocator);
    const Brx& MetaText() const;
private:
    void Initialise(const Brx& aMetaText);
private: // from Msg
    void Clear() override;
    Msg* Process(IMsgProcessor& aProcessor) override;
private:
    Bws<kMaxBytes> iMetaText;
};

enum class Multiroom
{
    Allowed,
    Forbidden
};

enum class RampType
{
    Sample,
    Volume
};

enum class SeekCapability
{
    None,
    SeekCache,  // Seek within pipeline cache, if possible, passing seek upstream towards protocol module otherwise.
    SeekSource  // Do not attempt to seek within pipeline cache. Seek within source, i.e., have protocol module handle seek.
};

class MsgEncodedStream : public Msg
{
    friend class MsgFactory;
public:
    static const TUint kMaxUriBytes = 1024;
    enum class Format
    {
        Encoded,
        Pcm,
        Dsd
    };
private:
    static const RampType kRampDefault = RampType::Sample;
    static const RampType kRampDsd = RampType::Volume;
public:
    MsgEncodedStream(AllocatorBase& aAllocator);
    const Brx& Uri() const;
    const Brx& MetaText() const;
    TUint64 TotalBytes() const;
    /*
     * Stream start position, in bytes.
     */
    TUint64 StartPos() const;
    TUint StreamId() const;
    TBool Seekable() const;
    Media::SeekCapability SeekCapability() const;
    TBool Live() const;
    Media::Multiroom Multiroom() const;
    IStreamHandler* StreamHandler() const;
    Format StreamFormat() const;
    const PcmStreamInfo& PcmStream() const;
    const DsdStreamInfo& DsdStream() const;
    RampType Ramp() const;
    /*
     * Desired start position for stream. Appropriate handler must seek to this position in stream, if necessary. May not align with StartPos() bytes.
     */
    TUint SeekPosMs() const;
private:
    void Initialise(const Brx& aUri, const Brx& aMetaText, TUint64 aTotalBytes, TUint64 aStartPos, TUint aStreamId, Media::SeekCapability aSeek, TBool aLive, Media::Multiroom aMultiroom, IStreamHandler* aStreamHandler, TUint aSeekPosMs);
    void Initialise(const Brx& aUri, const Brx& aMetaText, TUint64 aTotalBytes, TUint64 aStartPos, TUint aStreamId, Media::SeekCapability aSeek, TBool aLive, Media::Multiroom aMultiroom, IStreamHandler* aStreamHandler, const PcmStreamInfo& aPcmStream, RampType aRamp = kRampDefault);
    void Initialise(const Brx& aUri, const Brx& aMetaText, TUint64 aTotalBytes, TUint64 aStartPos, TUint aStreamId, Media::SeekCapability aSeek, TBool aLive, Media::Multiroom aMultiroom, IStreamHandler* aStreamHandler, const DsdStreamInfo& aDsdStream);
private: // from Msg
    void Clear() override;
    Msg* Process(IMsgProcessor& aProcessor) override;
private:
    Bws<kMaxUriBytes> iUri;
    Bws<MsgMetaText::kMaxBytes> iMetaText;
    TUint64 iTotalBytes;
    TUint64 iStartPos;
    TUint iStreamId;
    Media::SeekCapability iSeekCapability;
    TBool iLive;
    Format iStreamFormat;
    Media::Multiroom iMultiroom;
    IStreamHandler* iStreamHandler;
    PcmStreamInfo iPcmStreamInfo;
    DsdStreamInfo iDsdStreamInfo;
    RampType iRamp;
    TUint iSeekPos;
};

class MsgStreamSegment : public Msg
{
    friend class MsgFactory;
public:
    static const TUint kMaxIdBytes = 1024;
public:
    MsgStreamSegment(AllocatorBase& aAllocator);
    const Brx& Id() const;
private:
    void Initialise(const Brx& aId);
private: // from Msg
    void Clear() override;
    Msg* Process(IMsgProcessor& aProcessor) override;
private:
    Bws<kMaxIdBytes> iId;
};

class MsgAudioEncoded : public Msg
{
    friend class MsgFactory;
public:
    MsgAudioEncoded(AllocatorBase& aAllocator);
    MsgAudioEncoded* Split(TUint aBytes); // returns block after aBytes
    void Add(MsgAudioEncoded* aMsg); // combines MsgAudioEncoded instances so they report larger sizes etc
    TUint Append(const Brx& aData); // Appends a Data to existing msg.  Returns index into aData where copying terminated.
    TUint Append(const Brx& aData, TUint aMaxBytes); // Appends a Data to existing msg.  Returns index into aData where copying terminated.
    TUint Bytes() const;
    void CopyTo(TByte* aPtr);
    MsgAudioEncoded* Clone();
    const EncodedAudio& AudioData() const;
    TUint AudioDataOffset() const;
    inline void AddLogPoint(const TChar* aId);
private:
    void Initialise(EncodedAudio* aEncodedAudio);
private: // from Msg
    void Clear() override;
    Msg* Process(IMsgProcessor& aProcessor) override;
private:
    MsgAudioEncoded* iNextAudio;
    TUint iSize; // Bytes
    TUint iOffset; // Bytes
    EncodedAudio* iAudioData;
};

class MsgStreamInterrupted : public Msg
{
    friend class MsgFactory;
public:
    MsgStreamInterrupted(AllocatorBase& aAllocator);
    TUint Jiffies() const;
private:
    void Initialise(TUint aJiffies);
private: // from Msg
    void Clear() override;
    Msg* Process(IMsgProcessor& aProcessor) override;
private:
    TUint iJiffies;
};

/**
 * Indicates that a break in audio may follow and that this is expected.
 *
 * Audio can be assumed to have already ramped down (either naturally at the end of a
 * stream) or manually from an upstream pipeline element.
 */
class MsgHalt : public Msg
{
    friend class MsgFactory;
public:
    static const TUint kIdNone    = 0;
    static const TUint kIdInvalid = UINT_MAX;
public:
    MsgHalt(AllocatorBase& aAllocator);
    TUint Id() const;
    void ReportHalted();
private:
    void Initialise(TUint aId);
    void Initialise(TUint aId, Functor aCallback);
private: // from Msg
    void Clear() override;
    Msg* Process(IMsgProcessor& aProcessor) override;
private:
    TUint iId;
    Functor iCallback;
};

class MsgFlush : public Msg
{
public:
    static const TUint kIdInvalid;
public:
    MsgFlush(AllocatorBase& aAllocator);
    void Initialise(TUint aId);
    TUint Id() const;
private: // from Msg
    void Clear() override;
    Msg* Process(IMsgProcessor& aProcessor) override;
private:
    TUint iId;
};

class MsgWait : public Msg
{
public:
    MsgWait(AllocatorBase& aAllocator);
private: // from Msg
    Msg* Process(IMsgProcessor& aProcessor) override;
};

enum class AudioFormat
{
    Pcm,
    Dsd,
    Undefined
};

class DecodedStreamInfo
{
    friend class MsgDecodedStream;
private:
    static const RampType kRampDefault = RampType::Sample;
public:
    inline TUint StreamId() const;
    inline TUint BitRate() const;
    inline TUint BitDepth() const;
    inline TUint SampleRate() const;
    inline TUint NumChannels() const;
    inline const Brx& CodecName() const;
    inline TUint64 TrackLength() const;
    inline TUint64 SampleStart() const;
    inline TBool Lossless() const;
    inline TBool Seekable() const;
    inline TBool Live() const;
    inline TBool AnalogBypass() const;
    inline Media::Multiroom Multiroom() const;
    inline AudioFormat Format() const;
    inline const SpeakerProfile& Profile() const;
    inline IStreamHandler* StreamHandler() const;
    inline RampType Ramp() const;
private:
    DecodedStreamInfo();
    void Set(TUint aStreamId, TUint aBitRate, TUint aBitDepth, TUint aSampleRate, TUint aNumChannels,
             const Brx& aCodecName, TUint64 aTrackLength, TUint64 aSampleStart,
             TBool aLossless, TBool aSeekable, TBool aLive, TBool aAnalogBypass,
             AudioFormat aFormat, Media::Multiroom aMultiroom, const SpeakerProfile& aProfile,
             IStreamHandler* aStreamHandler, RampType aRamp);
private:
    TUint iStreamId;
    TUint iBitRate;
    TUint iBitDepth;
    TUint iSampleRate;
    TUint iNumChannels;
    BwsCodecName iCodecName;
    TUint64 iTrackLength; // jiffies
    TUint64 iSampleStart;
    TBool iLossless;
    TBool iSeekable;
    TBool iLive;
    TBool iAnalogBypass;
    AudioFormat iFormat;
    Media::Multiroom iMultiroom;
    SpeakerProfile iProfile;
    IStreamHandler* iStreamHandler;
    RampType iRamp;
};

/**
 * Indicates the start of a new audio stream.
 */
class MsgDecodedStream : public Msg
{
    friend class MsgFactory;
private:
    static const RampType kRampDefault = RampType::Sample;
public:
    MsgDecodedStream(AllocatorBase& aAllocator);
    const DecodedStreamInfo& StreamInfo() const;
private:
    void Initialise(TUint aStreamId, TUint aBitRate, TUint aBitDepth, TUint aSampleRate, TUint aNumChannels,
                    const Brx& aCodecName, TUint64 aTrackLength, TUint64 aSampleStart,
                    TBool aLossless, TBool aSeekable, TBool aLive, TBool aAnalogBypass,
                    AudioFormat aFormat, Media::Multiroom aMultiroom, const SpeakerProfile& aProfile,
                    IStreamHandler* aStreamHandler, RampType aRamp);
private: // from Msg
    void Clear() override;
    Msg* Process(IMsgProcessor& aProcessor) override;
private:
    DecodedStreamInfo iStreamInfo;
};

class IPipelineBufferObserver
{
public:
    virtual ~IPipelineBufferObserver() {}
    virtual void Update(TInt aDelta) = 0;
};

class MsgPlayable;
class MsgPlayablePcm;
class MsgPlayableDsd;
class MsgPlayableSilence;
class MsgPlayableSilenceDsd;

class MsgAudio : public Msg
{
    friend class MsgFactory;
public:
    void SetObserver(IPipelineBufferObserver& aPipelineBufferObserver);
    MsgAudio* Split(TUint aJiffies); // returns block after aJiffies
    virtual MsgAudio* Clone(); // create new MsgAudio, copy size/offset
    virtual MsgPlayable* CreatePlayable() = 0;
    TUint Jiffies() const;
    TUint SetRamp(TUint aStart, TUint& aRemainingDuration, Ramp::EDirection aDirection, MsgAudio*& aSplit); // returns iRamp.End()
    void ClearRamp();
    void SetMuted(); // should only be used with msgs immediately following a ramp down
    const Media::Ramp& Ramp() const;
    TUint MedianRampMultiplier(); // 1<<31 => full level.  Note - clears any existing ramp
    TBool HasBufferObserver() const;
protected:
    MsgAudio(AllocatorBase& aAllocator);
    void Initialise(TUint aSampleRate, TUint aBitDepth, TUint aChannels);
    void Clear() override;
private:
    virtual MsgAudio* Allocate() = 0;
    MsgAudio* DoSplit(TUint aJiffies);
    virtual void SplitCompleted(MsgAudio& aRemaining);
protected:
    TUint iSize; // Jiffies
    TUint iOffset; // Jiffies
    Media::Ramp iRamp;
    TUint iSampleRate;
    TUint iBitDepth;
    TUint iNumChannels;
    IPipelineBufferObserver* iPipelineBufferObserver;
};

class MsgAudioDecoded : public MsgAudio
{
    friend class MsgFactory;
public:
    static const TUint64 kTrackOffsetInvalid;
public:
    void Aggregate(MsgAudioDecoded* aMsg); // append aMsg to the end of this msg, removes ref on aMsg
    TUint64 TrackOffset() const; // offset of the start of this msg from the start of its track.  FIXME no tests for this yet
protected:
    MsgAudioDecoded(AllocatorBase& aAllocator);
protected: // from MsgAudio
    MsgAudio* Clone() override; // create new MsgAudio, take ref to DecodedAudio, copy size/offset
protected:
    void Initialise(DecodedAudio* aDecodedAudio, TUint aSampleRate, TUint aBitDepth, TUint aChannels,
                    TUint64 aTrackOffset, TUint aNumSubsamples,
                    Allocator<MsgPlayableSilence>& aAllocatorPlayableSilence);
    void InitialiseDsd(DecodedAudio* aDecodedAudio, TUint aSampleRate, TUint aChannels,
                    TUint aSampleBlockWords, TUint64 aTrackOffset, TUint aNumSubsamples,
                    Allocator<MsgPlayableSilenceDsd>& aAllocatorPlayableSilenceDsd);


protected: // from MsgAudio
    void SplitCompleted(MsgAudio& aRemaining) override;
protected: // from Msg
    void Clear() override;
private:
    virtual void AggregateComplete();
protected:
    DecodedAudio* iAudioData;
    Allocator<MsgPlayableSilence>* iAllocatorPlayableSilence;
    Allocator<MsgPlayableSilenceDsd>* iAllocatorPlayableSilenceDsd;
    TUint64 iTrackOffset;
    TUint iSampleBlockWords;
};

class MsgAudioPcm : public MsgAudioDecoded
{
    friend class MsgFactory;
public:
    static const TUint kUnityAttenuation;
public:
    MsgAudioPcm(AllocatorBase& aAllocator);
    void SetAttenuation(TUint aAttenuation);
    inline void AddLogPoint(const TChar* aId);
public: // from MsgAudio
    MsgAudio* Clone() override; // create new MsgAudio, take ref to DecodedAudio, copy size/offset
    MsgPlayable* CreatePlayable() override; // removes ref, transfer ownership of DecodedAudio
private:
    void Initialise(DecodedAudio* aDecodedAudio, TUint aSampleRate, TUint aBitDepth, TUint aChannels, TUint64 aTrackOffset,
                    Allocator<MsgPlayablePcm>& aAllocatorPlayablePcm,
                    Allocator<MsgPlayableSilence>& aAllocatorPlayableSilence);
private: // from MsgAudio
    MsgAudio* Allocate() override;
    void SplitCompleted(MsgAudio& aRemaining) override;
private: // from Msg
    void Clear() override;
    Msg* Process(IMsgProcessor& aProcessor) override;
private:
    Allocator<MsgPlayablePcm>* iAllocatorPlayablePcm;
    TUint iAttenuation;
};

class MsgAudioDsd : public MsgAudioDecoded
{
    friend class MsgFactory;
    friend class SuiteMsgAudioDsd;
    static const TUint kBitDepth = 1;
public:
    MsgAudioDsd(AllocatorBase& aAllocator);
    TUint JiffiesNonPlayable();
    TUint SizeTotalJiffies();
public: // from MsgAudio
    MsgAudio* Clone() override; // create new MsgAudio, take ref to DecodedAudio, copy size/offset
    MsgPlayable* CreatePlayable() override; // removes ref, transfer ownership of DecodedAudio
private:
    void Initialise(DecodedAudio* aDecodedAudio, TUint aSampleRate, TUint aChannels,
                    TUint aSampleBlockWords, TUint64 aTrackOffset, TUint aPadBytesPerChunk,
                    Allocator<MsgPlayableDsd>& aAllocatorPlayableDsd,
                    Allocator<MsgPlayableSilenceDsd>& aAllocatorPlayableSilenceDsd);
private: // from MsgAudio
    MsgAudio* Allocate() override;
    void SplitCompleted(MsgAudio& aRemaining) override;
private: // from Msg
    void Clear() override;
    Msg* Process(IMsgProcessor& aProcessor) override;
private: // from MsgAudioDecoded
    void AggregateComplete() override;
private:
    TUint JiffiesPlayableToJiffiesTotal(TUint aJiffies, TUint aJiffiesPerSampleBlockPlayable) const;
    TUint SamplesPerBlock(TUint aBlockWords) const;
    TUint SizeJiffiesTotal() const;
private:
    Allocator<MsgPlayableDsd>* iAllocatorPlayableDsd;
    TUint iSampleBlockWords;
    TUint iBlockWordsNoPad;
    TUint iSizeTotalJiffies;
    TUint iJiffiesNonPlayable;
};

class MsgPlayableSilence;
class MsgPlayableSilenceDsd;

class MsgSilence : public MsgAudio
{
    friend class MsgFactory;
public:
    MsgSilence(AllocatorBase& aAllocator);
public: // from MsgAudio
    MsgAudio* Clone() override;
    MsgPlayable* CreatePlayable() override; // removes ref
private:
    void Initialise(TUint& aJiffies, TUint aSampleRate, TUint aBitDepth, TUint aChannels, Allocator<MsgPlayableSilence>& aAllocatorPlayable);
    void InitialiseDsd(TUint& aJiffies, TUint aSampleRate, TUint aChannels, TUint aSampleBlockWords, TUint aPadBytesPerChunk, Allocator<MsgPlayableSilenceDsd>& aAllocatorPlayable);
private: // from MsgAudio
    MsgAudio* Allocate() override;
    void SplitCompleted(MsgAudio& aRemaining) override;
private: // from Msg
    Msg* Process(IMsgProcessor& aProcessor) override;
private:
    Allocator<MsgPlayableSilence>* iAllocatorPlayablePcm;
    Allocator<MsgPlayableSilenceDsd>* iAllocatorPlayableDsd;
    TUint iSampleBlockWords;
    TUint iSampleBlockJiffiesTotal;
    TUint iSampleBlockJiffiesPlayable;
    TUint iSizeJiffiesTotal;
};

class IPcmProcessor;
class IDsdProcessor;

/**
 * Holds decoded audio and can write it to a stream.
 *
 * MsgAudioPcm and MsgSilence can be converted into this.
 */
class MsgPlayable : public Msg
{
public:
    MsgPlayable* Split(TUint aBytes); // returns block after aBytes
    TUint Bytes() const;
    TUint Jiffies() const;
    const Media::Ramp& Ramp() const;
    TBool HasBufferObserver() const;
    /**
     * Extract pcm data from this msg.
     *
     * Any ramp is applied at the same time.
     *
     * @param[in] aProcessor       PCM data is returned via this interface.  Writing the data
     *                             in a blocks is preferred.  Data may be written sample at a
     *                             time if requested by the processor (say because it'll insert
     *                             padding around each sample) or if a ramp is being applied.
     */
    void Read(IPcmProcessor& aProcessor);
    void Read(IDsdProcessor& aProcessor);
    virtual TBool TryLogTimestamps();
protected:
    MsgPlayable(AllocatorBase& aAllocator);
    void Initialise(TUint aSizeBytes, TUint aJiffies,
                    TUint aSampleRate, TUint aBitDepth, TUint aNumChannels,
                    TUint aOffsetBytes, const Media::Ramp& aRamp,
                    Optional<IPipelineBufferObserver> aPipelineBufferObserver);
protected: // from Msg
    Msg* Process(IMsgProcessor& aProcessor) override;
    void Clear() override;
private:
    virtual MsgPlayable* Allocate() = 0;
    virtual void SplitCompleted(MsgPlayable& aRemaining);
    virtual void ReadBlock(IPcmProcessor& aProcessor);
    virtual void ReadBlock(IDsdProcessor& aProcessor);
protected:
    TUint iSize; // Bytes
    TUint iJiffies;
    TUint iSampleRate;
    TUint iBitDepth;
    TUint iNumChannels;
    TUint iOffset; // Bytes
    Media::Ramp iRamp;
    IPipelineBufferObserver* iPipelineBufferObserver;
};

class MsgPlayablePcm : public MsgPlayable
{
    friend class MsgAudioPcm;
public:
    MsgPlayablePcm(AllocatorBase& aAllocator);
private:
    void Initialise(DecodedAudio* aDecodedAudio, TUint aSizeBytes, TUint aJiffies,
                    TUint aSampleRate, TUint aBitDepth, TUint aNumChannels, TUint aOffsetBytes,
                    TUint aAttenuation, const Media::Ramp& aRamp,
                    Optional<IPipelineBufferObserver> aPipelineBufferObserver);
private: // from MsgPlayable
    MsgPlayable* Allocate() override;
    void SplitCompleted(MsgPlayable& aRemaining) override;
    void ReadBlock(IPcmProcessor& aProcessor) override;
    TBool TryLogTimestamps() override;
private: // from Msg
    void Clear() override;
private:
    void ApplyAttenuation(Bwx& aData);
private:
    DecodedAudio* iAudioData;
    TUint iAttenuation;
};

class MsgPlayableDsd : public MsgPlayable
{
    friend class MsgAudioDsd;
public:
    MsgPlayableDsd(AllocatorBase& aAllocator);
private:
    void Initialise(DecodedAudio* aDecodedAudio, TUint aSizeBytes, TUint aJiffies,
                    TUint aSampleRate, TUint aNumChannels, TUint aSampleBlockBits, TUint aOffsetBytes,
                    const Media::Ramp& aRamp, Optional<IPipelineBufferObserver> aPipelineBufferObserver);
private: // from MsgPlayable
    MsgPlayable* Allocate() override;
    void SplitCompleted(MsgPlayable& aRemaining) override;
    void ReadBlock(IDsdProcessor& aProcessor) override;
private: // from Msg
    void Clear() override;
private:
    DecodedAudio* iAudioData;
    TUint iSampleBlockWords;
};

class MsgPlayableSilence : public MsgPlayable
{
    friend class MsgSilence;
    friend class MsgAudioPcm;
public:
    MsgPlayableSilence(AllocatorBase& aAllocator);
private:
    void Initialise(TUint aSizeBytes, TUint aJiffies,
                    TUint aSampleRate, TUint aBitDepth,
                    TUint aNumChannels, const Media::Ramp& aRamp,
                    Optional<IPipelineBufferObserver> aPipelineBufferObserver);
private: // from MsgPlayable
    MsgPlayable* Allocate() override;
    void ReadBlock(IPcmProcessor& aProcessor) override;
};

class MsgPlayableSilenceDsd : public MsgPlayable
{
    friend class MsgSilence;
    friend class MsgAudioDsd;
public:
    MsgPlayableSilenceDsd(AllocatorBase& aAllocator);
private:
    void Initialise(TUint aSizeBytes, TUint aJiffies, TUint aSampleRate, TUint aBitDepth,
                    TUint aNumChannels, TUint aSampleBlockWords, const Media::Ramp& aRamp,
                    Optional<IPipelineBufferObserver> aPipelineBufferObserver);
private: // from MsgPlayable
    MsgPlayable* Allocate() override;
    void ReadBlock(IDsdProcessor& aProcessor) override;
private:
    TUint iSampleBlockWords;
};

/**
 * Indicates that the pipeline is shutting down.
 *
 * Do not attempt to Pull() further messages after receiving this.
 */
class MsgQuit : public Msg
{
public:
    MsgQuit(AllocatorBase& aAllocator);
private: // from Msg
    Msg* Process(IMsgProcessor& aProcessor) override;
};

/**
 * Utility to allow pipeline elements or clients to determine the type of Msg they've been passed.
 *
 * Derive from IMsgProcessor than call msg->Process(*this) to have the ProcessMsg overload for
 * the appropriate type called.
 */
class IMsgProcessor
{
public:
    virtual ~IMsgProcessor() {}
    virtual Msg* ProcessMsg(MsgMode* aMsg) = 0;
    virtual Msg* ProcessMsg(MsgTrack* aMsg) = 0;
    virtual Msg* ProcessMsg(MsgDrain* aMsg) = 0;
    virtual Msg* ProcessMsg(MsgDelay* aMsg) = 0;
    virtual Msg* ProcessMsg(MsgEncodedStream* aMsg) = 0;
    virtual Msg* ProcessMsg(MsgStreamSegment* aMsg) = 0;
    virtual Msg* ProcessMsg(MsgAudioEncoded* aMsg) = 0;
    virtual Msg* ProcessMsg(MsgMetaText* aMsg) = 0;
    virtual Msg* ProcessMsg(MsgStreamInterrupted* aMsg) = 0;
    virtual Msg* ProcessMsg(MsgHalt* aMsg) = 0;
    virtual Msg* ProcessMsg(MsgFlush* aMsg) = 0;
    virtual Msg* ProcessMsg(MsgWait* aMsg) = 0;
    virtual Msg* ProcessMsg(MsgDecodedStream* aMsg) = 0;
    virtual Msg* ProcessMsg(MsgAudioPcm* aMsg) = 0;
    virtual Msg* ProcessMsg(MsgAudioDsd* aMsg) = 0;
    virtual Msg* ProcessMsg(MsgSilence* aMsg) = 0;
    virtual Msg* ProcessMsg(MsgPlayable* aMsg) = 0;
    virtual Msg* ProcessMsg(MsgQuit* aMsg) = 0;
};

/**
 * Used to retrieve PCM audio data from a MsgPlayable
 */
class IPcmProcessor
{
public:
    virtual ~IPcmProcessor() {}
    /**
     * Called once per call to MsgPlayable::Read.
     *
     * Will be called before any calls to ProcessFragment.
     */
    virtual void BeginBlock() = 0;
    /**
     * Copy a block of audio data.
     *
     * @param aData            Packed big endian pcm data.  Will always be a complete number of samples.
     * @param aNumChannels     Number of channels.
     * @param aSubsampleBytes  Number of bytes per sample per channel (1, 2, 3 for 8, 16, 24-bit)
     */
    virtual void ProcessFragment(const Brx& aData, TUint aNumChannels, TUint aSubsampleBytes) = 0;
    /**
     * Copy a block of (silent) audio data.
     *
     * @param aData            Packed big endian pcm data.  Will always be a complete number of samples.
     * @param aNumChannels     Number of channels.
     * @param aSubsampleBytes  Number of bytes per sample per channel (1, 2, 3 for 8, 16, 24-bit)
     */
    virtual void ProcessSilence(const Brx& aData, TUint aNumChannels, TUint aSubsampleBytes) = 0;
    /**
     * Called once per call to MsgPlayable::Read.
     *
     * No more calls to ProcessFragment will be made after this.
     */
    virtual void EndBlock() = 0;
    /**
     * If this is called, the processor should pass on any buffered audio.
     */
    virtual void Flush() = 0;
};

/**
* Used to retrieve DSD audio data from a MsgPlayable
*/
class IDsdProcessor
{
public:
    virtual ~IDsdProcessor() {}
    /**
    * Called once per call to MsgPlayable::Read.
    *
    * Will be called before any calls to ProcessFragment.
    */
    virtual void BeginBlock() = 0;
    /**
    * Copy a block of audio data.
    *
    * 1 bit per subsample. Subsamples are packed in blocks of 16 (i.e. a stereo
    * track * has 16 subsamples for left followed by 16 for right).
    *
    * @param aData             Packed DSD data.  Will always be a complete number of samples.
    * @param aNumChannels      Number of channels.
    * @param aSampleBlockBits  Block size (in bits) of DSD data.  2 for stereo where left/right
    *                          channels are interleaved, 16 for stereo with one byte of left
    *                          subsamples followed by one byte of right subsamples, etc.
    */
    virtual void ProcessFragment(const Brx& aData, TUint aNumChannels, TUint aSampleBlockWords) = 0;
    /**
    * Called once per call to MsgPlayable::Read.
    *
    * No more calls to ProcessFragment will be made after this.
    */
    virtual void EndBlock() = 0;
    /**
    * If this is called, the processor should pass on any buffered audio.
    */
    virtual void Flush() = 0;
};

class MsgQueueBase
{
public:
    virtual ~MsgQueueBase();
protected:
    MsgQueueBase();
    void DoEnqueue(Msg* aMsg);
    Msg* DoDequeue();
    void DoEnqueueAtHead(Msg* aMsg);
    TBool IsEmpty() const;
    void DoClear();
    TUint NumMsgs() const;
private:
    void CheckMsgNotQueued(Msg* aMsg) const;
private:
    Msg* iHead;
    Msg* iTail;
    TUint iNumMsgs;
};

class MsgQueueLite : public MsgQueueBase
{
public:
    inline void Enqueue(Msg* aMsg);
    inline Msg* Dequeue();
    inline void EnqueueAtHead(Msg* aMsg);
    inline TBool IsEmpty() const;
    inline void Clear();
    inline TUint NumMsgs() const;
};

class MsgQueue : public MsgQueueBase
{
public:
    MsgQueue();
    void Enqueue(Msg* aMsg);
    Msg* Dequeue();
    void EnqueueAtHead(Msg* aMsg);
    TBool IsEmpty() const;
    void Clear();
    TUint NumMsgs() const; // test/debug use only
private:
    mutable Mutex iLock;
    Semaphore iSem;
};

class MsgReservoir
{
protected:
    MsgReservoir();
    virtual ~MsgReservoir();
    void DoEnqueue(Msg* aMsg);
    Msg* DoDequeue(TBool aAllowNull = false);
    void EnqueueAtHead(Msg* aMsg);
    TUint Jiffies() const;
    TUint EncodedBytes() const;
    TBool IsEmpty() const;
    TUint TrackCount() const;
    TUint DelayCount() const;
    TUint EncodedStreamCount() const;
    TUint MetaTextCount() const;
    TUint DecodedStreamCount() const;
    TUint EncodedAudioCount() const;
    TUint DecodedAudioCount() const;
    TUint NumMsgs() const; // Test use only
private:
    virtual void ProcessMsgIn(MsgMode* aMsg);
    virtual void ProcessMsgIn(MsgTrack* aMsg);
    virtual void ProcessMsgIn(MsgDrain* aMsg);
    virtual void ProcessMsgIn(MsgDelay* aMsg);
    virtual void ProcessMsgIn(MsgEncodedStream* aMsg);
    virtual void ProcessMsgIn(MsgStreamSegment* aMsg);
    virtual void ProcessMsgIn(MsgAudioEncoded* aMsg);
    virtual void ProcessMsgIn(MsgMetaText* aMsg);
    virtual void ProcessMsgIn(MsgStreamInterrupted* aMsg);
    virtual void ProcessMsgIn(MsgHalt* aMsg);
    virtual void ProcessMsgIn(MsgFlush* aMsg);
    virtual void ProcessMsgIn(MsgWait* aMsg);
    virtual void ProcessMsgIn(MsgDecodedStream* aMsg);
    virtual void ProcessMsgIn(MsgAudioPcm* aMsg);
    virtual void ProcessMsgIn(MsgAudioDsd* aMsg);
    virtual void ProcessMsgIn(MsgSilence* aMsg);
    virtual void ProcessMsgIn(MsgQuit* aMsg);
    virtual Msg* ProcessMsgOut(MsgMode* aMsg);
    virtual Msg* ProcessMsgOut(MsgTrack* aMsg);
    virtual Msg* ProcessMsgOut(MsgDrain* aMsg);
    virtual Msg* ProcessMsgOut(MsgDelay* aMsg);
    virtual Msg* ProcessMsgOut(MsgEncodedStream* aMsg);
    virtual Msg* ProcessMsgOut(MsgStreamSegment* aMsg);
    virtual Msg* ProcessMsgOut(MsgAudioEncoded* aMsg);
    virtual Msg* ProcessMsgOut(MsgMetaText* aMsg);
    virtual Msg* ProcessMsgOut(MsgStreamInterrupted* aMsg);
    virtual Msg* ProcessMsgOut(MsgHalt* aMsg);
    virtual Msg* ProcessMsgOut(MsgFlush* aMsg);
    virtual Msg* ProcessMsgOut(MsgWait* aMsg);
    virtual Msg* ProcessMsgOut(MsgDecodedStream* aMsg);
    virtual Msg* ProcessMsgOut(MsgAudioPcm* aMsg);
    virtual Msg* ProcessMsgOut(MsgAudioDsd* aMsg);
    virtual Msg* ProcessMsgOut(MsgSilence* aMsg);
    virtual Msg* ProcessMsgOut(MsgQuit* aMsg);
private:
    class ProcessorEnqueue : public IMsgProcessor, private INonCopyable
    {
    public:
        ProcessorEnqueue(MsgReservoir& aQueue);
    protected:
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
        void ProcessAudio(MsgAudioDecoded* aMsg);
    protected:
        MsgReservoir& iQueue;
    };
    class ProcessorQueueIn : public ProcessorEnqueue
    {
    public:
        ProcessorQueueIn(MsgReservoir& aQueue);
    private:
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
    };
    class ProcessorQueueOut : public IMsgProcessor, private INonCopyable
    {
    public:
        ProcessorQueueOut(MsgReservoir& aQueue);
    private:
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
        void ProcessAudio(MsgAudioDecoded* aMsg);
    private:
        MsgReservoir& iQueue;
    };
private:
    MsgQueue iQueue;
    mutable Mutex iLockEncoded; // see #5098
    TUint iEncodedBytes;
    std::atomic<TUint> iJiffies;
    std::atomic<TUint> iTrackCount;
    std::atomic<TUint> iDelayCount;
    std::atomic<TUint> iEncodedStreamCount;
    std::atomic<TUint> iMetaTextCount;
    std::atomic<TUint> iDecodedStreamCount;
    TUint iEncodedAudioCount;
    std::atomic<TUint> iDecodedAudioCount;
};

class PipelineElement : protected IMsgProcessor
{
protected:
    enum MsgType
    {
        eMode               = 1
       ,eTrack              = 1 <<  1
       ,eDrain              = 1 <<  2
       ,eDelay              = 1 <<  3
       ,eEncodedStream      = 1 <<  4
       ,eStreamSegment      = 1 <<  5 // Used to indicate new chunk within an encoded stream (e.g., for restarting container/codec recognition).
       ,eAudioEncoded       = 1 <<  6
       ,eMetatext           = 1 <<  7
       ,eStreamInterrupted  = 1 <<  8
       ,eHalt               = 1 <<  9
       ,eFlush              = 1 << 10
       ,eWait               = 1 << 11
       ,eDecodedStream      = 1 << 12
       ,eAudioPcm           = 1 << 13
       ,eAudioDsd           = 1 << 14
       ,eSilence            = 1 << 15
       ,ePlayable           = 1 << 16
       ,eQuit               = 1 << 17
    };
protected:
    PipelineElement(TUint aSupportedTypes);
    ~PipelineElement();
protected: // from IMsgProcessor
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
    inline void CheckSupported(MsgType aType) const;
private:
    TUint iSupportedTypes;
};

// removes ref on destruction.  Does NOT claim ref on construction.
class AutoAllocatedRef : private INonCopyable
{
public:
    AutoAllocatedRef(Allocated* aAllocated);
    ~AutoAllocatedRef();
private:
    Allocated* iAllocated;
};

/**
 * Interface to the start of the pipeline.  Use this to push data into the pipeline.
 */
class ISupply
{
public:
    static const TUint kMaxDrainMs;
public:
    virtual ~ISupply() {}
    /**
     * Inform the pipeline that a new track is starting.
     *
     * @param[in] aTrack           Track about to be played.
     * @param[in] aStartOfStream   false if this is called after OutputData.
     */
    virtual void OutputTrack(Track& aTrack, TBool aStartOfStream = true) = 0;
    /**
    * Inform the pipeline that the next stream cannot begin until all pending audio has been played.
    *
    * @param[in] aCallabck        Callback to run when all audio has been processed.
    */
    virtual void OutputDrain(Functor aCallback) = 0;
    /**
     * Apply a delay to subsequent audio in this stream.
     *
     * Any delay is calculated relative to previous delays for this session.
     * i.e. if you output the same delay twice, the second call has no effect.
     *
     * @param[in] aJiffies         Delay in Jiffies.
     */
    virtual void OutputDelay(TUint aJiffies) = 0;
    /**
     * Inform the pipeline that a new audio stream is starting
     *
     * @param[in] aUri             Uri of the stream
     * @param[in] aTotalBytes      Length in bytes of the stream
     * @param[in] aStartPos        Any offset from the actual start of the stream (e.g. when seeking)
     * @param[in] aSeekable        Whether the stream supports Seek requests
     * @param[in] aLive            Whether the stream is being broadcast live (and won't support seeking)
     * @param[in] aMultiroom       Whether the current stream is allowed to be broadcast to other music players.
     * @param[in] aStreamHandler   Stream handler.  Used to allow pipeline elements to communicate upstream.
     * @param[in] aStreamId        Identifier for the pending stream.  Unique within a single track only.
     * @param[in] aSeekPosMs       Desired start position for stream. Appropriate handler must seek to this position in stream if non-zero. May not align with StartPos() bytes.
     */
    virtual void OutputStream(const Brx& aUri, TUint64 aTotalBytes, TUint64 aStartPos, TBool aSeekable, TBool aLive, Media::Multiroom aMultiroom, IStreamHandler& aStreamHandler, TUint aStreamId, TUint aSeekPosMs = 0) = 0;
    /**
     * Inform the pipeline that a new (raw PCM) audio stream is starting
     *
     * @param[in] aUri             Uri of the stream
     * @param[in] aTotalBytes      Length in bytes of the stream
     * @param[in] aSeekable        Whether the stream supports Seek requests
     * @param[in] aLive            Whether the stream is being broadcast live (and won't support seeking)
     * @param[in] aMultiroom       Whether the current stream is allowed to be broadcast to other music players.
     * @param[in] aStreamHandler   Stream handler.  Used to allow pipeline elements to communicate upstream.
     * @param[in] aStreamId        Identifier for the pending stream.  Unique within a single track only.
     * @param[in] aPcmStream       Bit depth, sample rate, etc.
     */
    virtual void OutputPcmStream(const Brx& aUri, TUint64 aTotalBytes, TBool aSeekable, TBool aLive, Media::Multiroom aMultiroom, IStreamHandler& aStreamHandler, TUint aStreamId, const PcmStreamInfo& aPcmStream) = 0;
    /**
     * Inform the pipeline that a new (raw PCM) audio stream is starting
     *
     * @param[in] aUri             Uri of the stream
     * @param[in] aTotalBytes      Length in bytes of the stream
     * @param[in] aSeekable        Whether the stream supports Seek requests
     * @param[in] aLive            Whether the stream is being broadcast live (and won't support seeking)
     * @param[in] aMultiroom       Whether the current stream is allowed to be broadcast to other music players.
     * @param[in] aStreamHandler   Stream handler.  Used to allow pipeline elements to communicate upstream.
     * @param[in] aStreamId        Identifier for the pending stream.  Unique within a single track only.
     * @param[in] aPcmStream       Bit depth, sample rate, etc.
     * @param[in] aRamp            Type of ramp to use for fading in/out of stream.
     */
    virtual void OutputPcmStream(const Brx& aUri, TUint64 aTotalBytes, TBool aSeekable, TBool aLive, Media::Multiroom aMultiroom, IStreamHandler& aStreamHandler, TUint aStreamId, const PcmStreamInfo& aPcmStream, RampType aRamp) = 0;
    /**
     * Inform the pipeline that a new (raw DSD) audio stream is starting
     *
     * @param[in] aUri             Uri of the stream
     * @param[in] aTotalBytes      Length in bytes of the stream
     * @param[in] aSeekable        Whether the stream supports Seek requests
     * @param[in] aStreamHandler   Stream handler.  Used to allow pipeline elements to communicate upstream.
     * @param[in] aStreamId        Identifier for the pending stream.  Unique within a single track only.
     * @param[in] aDsdStream       Sample rate, etc.
     */
    virtual void OutputDsdStream(const Brx& aUri, TUint64 aTotalBytes, TBool aSeekable, IStreamHandler& aStreamHandler, TUint aStreamId, const DsdStreamInfo& aDsdStream) = 0;
    /**
     * Inform the pipeline that a new segment is starting within this audio stream (e.g., for chunked streaming protocols).
     *
     * @param[in] aId              ID of the segment. May be, e.g., segment URI or some relative indicator within the stream.
     */
    virtual void OutputSegment(const Brx& aId) = 0;
    /**
     * Push a block of (encoded or PCM) audio into the pipeline.
     *
     * Data is copied into the pipeline.  The caller is free to reuse its buffer.
     *
     * @param[in] aData            Encoded audio.  Must be <= EncodedAudio::kMaxBytes
     */
    virtual void OutputData(const Brx& aData) = 0;
    /**
     * Push metadata, describing the current stream, into the pipeline.
     *
     * Use of this is optional.  The pipeline will treat the data as opaque and will merely
     * pass it on to any observers.  Each call to this should pass a complete block of
     * metadata that is capable of being interpreted independantly of any other.
     *
     * @param[in] aMetadata        Metadata.  Must be <= MsgMetaText::kMaxBytes
     */
    virtual void OutputMetadata(const Brx& aMetadata) = 0;
    /**
     * Push a Halt notification into the pipeline.
     *
     * This is called to indicate an expected discontinuity in audio (e.g. when a
     * remote sender indicates they have paused).
     *
     * @param[in] aHaltId          Unique identifier for the pipeline msg that will be created.
     */
    virtual void OutputHalt(TUint aHaltId = MsgHalt::kIdNone) = 0;
    /**
     * Push a Flush command into the pipeline.
     *
     * This is typically called after a call to TrySeek() or TryStop() from IStreamHandler.
     *
     * @param[in] aFlushId         Unique identifier for this command.  Will normally have
     *                             earlier been returned by TrySeek() or TryStop().
     */
    virtual void OutputFlush(TUint aFlushId) = 0;
    /**
     * Push a Wait command into the pipeline.
     *
     * This causes the pipeline to report state Waiting (no audio, this isn't unexpected)
     * rather than Buffering (no audio, error) until OutputData() or OutputStream() is next
     * called.
     */
    virtual void OutputWait() = 0;
};

class IFlushIdProvider
{
public:
    virtual ~IFlushIdProvider() {}
    virtual TUint NextFlushId() = 0;
};

enum EStreamPlay
{
    ePlayYes
   ,ePlayNo
   ,ePlayLater
};
extern const TChar* kStreamPlayNames[];

class IStreamPlayObserver
{
public:
    virtual ~IStreamPlayObserver() {}
    virtual void NotifyTrackFailed(TUint aTrackId) = 0;
    virtual void NotifyStreamPlayStatus(TUint aTrackId, TUint aStreamId, EStreamPlay aStatus) = 0;
};

class IPipelineIdProvider
{
public:
    static const TUint kStreamIdInvalid = 0;
public:
    virtual ~IPipelineIdProvider() {}
    virtual TUint NextStreamId() = 0;
    virtual EStreamPlay OkToPlay(TUint aStreamId) = 0;
};

class IPipelineIdManager
{
public:
    virtual ~IPipelineIdManager() {}
    virtual void InvalidateAt(TUint aId) = 0;
    virtual void InvalidateAfter(TUint aId) = 0;
    virtual void InvalidatePending() = 0;
    virtual void InvalidateAll() = 0;
};

class IPipelineIdTracker
{
public:
    virtual ~IPipelineIdTracker() {}
    virtual void AddStream(TUint aId, TUint aStreamId, TBool aPlayNow) = 0;
};

/**
 * Interface to allow pipeline elements to request action from an upstream component
 */
class IStreamHandler
{
public:
    virtual ~IStreamHandler() {}
    /**
     * Request permission to play a stream.
     *
     * @param[in] aStreamId        Unique stream identifier.
     *
     * @return  Whether the stream can be played.  One of
     *            ePlayYes   - play the stream immediately.
     *            ePlayNo    - do not play the stream.  Discard its contents immediately.
     *            ePlayLater - play the stream later.  Do not play yet but also do not discard.
     */
    virtual EStreamPlay OkToPlay(TUint aStreamId) = 0;
    /**
     * Attempt to seek inside the currently playing stream.
     *
     * This may be called from a different thread.  The implementor is responsible for any synchronisation.
     * Note that this may fail if the stream is non-seekable or the entire stream is
     * already in the pipeline
     *
     * @param[in] aStreamId        Stream identifier, unique in the context of the current track only.
     * @param[in] aOffset          Byte offset into the stream.
     *
     * @return  Flush id.  MsgFlush::kIdInvalid if the seek request failed.
     *          Any other value indicates success.  The code which issues the seek request
     *          should discard data until it pulls a MsgFlush with this id.
     */
    virtual TUint TrySeek(TUint aStreamId, TUint64 aOffset) = 0;
    /**
     * Attempt to reduce latency by discarding buffered content.
     *
     * All calls will be within the same thread pipeline elements normally run in.
     * No synchronisation is required.
     *
     * @param[in] aJiffies         Amount of audio to discard.
     *
     * @return  Flush id.  MsgFlush::kIdInvalid if the request failed.  Any other value
     *                     indicates success.  Pulling MsgFlush with this id informs the
     *                     caller that all data has been discarded.
     */
    virtual TUint TryDiscard(TUint aJiffies) = 0;
    /**
     * Attempt to stop delivery of the currently playing stream.
     *
     * This may be called from a different thread.  The implementor is responsible for any synchronisation.
     * Note that this may report failure if the entire stream is already in the pipeline.
     *
     * @param[in] aStreamId        Stream identifier, unique in the context of the current track only.
     *
     * @return  Flush id.  MsgFlush::kIdInvalid if the stop request failed.
     *          Any other value indicates success.  The code which issues the seek request
     *          should discard data until it pulls a MsgFlush with this id.
     */
    virtual TUint TryStop(TUint aStreamId) = 0;
    /**
     * Inform interested parties of an unexpected break in audio.
     *
     * Sources which are sensitive to latency may need to restart.
     * This may be called from a different thread.  The implementor is responsible for any synchronisation.
     *
     * @param[in] aMode            Reported by the MsgMode which preceded the stream which dropped out.
     *                             i.e. identifier for the UriProvider associated with this stream
     * @param[in] aStreamId        Stream identifier, unique in the context of the current track only.
     * @param[in] aStarving        true if entering starvation; false if starvation is over.
     */
    virtual void NotifyStarving(const Brx& aMode, TUint aStreamId, TBool aStarving) = 0;
};

class IUrlBlockWriter
{
public:
    /**
     * Read a block of data out of band, without affecting the state of the current stream.
     *
     * This may be called from a different thread.  The implementor is responsible for any synchronisation.
     * This is relatively inefficient so should be used with care.  (It is intended for use
     * during format recognition for a new stream; more frequent use would be questionable.)
     *
     * @param[in] aWriter          Interface used to return the requested data.
     * @param[in] aUrl             Uri to read from.
     * @param[in] aOffset          Byte offset to start reading from
     * @param[in] aBytes           Number of bytes to read
     *
     * @return  true if exactly aBytes were read; false otherwise
     */
    virtual TBool TryGet(IWriter& aWriter, const Brx& aUrl, TUint64 aOffset, TUint aBytes) = 0; // return false if we failed to get aBytes
    virtual ~IUrlBlockWriter() {}
};

class ISeekObserver
{
public:
    virtual void NotifySeekComplete(TUint aHandle, TUint aFlushId) = 0;
};

class ISeeker
{
public:
    static const TUint kHandleError = UINT_MAX;
public:
    virtual void StartSeek(TUint aStreamId, TUint aSecondsAbsolute, ISeekObserver& aObserver, TUint& aHandle) = 0; // aHandle will be set to value that is later passed to NotifySeekComplete.  Or kHandleError.
};

class ISeekRestreamer
{
public:
    virtual ~ISeekRestreamer() {}
    virtual TUint SeekRestream(const Brx& aMode, TUint aTrackId) = 0; // returns flush id that'll preceed restreamed track
};

class IStopper
{
public:
    virtual ~IStopper() {}
    virtual void RemoveStream(TUint aStreamId) = 0;
};

class IPipelineElementUpstream
{
public:
    virtual ~IPipelineElementUpstream() {}
    virtual Msg* Pull() = 0;
};

class IPipelineElementDownstream
{
public:
    virtual ~IPipelineElementDownstream() {}
    virtual void Push(Msg* aMsg) = 0;
};


/**
 * Should be implemented by the object that animates (calls Pull() on) Pipeline.
 */
class IPipelineAnimator
{
public:
    virtual ~IPipelineAnimator() {}
    /**
     * Query how much (if any) buffering is performed post-pipeline
     *
     * @return     Delay currently applied beyond the pipeline in Jiffies.
     *             See Jiffies class for time conversion utilities.
     */
    virtual TUint PipelineAnimatorBufferJiffies() const = 0;
    /**
     * Report any post-pipeline delay.
     *
     * Throws FormatUnsupported if aFormat is not supported.
     * Throws SampleRateUnsupported if aSampleRate is not supported.
     * Throws BitDepthUnsupported if aBitDepth is not supported.
     *
     * @param[in] aFormat           Audio format of the stream.
     * @param[in] aSampleRate       Sample rate (in Hz).
     * @param[in] aBitDepth         Bit depth (8, 16, 24 or 32).
     * @param[in] aNumChannels      Number of channels [1..8].
     *
     * @return     Delay applied beyond the pipeline in Jiffies.
     *             See Jiffies class for time conversion utilities.
     */
    virtual TUint PipelineAnimatorDelayJiffies(AudioFormat aFormat, TUint aSampleRate, TUint aBitDepth, TUint aNumChannels) const = 0;
    /**
     * Report sample packing requirements for DSD
     *
     * @return     Granularity of DSD data.  (Effectively, the points that audio can be split at.)
     *             Throws FormatUnsupported if DSD is not supported.
     */
    virtual TUint PipelineAnimatorDsdBlockSizeWords() const = 0;
    /**
     * Report the maximum bit depth supported.
     *
     */
    virtual TUint PipelineAnimatorMaxBitDepth() const = 0;
    /**
     * Report the maximum sample rates supported.
     *
     * @param[out] aPcm             Max supported rate for PCM audio
     * @param[out] aDsd             Max supported rate for DSD audio
     */
    virtual void PipelineAnimatorGetMaxSampleRates(TUint& aPcm, TUint& aDsd) const = 0;
};

class IPipeline : public IPipelineElementUpstream
{
public:
    virtual ~IPipeline() {}
    virtual void SetAnimator(IPipelineAnimator& aAnimator) = 0;
};

class IPostPipelineLatencyObserver
{
public:
    virtual void PostPipelineLatencyChanged() = 0;
    virtual ~IPostPipelineLatencyObserver() {}
};

class TrackFactory
{
public:
    TrackFactory(IInfoAggregator& aInfoAggregator, TUint aTrackCount);
    Track* CreateTrack(const Brx& aUri, const Brx& aMetaData);
    Track* CreateNullTrack();
private:
    Allocator<Track> iAllocatorTrack;
    Mutex iLock;
    TUint iNextId;
};

class MsgFactory;
class MsgFactoryInitParams
{
    friend class MsgFactory;
public:
    inline MsgFactoryInitParams();
    inline void SetMsgModeCount(TUint aCount);
    inline void SetMsgTrackCount(TUint aCount);
    inline void SetMsgDrainCount(TUint aCount);
    inline void SetMsgDelayCount(TUint aCount);
    inline void SetMsgEncodedStreamCount(TUint aCount);
    inline void SetMsgStreamSegmentCount(TUint aCount);
    inline void SetMsgAudioEncodedCount(TUint aCount, TUint aEncodedAudioCount);
    inline void SetMsgMetaTextCount(TUint aCount);
    inline void SetMsgStreamInterruptedCount(TUint aCount);
    inline void SetMsgHaltCount(TUint aCount);
    inline void SetMsgFlushCount(TUint aCount);
    inline void SetMsgWaitCount(TUint aCount);
    inline void SetMsgDecodedStreamCount(TUint aCount);
    inline void SetMsgAudioPcmCount(TUint aCount, TUint aDecodedAudioCount);
    inline void SetMsgAudioDsdCount(TUint aCount); // DecodedAudioCount defined with AudioPcmCount
    inline void SetMsgSilenceCount(TUint aCount);
    inline void SetMsgPlayableCount(TUint aPcmCount, TUint aDsdCount, TUint aSilenceCount);
    inline void SetMsgQuitCount(TUint aCount);
private:
    TUint iMsgModeCount;
    TUint iMsgTrackCount;
    TUint iMsgDrainCount;
    TUint iMsgDelayCount;
    TUint iMsgEncodedStreamCount;
    TUint iMsgStreamSegmentCount;
    TUint iEncodedAudioCount;
    TUint iMsgAudioEncodedCount;
    TUint iMsgMetaTextCount;
    TUint iMsgStreamInterruptedCount;
    TUint iMsgHaltCount;
    TUint iMsgFlushCount;
    TUint iMsgWaitCount;
    TUint iMsgDecodedStreamCount;
    TUint iDecodedAudioCount;
    TUint iMsgAudioPcmCount;
    TUint iMsgAudioDsdCount;
    TUint iMsgSilenceCount;
    TUint iMsgPlayablePcmCount;
    TUint iMsgPlayableDsdCount;
    TUint iMsgPlayableSilenceCount;
    TUint iMsgQuitCount;
};

class MsgFactory
{
public:
    MsgFactory(IInfoAggregator& aInfoAggregator, const MsgFactoryInitParams& aInitParams);

    MsgMode* CreateMsgMode(const Brx& aMode, const ModeInfo& aInfo, Optional<IClockPuller> aClockPuller, const ModeTransportControls& aTransportControls);
    MsgMode* CreateMsgMode(const Brx& aMode);
    MsgTrack* CreateMsgTrack(Media::Track& aTrack, TBool aStartOfStream = true);
    MsgDrain* CreateMsgDrain(Functor aCallback);
    MsgDelay* CreateMsgDelay(TUint aTotalJiffies);
    MsgDelay* CreateMsgDelay(TUint aRemainingJiffies, TUint aTotalJiffies);
    MsgEncodedStream* CreateMsgEncodedStream(const Brx& aUri, const Brx& aMetaText, TUint64 aTotalBytes, TUint64 aOffset, TUint aStreamId, TBool aSeekable, TBool aLive, Media::Multiroom aMultiroom, IStreamHandler* aStreamHandler, TUint aSeekPosMs = 0);
    MsgEncodedStream* CreateMsgEncodedStream(const Brx& aUri, const Brx& aMetaText, TUint64 aTotalBytes, TUint64 aOffset, TUint aStreamId, Media::SeekCapability aSeek, TBool aLive, Media::Multiroom aMultiroom, IStreamHandler* aStreamHandler, TUint aSeekPosMs);
    MsgEncodedStream* CreateMsgEncodedStream(const Brx& aUri, const Brx& aMetaText, TUint64 aTotalBytes, TUint64 aOffset, TUint aStreamId, TBool aSeekable, TBool aLive, Media::Multiroom aMultiroom, IStreamHandler* aStreamHandler, const PcmStreamInfo& aPcmStream);
    MsgEncodedStream* CreateMsgEncodedStream(const Brx& aUri, const Brx& aMetaText, TUint64 aTotalBytes, TUint64 aOffset, TUint aStreamId, TBool aSeekable, TBool aLive, Media::Multiroom aMultiroom, IStreamHandler* aStreamHandler, const PcmStreamInfo& aPcmStream, RampType aRamp);
    MsgEncodedStream* CreateMsgEncodedStream(const Brx& aUri, const Brx& aMetaText, TUint64 aTotalBytes, TUint64 aOffset, TUint aStreamId, TBool aSeekable, IStreamHandler* aStreamHandler, const DsdStreamInfo& aDsdStream);
    MsgEncodedStream* CreateMsgEncodedStream(MsgEncodedStream* aMsg, IStreamHandler* aStreamHandler);
    MsgStreamSegment* CreateMsgStreamSegment(const Brx& aId);
    MsgAudioEncoded* CreateMsgAudioEncoded(const Brx& aData);
    MsgMetaText* CreateMsgMetaText(const Brx& aMetaText);
    MsgStreamInterrupted* CreateMsgStreamInterrupted(TUint aJiffies = 0);
    MsgHalt* CreateMsgHalt(TUint aId = MsgHalt::kIdNone);
    MsgHalt* CreateMsgHalt(TUint aId, Functor aCallback);
    MsgFlush* CreateMsgFlush(TUint aId);
    MsgWait* CreateMsgWait();
    MsgDecodedStream* CreateMsgDecodedStream(TUint aStreamId, TUint aBitRate, TUint aBitDepth, TUint aSampleRate, TUint aNumChannels, const Brx& aCodecName, TUint64 aTrackLength, TUint64 aSampleStart, TBool aLossless, TBool aSeekable, TBool aLive, TBool aAnalogBypass, AudioFormat aFormat, Media::Multiroom aMultiroom, const SpeakerProfile& aProfile, IStreamHandler* aStreamHandler, RampType aRamp);
    MsgDecodedStream* CreateMsgDecodedStream(MsgDecodedStream* aMsg, IStreamHandler* aStreamHandler);
    MsgAudioPcm* CreateMsgAudioPcm(const Brx& aData, TUint aChannels, TUint aSampleRate, TUint aBitDepth, AudioDataEndian aEndian, TUint64 aTrackOffset);
    MsgAudioPcm* CreateMsgAudioPcm(MsgAudioEncoded* aAudio, TUint aChannels, TUint aSampleRate, TUint aBitDepth, TUint64 aTrackOffset); // aAudio must contain big endian pcm data
    MsgAudioPcm* CreateMsgAudioPcm(DecodedAudio* aAudioData, TUint aChannels, TUint aSampleRate, TUint aBitDepth, TUint64 aTrackOffset);
    MsgAudioDsd* CreateMsgAudioDsd(const Brx& aData, TUint aChannels, TUint aSampleRate, TUint aSampleBlockBits, TUint64 aTrackOffset, TUint aPadBytesPerChunk);
    MsgAudioDsd* CreateMsgAudioDsd(MsgAudioEncoded* aAudio, TUint aChannels, TUint aSampleRate, TUint aSampleBlockBits, TUint64 aTrackOffset, TUint aPadBytesPerChunk);
    MsgSilence* CreateMsgSilence(TUint& aSizeJiffies, TUint aSampleRate, TUint aBitDepth, TUint aChannels);
    MsgSilence* CreateMsgSilenceDsd(TUint& aSizeJiffies, TUint aSampleRate, TUint aChannels, TUint aSampleBlockWords, TUint aPadBytesPerChunk);
    MsgQuit* CreateMsgQuit();
    DecodedAudio* CreateDecodedAudio();
public:
    inline TUint AllocatorModeCount() const;
    inline TUint AllocatorTrackCount() const;
    inline TUint AllocatorDrainCount() const;
    inline TUint AllocatorDelayCount() const;
    inline TUint AllocatorEncodedStreamCount() const;
    inline TUint AllocatorStreamSegmentCount() const;
    inline TUint AllocatorAudioDataCount() const;
    inline TUint AllocatorAudioEncodedCount() const;
    inline TUint AllocatorMetaTextCount() const;
    inline TUint AllocatorStreamInterruptedCount() const;
    inline TUint AllocatorHaltCount() const;
    inline TUint AllocatorFlushCount() const;
    inline TUint AllocatorWaitCount() const;
    inline TUint AllocatorDecodedStreamCount() const;
    inline TUint AllocatorBitRateCount() const;
    inline TUint AllocatorAudioPcmCount() const;
    inline TUint AllocatorAudioDsdCount() const;
    inline TUint AllocatorSilenceCount() const;
    inline TUint AllocatorPlayablePcmCount() const;
    inline TUint AllocatorPlayableDsdCount() const;
    inline TUint AllocatorPlayableSilenceCount() const;
    inline TUint AllocatorPlayableSilenceDsdCount() const;
    inline TUint AllocatorQuitCount() const;
private:
    EncodedAudio* CreateEncodedAudio(const Brx& aData);
    DecodedAudio* CreateDecodedAudio(const Brx& aData, TUint aBitDepth, AudioDataEndian aEndian);
    MsgAudioDsd* CreateMsgAudioDsd(DecodedAudio* aAudioData, TUint aChannels, TUint aSampleRate, TUint aSampleBlockBits, TUint64 aTrackOffset, TUint aPadBytesPerChunk);
private:
    Allocator<MsgMode> iAllocatorMsgMode;
    Allocator<MsgTrack> iAllocatorMsgTrack;
    Allocator<MsgDrain> iAllocatorMsgDrain;
    TUint iDrainId;
    Allocator<MsgDelay> iAllocatorMsgDelay;
    Allocator<MsgEncodedStream> iAllocatorMsgEncodedStream;
    Allocator<MsgStreamSegment> iAllocatorMsgStreamSegment;
    Allocator<AudioData> iAllocatorAudioData;
    Allocator<MsgAudioEncoded> iAllocatorMsgAudioEncoded;
    Allocator<MsgMetaText> iAllocatorMsgMetaText;
    Allocator<MsgStreamInterrupted> iAllocatorMsgStreamInterrupted;
    Allocator<MsgHalt> iAllocatorMsgHalt;
    Allocator<MsgFlush> iAllocatorMsgFlush;
    Allocator<MsgWait> iAllocatorMsgWait;
    Allocator<MsgDecodedStream> iAllocatorMsgDecodedStream;
    Allocator<MsgAudioPcm> iAllocatorMsgAudioPcm;
    Allocator<MsgAudioDsd> iAllocatorMsgAudioDsd;
    Allocator<MsgSilence> iAllocatorMsgSilence;
    Allocator<MsgPlayablePcm> iAllocatorMsgPlayablePcm;
    Allocator<MsgPlayableDsd> iAllocatorMsgPlayableDsd;
    Allocator<MsgPlayableSilence> iAllocatorMsgPlayableSilence;
    Allocator<MsgPlayableSilenceDsd> iAllocatorMsgPlayableSilenceDsd;
    Allocator<MsgQuit> iAllocatorMsgQuit;
};

#include <OpenHome/Media/Pipeline/Msg.inl>

} // namespace Media
} // namespace OpenHome

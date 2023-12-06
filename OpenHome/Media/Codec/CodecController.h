#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Pipeline/Rewinder.h>

#include <atomic>
#include <vector>

EXCEPTION(CodecStreamStart);
EXCEPTION(CodecStreamEnded);
EXCEPTION(CodecStreamStopped);
EXCEPTION(CodecStreamFlush);
EXCEPTION(CodecStreamFeatureUnsupported);
EXCEPTION(CodecRecognitionOutOfData);

namespace OpenHome {
namespace Media {
    class Logger;
    class MsgAudioEncoded;
namespace Codec {

/**
 * Interface used by codecs to communicate with the pipeline.
 */
class ICodecController
{
public:
    /**
     * Read up to a specified number of bytes.
     *
     * Appends to the client's buffer.  May return a smaller number of bytes than
     * requested at the end of the stream.
     *
     * @param[in] aBuf           Buffer to write into.  Data is appended to any existing content.
     * @param[in] aBytes         Number of bytes to read.
     *                           Fewer bytes may be returned at the end of a stream.
     */
    virtual void Read(Bwx& aBuf, TUint aBytes) = 0;
    /**
     * Read the content of the next audio msg.
     *
     * Appends to the client's buffer.  May return less than the content of a single
     * audio msg if the audio data is larger than the client's buffer.
     * This offers a small efficiency gain for audio formats that are transmitted in frames
     * which can only be processed in their entirety.
     *
     * @param[in] aBuf           Buffer to write into.  Data is appended to any existing content.
     */
    virtual void ReadNextMsg(Bwx& aBuf) = 0;
    /**
     * Retrieve an opaque pointer to the next audio msg.
     *
     * This is the lowest cost but least flexible way to read audio data.
     * The only supported use of MsgAudioEncoded is to pass it back out via
     * the appropriate overload of OutputAudioPcm
     *
     * @return     Opaque pointer to the next audio msg
     */
    virtual MsgAudioEncoded* ReadNextMsg() = 0;
    /**
     * Read a block of data out of band, without affecting the state of the current stream.
     *
     * This is useful if a codec's Recognise() function requires data from near the end
     * of a stream.  (Seeking during recognition would be awkward; calling Read() repeatedly
     * would attempt to buffer more data than the pipeline supports and would risk a protocol
     * module moving on to the next stream.)
     * This is relatively inefficient so should be used with care.  (Use during recognition is
     * likely reasonable; use during decoding would be questionable.)
     *
     * @param[in] aWriter        Interface used to return the requested data.
     * @param[in] aOffset        Byte offset into the stream.
     * @param[in] aBytes         Length (in bytes) of data to be read.
     *
     * @return     true if the read succeeded; false if [0..aBytes) were written.
     */
    virtual TBool Read(IWriter& aWriter, TUint64 aOffset, TUint aBytes) = 0;
    /**
     * Request a seek to a different point in the stream.
     *
     * @param[in] aStreamId      Stream identifier.  Passed to a codec's TrySeek() function.
     * @param[in] aBytePos       Byte offset to seek to.
     *
     * @return     true if the request succeeded (the next call to Read() will return data from aBytePos);
     *             false if the request failed.
     */
    virtual TBool TrySeekTo(TUint aStreamId, TUint64 aBytePos) = 0;
    /**
     * Query the length (in bytes) of the current stream.
     *
     * @return     The total number of bytes in the current stream; 0 if this isn't available.
     */
    virtual TUint64 StreamLength() const = 0;
    /**
     * Query our position in the current stream.
     *
     * The next call to Read() will return data at this position.
     *
     * @return     Number of bytes the codec has consumed from the stream.
     */
    virtual TUint64 StreamPos() const = 0;
    /**
     * Notify the pipeline of a new stream or a discontinuity in the current stream.
     *
     * Must be called before any other Output function at the start of a stream,  This is
     * typically done from either StreamInitialise() or Process().  Must also be called after
     * a successful seek.  Should not be called at any other times.
     *
     * @param[in] aBitRate       The bit rate of the stream.  Reported to UI code; not used by the pipeline.
     * @param[in] aBitDepth      The bit depth of the decoded stream.  Must be one of 8, 16 or 24.
     * @param[in] aSampleRate    The sample rate of the decoded stream.
     * @param[in] aNumChannels   The number of channels in the decoded stream.  Must be in the range [2..8].
     * @param[in] aCodecName     The name of the codec.  Reported to UI code; not used by the pipeline.
     * @param[in] aLength        Number of jiffies in the decoded stream.  Reported to UI code; not used by the pipeline.
     * @param[in] aSampleStart   The first sample number in the next audio data to be output.  0 at the start of a stream.
     * @param[in] aLossless      Whether the stream is in a lossless format.  Reported to UI code; not used by the pipeline.
     * @param[in] aProfile       Speaker profile (channel allocation) of the decoded stream
     * @param[in] aAnalogBypass  Whether the stream being played is entirely in the analog domain.  (This stream will still
     *                           be used for casting to other devices and to help control volume ramping.)
     */
    virtual void OutputDecodedStream(TUint aBitRate, TUint aBitDepth, TUint aSampleRate, TUint aNumChannels,
                                     const Brx& aCodecName, TUint64 aLength, TUint64 aSampleStart, TBool aLossless,
                                     SpeakerProfile aProfile, TBool aAnalogBypass = false) = 0;
    /**
    * Notify the pipeline of a new DSD stream or a discontinuity in the current stream.
    *
    * Must be called before any other Output function at the start of a stream,  This is
    * typically done from either StreamInitialise() or Process().  Must also be called after
    * a successful seek.  Should not be called at any other times.
    *
    * @param[in] aSampleRate    The sample rate of the decoded stream.
    * @param[in] aNumChannels   The number of channels in the decoded stream.  Must be in the range [2..8].
    * @param[in] aCodecName     The name of the codec.  Reported to UI code; not used by the pipeline.
    * @param[in] aLength        Number of jiffies in the decoded stream.  Reported to UI code; not used by the pipeline.
    * @param[in] aSampleStart   The first sample number in the next audio data to be output.  0 at the start of a stream.
    * @param[in] aProfile       Speaker profile (channel allocation) of the decoded stream
    */
    virtual void OutputDecodedStreamDsd(TUint aSampleRate, TUint aNumChannels, const Brx& aCodecName,
                                        TUint64 aLength, TUint64 aSampleStart, SpeakerProfile aProfile) = 0;
    /**
     * Add a block of decoded (PCM) audio to the pipeline.
     *
     * @param[in] aData          PCM audio data.  Must contain an exact number of samples.
     *                           i.e. aData.Bytes() % (aChannels * aBitDepth/8) == 0
     * @param[in] aChannels      Number of channels.  Must be in the range [1..8].
     * @param[in] aSampleRate    Sample rate.
     * @param[in] aBitDepth      Number of bits of audio for a single sample for a single channel.
     * @param[in] aEndian        Endianness of audio data.
     * @param[in] aTrackOffset   Offset (in jiffies) into the stream at the start of aData.
     *
     * @return     Number of jiffies of audio contained in aData.
     */
    virtual TUint64 OutputAudioPcm(const Brx& aData, TUint aChannels, TUint aSampleRate, TUint aBitDepth, AudioDataEndian aEndian, TUint64 aTrackOffset) = 0;
    /**
     * Add a block of decoded (PCM) audio to the pipeline.
     *
     * Only supported for encoded audio that is already big endian, packed PCM.
     *
     * @param[in] aMsg           Returned from ReadNextMsg().
     * @param[in] aChannels      Number of channels.  Must be in the range [2..8].
     * @param[in] aSampleRate    Sample rate.
     * @param[in] aBitDepth      Number of bits of audio for a single sample for a single channel.
     * @param[in] aTrackOffset   Offset (in jiffies) into the stream at the start of aData.
     *
     * @return     Number of jiffies of audio contained in aMsg.
     */
    virtual TUint64 OutputAudioPcm(MsgAudioEncoded* aMsg, TUint aChannels, TUint aSampleRate, TUint aBitDepth, TUint64 aTrackOffset) = 0;
    /**
    * Add a block of DSD audio to the pipeline.
    *
    * @param[in] aData             DSD audio data.  Must contain an exact number of samples.
    * @param[in] aChannels         Number of channels.
    * @param[in] aSampleRate       Sample rate.
    * @param[in] aSampleBlockWords Block size (in words) of DSD data.  The minimum required output
    *                              of DSD data is 16bits x left, 16bits x right, so any valid
    *                              output must be a multiple of this convention.
    * @param[in] aPadBytesPerChunk  The number of bytes padding present for every word of playable data.
    *                              If aPadBytesPerChunk = 2, the total chunk size will be 6 bytes, (4 bytes playable + padding)
    * @param[in] aTrackOffset      Offset (in jiffies) into the stream at the start of aData.
    *
    * @return     Number of jiffies of audio contained in aData.
    */
    virtual TUint64 OutputAudioDsd(const Brx& aData, TUint aChannels, TUint aSampleRate, TUint aSampleBlockWords, TUint64 aTrackOffset, TUint aPadBytesPerChunk) = 0;
    /**
    * Add a block of decoded (DSD) audio to the pipeline.
    *
    * Only supported for encoded audio that is already packed in a format expected by the pipeline animator.
    *
    * @param[in] aMsg              Returned from ReadNextMsg().
    * @param[in] aChannels         Number of channels.  Must be in the range [1..2].
    * @param[in] aSampleRate       Sample rate.
    * @param[in] aSampleBlockWords Block size (in words) of DSD data including any padding applied by the codecs.
    *                              There is a 1 word (4 byte) minimum for any DSD data. Where 32 bits are
    *                              assigned for a stereo sample pair (16 bits x R, 16 bits x R).
    *                              BlockSize will increase depending on padding applied and the output method.
    * @param[in] aPadBytesPerChunk  The number of bytes padding present for every word of playable data.
    *                              If aPadBytesPerChunk = 2, the total chunk size will be 6 bytes, (4 bytes playable + padding)
    * @param[in] aTrackOffset      Offset (in jiffies) into the stream at the start of aData.
    *
    * @return     Number of jiffies of audio contained in aMsg.
    */
    virtual TUint64 OutputAudioDsd(MsgAudioEncoded* aMsg, TUint aChannels, TUint aSampleRate, TUint aSampleBlockWords, TUint64 aTrackOffset, TUint aPadBytesPerChunk) = 0;
    /**
     * Notify the pipeline of an update in meta text.
     *
     * This allows the pipeline to output additional information about a stream.
     *
     * @param[in] aMetaText          Meta text. Must be in DIDL-Lite format.
     */
    virtual void OutputMetaText(const Brx& aMetaText) = 0;
    /**
     * Notify the pipeline of a discontinuity in audio.
     *
     * This allows the pipeline to ramp audio down/up to avoid glitches caused by a stream discontinuity. A MsgDecodedStream must follow this.
     */
    virtual void OutputStreamInterrupted() = 0;
    virtual void GetAudioBuf(TByte*& aDest, TUint& aSamples) = 0;
    virtual void OutputAudioBuf(TUint aSamples, TUint64& aTrackOffset) = 0;
    virtual TUint MaxBitDepth() const = 0;
};

class EncodedStreamInfo
{
    friend class CodecController;
public:
    enum class Format
    {
        Encoded,
        Pcm,
        Dsd
    };
public:
    Format StreamFormat() const;
    TUint BitDepth() const;
    TUint SampleRate() const;
    TUint NumChannels() const;
    AudioDataEndian Endian() const;
    SpeakerProfile Profile() const;
    TUint64 StartSample() const;
    TBool AnalogBypass() const;
    const Brx& CodecName() const;
    TBool Lossless() const;
    TUint SampleBlockWords() const;
private:
    EncodedStreamInfo();
    void SetPcm(TUint aBitDepth, TUint aSampleRate, TUint aNumChannels, AudioDataEndian aEndian, SpeakerProfile aProfile,
                TUint64 aStartSample, TBool aAnalogBypass, const Brx& aCodecName, TBool aLossless);
    void SetDsd(TUint aSampleRate, TUint aNumChannels, TUint aSampleBlockWords, TUint64 aStartSample, const Brx& aCodecName);
private:
    Format iFormat;
    TBool iAnalogBypass;
    TBool iLossless;
    TUint iBitDepth;
    TUint iSampleRate;
    TUint iNumChannels;
    TUint iDsdSampleBlockWords;
    AudioDataEndian iEndian;
    SpeakerProfile iProfile;
    TUint64 iStartSample;
    BwsCodecName iCodecName;
};

/**
 * Base class for all codecs.
 *
 * A codec accepts encoded data and outputs PCM.
 * Each instance can choose to decode one or more audio formats.
 */
class CodecBase : private INonCopyable
{
    friend class CodecController;
public:
    enum RecognitionComplexity
    {
        kCostVeryLow
       ,kCostLow
       ,kCostMedium
       ,kCostHigh
    };
public:
    virtual ~CodecBase();
public:
    /**
     * Report whether a new audio stream is handled by this codec.
     *
     * Data is fetched by calling iController->Read().  Any data read during recognition will
     * also be made available to other codecs (if recognition fails) or this codec again
     * during processing (if recognition succeeds).
     *
     * @param[in] aStreamInfo    Details Info describing the current encoded stream
     *
     * @return     true if this codec can decode the audio stream; false otherwise.
     */
    virtual TBool Recognise(const EncodedStreamInfo& aStreamInfo) = 0;
    /**
     * Notify a codec that decoding of a stream is about to begin.
     *
     * Called after Recognise() succeeds but before any call to Process().
     * May be used to initialise any state that is specific to the current stream.
     */
    virtual void StreamInitialise();
    /**
     * Decode a chunk of the stream.
     *
     * Will be called repeatedly until the stream is fully decoded.
     * Call iController->Read() to pull encoded data.  Each call can decode as much or as
     * little audio as is convenient.  Note however that decoding in very large blocks
     * risks delaying any Seek requests.
     * Can either throw CodecStreamEnded or just continue to try reading (causing an exception
     * to be thrown) to indicate it has reached the end of the stream.
     */
    virtual void Process() = 0;
    /**
     * Seek to a given sample position in the stream.
     *
     * The codec should translate the specified sample position into a byte offset then call
     * iController->TrySeekTo().  The controller can be called many times for a single seek
     * request if necessary.
     * @param[in] aStreamId      Stream identifier.  Passed to iController->TrySeekTo().
     * @param[in] aSample        Decoded sample position (zero-based) to seek to.
     *
     * @return     true if the seek succeeded; false otherwise.
     */
    virtual TBool TrySeek(TUint aStreamId, TUint64 aSample) = 0;
    /**
     * Notify a codec that decoding of a stream has finished.
     *
     * Called after the final call to Process() for a stream.
     * May be used to destroy any state that is specific to the current stream.
     */
    virtual void StreamCompleted();
    /**
     * Read the identifier (name) for this codec
     *
     * @return     Codec identifier
     */
    const TChar* Id() const;
protected:
    CodecBase(const TChar* aId, RecognitionComplexity aRecognitionCost=kCostMedium);
    static SpeakerProfile DeriveProfile(TUint aChannels);
private:
    void Construct(ICodecController& aController);
protected:
    ICodecController* iController;
private:
    const TChar* iId;
    RecognitionComplexity iRecognitionCost;
};

class CodecController : public ISeeker, private ICodecController, private IMsgProcessor, private IStreamHandler, private INonCopyable
{
private:
    class InitialSeekObserver : public ISeekObserver
    {
    private: // from ISeekObserver
        void NotifySeekComplete(TUint aHandle, TUint aFlushId) override;
    };
public:
    CodecController(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstreamElement, IPipelineElementDownstream& aDownstreamElement,
                    IUrlBlockWriter& aUrlBlockWriter, TUint aMaxOutputJiffies, TUint aThreadPriority, TBool aLogger);
    virtual ~CodecController();
    void AddCodec(CodecBase* aCodec);
    void Start();
    void SetAnimator(IPipelineAnimator& aAnimator);
    void Flush(TUint aFlushId);
private:
    void CodecThread();
    void Rewind();
    Msg* PullMsg();
    void Queue(Msg* aMsg);
    TBool QueueTrackData() const;
    void ReleaseAudioEncoded();
    void ReleaseAudioDecoded();
    TBool DoRead(Bwx& aBuf, TUint aBytes);
    void DoOutputDecodedStream(MsgDecodedStream* aMsg);
    TUint64 DoOutputAudio(MsgAudio* aAudioMsg);
private: // ISeeker
    void StartSeek(TUint aStreamId, TUint aSecondsAbsolute, ISeekObserver& aObserver, TUint& aHandle) override;
private: // ICodecController
    void Read(Bwx& aBuf, TUint aBytes) override;
    void ReadNextMsg(Bwx& aBuf) override;
    MsgAudioEncoded* ReadNextMsg() override;
    TBool Read(IWriter& aWriter, TUint64 aOffset, TUint aBytes) override; // Read an arbitrary amount of data from current stream, out-of-band from pipeline
    TBool TrySeekTo(TUint aStreamId, TUint64 aBytePos) override;
    TUint64 StreamLength() const override;
    TUint64 StreamPos() const override;
    void OutputDecodedStream(TUint aBitRate, TUint aBitDepth, TUint aSampleRate, TUint aNumChannels, const Brx& aCodecName, TUint64 aTrackLength, TUint64 aSampleStart, TBool aLossless, SpeakerProfile aProfile, TBool aAnalogBypass) override;
    void OutputDecodedStreamDsd(TUint aSampleRate, TUint aNumChannels, const Brx& aCodecName, TUint64 aLength, TUint64 aSampleStart, SpeakerProfile aProfile) override;
    TUint64 OutputAudioPcm(const Brx& aData, TUint aChannels, TUint aSampleRate, TUint aBitDepth, AudioDataEndian aEndian, TUint64 aTrackOffset) override;
    TUint64 OutputAudioPcm(MsgAudioEncoded* aMsg, TUint aChannels, TUint aSampleRate, TUint aBitDepth, TUint64 aTrackOffset) override;
    TUint64 OutputAudioDsd(const Brx& aData, TUint aChannels, TUint aSampleRate, TUint aSampleBlockWords, TUint64 aTrackOffset, TUint aPadBytesPerChunk) override;
    TUint64 OutputAudioDsd(MsgAudioEncoded* aMsg, TUint aChannels, TUint aSampleRate, TUint aSampleBlockWords, TUint64 aTrackOffset, TUint aPadBytesPerChunk) override;
    void OutputMetaText(const Brx& aMetaText) override;
    void OutputStreamInterrupted() override;
    void GetAudioBuf(TByte*& aDest, TUint& aSamples) override;
    void OutputAudioBuf(TUint aSamples, TUint64& aTrackOffset) override;
    TUint MaxBitDepth() const override;
private: // IMsgProcessor
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
private: // IStreamHandler
    EStreamPlay OkToPlay(TUint aStreamId) override;
    TUint TrySeek(TUint aStreamId, TUint64 aOffset) override;
    TUint TryDiscard(TUint aJiffies) override;
    TUint TryStop(TUint aStreamId) override;
    void NotifyStarving(const Brx& aMode, TUint aStreamId, TBool aStarving) override;
private:
    MsgFactory& iMsgFactory;
    Rewinder iRewinder;
    Logger* iLoggerRewinder;
    IPipelineElementUpstream* iUpstream;
    IPipelineElementDownstream& iDownstreamElement;
    IUrlBlockWriter& iUrlBlockWriter;
    Mutex iLock;
    Semaphore iShutdownSem;
    std::vector<CodecBase*> iCodecs;
    ThreadFunctor* iDecoderThread;
    IPipelineAnimator* iAnimator;
    CodecBase* iActiveCodec;
    Msg* iPendingMsg;
    Msg* iPendingQuit;
    TBool iQueueTrackData;
    TBool iStreamStarted;
    TBool iStreamEnded;
    TBool iStreamStopped;
    TBool iQuit;
    TBool iSeek;
    TBool iRecognising;
    TBool iSeekInProgress;
    TUint iSeekSeconds;
    TUint iExpectedFlushId;
    TBool iConsumeExpectedFlush;
    ISeekObserver* iSeekObserver;
    TUint iSeekHandle;
    TUint iExpectedSeekFlushId;
    MsgFlush* iPostSeekFlush;
    MsgDecodedStream* iPostSeekStreamInfo;
    MsgAudioEncoded* iAudioEncoded;

    TBool iSeekable;
    TBool iLive;
    MsgEncodedStream::Format iStreamFormat;
    Media::Multiroom iMultiroom;
    PcmStreamInfo iPcmStream;
    DsdStreamInfo iDsdStream;
    std::atomic<IStreamHandler*> iStreamHandler;
    TUint iStreamId;
    BwsTrackUri iTrackUri;
    TUint iChannels;    // Only for detecting out-of-sequence MsgAudioPcm.
    TUint iSampleRate;
    TUint iBitDepth;    // Only for detecting out-of-sequence MsgAudioPcm
    TUint iBytesPerSample;
    TUint64 iStreamLength;
    TUint64 iStreamPos;
    TUint iTrackId;
    TUint iMaxOutputSamples;
    TUint iMaxOutputBytes;
    const TUint iMaxOutputJiffies;
    DecodedAudio* iAudioDecoded;
    TUint iAudioDecodedBytes;
    RampType iRamp;
    TUint iInitialSeekPos;
    InitialSeekObserver iInitialSeekObserver;
};

class CodecBufferedReader : public IReader, private INonCopyable
{
private:
    enum EState
    {
        eReading,
        eEos,
        eBeyondEos,
    };
public:
    CodecBufferedReader(ICodecController& aCodecController, Bwx& aBuf);
public: // from IReader
    Brn Read(TUint aBytes) override; // Returns [0..aBytes].  0 => stream closed, followed by ReaderError on subsequent reads beyond end of stream.
    void ReadFlush() override;
    void ReadInterrupt() override;  // ASSERTS
private:
    ICodecController& iCodecController;
    Bwx& iBuf;
    EState iState;
};

} // namespace Codec
} // namespace Media
} // namespace OpenHome

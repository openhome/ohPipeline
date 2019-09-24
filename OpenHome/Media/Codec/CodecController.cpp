#include <OpenHome/Media/Codec/CodecController.h>
#include <OpenHome/Media/Codec/Container.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Codec/Id3v2.h>
#include <OpenHome/Media/Pipeline/Rewinder.h>
#include <OpenHome/Media/Pipeline/Logger.h>
#include <OpenHome/Media/Debug.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Codec;


// class EncodedStreamInfo

EncodedStreamInfo::Format EncodedStreamInfo::StreamFormat() const
{
    return iFormat;
}

TUint EncodedStreamInfo::BitDepth() const
{
    ASSERT(iFormat != Format::Encoded);
    return iBitDepth;
}

TUint EncodedStreamInfo::SampleRate() const
{
    ASSERT(iFormat != Format::Encoded);
    return iSampleRate;
}

TUint EncodedStreamInfo::NumChannels() const
{
    ASSERT(iFormat != Format::Encoded);
    return iNumChannels;
}

TUint EncodedStreamInfo::SampleBlockWords() const
{
    ASSERT(iFormat == Format::Dsd);
    return iDsdSampleBlockWords;
}

AudioDataEndian EncodedStreamInfo::Endian() const
{
    return iEndian;
}

SpeakerProfile EncodedStreamInfo::Profile() const
{
    ASSERT(iFormat == Format::Pcm);
    return iProfile;
}

TUint64 EncodedStreamInfo::StartSample() const
{
    return iStartSample;
}

TBool EncodedStreamInfo::AnalogBypass() const
{
    return iAnalogBypass;
}

const Brx& EncodedStreamInfo::CodecName() const
{
    ASSERT(iFormat != Format::Encoded);
    return iCodecName;
}

TBool EncodedStreamInfo::Lossless() const
{
    ASSERT(iFormat != Format::Encoded);
    return iLossless;
}

EncodedStreamInfo::EncodedStreamInfo()
    : iFormat(Format::Encoded)
    , iAnalogBypass(false)
    , iLossless(false)
    , iBitDepth(UINT_MAX)
    , iSampleRate(UINT_MAX)
    , iNumChannels(UINT_MAX)
    , iDsdSampleBlockWords(UINT_MAX)
    , iEndian(AudioDataEndian::Invalid)
    , iStartSample(0)
{
}

void EncodedStreamInfo::SetPcm(TUint aBitDepth, TUint aSampleRate, TUint aNumChannels, AudioDataEndian aEndian, SpeakerProfile aProfile,
                               TUint64 aStartSample, TBool aAnalogBypass, const Brx& aCodecName, TBool aLossless)
{
    iFormat = Format::Pcm;
    iBitDepth = aBitDepth;
    iSampleRate = aSampleRate;
    iNumChannels = aNumChannels;
    iEndian = aEndian;
    iProfile = aProfile;
    iStartSample = aStartSample;
    iAnalogBypass = aAnalogBypass;
    iCodecName.Replace(aCodecName);
    iLossless = aLossless;
}

void EncodedStreamInfo::SetDsd(TUint aSampleRate, TUint aNumChannels, TUint aSampleBlockWords, TUint64 aStartSample, const Brx& aCodecName)
{
    iFormat = Format::Dsd;
    iBitDepth = 1;
    iSampleRate = aSampleRate;
    iNumChannels = aNumChannels;
    iDsdSampleBlockWords = aSampleBlockWords;
    iStartSample = aStartSample;
    iCodecName.Replace(aCodecName);
    iLossless = true;
}


// CodecBase

CodecBase::~CodecBase()
{
}

void CodecBase::StreamInitialise()
{
}

void CodecBase::StreamCompleted()
{
}

const TChar* CodecBase::Id() const
{
    return iId;
}

CodecBase::CodecBase(const TChar* aId, RecognitionComplexity aRecognitionCost)
    : iController(nullptr)
    , iId(aId)
    , iRecognitionCost(aRecognitionCost)
{
}

void CodecBase::Construct(ICodecController& aController)
{
    iController = &aController;
}

SpeakerProfile CodecBase::DeriveProfile(TUint aChannels)
{
    return (aChannels == 1) ? SpeakerProfile(1) : SpeakerProfile(2);
}


// CodecController

CodecController::CodecController(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstreamElement, IPipelineElementDownstream& aDownstreamElement,
                                 IUrlBlockWriter& aUrlBlockWriter, TUint aMaxOutputJiffies, TUint aThreadPriority, TBool aLogger)
    : iMsgFactory(aMsgFactory)
    , iRewinder(aMsgFactory, aUpstreamElement)
    , iLoggerRewinder(nullptr)
    , iUpstream(&iRewinder)
    , iDownstreamElement(aDownstreamElement)
    , iUrlBlockWriter(aUrlBlockWriter)
    , iLock("CDCC")
    , iShutdownSem("CDC2", 0)
    , iActiveCodec(nullptr)
    , iPendingMsg(nullptr)
    , iPendingQuit(nullptr)
    , iSeekObserver(nullptr)
    , iSeekHandle(0)
    , iPostSeekFlush(nullptr)
    , iPostSeekStreamInfo(nullptr)
    , iAudioEncoded(nullptr)
    , iSeekable(false)
    , iLive(false)
    , iStreamFormat(MsgEncodedStream::Format::Encoded)
    , iStreamHandler(nullptr)
    , iStreamId(0)
    , iChannels(0)
    , iSampleRate(0)
    , iBitDepth(0)
    , iStreamLength(0)
    , iStreamPos(0)
    , iTrackId(UINT_MAX)
    , iMaxOutputSamples(0)
    , iMaxOutputBytes(0)
    , iMaxOutputJiffies(aMaxOutputJiffies)
    , iAudioDecoded(nullptr)
    , iAudioDecodedBytes(0)
{
    iDecoderThread = new ThreadFunctor("CodecController", MakeFunctor(*this, &CodecController::CodecThread), aThreadPriority);
    if (aLogger) {
        iLoggerRewinder = new Logger(iRewinder, "Rewinder");
        iUpstream = iLoggerRewinder;
        //iLoggerRewinder->SetEnabled(true);
        //iLoggerRewinder->SetFilter(Logger::EMsgAll);
    }
}

CodecController::~CodecController()
{
    iShutdownSem.Wait();
    delete iDecoderThread;
    ASSERT(iPendingMsg == nullptr);
    for (size_t i=0; i<iCodecs.size(); i++) {
        delete iCodecs[i];
    }
    ReleaseAudioEncoded();
    ReleaseAudioDecoded();
    if (iPostSeekFlush != nullptr) {
        iPostSeekFlush->RemoveRef();
    }
    if (iPostSeekStreamInfo != nullptr) {
        iPostSeekStreamInfo->RemoveRef();
    }
    delete iLoggerRewinder;
}

void CodecController::AddCodec(CodecBase* aCodec)
{
    aCodec->Construct(*this);
    const CodecBase::RecognitionComplexity cost = aCodec->iRecognitionCost;
    auto it = iCodecs.begin();
    for (; it!=iCodecs.end(); ++it) {
        if ((*it)->iRecognitionCost > cost) {
            break;
        }
    }
    iCodecs.insert(it, aCodec);
#if 0
    Log::Print("Sorted codecs are: ");
    it = iCodecs.begin();
    for (; it!=iCodecs.end(); ++it) {
        Log::Print("%s, ", (*it)->iId);
    }
    Log::Print("\n");
#endif
}

void CodecController::Start()
{
    iDecoderThread->Start();
}

void CodecController::StartSeek(TUint aStreamId, TUint aSecondsAbsolute, ISeekObserver& aObserver, TUint& aHandle)
{
    AutoMutex a(iLock);
    if (aStreamId != iStreamId) {
        LOG_ERROR(kMedia, "CodecController::StartSeek(%u, %u) fail - wrong stream id (current %u)\n", aStreamId, aSecondsAbsolute, iStreamId);
        aHandle = ISeeker::kHandleError;
        return;
    }
    if (iActiveCodec == nullptr) {
        LOG_ERROR(kMedia, "CodecController::StartSeek(%u, %u) fail - no active codec\n", aStreamId, aSecondsAbsolute);
        aHandle = ISeeker::kHandleError;
        return;
    }
    if (!iSeekable) {
        LOG_ERROR(kMedia, "CodecController::StartSeek(%u, %u) fail - stream not seekable\n", aStreamId, aSecondsAbsolute);
        aHandle = ISeeker::kHandleError;
        return;
    }
    if (iSeek) {
        LOG_ERROR(kMedia, "CodecController::StartSeek(%u, %u) fail - seek already in progress\n", aStreamId, aSecondsAbsolute);
        aHandle = ISeeker::kHandleError;
        return;
    }
    aHandle = ++iSeekHandle;
    iSeekObserver = &aObserver;
    iSeek = true;
    iSeekSeconds = aSecondsAbsolute;
}

void CodecController::CodecThread()
{
    iStreamStarted = false;
    iSeek = false;
    iQuit = false;
    iExpectedFlushId = iExpectedSeekFlushId = MsgFlush::kIdInvalid;
    iConsumeExpectedFlush = false;
    while (!iQuit) {
        // push out any pending msg (from previous run of loop)
        if (iPendingMsg != nullptr) {
            Queue(iPendingMsg);
            iPendingMsg = nullptr;
        }
        try {
            iLock.Wait();
            iQueueTrackData = iStreamEnded = iStreamStopped = iSeek = iRecognising = iSeekInProgress = false;
            iActiveCodec = nullptr;
            iChannels = iBitDepth = iBytesPerSample = 0;
            iSampleRate = iSeekSeconds = 0;
            iStreamPos = 0LL;
            ReleaseAudioEncoded();
            ReleaseAudioDecoded();
            iLock.Signal();

            LOG(kMedia, "CodecThread - search for new stream\n");
            // Find next start of stream marker, ignoring any audio or meta data we encounter
            while (!iStreamStarted && !iQuit) {
                Msg* msg = PullMsg();
                if (msg != nullptr) {
                    Queue(msg);
                }
            }
            if (iQuit) {
                break;
            }
            iQueueTrackData = true;
            iStreamStarted = iStreamEnded = false;
            iRecognising = true;
            EncodedStreamInfo streamInfo;
            if (iStreamFormat == MsgEncodedStream::Format::Pcm) {
                streamInfo.SetPcm(iPcmStream.BitDepth(), iPcmStream.SampleRate(), iPcmStream.NumChannels(),
                                  iPcmStream.Endian(), iPcmStream.Profile(), iPcmStream.StartSample(), iPcmStream.AnalogBypass(),
                                  iPcmStream.CodecName(), iPcmStream.Lossless());
            }
            else if (iStreamFormat == MsgEncodedStream::Format::Dsd) {
                streamInfo.SetDsd(iDsdStream.SampleRate(), iDsdStream.NumChannels(),
                                  iDsdStream.SampleBlockWords(), iDsdStream.StartSample(),
                                  iDsdStream.CodecName());
            }

            LOG(kMedia, "CodecThread: start recognition.  iTrackId=%u, iStreamId=%u\n", iTrackId, iStreamId);
            TBool streamEnded = false;

            for (size_t i=0; i<iCodecs.size() && !iQuit && !iStreamStopped; i++) {
                CodecBase* codec = iCodecs[i];
                TBool recognised = false;
                try {
                    recognised = codec->Recognise(streamInfo);
                }
                catch (CodecStreamStart&) {}
                catch (CodecStreamEnded&) {}
                catch (CodecStreamStopped&) {}
                catch (CodecStreamFlush&) {
                    break;
                }
                catch (CodecStreamCorrupt&) {}
                catch (CodecStreamFeatureUnsupported&) {}
                catch (CodecRecognitionOutOfData&) {
                    Log::Print("WARNING: codec %s filled Rewinder during recognition\n", codec->Id());
                }
                iLock.Wait();
                if (iStreamStarted || iStreamEnded) {
                    streamEnded = true;
                }
                iStreamStarted = iStreamEnded = false; // Rewind() will result in us receiving any additional Track or EncodedStream msgs again
                Rewind();
                iLock.Signal();
                if (recognised) {
                    iActiveCodec = codec;
                    break;
                }
            }
            iRecognising = false;
            iRewinder.Stop(); // stop buffering audio
            if (iQuit) {
                break;
            }
            LOG(kMedia, "CodecThread: recognition complete\n");
            if (iActiveCodec == nullptr) {
                if (iStreamId != 0  && // FIXME - hard-coded assumption about Filler's NullTrack
                    !iStreamStopped && // we wouldn't necessarily expect to recognise a track if we're told to stop
                    !streamEnded) {    // ...or reach the track end during recognition
                    Log::Print("Failed to recognise audio format (iStreamStopped=%u, iExpectedFlushId=%u), flushing stream...\n", iStreamStopped, iExpectedFlushId);
                }
                iLock.Wait();
                if (iExpectedFlushId == MsgFlush::kIdInvalid) {
                    auto streamHandler = iStreamHandler.load();
                    (void)streamHandler->OkToPlay(iStreamId);
                    iExpectedFlushId = streamHandler->TryStop(iStreamId);
                    if (iExpectedFlushId != MsgFlush::kIdInvalid) {
                        iConsumeExpectedFlush = true;
                    }
                }
                iLock.Signal();
                continue;
            }

            // tell codec to process audio data
            // (blocks until end of stream or a flush)
            try {
                iActiveCodec->StreamInitialise();
                for (;;) {
                    iLock.Wait();
                    const TBool seek = iSeek;
                    const TUint seekHandle = iSeekHandle;
                    iLock.Signal();
                    if (!seek) {
                        iActiveCodec->Process();
                    }
                    else {
                        iExpectedSeekFlushId = MsgFlush::kIdInvalid;
                        TUint64 sampleNum = iSeekSeconds * static_cast<TUint64>(iSampleRate);
                        iSeekInProgress = true;
                        try {
                            (void)iActiveCodec->TrySeek(iStreamId, sampleNum);
                        }
                        catch (Exception&) {
                            LOG_ERROR(kPipeline, "Exception from TrySeek\n");
                            iSeekObserver->NotifySeekComplete(seekHandle, MsgFlush::kIdInvalid);
                            throw;
                        }
                        iSeekInProgress = false;
                        iLock.Wait();
                        const TBool notify = (iSeek && iSeekHandle == seekHandle);
                        if (notify) {
                            iSeek = false;
                        }
                        ISeekObserver* seekObserver = iSeekObserver;
                        iLock.Signal();
                        if (notify) {
                            seekObserver->NotifySeekComplete(seekHandle, iExpectedSeekFlushId);
                            if (iPostSeekFlush != nullptr) {
                                Queue(iPostSeekFlush);
                                iPostSeekFlush = nullptr;
                            }
                        }
                    }
                }
            }
            catch (CodecStreamStart&) {}
            catch (CodecStreamEnded&) {
                iStreamEnded = true;
            }
            catch (CodecStreamCorrupt&) {
                if (!iStreamStopped) {
                    LOG_ERROR(kPipeline, "WARNING: CodecStreamCorrupt\n");
                }
            }
            catch (CodecStreamFeatureUnsupported&) {
                LOG_ERROR(kPipeline, "WARNING: CodecStreamFeatureUnsupported\n");
            }
        }
        catch (CodecStreamStopped&) {}
        catch (CodecStreamFlush&) {}
        catch (AssertionFailed&) {
            throw;
        }
        catch (Exception& ex) {
            LOG_ERROR(kPipeline, "WARNING: codec threw %s\n", ex.Message());
        }
        if (iActiveCodec != nullptr) {
            iActiveCodec->StreamCompleted();
        }
        if (!iStreamStarted && !iStreamEnded) {
            iLock.Wait();
            if (iExpectedFlushId == MsgFlush::kIdInvalid) {
                auto streamHandler = iStreamHandler.load();
                iExpectedFlushId = streamHandler->TryStop(iStreamId);
                if (iExpectedFlushId != MsgFlush::kIdInvalid) {
                    iConsumeExpectedFlush = true;
                }
            }
            iLock.Signal();
        }
    }
    if (iPendingMsg != nullptr) {
        Queue(iPendingMsg);
        iPendingMsg = nullptr;
    }
    if (iPendingQuit != nullptr) {
        Queue(iPendingQuit);
        iPendingQuit = nullptr;
    }
}

void CodecController::Rewind()
{
    iRewinder.Rewind();
    ReleaseAudioEncoded();
    iStreamPos = 0;
}

Msg* CodecController::PullMsg()
{
    {
        AutoMutex _(iLock);
        if (iRecognising && iExpectedFlushId != MsgFlush::kIdInvalid) {
            /* waiting for a Flush causes QueueTrackData() to discard all msgs.
               If we're trying to recognise a new stream, Rewinder is active and will buffer all
               the msgs we're busily discarding.  We'll probably run out of memory at this point.
               Even if we don't, we don't want to be able to replay msgs that are certain to be
               discarded.  Throwing here allows us to break out of the recognise loop and safely
               allow QueueTrackData() to discard as much data as it wants. */
            THROW(CodecStreamFlush);
        }
    }
    Msg* msg = iUpstream->Pull();
    if (msg == nullptr) {
        ASSERT(iRecognising);
        THROW(CodecRecognitionOutOfData);
    }
    msg = msg->Process(*this);
    return msg;
}

void CodecController::Queue(Msg* aMsg)
{
    iDownstreamElement.Push(aMsg);
    if (iQuit) {
        iShutdownSem.Signal();
    }
}

TBool CodecController::QueueTrackData() const
{
    return (iQueueTrackData && iExpectedFlushId == MsgFlush::kIdInvalid);
}

void CodecController::ReleaseAudioEncoded()
{
    if (iAudioEncoded != nullptr) {
        iAudioEncoded->RemoveRef();
        iAudioEncoded = nullptr;
    }
}

void CodecController::ReleaseAudioDecoded()
{
    if (iAudioDecoded != nullptr) {
        iAudioDecoded->RemoveRef();
        iAudioDecoded = nullptr;
    }
}

void CodecController::Read(Bwx& aBuf, TUint aBytes)
{
    if (iPendingMsg != nullptr) {
        if (DoRead(aBuf, aBytes)) {
            return;
        }
        THROW(CodecStreamEnded);
    }
    if (iStreamEnded || iStreamStopped) {
        if (DoRead(aBuf, aBytes)) {
            return;
        }
        if (iStreamStopped) {
            THROW(CodecStreamStopped);
        }
        if (iStreamStarted) {
            THROW(CodecStreamStart);
        }
        THROW(CodecStreamEnded);
    }
    while (!iStreamEnded && (iAudioEncoded == nullptr || iAudioEncoded->Bytes() < aBytes)) {
        Msg* msg = PullMsg();
        if (msg != nullptr) {
            ASSERT(iPendingMsg == nullptr);
            iPendingMsg = msg;
            break;
        }
    }
    if (!DoRead(aBuf, aBytes)) {
        if (iStreamStarted) {
            THROW(CodecStreamStart);
        }
        THROW(CodecStreamEnded);
     }
}

TBool CodecController::DoRead(Bwx& aBuf, TUint aBytes)
{
    if (aBytes == 0) {
        return true;
    }
    if (iAudioEncoded == nullptr) {
        return false;
    }
    MsgAudioEncoded* remaining = nullptr;
    const TUint bufSpace = aBuf.MaxBytes() - aBuf.Bytes();
    const TUint toRead = std::min(bufSpace, aBytes);
    if (toRead < iAudioEncoded->Bytes()) {
        remaining = iAudioEncoded->Split(toRead);
    }
    const TUint bytes = iAudioEncoded->Bytes();
    ASSERT(aBuf.Bytes() + bytes <= aBuf.MaxBytes());
    TByte* ptr = const_cast<TByte*>(aBuf.Ptr()) + aBuf.Bytes();
    iAudioEncoded->CopyTo(ptr);
    aBuf.SetBytes(aBuf.Bytes() + bytes);
    iAudioEncoded->RemoveRef();
    iAudioEncoded = remaining;
    iStreamPos += bytes;
    return true;
}

void CodecController::ReadNextMsg(Bwx& aBuf)
{
    while (iAudioEncoded == nullptr) {
        Msg* msg = PullMsg();
        if (msg != nullptr) {
            Queue(msg);
        }
        if (iStreamEnded || iQuit) {
            THROW(CodecStreamEnded);
        }
    }
    DoRead(aBuf, iAudioEncoded->Bytes());
}

MsgAudioEncoded* CodecController::ReadNextMsg()
{
    while (iAudioEncoded == nullptr) {
        Msg* msg = PullMsg();
        if (iQuit && iPendingQuit == nullptr) {
            iPendingQuit = msg;
            msg = nullptr;
        }
        if (msg != nullptr) {
            Queue(msg);
        }
        if (iStreamEnded || iQuit) {
            THROW(CodecStreamEnded);
        }
    }
    auto msg = iAudioEncoded;
    iAudioEncoded = nullptr;
    return msg;
}

TBool CodecController::Read(IWriter& aWriter, TUint64 aOffset, TUint aBytes)
{
    if (!iStreamEnded && !iQuit) {
        return iUrlBlockWriter.TryGet(aWriter, iTrackUri, aOffset, aBytes);
    }
    return false;
}

TBool CodecController::TrySeekTo(TUint aStreamId, TUint64 aBytePos)
{
    {
        AutoMutex a(iLock);
        if (iStreamStopped) {
            // Don't want to seek when in a stopped state.
            THROW(CodecStreamStopped);
        }
    }

    auto streamHandler = iStreamHandler.load();
    if (aStreamId == iStreamId && aBytePos >= iStreamLength) {
        // Seek on valid stream, but aBytePos is beyond end of file.
        LOG(kPipeline, "CodecController::TrySeekTo(%u, %llu) - failure: seek point is beyond the end of stream (streamLen=%llu)\n",
                       aStreamId, aBytePos, iStreamLength);
        LOG(kPipeline, "...skip forwards to next stream\n");
        iStreamEnded = true;
        iExpectedFlushId = streamHandler->TryStop(iStreamId);
        if (iExpectedFlushId != MsgFlush::kIdInvalid) {
            iConsumeExpectedFlush = true;
        }
        return false;
    }
    TUint flushId = streamHandler->TrySeek(aStreamId, aBytePos);
    LOG(kPipeline, "CodecController::TrySeekTo(%u, %llu) returning %u\n", aStreamId, aBytePos, flushId);
    if (flushId != MsgFlush::kIdInvalid) {
        ReleaseAudioEncoded();
        ReleaseAudioDecoded();
        iExpectedFlushId = flushId;
        iConsumeExpectedFlush = false;
        iExpectedSeekFlushId = flushId;
        iStreamPos = aBytePos;
        return true;
    }
    return false;
}

TUint64 CodecController::StreamLength() const
{
    return iStreamLength;
}

TUint64 CodecController::StreamPos() const
{
    return iStreamPos;
}

void CodecController::OutputDecodedStream(TUint aBitRate, TUint aBitDepth, TUint aSampleRate,
                                          TUint aNumChannels, const Brx& aCodecName,
                                          TUint64 aTrackLength, TUint64 aSampleStart,
                                          TBool aLossless, SpeakerProfile aProfile, TBool aAnalogBypass)
{
    if (!Jiffies::IsValidSampleRate(aSampleRate)) {
        THROW(CodecStreamFeatureUnsupported);
    }
    if (iStreamFormat != MsgEncodedStream::Format::Pcm && aNumChannels > 2) {
        Log::Print("ERROR: encoded stream with %u channels cannot be played\n", aNumChannels);
        THROW(CodecStreamFeatureUnsupported);
    }
    MsgDecodedStream* msg =
        iMsgFactory.CreateMsgDecodedStream(iStreamId, aBitRate, aBitDepth, aSampleRate, aNumChannels,
                                           aCodecName, aTrackLength, aSampleStart,
                                           aLossless, iSeekable, iLive, aAnalogBypass,
                                           AudioFormat::Pcm, iMultiroom, aProfile, this);
    DoOutputDecodedStream(msg);
}

void CodecController::OutputDecodedStreamDsd(TUint aSampleRate, TUint aNumChannels, const Brx& aCodecName, TUint64 aTrackLength, TUint64 aSampleStart, SpeakerProfile aProfile)
{
    if (!Jiffies::IsValidSampleRate(aSampleRate)) {
        THROW(CodecStreamFeatureUnsupported);
    }
    if (aNumChannels > 2) {
        Log::Print("ERROR: DSD stream with %u channels cannot be played\n", aNumChannels);
        THROW(CodecStreamFeatureUnsupported);
    }
    static const TUint kBitDepth = 1;
    const TUint bitRate = aSampleRate * aNumChannels;
    auto msg =
        iMsgFactory.CreateMsgDecodedStream(iStreamId, bitRate, kBitDepth, aSampleRate, aNumChannels,
                                           aCodecName, aTrackLength, aSampleStart,
                                           true, iSeekable, iLive, false,
                                           AudioFormat::Dsd, Multiroom::Forbidden, aProfile, this);
    DoOutputDecodedStream(msg);
}

void CodecController::DoOutputDecodedStream(MsgDecodedStream* aMsg)
{
    TBool queue = true;
    auto& stream = aMsg->StreamInfo();
    {
        AutoMutex _(iLock);
        iChannels = stream.NumChannels();
        iSampleRate = stream.SampleRate();
        iBitDepth = stream.BitDepth();
        iBytesPerSample = iChannels * iBitDepth / 8;

        // Handle when the new MsgDecodedStream should be output.
        if (iPostSeekStreamInfo != nullptr) {
            // There is either a seek in progress and a MsgDecodedStream is already
            // pending, or a seek has just finished and a new MsgDecodedStream has
            // come in before the pending one has been output.
            //
            // Clear the pending MsgDecodedStream before deciding what action to
            // take for the new MsgDecodedStream.
            iPostSeekStreamInfo->RemoveRef();
            iPostSeekStreamInfo = nullptr;
        }
        if (iSeekInProgress) {
            // This message should not be directly queued. Cache it in
            // iPostSeekStreamInfo.
            iPostSeekStreamInfo = aMsg;
            queue = false;
        }

        iMaxOutputSamples = Jiffies::ToSamples(iMaxOutputJiffies, iSampleRate);
        iMaxOutputBytes = (iMaxOutputSamples * iBitDepth  * iChannels) / 8;
    }
    if (queue) {
        Queue(aMsg);
    }
}

void CodecController::OutputDelay(TUint aJiffies)
{
    MsgDelay* msg = iMsgFactory.CreateMsgDelay(aJiffies);
    Queue(msg);
}

TUint64 CodecController::OutputAudioPcm(const Brx& aData, TUint aChannels, TUint aSampleRate, TUint aBitDepth, AudioDataEndian aEndian, TUint64 aTrackOffset)
{
    ASSERT(aChannels == iChannels);
    ASSERT(aSampleRate == iSampleRate);
    ASSERT(aBitDepth == iBitDepth);

    if (aData.Bytes() == 0) {
        // allow for codecs which had a tiny bit of data which was later rounded down to 0 samples
        return 0;
    }

    Brn data(aData);
    const TUint64 offsetBefore = aTrackOffset;
    const TByte* p = data.Ptr();
    TUint remaining = data.Bytes();
    do {
        const TUint bytes = std::min(iMaxOutputBytes, data.Bytes());
        Brn buf(p, bytes);
        MsgAudioPcm* audio = iMsgFactory.CreateMsgAudioPcm(buf, aChannels, aSampleRate, aBitDepth, aEndian, aTrackOffset);
        const TUint64 jiffies = DoOutputAudio(audio);
        aTrackOffset += jiffies;
        p += bytes;
        remaining -= bytes;
        data.Set(p, remaining);
    } while (remaining > 0);

    return aTrackOffset - offsetBefore;
}

TUint64 CodecController::OutputAudioPcm(MsgAudioEncoded* aMsg, TUint aChannels, TUint aSampleRate, TUint aBitDepth, TUint64 aTrackOffset)
{
    ASSERT(aChannels == iChannels);
    ASSERT(aSampleRate == iSampleRate);
    ASSERT(aBitDepth == iBitDepth);
    MsgAudioPcm* audio = iMsgFactory.CreateMsgAudioPcm(aMsg, aChannels, aSampleRate, aBitDepth, aTrackOffset);
    aMsg->RemoveRef();
    return DoOutputAudio(audio);
}

TUint64 CodecController::DoOutputAudio(MsgAudio* aAudioMsg)
{
    if (iExpectedFlushId != MsgFlush::kIdInvalid) {
        // Codec outputting audio while flush is pending
        // This audio may be cached by third party code so it's easier to ignore it here rather than tracking down all causes of it
        aAudioMsg->RemoveRef();
        return 0;
    }
    if (iSeek && iSeekInProgress) {
        iSeekObserver->NotifySeekComplete(iSeekHandle, iExpectedSeekFlushId);
        iSeek = false;
    }
    if (iPostSeekFlush != nullptr) {
        Queue(iPostSeekFlush);
        iPostSeekFlush = nullptr;
    }
    if (iPostSeekStreamInfo != nullptr) {
        Queue(iPostSeekStreamInfo);
        iPostSeekStreamInfo = nullptr;
    }
    const TUint jiffies= aAudioMsg->Jiffies();
    Queue(aAudioMsg);
    return jiffies;
}

TUint64 CodecController::OutputAudioDsd(const Brx& aData, TUint aChannels, TUint aSampleRate,
                                        TUint aSampleBlockWords, TUint64 aTrackOffset, TUint aPadBytesPerChunk)
{
    ASSERT(aChannels == iChannels);
    ASSERT(aSampleRate == iSampleRate);
    ASSERT((aData.Bytes() % aSampleBlockWords) == 0);

    if (aData.Bytes() == 0) {
        // allow for codecs which had a tiny bit of data which was later rounded down to 0 samples
        return 0;
    }

    Brn data(aData);
    const TUint64 offsetBefore = aTrackOffset;
    const TByte* p = data.Ptr();
    TUint remaining = data.Bytes();
    do {
        // We don't songcast DSD so have no need to split audio into 5ms chunks
        // Instead, pack as much audio as we can into as few msgs as possible
        const TUint bytes = std::min(AudioData::kMaxBytes, data.Bytes());
        // ASSERT((bytes % aSampleBlockWords) == 0);
        Brn buf(p, bytes);
        auto audio = iMsgFactory.CreateMsgAudioDsd(buf, aChannels, aSampleRate,
                                                   aSampleBlockWords, aTrackOffset, aPadBytesPerChunk);
        const TUint64 jiffies = DoOutputAudio(audio);
        aTrackOffset += jiffies;
        p += bytes;
        remaining -= bytes;
        data.Set(p, remaining);
    } while (remaining > 0);

    return aTrackOffset - offsetBefore;
}

TUint64 CodecController::OutputAudioDsd(MsgAudioEncoded* aMsg, TUint aChannels, TUint aSampleRate, TUint aSampleBlockWords, TUint64 aTrackOffset, TUint aPadBytesPerChunk)
{
    ASSERT(aChannels == iChannels);
    ASSERT(aSampleRate == iSampleRate);
    auto audio = iMsgFactory.CreateMsgAudioDsd(aMsg, aChannels, aSampleRate, aSampleBlockWords, aTrackOffset, aPadBytesPerChunk);
    aMsg->RemoveRef();
    return DoOutputAudio(audio);
}

void CodecController::OutputBitRate(TUint aBitRate)
{
    auto msg = iMsgFactory.CreateMsgBitRate(aBitRate);
    Queue(msg);
}

void CodecController::OutputWait()
{
    MsgWait* wait = iMsgFactory.CreateMsgWait();
    Queue(wait);
}

void CodecController::OutputHalt()
{
    MsgHalt* halt = iMsgFactory.CreateMsgHalt();
    Queue(halt);
}

void CodecController::OutputMetaText(const Brx& aMetaText)
{
    MsgMetaText* text = iMsgFactory.CreateMsgMetaText(aMetaText);
    Queue(text);
}

void CodecController::OutputStreamInterrupted()
{
    MsgStreamInterrupted* interrupted = iMsgFactory.CreateMsgStreamInterrupted();
    Queue(interrupted);
}

void CodecController::GetAudioBuf(TByte*& aDest, TUint& aSamples)
{
    if (iAudioDecoded == nullptr) {
        iAudioDecoded = iMsgFactory.CreateDecodedAudio();
        iAudioDecodedBytes = 0;
    }
    aDest = iAudioDecoded->PtrW() + iAudioDecodedBytes;
    const auto samplesMsg = (AudioData::kMaxBytes - iAudioDecodedBytes) / iBytesPerSample;
    aSamples = std::min(iMaxOutputSamples, samplesMsg);
}

void CodecController::OutputAudioBuf(TUint aSamples, TUint64& aTrackOffset)
{
    iAudioDecodedBytes += (aSamples * iBytesPerSample);
    iAudioDecoded->SetBytes(iAudioDecodedBytes);
    auto audioPcm = iMsgFactory.CreateMsgAudioPcm(iAudioDecoded, iChannels, iSampleRate, iBitDepth, aTrackOffset);
    iAudioDecoded = nullptr; // ownership of reference passed to audioPcm
    iAudioDecodedBytes = 0;
    const TUint64 jiffies = DoOutputAudio(audioPcm);
    aTrackOffset += jiffies;
}

Msg* CodecController::ProcessMsg(MsgMode* aMsg)
{
    // There should be no pending flushes by the time a MsgMode is received.
    // Accommodate any buggy protocol modules that failed to send their MsgFlush by sending a MsgFlush with the expected flush ID from here.
    if (iExpectedFlushId != MsgFlush::kIdInvalid) {
        LOG_WARNING(kMedia, "CodecController::ProcessMsg(MsgMode*) expected flush ID (%u) has not been received\n", iExpectedFlushId);
        auto* flush = iMsgFactory.CreateMsgFlush(iExpectedFlushId);
        iExpectedFlushId = MsgFlush::kIdInvalid;
        iConsumeExpectedFlush = false;
        Queue(flush);
    }
    if (iRecognising) {
        iStreamEnded = true;
        aMsg->RemoveRef();
        return nullptr;
    }
    return aMsg;
}

Msg* CodecController::ProcessMsg(MsgTrack* aMsg)
{
    if (iRecognising) {
        if (aMsg->StartOfStream()) {
            iStreamEnded = true;
        }
        aMsg->RemoveRef();
        return nullptr;
    }

    iTrackId = aMsg->Track().Id();
    return aMsg;
}

Msg* CodecController::ProcessMsg(MsgDrain* aMsg)
{
    if (iRecognising) {
        iStreamEnded = true;
        aMsg->RemoveRef();
        return nullptr;
    }
    Queue(aMsg);
    return nullptr;
}

Msg* CodecController::ProcessMsg(MsgDelay* aMsg)
{
    if (iRecognising) {
        aMsg->RemoveRef();
        return nullptr;
    }
    Queue(aMsg);
    return nullptr;
}

Msg* CodecController::ProcessMsg(MsgEncodedStream* aMsg)
{
    iStreamEnded = true;
    iTrackUri.Replace(aMsg->Uri());
    if (iRecognising) {
        aMsg->RemoveRef();
        return nullptr;
    }

    // If there was a MsgDecodedStream pending following a flush, but no audio followed, release it here as that stream is now invalid.
    if (iPostSeekStreamInfo != nullptr) {
        iPostSeekStreamInfo->RemoveRef();
        iPostSeekStreamInfo = nullptr;
    }

    iStreamStarted = true;
    iStreamId = aMsg->StreamId();
    iSeek = false; // clear any pending seek - it'd have been against a previous track now
    iStreamStopped = false; // likewise, if iStreamStopped was set, this was for the previous stream
    iStreamLength = aMsg->TotalBytes();
    iSeekable = aMsg->Seekable();
    iLive = aMsg->Live();
    iStreamHandler.store(aMsg->StreamHandler());
    auto msg = iMsgFactory.CreateMsgEncodedStream(aMsg, this);
    iStreamFormat = aMsg->StreamFormat();
    iMultiroom = aMsg->Multiroom();
    iPcmStream.Clear();
    iDsdStream.Clear();
    if (iStreamFormat == MsgEncodedStream::Format::Pcm) {
        iPcmStream = aMsg->PcmStream();
    }
    else if (iStreamFormat == MsgEncodedStream::Format::Dsd) {
        iDsdStream = aMsg->DsdStream();
    }
    aMsg->RemoveRef();
    return msg;
}

Msg* CodecController::ProcessMsg(MsgAudioEncoded* aMsg)
{
    if (!QueueTrackData()) {
        aMsg->RemoveRef();
    }
    else if (iAudioEncoded == nullptr) {
        iAudioEncoded = aMsg;
    }
    else {
        iAudioEncoded->Add(aMsg);
    }
    return nullptr;
}

Msg* CodecController::ProcessMsg(MsgMetaText* aMsg)
{
    if (iRecognising) {
        aMsg->RemoveRef();
        return nullptr;
    }
    Queue(aMsg);
    return nullptr;
}

Msg* CodecController::ProcessMsg(MsgStreamInterrupted* aMsg)
{
    iStreamEnded = true;
    Queue(aMsg);
    return nullptr;
}

Msg* CodecController::ProcessMsg(MsgHalt* aMsg)
{
    return aMsg;
}

Msg* CodecController::ProcessMsg(MsgFlush* aMsg)
{
    ReleaseAudioEncoded();
    TBool queue = false;
    {
        AutoMutex _(iLock);
        ASSERT(iExpectedFlushId == MsgFlush::kIdInvalid || iExpectedFlushId >= aMsg->Id());
        if (iRecognising) {
            iStreamEnded = true;
            aMsg->RemoveRef();
            return nullptr;
        }
        if (iExpectedFlushId == MsgFlush::kIdInvalid || iExpectedFlushId != aMsg->Id()) {
            // Return aMsg so that it becomes a pending msg, allowing a codec to flush out any audio that it has buffered before the MsgFlush is pushed down the pipeline.
            return aMsg;
        }
        else {
            iExpectedFlushId = MsgFlush::kIdInvalid;
            if (iConsumeExpectedFlush) {
                iConsumeExpectedFlush = false;
                aMsg->RemoveRef();
            }
            else if (aMsg->Id() == iExpectedSeekFlushId && iSeekInProgress) {
                if (iPostSeekFlush != nullptr) {
                    iPostSeekFlush->RemoveRef();
                }
                iPostSeekFlush = aMsg;
            }
            else {
                queue = true;
            }
        }
    }
    if (queue) {
        Queue(aMsg);
    }
    return nullptr;
}

Msg* CodecController::ProcessMsg(MsgWait* aMsg)
{
    return aMsg;
}

Msg* CodecController::ProcessMsg(MsgDecodedStream* /*aMsg*/)
{
    ASSERTS(); // expect this to be generated by a codec
    // FIXME - volkano has containers which also generate this
    return nullptr;
}

Msg* CodecController::ProcessMsg(MsgBitRate* /*aMsg*/)
{
    ASSERTS(); // expect this to be generated by a codec
    return nullptr;
}

Msg* CodecController::ProcessMsg(MsgAudioPcm* /*aMsg*/)
{
    ASSERTS(); // not expected at this stage of the pipeline
    return nullptr;
}

Msg* CodecController::ProcessMsg(MsgAudioDsd* /*aMsg*/)
{
    ASSERTS(); // not expected at this stage of the pipeline
    return nullptr;
}

Msg* CodecController::ProcessMsg(MsgSilence* /*aMsg*/)
{
    ASSERTS(); // not expected at this stage of the pipeline
    return nullptr;
}

Msg* CodecController::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    ASSERTS(); // not expected at this stage of the pipeline
    return nullptr;
}

Msg* CodecController::ProcessMsg(MsgQuit* aMsg)
{
    iQuit = true;
    //iStreamEnded = true;  // will cause codec to quit prematurely
    return aMsg;
}

EStreamPlay CodecController::OkToPlay(TUint aStreamId)
{
    auto streamHandler = iStreamHandler.load();
    EStreamPlay canPlay = streamHandler->OkToPlay(aStreamId);
    //Log::Print("CodecController::OkToPlay(%u) returned %s\n", aStreamId, kStreamPlayNames[canPlay]);
    return canPlay;
}

TUint CodecController::TrySeek(TUint /*aStreamId*/, TUint64 /*aOffset*/)
{
    ASSERTS(); // expect Seek requests to come to this class' public API, not from downstream
    return MsgFlush::kIdInvalid;
}

TUint CodecController::TryDiscard(TUint /*aJiffies*/)
{
    ASSERTS();
    return MsgFlush::kIdInvalid;
}

TUint CodecController::TryStop(TUint aStreamId)
{
    AutoMutex a(iLock);
    if (iStreamId == aStreamId) {
        iStreamStopped = true;
    }
    auto streamHandler = iStreamHandler.load();
    if (streamHandler == nullptr) {
        LOG(kMedia, "CodecController::TryStop returning MsgFlush::kIdInvalid (no stream handler)\n");
        return MsgFlush::kIdInvalid;
    }
    const TUint flushId = streamHandler->TryStop(aStreamId);
    if (flushId != MsgFlush::kIdInvalid) {
        iExpectedFlushId = flushId;
        iConsumeExpectedFlush = false;
    }
    LOG(kMedia, "CodecController::TryStop(%u) returning %u.  iStreamId=%u, iStreamStopped=%u\n",
                aStreamId, flushId, iStreamId, iStreamStopped);

    return flushId;
}

void CodecController::NotifyStarving(const Brx& aMode, TUint aStreamId, TBool aStarving)
{
    auto streamHandler = iStreamHandler.load();
    if (streamHandler != nullptr) {
        streamHandler->NotifyStarving(aMode, aStreamId, aStarving);
    }
}


// CodecBufferedReader

CodecBufferedReader::CodecBufferedReader(ICodecController& aCodecController, Bwx& aBuf)
    : iCodecController(aCodecController)
    , iBuf(aBuf)
    , iState(eReading)
{
}

Brn CodecBufferedReader::Read(TUint aBytes)
{
    if (iState == eEos) {
        iState = eBeyondEos;
        return Brx::Empty();
    }
    else if (iState == eBeyondEos) {
        THROW(ReaderError); // Reading beyond EoS is an error.
    }
    else if (iState == eReading) {
        iBuf.SetBytes(0);
        // Valid to return up to aBytes, so if aBytes > iBuf.Bytes(), only return iBuf.Bytes().
        TUint bytes = aBytes;
        if (bytes > iBuf.MaxBytes()) {
            bytes = iBuf.MaxBytes();
        }

        iCodecController.Read(iBuf, bytes);
        if (iBuf.Bytes() < bytes) {
            // Reached end of stream.
            iState = eEos;
        }
        return Brn(iBuf.Ptr(), iBuf.Bytes());
    }

    ASSERTS();              // Uknown state.
    return Brx::Empty();    // Unreachable code.
}

void CodecBufferedReader::ReadFlush()
{
    iBuf.SetBytes(0);
}

void CodecBufferedReader::ReadInterrupt()
{
    ASSERTS();
}

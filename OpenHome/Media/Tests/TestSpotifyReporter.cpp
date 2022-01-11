#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/SuiteUnitTest.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Pipeline/Reporter.h>
#include <OpenHome/Media/Pipeline/SpotifyReporter.h>
#include <OpenHome/Media/Utils/AllocatorInfoLogger.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Tests/TestPipe.h>

#include <limits>
#include <string.h>
#include <vector>

namespace OpenHome {
namespace Media {
namespace Test {

template <class T>
class MsgIdentifier : public PipelineElement
{
public:
    MsgIdentifier();
    T* GetMsg(Msg* aMsg);   // ASSERTS if not msg of type T passed in.
private: // from PipelineElement
    Msg* ProcessMsg(T* aMsg) override;
private:
    T* iMsg;
};

class MockSpotifyMetadataAllocated;

class MockSpotifyMetadataAllocator
{
public:
    MockSpotifyMetadataAllocator();
    ~MockSpotifyMetadataAllocator();
    MockSpotifyMetadataAllocated* Allocate(const Brx& aTrack, const Brx& aArtist, const Brx& aAlbum, const Brx& aAlbumArtUrl, TUint aDurationMs, TUint aBitrate);
    void Destroy(MockSpotifyMetadataAllocated* aMetadata);
    TUint DeallocatedCount() const;
private:
    TUint iAllocCount;
    TUint iDeallocCount;
};

class MockSpotifyMetadata : public ISpotifyMetadata, private INonCopyable
{
public:
    MockSpotifyMetadata(const Brx& aTrack, const Brx& aArtist, const Brx& aAlbum, const Brx& aAlbumArtUrl, TUint aDurationMs, TUint aBitrate);
public: // from ISpotifyMetadata
    const Brx& PlaybackSource() const override;
    const Brx& PlaybackSourceUri() const override;
    const Brx& Track() const override;
    const Brx& TrackUri() const override;
    const Brx& Artist() const override;
    const Brx& ArtistUri() const override;
    const Brx& Album() const override;
    const Brx& AlbumUri() const override;
    const Brx& AlbumCoverUri() const override;
    const Brx& AlbumCoverUrl() const override;
    TUint DurationMs() const override;
    TUint Bitrate() const override;
private:
    const Brh iTrack;
    const Brh iArtist;
    const Brh iAlbum;
    const Brh iAlbumArtUrl;
    const TUint iDurationMs;
    const TUint iBitrate;
};

class MockSpotifyMetadataAllocated : public ISpotifyMetadataAllocated
{
    friend class MockSpotifyMetadataAllocator;
public:
    MockSpotifyMetadataAllocated(MockSpotifyMetadataAllocator& aAllocator, const Brx& aTrack, const Brx& aArtist, const Brx& aAlbum, const Brx& aAlbumArtUrl, TUint aDurationMs, TUint aBitrate);
public: // from ISpotifyMetadataAllocated
    const ISpotifyMetadata& Metadata() const override;
    void AddReference() override;
    void RemoveReference() override;
private:
    MockSpotifyMetadataAllocator& iAllocator;
    MockSpotifyMetadata iMetadata;
    TUint iRefCount;
};

class MockPipelineElementUpstream : public IPipelineElementUpstream
{
public:
    MockPipelineElementUpstream(TUint aMaxMsgs);
    ~MockPipelineElementUpstream();
    void Enqueue(Msg* aMsg);
private: // from IPipelineElementUpstream
    Msg* Pull() override;
private:
    FifoLiteDynamic<Msg*> iMsgs;
    Mutex iLock;
};

class WriterBool : private INonCopyable
{
public:
    WriterBool(IWriter& aWriter);
    void WriteBool(TBool aBool);
private:
    IWriter& iWriter;
};

class MockMsgProcessor : public IMsgProcessor, private INonCopyable
{
private:
    static const TUint kMaxMsgBytes = OpenHome::Test::TestPipeDynamic::kMaxMsgBytes;
public:
    MockMsgProcessor(OpenHome::Test::ITestPipeWritable& aTestPipe);
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
    Msg* ProcessMsg(MsgBitRate* aMsg) override;
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgAudioDsd* aMsg) override;
    Msg* ProcessMsg(MsgSilence* aMsg) override;
    Msg* ProcessMsg(MsgPlayable* aMsg) override;
    Msg* ProcessMsg(MsgQuit* aMsg) override;
private:
    OpenHome::Test::ITestPipeWritable& iTestPipe;
};

class SuiteSpotifyReporter : public TestFramework::SuiteUnitTest
{
    static const Brn kTrackTitle;
    static const Brn kTrackArtist;
    static const Brn kTrackAlbum;
    static const Brn kTrackAlbumArt;
    static const TUint kBitDepth      = 16;
    static const TUint kByteDepth = kBitDepth/8;
    static const TUint kDefaultSampleRate = 44100;
    static const TUint kDefaultNumChannels = 2;
    static const SpeakerProfile kDefaultProfile;
    static const TUint kDefaultBitrate = kBitDepth * kDefaultSampleRate;
    static const TUint kDefaultTrackLength = Jiffies::kPerSecond * 10;
    static const TUint kDefaultSampleStart = 0;
    static const TUint64 kTrackLength = Jiffies::kPerSecond * 60;
    static const TBool kLossless      = true;
    static const TUint kDataBytes = 3 * 1024;   // bytes per MsgAudioPcm
public:
    SuiteSpotifyReporter();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    MsgAudio* CreateAudio(TUint aNumChannels, TUint aSampleRate, TUint64& aTrackOffset);
    void TestMsgsCauseAssertion();
    void TestMsgsPassedThroughNoSamplesInPipeline();
    void TestMsgsPassedThroughSamplesInPipeline();
    void TestMsgModeResets();
    void TestSubSamples();
    void TestSampleRateChange();
    void TestNumChannelsChange();
    void TestInvalidSampleRate();
    void TestInvalidNumChannels();
    void TestPassThroughInjectTrack();
    void TestModeSpotifyTrackInjected();
    void TestModeSpotifySeek();
    void TestModeSpotifySyncLost();
    void TestModeSpotifyMetadataChanged();
private:
    OpenHome::Test::TestPipeDynamic* iTestPipe;
    MockPipelineElementUpstream* iUpstream;
    MockMsgProcessor* iMsgProcessor;
    MsgFactory* iMsgFactory;
    TrackFactory* iTrackFactory;
    AllocatorInfoLogger iInfoAggregator;
    MockSpotifyMetadataAllocator* iMetadataAllocator;
    SpotifyReporter* iReporter;
};

} // namespace Test
} // namespace Media
} // namespace OpenHome


using namespace OpenHome;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Test;


// MsgIdentifier

template <class T>
MsgIdentifier<T>::MsgIdentifier()
    : PipelineElement(0)
    , iMsg(nullptr)
{
}

template <class T>
T* MsgIdentifier<T>::GetMsg(Msg* aMsg)
{
    Msg* msg = aMsg->Process(*this);
    ASSERT(msg == nullptr);
    ASSERT(iMsg != nullptr);
    T* msgOut = iMsg;
    iMsg = nullptr;
    return msgOut;
}

template <class T>
Msg* MsgIdentifier<T>::ProcessMsg(T* aMsg)
{
    iMsg = aMsg;
    return nullptr;
}


// MockSpotifyMetadataAllocator

MockSpotifyMetadataAllocator::MockSpotifyMetadataAllocator()
    : iAllocCount(0)
    , iDeallocCount(0)
{
}

MockSpotifyMetadataAllocator::~MockSpotifyMetadataAllocator()
{
    // Ensure all metadata has been deallocated.
    ASSERT(iAllocCount == iDeallocCount);
}

MockSpotifyMetadataAllocated* MockSpotifyMetadataAllocator::Allocate(const Brx& aTrack, const Brx& aArtist, const Brx& aAlbum, const Brx& aAlbumArtUrl, TUint aDurationMs, TUint aBitrate)
{
    MockSpotifyMetadataAllocated* metadata = new MockSpotifyMetadataAllocated(*this, aTrack, aArtist, aAlbum, aAlbumArtUrl, aDurationMs, aBitrate);
    iAllocCount++;
    return metadata;
}

void MockSpotifyMetadataAllocator::Destroy(MockSpotifyMetadataAllocated* aMetadata)
{
    ASSERT(aMetadata != nullptr);
    iDeallocCount++;
    delete aMetadata;   // FIXME - calling this while in MockSpotifyMetadataAllocated::RemoveRef() method.
}

TUint MockSpotifyMetadataAllocator::DeallocatedCount() const
{
    return iDeallocCount;
}


// MockSpotifyMetadata

MockSpotifyMetadata::MockSpotifyMetadata(const Brx& aTrack, const Brx& aArtist, const Brx& aAlbum, const Brx& aAlbumArtUrl, TUint aDurationMs, TUint aBitrate)
    : iTrack(aTrack)
    , iArtist(aArtist)
    , iAlbum(aAlbum)
    , iAlbumArtUrl(aAlbumArtUrl)
    , iDurationMs(aDurationMs)
    , iBitrate(aBitrate)
{
}

const Brx& MockSpotifyMetadata::PlaybackSource() const
{
    ASSERTS();
    return Brx::Empty();
}

const Brx& MockSpotifyMetadata::PlaybackSourceUri() const
{
    ASSERTS();
    return Brx::Empty();
}

const Brx& MockSpotifyMetadata::Track() const
{
    return iTrack;
}

const Brx& MockSpotifyMetadata::TrackUri() const
{
    ASSERTS();
    return Brx::Empty();
}

const Brx& MockSpotifyMetadata::Artist() const
{
    return iArtist;
}

const Brx& MockSpotifyMetadata::ArtistUri() const
{
    ASSERTS();
    return Brx::Empty();
}

const Brx& MockSpotifyMetadata::Album() const
{
    return iAlbum;
}

const Brx& MockSpotifyMetadata::AlbumUri() const
{
    ASSERTS();
    return Brx::Empty();
}

const Brx& MockSpotifyMetadata::AlbumCoverUri() const
{
    ASSERTS();
    return Brx::Empty();
}

const Brx& MockSpotifyMetadata::AlbumCoverUrl() const
{
    return iAlbumArtUrl;
}

TUint MockSpotifyMetadata::DurationMs() const
{
    return iDurationMs;
}

TUint MockSpotifyMetadata::Bitrate() const
{
    return iBitrate;
}


// MockSpotifyMetadataAllocated

MockSpotifyMetadataAllocated::MockSpotifyMetadataAllocated(MockSpotifyMetadataAllocator& aAllocator, const Brx& aTrack, const Brx& aArtist, const Brx& aAlbum, const Brx& aAlbumArtUrl, TUint aDurationMs, TUint aBitrate)
    : iAllocator(aAllocator)
    , iMetadata(aTrack, aArtist, aAlbum, aAlbumArtUrl, aDurationMs, aBitrate)
    , iRefCount(1)
{
}

const ISpotifyMetadata& MockSpotifyMetadataAllocated::Metadata() const
{
    return iMetadata;
}

void MockSpotifyMetadataAllocated::AddReference()
{
    iRefCount++;
}

void MockSpotifyMetadataAllocated::RemoveReference()
{
    ASSERT(iRefCount > 0);
    iRefCount--;
    if (iRefCount == 0) {
        iAllocator.Destroy(this);
    }
}


// MockPipelineElementUpstream

MockPipelineElementUpstream::MockPipelineElementUpstream(TUint aMaxMsgs)
    : iMsgs(aMaxMsgs)
    , iLock("MPEU")
{
}

MockPipelineElementUpstream::~MockPipelineElementUpstream()
{
    AutoMutex _(iLock);
    ASSERT(iMsgs.SlotsUsed() == 0);
}

void MockPipelineElementUpstream::Enqueue(Msg* aMsg)
{
    AutoMutex _(iLock);
    ASSERT(iMsgs.SlotsFree() > 0);
    iMsgs.Write(aMsg);
}

Msg* MockPipelineElementUpstream::Pull()
{
    AutoMutex _(iLock);
    ASSERT(iMsgs.SlotsUsed() > 0);
    auto* m = iMsgs.Read();
    return m;
}


// WriterBool

WriterBool::WriterBool(IWriter& aWriter)
    : iWriter(aWriter)
{
}

void WriterBool::WriteBool(TBool aBool)
{
    if (aBool) {
        iWriter.Write('Y');
    }
    else {
        iWriter.Write('N');
    }
}


// MockMsgProcessor

MockMsgProcessor::MockMsgProcessor(OpenHome::Test::ITestPipeWritable& aTestPipe)
    : iTestPipe(aTestPipe)
{
}

Msg* MockMsgProcessor::ProcessMsg(MsgMode* aMsg)
{
    Bws<kMaxMsgBytes> buf("MMP::ProcessMsg MsgMode ");
    buf.Append(aMsg->Mode());
    iTestPipe.Write(buf);
    return aMsg;
}

Msg* MockMsgProcessor::ProcessMsg(MsgTrack* aMsg)
{
    Bws<kMaxMsgBytes> buf("MMP::ProcessMsg MsgTrack ");
    WriterBuffer writerBuffer(buf);
    WriterAscii writerAscii(writerBuffer);
    WriterBool writerBool(writerBuffer);
    writerAscii.Write(aMsg->Track().Uri());
    writerAscii.WriteSpace();
    writerAscii.WriteUint(aMsg->Track().Id());
    writerAscii.WriteSpace();
    writerBool.WriteBool(aMsg->StartOfStream());
    iTestPipe.Write(buf);
    return aMsg;
}

Msg* MockMsgProcessor::ProcessMsg(MsgDrain* aMsg)
{
    Bws<kMaxMsgBytes> buf("MMP::ProcessMsg MsgDrain ");
    WriterBuffer writerBuffer(buf);
    WriterAscii writerAscii(writerBuffer);
    writerAscii.WriteUint(aMsg->Id());
    iTestPipe.Write(buf);
    return aMsg;
}

Msg* MockMsgProcessor::ProcessMsg(MsgDelay* aMsg)
{
    Bws<kMaxMsgBytes> buf("MMP::ProcessMsg MsgDelay ");
    WriterBuffer writerBuffer(buf);
    WriterAscii writerAscii(writerBuffer);
    writerAscii.WriteUint(aMsg->RemainingJiffies());
    iTestPipe.Write(buf);
    return aMsg;
}

Msg* MockMsgProcessor::ProcessMsg(MsgEncodedStream* aMsg)
{
    Bws<kMaxMsgBytes> buf("MMP::ProcessMsg MsgEncodedStream ");
    WriterBuffer writerBuffer(buf);
    WriterAscii writerAscii(writerBuffer);
    WriterBool writerBool(writerBuffer);
    writerAscii.Write(aMsg->Uri());
    writerAscii.WriteSpace();
    writerAscii.WriteUint64(aMsg->TotalBytes());
    writerAscii.WriteSpace();
    writerAscii.WriteUint64(aMsg->StartPos());
    writerAscii.WriteSpace();
    writerAscii.WriteUint(aMsg->StreamId());
    writerAscii.WriteSpace();
    writerBool.WriteBool(aMsg->Seekable());
    writerAscii.WriteSpace();
    writerBool.WriteBool(aMsg->Live());
    writerAscii.WriteSpace();
    writerBool.WriteBool(aMsg->StreamFormat() == MsgEncodedStream::Format::Pcm);
    iTestPipe.Write(buf);
    return aMsg;
}

Msg* MockMsgProcessor::ProcessMsg(MsgStreamSegment* aMsg)
{
    Bws<kMaxMsgBytes> buf("MMP::ProcessMsg MsgStreamSegment ");
    buf.Append(aMsg->Id());
    iTestPipe.Write(buf);
    return aMsg;
}

Msg* MockMsgProcessor::ProcessMsg(MsgAudioEncoded* aMsg)
{
    Bws<kMaxMsgBytes> buf("MMP::ProcessMsg MsgAudioEncoded ");
    WriterBuffer writerBuffer(buf);
    WriterAscii writerAscii(writerBuffer);
    writerAscii.WriteUint(aMsg->Bytes());
    iTestPipe.Write(buf);
    return aMsg;
}

Msg* MockMsgProcessor::ProcessMsg(MsgMetaText* aMsg)
{
    Bws<kMaxMsgBytes> buf("MMP::ProcessMsg MsgMetaText ");
    buf.Append(aMsg->MetaText());
    iTestPipe.Write(buf);
    return aMsg;
}

Msg* MockMsgProcessor::ProcessMsg(MsgStreamInterrupted* aMsg)
{
    Bws<kMaxMsgBytes> buf("MMP::ProcessMsg MsgStreamInterrupted");
    iTestPipe.Write(buf);
    return aMsg;
}

Msg* MockMsgProcessor::ProcessMsg(MsgHalt* aMsg)
{
    Bws<kMaxMsgBytes> buf("MMP::ProcessMsg MsgHalt ");
    WriterBuffer writerBuffer(buf);
    WriterAscii writerAscii(writerBuffer);
    writerAscii.WriteUint(aMsg->Id());
    iTestPipe.Write(buf);
    return aMsg;
}

Msg* MockMsgProcessor::ProcessMsg(MsgFlush* aMsg)
{
    Bws<kMaxMsgBytes> buf("MMP::ProcessMsg MsgFlush ");
    WriterBuffer writerBuffer(buf);
    WriterAscii writerAscii(writerBuffer);
    writerAscii.WriteUint(aMsg->Id());
    iTestPipe.Write(buf);
    return aMsg;
}

Msg* MockMsgProcessor::ProcessMsg(MsgWait* aMsg)
{
    Bws<kMaxMsgBytes> buf("MMP::ProcessMsg MsgWait");
    iTestPipe.Write(buf);
    return aMsg;
}

Msg* MockMsgProcessor::ProcessMsg(MsgDecodedStream* aMsg)
{
    Bws<kMaxMsgBytes> buf("MMP::ProcessMsg MsgDecodedStream ");
    WriterBuffer writerBuffer(buf);
    WriterAscii writerAscii(writerBuffer);
    WriterBool writerBool(writerBuffer);
    writerAscii.WriteUint(aMsg->StreamInfo().StreamId());
    writerAscii.WriteSpace();
    writerAscii.WriteUint(aMsg->StreamInfo().BitRate());
    writerAscii.WriteSpace();
    writerAscii.WriteUint(aMsg->StreamInfo().BitDepth());
    writerAscii.WriteSpace();
    writerAscii.WriteUint(aMsg->StreamInfo().SampleRate());
    writerAscii.WriteSpace();
    writerAscii.WriteUint(aMsg->StreamInfo().NumChannels());
    writerAscii.WriteSpace();
    writerAscii.Write(aMsg->StreamInfo().CodecName());
    writerAscii.WriteSpace();
    writerAscii.WriteUint64(aMsg->StreamInfo().TrackLength());
    writerAscii.WriteSpace();
    writerAscii.WriteUint64(aMsg->StreamInfo().SampleStart());
    writerAscii.WriteSpace();
    writerBool.WriteBool(aMsg->StreamInfo().Lossless());
    writerAscii.WriteSpace();
    writerBool.WriteBool(aMsg->StreamInfo().Seekable());
    writerAscii.WriteSpace();
    writerBool.WriteBool(aMsg->StreamInfo().Live());
    iTestPipe.Write(buf);
    return aMsg;
}

Msg* MockMsgProcessor::ProcessMsg(MsgBitRate* aMsg)
{
    Bws<kMaxMsgBytes> buf("MMP::ProcessMsg MsgBitRate ");
    WriterBuffer writerBuffer(buf);
    WriterAscii writerAscii(writerBuffer);
    writerAscii.WriteUint(aMsg->BitRate());
    iTestPipe.Write(buf);
    return aMsg;
}

Msg* MockMsgProcessor::ProcessMsg(MsgAudioPcm* aMsg)
{
    Bws<kMaxMsgBytes> buf("MMP::ProcessMsg MsgAudioPcm ");
    WriterBuffer writerBuffer(buf);
    WriterAscii writerAscii(writerBuffer);
    writerAscii.WriteUint(aMsg->Jiffies());
    writerAscii.WriteSpace();
    writerAscii.WriteUint64(aMsg->TrackOffset());
    iTestPipe.Write(buf);
    return aMsg;
}

Msg* MockMsgProcessor::ProcessMsg(MsgAudioDsd* aMsg)
{
    Bws<kMaxMsgBytes> buf("MMP::ProcessMsg MsgAudioDsd ");
    WriterBuffer writerBuffer(buf);
    WriterAscii writerAscii(writerBuffer);
    writerAscii.WriteUint(aMsg->Jiffies());
    writerAscii.WriteSpace();
    writerAscii.WriteUint64(aMsg->TrackOffset());
    iTestPipe.Write(buf);
    return aMsg;
}

Msg* MockMsgProcessor::ProcessMsg(MsgSilence* aMsg)
{
    Bws<kMaxMsgBytes> buf("MMP::ProcessMsg MsgSilence ");
    WriterBuffer writerBuffer(buf);
    WriterAscii writerAscii(writerBuffer);
    writerAscii.WriteUint(aMsg->Jiffies());
    iTestPipe.Write(buf);
    return aMsg;
}

Msg* MockMsgProcessor::ProcessMsg(MsgPlayable* aMsg)
{
    Bws<kMaxMsgBytes> buf("MMP::ProcessMsg MsgPlayable ");
    WriterBuffer writerBuffer(buf);
    WriterAscii writerAscii(writerBuffer);
    writerAscii.WriteUint(aMsg->Bytes());
    writerAscii.WriteSpace();
    writerAscii.WriteUint(aMsg->Jiffies());
    iTestPipe.Write(buf);
    return aMsg;
}

Msg* MockMsgProcessor::ProcessMsg(MsgQuit* aMsg)
{
    Bws<kMaxMsgBytes> buf("MMP::ProcessMsg MsgQuit");
    iTestPipe.Write(buf);
    return aMsg;
}


// SuiteSpotifyReporter

const Brn SuiteSpotifyReporter::kTrackTitle("spotify track");
const Brn SuiteSpotifyReporter::kTrackArtist("spotify artist");
const Brn SuiteSpotifyReporter::kTrackAlbum("spotify album");
const Brn SuiteSpotifyReporter::kTrackAlbumArt("http://some/album/art.jpg");

SuiteSpotifyReporter::SuiteSpotifyReporter()
    : SuiteUnitTest("SuiteSpotifyReporter")
{
    AddTest(MakeFunctor(*this, &SuiteSpotifyReporter::TestMsgsCauseAssertion), "TestMsgsCauseAssertion");
    AddTest(MakeFunctor(*this, &SuiteSpotifyReporter::TestMsgsPassedThroughNoSamplesInPipeline), "TestMsgsPassedThroughNoSamplesInPipeline");
    AddTest(MakeFunctor(*this, &SuiteSpotifyReporter::TestMsgsPassedThroughSamplesInPipeline), "TestMsgsPassedThroughSamplesInPipeline");
    AddTest(MakeFunctor(*this, &SuiteSpotifyReporter::TestMsgModeResets), "TestMsgModeResets");
    AddTest(MakeFunctor(*this, &SuiteSpotifyReporter::TestSubSamples), "TestSubSamples");
    AddTest(MakeFunctor(*this, &SuiteSpotifyReporter::TestSampleRateChange), "TestSampleRateChange");
    AddTest(MakeFunctor(*this, &SuiteSpotifyReporter::TestNumChannelsChange), "TestNumChannelsChange");
    AddTest(MakeFunctor(*this, &SuiteSpotifyReporter::TestInvalidSampleRate), "TestInvalidSampleRate");
    AddTest(MakeFunctor(*this, &SuiteSpotifyReporter::TestInvalidNumChannels), "TestInvalidNumChannels");
    AddTest(MakeFunctor(*this, &SuiteSpotifyReporter::TestPassThroughInjectTrack), "TestPassThroughInjectTrack");
    AddTest(MakeFunctor(*this, &SuiteSpotifyReporter::TestModeSpotifyTrackInjected), "TestModeSpotifyTrackInjected");
    AddTest(MakeFunctor(*this, &SuiteSpotifyReporter::TestModeSpotifySeek), "TestModeSpotifySeek");
    AddTest(MakeFunctor(*this, &SuiteSpotifyReporter::TestModeSpotifySyncLost), "TestModeSpotifySyncLost");
    AddTest(MakeFunctor(*this, &SuiteSpotifyReporter::TestModeSpotifyMetadataChanged), "TestModeSpotifyMetadataChanged");
}

void SuiteSpotifyReporter::Setup()
{
    iTestPipe = new OpenHome::Test::TestPipeDynamic();
    iUpstream = new MockPipelineElementUpstream(10);
    iMsgProcessor = new MockMsgProcessor(*iTestPipe);

    MsgFactoryInitParams init;
    init.SetMsgDecodedStreamCount(2);   // SpotifyReporter always caches last seen MsgDecodedStream, so require at least 2 in pipeline.
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
    iTrackFactory = new TrackFactory(iInfoAggregator, 2);   // Require at least 2 Tracks for SpotifyReporter, as it will cache one.
    iMetadataAllocator = new MockSpotifyMetadataAllocator();

    iReporter = new SpotifyReporter(*iUpstream, *iMsgFactory, *iTrackFactory);

    TEST(iTestPipe->ExpectEmpty());
}

void SuiteSpotifyReporter::TearDown()
{
    delete iReporter;
    delete iMetadataAllocator;
    delete iTrackFactory;
    delete iMsgFactory;

    delete iMsgProcessor;
    delete iUpstream;
    TEST(iTestPipe->ExpectEmpty());
    delete iTestPipe;
}

// FIXME - have this take aDataBytes as a param.
MsgAudio* SuiteSpotifyReporter::CreateAudio(TUint aNumChannels, TUint aSampleRate, TUint64& aTrackOffset)
{
    TByte encodedAudioData[kDataBytes];
    (void)memset(encodedAudioData, 0xff, kDataBytes);
    Brn encodedAudioBuf(encodedAudioData, kDataBytes);
    MsgAudioPcm* audio = iMsgFactory->CreateMsgAudioPcm(encodedAudioBuf, aNumChannels, aSampleRate, kBitDepth, AudioDataEndian::Little, aTrackOffset);
    aTrackOffset += audio->Jiffies();
    return audio;
}

void SuiteSpotifyReporter::TestMsgsCauseAssertion()
{
    // Don't expect to see certain msgs at the point in pipeline where
    // SpotifyReporter is placed.

    // MsgEncodedStream
    MsgEncodedStream* msgEncodedStream = iMsgFactory->CreateMsgEncodedStream(Brn("spotify://"), Brx::Empty(), 1234, 0, 1, true, false, Multiroom::Allowed, nullptr);
    iUpstream->Enqueue(msgEncodedStream);
    TEST_THROWS(iReporter->Pull(), AssertionFailed);
    msgEncodedStream->RemoveRef();   // Avoid memory leaks.

    // MsgAudioEncoded
    Brn audioEncodedData("01234567");
    MsgAudioEncoded* msgAudioEncoded = iMsgFactory->CreateMsgAudioEncoded(audioEncodedData);
    iUpstream->Enqueue(msgAudioEncoded);
    TEST_THROWS(iReporter->Pull(), AssertionFailed);
    msgAudioEncoded->RemoveRef();   // Avoid memory leaks.

    // MsgPlayable
    // Need to first create a MsgAudioPcm, and then extract a MsgPlayable from it.
    Brn msgAudioPcmData("01234567");
    MsgAudioPcm* msgAudioPcm = iMsgFactory->CreateMsgAudioPcm(msgAudioPcmData, 2, 44100, 16, AudioDataEndian::Little, 0);
    MsgPlayable* msgPlayable = msgAudioPcm->CreatePlayable(); // Removes ref from owning MsgAudioPcm.
    iUpstream->Enqueue(msgPlayable);
    TEST_THROWS(iReporter->Pull(), AssertionFailed);
    msgPlayable->RemoveRef();   // Avoid memory leaks.
}

void SuiteSpotifyReporter::TestMsgsPassedThroughNoSamplesInPipeline()
{
    // All msgs should pass through unchanged. However, only MsgMode,
    // MsgDecodedStream and MsgAudioPcm should change the state of the
    // SpotifyReporter, so test the others.

    Track* track = iTrackFactory->CreateTrack(Brn("spotify://"), Brn("Spotify track meta text"));
    iUpstream->Enqueue(iMsgFactory->CreateMsgTrack(*track));
    track->RemoveRef();

    iUpstream->Enqueue(iMsgFactory->CreateMsgDelay(0));
    iUpstream->Enqueue(iMsgFactory->CreateMsgMetaText(Brn("Spotify meta text")));
    iUpstream->Enqueue(iMsgFactory->CreateMsgHalt());
    iUpstream->Enqueue(iMsgFactory->CreateMsgWait());
    TUint sizeJiffies = Jiffies::kPerSecond * 10;
    iUpstream->Enqueue(iMsgFactory->CreateMsgSilence(sizeJiffies, 44100, 16, 2));
    iUpstream->Enqueue(iMsgFactory->CreateMsgQuit());

    for (TUint i=0; i<7; i++) {
        Msg* msg = iReporter->Pull();
        msg->Process(*iMsgProcessor);
        msg->RemoveRef();
        // No audio, so no subsamples to report.
        TEST(iReporter->SubSamples() == 0);
    }

    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 1 Y")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgDelay 0")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgMetaText Spotify meta text")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgHalt 0")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgWait")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgSilence 564480000")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgQuit")));
}

void SuiteSpotifyReporter::TestMsgsPassedThroughSamplesInPipeline()
{
    // First, put some audio into pipeline.
    iUpstream->Enqueue(iMsgFactory->CreateMsgMode(Brn("null")));
    Track* track = iTrackFactory->CreateTrack(Brn("spotify://"), Brn("Spotify track meta text"));
    iUpstream->Enqueue(iMsgFactory->CreateMsgTrack(*track));
    track->RemoveRef();
    iUpstream->Enqueue(iMsgFactory->CreateMsgDecodedStream(0, 705600, 16, 44100, 2, Brn("CODC"), 3386880000, 0, true, false, false, false, AudioFormat::Pcm, Multiroom::Allowed, SpeakerProfile(2), nullptr, RampType::Sample));
    TUint64 trackOffset = 0;
    iUpstream->Enqueue(CreateAudio(2, 44100, trackOffset));

    for (TUint i=0; i<4; i++) {
        Msg* msg = iReporter->Pull();
        msg->Process(*iMsgProcessor);
        msg->RemoveRef();
    }
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgMode null")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 1 Y")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgDecodedStream 0 705600 16 44100 2 CODC 3386880000 0 Y N N")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgAudioPcm 983040 0")));

    // Even although MsgAudioPcm was passed through, it was in a stream with a
    // non-Spotify MsgMode, so 0 subsamples should be reported.
    TEST(iReporter->SubSamples() == 0);
}

void SuiteSpotifyReporter::TestMsgModeResets()
{
    const TUint samplesExpected = kDataBytes/kByteDepth;

    MockSpotifyMetadataAllocated* metadata = iMetadataAllocator->Allocate(Brn(kTrackTitle), Brn(kTrackArtist), Brn(kTrackAlbum), Brn(kTrackAlbumArt), 1234, 320);
    iReporter->MetadataChanged(metadata);

    // Send in a Spotify MsgMode.
    iUpstream->Enqueue(iMsgFactory->CreateMsgMode(Brn("Spotify")));
    Track* track = iTrackFactory->CreateTrack(Brn("spotify://"), Brn("Spotify track meta text"));
    iUpstream->Enqueue(iMsgFactory->CreateMsgTrack(*track));
    track->RemoveRef();
    iUpstream->Enqueue(iMsgFactory->CreateMsgDecodedStream(0, 705600, 16, 44100, 2, Brn("CODC"), 3386880000, 0, true, false, false, false, AudioFormat::Pcm, Multiroom::Allowed, SpeakerProfile(2), nullptr, RampType::Sample));
    TUint64 trackOffset = 0;
    iUpstream->Enqueue(CreateAudio(2, 44100, trackOffset));

    for (TUint i=0; i<5; i++) {
        Msg* msg = iReporter->Pull();
        msg->Process(*iMsgProcessor);
        msg->RemoveRef();
    }
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgMode Spotify")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 1 Y")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 2 N")));
    // Track duration is from track message injected into SpotifyReporter.
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgDecodedStream 0 705600 16 44100 2 CODC 69656832 0 Y N N")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgAudioPcm 983040 0")));

    TEST(iReporter->SubSamples() == samplesExpected);

    // Now, send another Spotify MsgMode, which should reset sample count.
    iUpstream->Enqueue(iMsgFactory->CreateMsgMode(Brn("Spotify")));
    Msg* msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgMode Spotify")));
    TEST(iReporter->SubSamples() == 0);

    // FIXME - could also test sending more audio in Spotify mode, so that iReporter->SubSamples() again reports > 0. Then, send a non-Spotify MsgMode, which should reset subsample count.
    // However, it is sufficient that it successfully reset when a Spotify mode is seen.
}

void SuiteSpotifyReporter::TestSubSamples()
{
    // FIXME - vary number of samples in msgs to catch overflow issue
    // Will need to have an iDataBytes value, instead of kDataBytes, that can
    // be varied
    TUint samplesExpectedPerMsg = kDataBytes/kByteDepth;
    TUint samplesExpected = samplesExpectedPerMsg;

    // Set up sequence.
    MockSpotifyMetadataAllocated* metadata = iMetadataAllocator->Allocate(Brn(kTrackTitle), Brn(kTrackArtist), Brn(kTrackAlbum), Brn(kTrackAlbumArt), 1234, 320);
    iReporter->MetadataChanged(metadata);

    // Send in a Spotify MsgMode.
    iUpstream->Enqueue(iMsgFactory->CreateMsgMode(Brn("Spotify")));
    Track* track = iTrackFactory->CreateTrack(Brn("spotify://"), Brn("Spotify track meta text"));
    iUpstream->Enqueue(iMsgFactory->CreateMsgTrack(*track));
    track->RemoveRef();
    iUpstream->Enqueue(iMsgFactory->CreateMsgDecodedStream(0, 705600, 16, 44100, 2, Brn("CODC"), 3386880000, 0, true, false, false, false, AudioFormat::Pcm, Multiroom::Allowed, SpeakerProfile(2), nullptr, RampType::Sample));

    for (TUint i=0; i<4; i++) {
        Msg* msg = iReporter->Pull();
        msg->Process(*iMsgProcessor);
        msg->RemoveRef();
    }
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgMode Spotify")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 1 Y")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 2 N")));
    // Track duration is from track message injected into SpotifyReporter.
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgDecodedStream 0 705600 16 44100 2 CODC 69656832 0 Y N N")));

    TEST(iReporter->SubSamples() == 0);


    // Send audio.
    TUint64 trackOffset = 0;
    for (TUint i=0; i<3; i++) {
        iUpstream->Enqueue(CreateAudio(2, 44100, trackOffset));
        Msg* msg = iReporter->Pull();
        msg->Process(*iMsgProcessor);
        msg->RemoveRef();
        TEST(iReporter->SubSamples() == samplesExpected);
        samplesExpected += samplesExpectedPerMsg;
    }
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgAudioPcm 983040 0")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgAudioPcm 983040 983040")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgAudioPcm 983040 1966080")));
}

void SuiteSpotifyReporter::TestSampleRateChange()
{
    TUint samplesExpectedPerMsg = kDataBytes/kByteDepth;
    TUint samplesExpected = 0;

    // Set up sequence.
    MockSpotifyMetadataAllocated* metadata = iMetadataAllocator->Allocate(Brn(kTrackTitle), Brn(kTrackArtist), Brn(kTrackAlbum), Brn(kTrackAlbumArt), 1234, 320);
    iReporter->MetadataChanged(metadata);

    // Send in a Spotify MsgMode.
    iUpstream->Enqueue(iMsgFactory->CreateMsgMode(Brn("Spotify")));
    Track* track = iTrackFactory->CreateTrack(Brn("spotify://"), Brn("Spotify track meta text"));
    iUpstream->Enqueue(iMsgFactory->CreateMsgTrack(*track));
    track->RemoveRef();
    iUpstream->Enqueue(iMsgFactory->CreateMsgDecodedStream(0, 705600, 16, 44100, 2, Brn("CODC"), 3386880000, 0, true, false, false, false, AudioFormat::Pcm, Multiroom::Allowed, SpeakerProfile(2), nullptr, RampType::Sample));

    for (TUint i=0; i<4; i++) {
        Msg* msg = iReporter->Pull();
        msg->Process(*iMsgProcessor);
        msg->RemoveRef();
    }
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgMode Spotify")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 1 Y")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 2 N")));
    // Track duration is from track message injected into SpotifyReporter.
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgDecodedStream 0 705600 16 44100 2 CODC 69656832 0 Y N N")));

    TEST(iReporter->SubSamples() == 0);


    // Send audio.
    TUint64 trackOffset = 0;
    for (TUint i=0; i<3; i++) {
        iUpstream->Enqueue(CreateAudio(2, 44100, trackOffset));
        Msg* msg = iReporter->Pull();
        msg->Process(*iMsgProcessor);
        msg->RemoveRef();
        samplesExpected += samplesExpectedPerMsg;
        TEST(iReporter->SubSamples() == samplesExpected);
    }
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgAudioPcm 983040 0")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgAudioPcm 983040 983040")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgAudioPcm 983040 1966080")));


    // Now, change sample rate and send more audio.
    Track* track2 = iTrackFactory->CreateTrack(Brn("spotify://"), Brn("Spotify track meta text 2"));
    iUpstream->Enqueue(iMsgFactory->CreateMsgTrack(*track2));
    track2->RemoveRef();
    iUpstream->Enqueue(iMsgFactory->CreateMsgDecodedStream(0, 768000, 16, 48000, 2, Brn("CODC"), 3386880000, 0, true, false, false, false, AudioFormat::Pcm, Multiroom::Allowed, SpeakerProfile(2), nullptr, RampType::Sample));
    for (TUint i=0; i<3; i++) {
        Msg* msg = iReporter->Pull();
        msg->Process(*iMsgProcessor);
        msg->RemoveRef();
    }
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 3 Y")));
    // Track generated by Spotify reporter, marked as not start of stream.
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 4 N")));
    // Track duration is from track message injected into SpotifyReporter.
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgDecodedStream 0 768000 16 48000 2 CODC 69656832 0 Y N N")));

    // Send audio.
    iUpstream->Enqueue(CreateAudio(2, 48000, trackOffset));
    Msg* msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();

    samplesExpected += samplesExpectedPerMsg;
    TEST(iReporter->SubSamples() == samplesExpected);

    // There are still the same number of samples per message, as the same
    // number of bytes is going into same message.
    // However, as the sample rate has increased, while the number of samples
    // has remained the same, there will be fewer samples per time unit
    // (i.e., jiffies, in this case).

    // 3072 bytes in this message.
    // 3072 / 2 bytes (for byte depth) = 1536 samples in message (across 2 channels).
    // 1536 / 2 (for number of channels) = 768 samples per channel.
    // 56648000 / 48000 = 1176 jiffies per sample @ 48000KHz.
    // 768 * 1176 = 903168 jiffies in this message.
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgAudioPcm 903168 2949120")));
}

void SuiteSpotifyReporter::TestNumChannelsChange()
{
    TUint samplesExpectedPerMsg = kDataBytes/kByteDepth;
    TUint samplesExpected = 0;

    // Set up sequence.
    MockSpotifyMetadataAllocated* metadata = iMetadataAllocator->Allocate(Brn(kTrackTitle), Brn(kTrackArtist), Brn(kTrackAlbum), Brn(kTrackAlbumArt), 1234, 320);
    iReporter->MetadataChanged(metadata);

    // Send in a Spotify MsgMode.
    iUpstream->Enqueue(iMsgFactory->CreateMsgMode(Brn("Spotify")));
    Track* track = iTrackFactory->CreateTrack(Brn("spotify://"), Brn("Spotify track meta text"));
    iUpstream->Enqueue(iMsgFactory->CreateMsgTrack(*track));
    track->RemoveRef();
    iUpstream->Enqueue(iMsgFactory->CreateMsgDecodedStream(0, 705600, 16, 44100, 2, Brn("CODC"), 3386880000, 0, true, false, false, false, AudioFormat::Pcm, Multiroom::Allowed, SpeakerProfile(2), nullptr, RampType::Sample));

    for (TUint i=0; i<4; i++) {
        Msg* msg = iReporter->Pull();
        msg->Process(*iMsgProcessor);
        msg->RemoveRef();
    }
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgMode Spotify")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 1 Y")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 2 N")));
    // Track duration is from track message injected into SpotifyReporter.
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgDecodedStream 0 705600 16 44100 2 CODC 69656832 0 Y N N")));

    TEST(iReporter->SubSamples() == 0);


    // Send audio.
    TUint64 trackOffset = 0;
    for (TUint i=0; i<3; i++) {
        iUpstream->Enqueue(CreateAudio(2, 44100, trackOffset));
        Msg* msg = iReporter->Pull();
        msg->Process(*iMsgProcessor);
        msg->RemoveRef();
        samplesExpected += samplesExpectedPerMsg;
        TEST(iReporter->SubSamples() == samplesExpected);
    }
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgAudioPcm 983040 0")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgAudioPcm 983040 983040")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgAudioPcm 983040 1966080")));


    // Now, change number of channels and send more audio.
    Track* track2 = iTrackFactory->CreateTrack(Brn("spotify://"), Brn("Spotify track meta text"));
    iUpstream->Enqueue(iMsgFactory->CreateMsgTrack(*track2));
    track2->RemoveRef();
    iUpstream->Enqueue(iMsgFactory->CreateMsgDecodedStream(0, 705600, 16, 44100, 1, Brn("CODC"), 3386880000, 0, true, false, false, false, AudioFormat::Pcm, Multiroom::Allowed, SpeakerProfile(1), nullptr, RampType::Sample));
    for (TUint i=0; i<3; i++) {
        Msg* msg = iReporter->Pull();
        msg->Process(*iMsgProcessor);
        msg->RemoveRef();
    }
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 3 Y")));
    // Track generated by Spotify reporter, marked as not start of stream.
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 4 N")));
    // Track duration is from track message injected into SpotifyReporter.
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgDecodedStream 0 705600 16 44100 1 CODC 69656832 0 Y N N")));

    for (TUint i=0; i<3; i++) {
        iUpstream->Enqueue(CreateAudio(1, 44100, trackOffset));
        Msg* msg = iReporter->Pull();
        msg->Process(*iMsgProcessor);
        msg->RemoveRef();
        samplesExpected += samplesExpectedPerMsg;
        TEST(iReporter->SubSamples() == samplesExpected);
    }
    // Number of jiffies is now double that previously reported, due to same
    // amount of data in each message, but only half the number of channels.
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgAudioPcm 1966080 2949120")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgAudioPcm 1966080 4915200")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgAudioPcm 1966080 6881280")));
}

void SuiteSpotifyReporter::TestInvalidSampleRate()
{
    const TUint sampleRate = 0; // Invalid sample rate.
    MockSpotifyMetadataAllocated* metadata = iMetadataAllocator->Allocate(Brn(kTrackTitle), Brn(kTrackArtist), Brn(kTrackAlbum), Brn(kTrackAlbumArt), 1234, 320);
    iReporter->MetadataChanged(metadata);

    // Send in a Spotify MsgMode.
    iUpstream->Enqueue(iMsgFactory->CreateMsgMode(Brn("Spotify")));
    Track* track = iTrackFactory->CreateTrack(Brn("spotify://"), Brn("Spotify track meta text"));
    iUpstream->Enqueue(iMsgFactory->CreateMsgTrack(*track));
    track->RemoveRef();
    MsgDecodedStream* decodedStream = iMsgFactory->CreateMsgDecodedStream(0, 705600, 16, sampleRate, 2, Brn("CODC"), 3386880000, 0, true, false, false, false, AudioFormat::Pcm, Multiroom::Allowed, SpeakerProfile(2), nullptr, RampType::Sample);
    iUpstream->Enqueue(decodedStream);

    for (TUint i=0; i<2; i++) {
        Msg* msg = iReporter->Pull();
        msg->Process(*iMsgProcessor);
        msg->RemoveRef();
    }
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgMode Spotify")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 1 Y")));

    TEST_THROWS(iReporter->Pull(), AssertionFailed);
    decodedStream->RemoveRef(); // Avoid memory leaks.
}

void SuiteSpotifyReporter::TestInvalidNumChannels()
{
    const TUint channels = 0;
    const SpeakerProfile profile(0);

    MockSpotifyMetadataAllocated* metadata = iMetadataAllocator->Allocate(Brn(kTrackTitle), Brn(kTrackArtist), Brn(kTrackAlbum), Brn(kTrackAlbumArt), 1234, 320);
    iReporter->MetadataChanged(metadata);

    // Send in a Spotify MsgMode.
    iUpstream->Enqueue(iMsgFactory->CreateMsgMode(Brn("Spotify")));
    Track* track = iTrackFactory->CreateTrack(Brn("spotify://"), Brn("Spotify track meta text"));
    iUpstream->Enqueue(iMsgFactory->CreateMsgTrack(*track));
    track->RemoveRef();
    MsgDecodedStream* decodedStream = iMsgFactory->CreateMsgDecodedStream(0, 705600, 16, 44100, channels, Brn("CODC"), 3386880000, 0, true, false, false, false, AudioFormat::Pcm, Multiroom::Allowed, profile, nullptr, RampType::Sample);
    iUpstream->Enqueue(decodedStream);

    for (TUint i=0; i<2; i++) {
        Msg* msg = iReporter->Pull();
        msg->Process(*iMsgProcessor);
        msg->RemoveRef();
    }
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgMode Spotify")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 1 Y")));

    TEST_THROWS(iReporter->Pull(), AssertionFailed);
    decodedStream->RemoveRef(); // Avoid memory leaks.
}

void SuiteSpotifyReporter::TestPassThroughInjectTrack()
{
    // This could happen if Spotify source is just starting, but audio has yet to arrive at SpotifyReporter, so track is injected during non-Spotify stream.
    static const Brn kSpotifyTrackUri("spotify://");
    const TUint kDurationMs = 1234;

    MockSpotifyMetadataAllocated* metadata = iMetadataAllocator->Allocate(Brn(kTrackTitle), Brn(kTrackArtist), Brn(kTrackAlbum), Brn(kTrackAlbumArt), kDurationMs, 320);
    iReporter->MetadataChanged(metadata);
    static const TUint kSeekMs = 500;
    iReporter->TrackOffsetChanged(kSeekMs);

    // NOT "Spotify" mode.
    iUpstream->Enqueue(iMsgFactory->CreateMsgMode(Brn("null")));
    Msg* msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgMode null")));

    Track* track = iTrackFactory->CreateTrack(Brn("spotify://"), Brn("Spotify track meta text"));
    iUpstream->Enqueue(iMsgFactory->CreateMsgTrack(*track));
    track->RemoveRef();
    msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 1 Y")));
    msg->RemoveRef();
    // If in pass-through mode, metadata won't be deallocated until more is passed in, forcing current metadata to be deallocated, or at shutdown (which internal allocator check will catch).
    TEST(iMetadataAllocator->DeallocatedCount() == 0);

    iUpstream->Enqueue(iMsgFactory->CreateMsgDecodedStream(0, 705600, 16, 44100, 2, Brn("CODC"), 3386880000, 0, true, false, false, false, AudioFormat::Pcm, Multiroom::Allowed, SpeakerProfile(2), nullptr, RampType::Sample));
    msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    // Check a modified MsgDecodedStream wasn't inserted (should report track
    // duration of the MsgDecodedStream sent down pipeline, instead of injected
    // track).
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgDecodedStream 0 705600 16 44100 2 CODC 3386880000 0 Y N N")));
    msg->RemoveRef();

    // Pull some audio to check that no modified MsgTrack or MsgDecodedStream is injected.
    TUint64 trackOffset = 0;
    iUpstream->Enqueue(CreateAudio(2, 44100, trackOffset));
    msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgAudioPcm 983040 0")));
    TEST(iReporter->SubSamples() == 0); // Was not "Spotify" mode, so no subsamples should be reported.
}

void SuiteSpotifyReporter::TestModeSpotifyTrackInjected()
{
    // Inject a track to simulate real-world condition where out-of-band track notification is reach SpotifyReporter before MsgMode at Spotify initialisation.
    static const Brn kSpotifyTrackUri("spotify://");
    const TUint kDurationMs = 1234;
    MockSpotifyMetadataAllocated* metadata = iMetadataAllocator->Allocate(Brn(kTrackTitle), Brn(kTrackArtist), Brn(kTrackAlbum), Brn(kTrackAlbumArt), kDurationMs, 320);
    iReporter->MetadataChanged(metadata);
    static const TUint kSeekMs = 500; // Sample 22050 @ 44.1KHz.
    iReporter->TrackOffsetChanged(kSeekMs);

    // Pull mode.
    iUpstream->Enqueue(iMsgFactory->CreateMsgMode(Brn("Spotify")));
    Msg* msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgMode Spotify")));

    // Set track to be next msg down pipeline.
    // Pull again. Should be in-band pipeline MsgTrack.
    Track* track = iTrackFactory->CreateTrack(Brn("spotify://"), Brn("Spotify track meta text"));
    iUpstream->Enqueue(iMsgFactory->CreateMsgTrack(*track));
    track->RemoveRef();
    msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 1 Y")));

    // Queue up MsgDecodedStream and pull again. Should be injected track.
    iUpstream->Enqueue(iMsgFactory->CreateMsgDecodedStream(0, 705600, 16, 44100, 2, Brn("CODC"), 3386880000, 0, true, false, false, false, AudioFormat::Pcm, Multiroom::Allowed, SpeakerProfile(2), nullptr, RampType::Sample));
    msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 2 N")));
    TEST(iMetadataAllocator->DeallocatedCount() == 0);  // Metadata should be cached and not deallocated.

    // Pull again. Modified MsgDecodedStream should be output.
    msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    // Track duration is from track message injected into SpotifyReporter.
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgDecodedStream 0 705600 16 44100 2 CODC 69656832 22050 Y N N")));

    // Now, queue up some audio.
    const TUint samplesExpectedPerMsg = kDataBytes/kByteDepth;
    TUint samplesExpected = samplesExpectedPerMsg;
    TUint64 trackOffset = 0;
    iUpstream->Enqueue(CreateAudio(2, 44100, trackOffset));
    msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgAudioPcm 983040 0")));
    TEST(iReporter->SubSamples() == samplesExpected);


    // Now, insert another track to signify track change.

    // Inject a MsgTrack.
    const TUint kDuration2 = 5678;
    metadata = iMetadataAllocator->Allocate(Brn(kTrackTitle), Brn(kTrackArtist), Brn(kTrackAlbum), Brn(kTrackAlbumArt), kDuration2, 320);
    iReporter->MetadataChanged(metadata);
    // TrackOffsetChanged call should come in around same time as TrackChanged() call. In this case, moving to start of new track.
    iReporter->TrackOffsetChanged(0);

    // Now pull. Should get generated MsgTrack.
    msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 3 N")));
    TEST(iMetadataAllocator->DeallocatedCount() == 1);  // Old metadata should have been deallocated; current metadata should still be cached.
    // Pull again. Should be generated MsgDecodedStream. SampleStart should now be 0, as injected track resets it.
    msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgDecodedStream 0 705600 16 44100 2 CODC 320511744 0 Y N N")));
    // Pull audio.
    iUpstream->Enqueue(CreateAudio(2, 44100, trackOffset));
    msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    samplesExpected += samplesExpectedPerMsg;
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgAudioPcm 983040 983040")));
    TEST(iReporter->SubSamples() == samplesExpected);
}

void SuiteSpotifyReporter::TestModeSpotifySeek()
{
    // Pass in a MsgMode followed by a MsgDecodedStream mid-way through stream to simulate a seek.
    // First part of this test is already tested by TestModeSpotifyTrackInjected().
    static const Brn kSpotifyTrackUri("spotify://");
    const TUint kDuration = 1234;
    MockSpotifyMetadataAllocated* metadata = iMetadataAllocator->Allocate(Brn(kTrackTitle), Brn(kTrackArtist), Brn(kTrackAlbum), Brn(kTrackAlbumArt), kDuration, 320);
    iReporter->MetadataChanged(metadata);
    static const TUint kSeekMs = 500; // Sample 22050 @ 44.1KHz.
    iReporter->TrackOffsetChanged(kSeekMs);

    // Pull mode.
    iUpstream->Enqueue(iMsgFactory->CreateMsgMode(Brn("Spotify")));
    Msg* msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgMode Spotify")));

    // Set track to be next msg down pipeline.
    // Pull again. Should be in-band pipeline MsgTrack.
    Track* track = iTrackFactory->CreateTrack(Brn("spotify://"), Brn("Spotify track meta text"));
    iUpstream->Enqueue(iMsgFactory->CreateMsgTrack(*track));
    track->RemoveRef();
    msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 1 Y")));

    // Queue up MsgDecodedStream and pull again. Should get injected track.
    iUpstream->Enqueue(iMsgFactory->CreateMsgDecodedStream(0, 705600, 16, 44100, 2, Brn("CODC"), 3386880000, 0, true, false, false, false, AudioFormat::Pcm, Multiroom::Allowed, SpeakerProfile(2), nullptr, RampType::Sample));
    msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 2 N")));
    TEST(iMetadataAllocator->DeallocatedCount() == 0);  // Metadata should be cached and not deallocated.

    // Pull again. Delayed MsgDecodedStream should be output with modified info.
    msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgDecodedStream 0 705600 16 44100 2 CODC 69656832 22050 Y N N")));

    // Now, queue up some audio.
    const TUint samplesExpectedPerMsg = kDataBytes/kByteDepth;
    TUint samplesExpected = samplesExpectedPerMsg;
    TUint64 trackOffset = 0;
    iUpstream->Enqueue(CreateAudio(2, 44100, trackOffset));
    msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgAudioPcm 983040 0")));
    TEST(iReporter->SubSamples() == samplesExpected);

    /* ---------- Setup code ends; test case begins. ---------- */

    // Tell SpotifyReporter about a seek.
    static const TUint kSeekMs2 = 250; // Sample 11025 @ 44.1KHz.

    // MsgDrain, to signify a flush.
    Semaphore sem("TSRS", 0);
    iUpstream->Enqueue(iMsgFactory->CreateMsgDrain(MakeFunctor(sem, &Semaphore::Signal)));
    msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    dynamic_cast<MsgDrain*>(msg)->ReportDrained();
    msg->RemoveRef();
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgDrain 0")));

    // FIXME - race condition. If NotifySeek() is called before the MsgDrain is pulled through, it means the generated MsgDecodedStream will be pushed out before the MsgDrain is passed on. However, that shouldn't be a problem.
    // In the implementation, it certainly isn't a problem, as flushing the pipeline is a synchronous call (i.e., it doesn't return until it gets the MsgDrain callback), so shouldn't get that odd race condition.

    // NotifySeek() triggers generation of a new MsgDecodedStream with new start offset.
    iReporter->TrackOffsetChanged(kSeekMs2);
    msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    // Start offset is updated due to NotifySeek() call to 250 ms above.
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgDecodedStream 0 705600 16 44100 2 CODC 69656832 11025 Y N N")));

    // Pull some audio.
    iUpstream->Enqueue(CreateAudio(2, 44100, trackOffset));
    msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    samplesExpected += samplesExpectedPerMsg;
    TEST(iReporter->SubSamples() == samplesExpected);
    samplesExpected += samplesExpectedPerMsg;
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgAudioPcm 983040 983040")));
}

void SuiteSpotifyReporter::TestModeSpotifySyncLost()
{
    const TUint samplesExpectedPerMsg = kDataBytes/kByteDepth;

    // Set up sequence.
    MockSpotifyMetadataAllocated* metadata = iMetadataAllocator->Allocate(Brn(kTrackTitle), Brn(kTrackArtist), Brn(kTrackAlbum), Brn(kTrackAlbumArt), 1234, 320);
    iReporter->MetadataChanged(metadata);
    iReporter->TrackOffsetChanged(0);

    // Send in a Spotify MsgMode.
    iUpstream->Enqueue(iMsgFactory->CreateMsgMode(Brn("Spotify")));
    Track* track = iTrackFactory->CreateTrack(Brn("spotify://"), Brn("Spotify track meta text"));
    iUpstream->Enqueue(iMsgFactory->CreateMsgTrack(*track));
    track->RemoveRef();
    iUpstream->Enqueue(iMsgFactory->CreateMsgDecodedStream(0, 705600, 16, 44100, 2, Brn("CODC"), 3386880000, 0, true, false, false, false, AudioFormat::Pcm, Multiroom::Allowed, SpeakerProfile(2), nullptr, RampType::Sample));

    for (TUint i=0; i<4; i++) {
        Msg* msg = iReporter->Pull();
        msg->Process(*iMsgProcessor);
        msg->RemoveRef();
    }
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgMode Spotify")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 1 Y")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 2 N")));
    // Track duration is from track message injected into SpotifyReporter.
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgDecodedStream 0 705600 16 44100 2 CODC 69656832 0 Y N N")));

    TEST(iReporter->SubSamples() == 0);


    // Now, queue up some audio.
    TUint samplesExpected = samplesExpectedPerMsg;
    TUint64 trackOffset = 0;
    iUpstream->Enqueue(CreateAudio(2, 44100, trackOffset));

    // Report TrackPosition 1999 ms from MsgDecodedStream stream start.
    const TUint trackPositionBelowThreshold = 1999;
    iReporter->TrackPosition(trackPositionBelowThreshold);
    // Should not result in a new MsgDecodedStream being output. Should get audio instead.
    Msg* msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgAudioPcm 983040 0")));
    TEST(iReporter->SubSamples() == samplesExpected);

    // Queue up more audio.
    samplesExpected += samplesExpectedPerMsg;
    iUpstream->Enqueue(CreateAudio(2, 44100, trackOffset));

    // Report TrackPosition 2000 ms from MsgDecodedStream stream start.
    const TUint trackPositionOnThreshold = 3999;    // 1999 + 2000
    iReporter->TrackPosition(trackPositionOnThreshold);
    // Should not result in a new MsgDecodedStream being output. Should get audio instead.
    msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgAudioPcm 983040 983040")));
    TEST(iReporter->SubSamples() == samplesExpected);

    // Queue up more audio.
    samplesExpected += samplesExpectedPerMsg;
    iUpstream->Enqueue(CreateAudio(2, 44100, trackOffset));

    // Report TrackPosition 2001 ms from MsgDecodedStream stream start.
    const TUint trackPositionAboveThreshold = 6000;     // 3999 + 2001
    iReporter->TrackPosition(trackPositionAboveThreshold);
    // Should result in a new MsgDecodedStream being output.
    msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgDecodedStream 0 705600 16 44100 2 CODC 69656832 264600 Y N N")));
    // Pull audio through.
    msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgAudioPcm 983040 1966080")));
    TEST(iReporter->SubSamples() == samplesExpected);
}

void SuiteSpotifyReporter::TestModeSpotifyMetadataChanged()
{
    const TUint samplesExpectedPerMsg = kDataBytes/kByteDepth;

    // Set up sequence.
    MockSpotifyMetadataAllocated* metadata = iMetadataAllocator->Allocate(Brn(kTrackTitle), Brn(kTrackArtist), Brn(kTrackAlbum), Brn(kTrackAlbumArt), 1234, 320);
    iReporter->MetadataChanged(metadata);
    iReporter->TrackOffsetChanged(0);

    // Send in a Spotify MsgMode.
    iUpstream->Enqueue(iMsgFactory->CreateMsgMode(Brn("Spotify")));
    Track* track = iTrackFactory->CreateTrack(Brn("spotify://"), Brn("Spotify track meta text"));
    iUpstream->Enqueue(iMsgFactory->CreateMsgTrack(*track));
    track->RemoveRef();
    iUpstream->Enqueue(iMsgFactory->CreateMsgDecodedStream(0, 705600, 16, 44100, 2, Brn("CODC"), 3386880000, 0, true, false, false, false, AudioFormat::Pcm, Multiroom::Allowed, SpeakerProfile(2), nullptr, RampType::Sample));

    for (TUint i=0; i<4; i++) {
        Msg* msg = iReporter->Pull();
        msg->Process(*iMsgProcessor);
        msg->RemoveRef();
    }
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgMode Spotify")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 1 Y")));
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 2 N")));
    // Track duration is from track message injected into SpotifyReporter.
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgDecodedStream 0 705600 16 44100 2 CODC 69656832 0 Y N N")));
    TEST(iReporter->SubSamples() == 0);


    // Now, queue up some audio.
    TUint samplesExpected = samplesExpectedPerMsg;
    TUint64 trackOffset = 0;
    iUpstream->Enqueue(CreateAudio(2, 44100, trackOffset));

    // Report change in metadata, but not track or position.
    metadata = iMetadataAllocator->Allocate(Brn("artist2"), Brn("trackartist2"), Brn("trackalbum2"), Brn("trackalbumart2"), 5678, 160);
    iReporter->MetadataChanged(metadata);
    // Should pull new MsgTrack.
    Msg* msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 3 N")));
    TEST(iMetadataAllocator->DeallocatedCount() == 1);  // Should have deallocated old metadata and cached new metadata.
    // Should pull new MsgDecodedStream, but with same start offset as previous, as track position is not reported as changed. Should report new track length.
    msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgDecodedStream 0 705600 16 44100 2 CODC 320511744 0 Y N N")));

    // Report change in track position AND change in metadata.
    iReporter->TrackOffsetChanged(30000);
    metadata = iMetadataAllocator->Allocate(Brn("artist3"), Brn("trackartist3"), Brn("trackalbum3"), Brn("trackalbumart3"), 9012, 160);
    iReporter->MetadataChanged(metadata);
    // Should pull new MsgTrack.
    msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgTrack spotify:// 4 N")));
    TEST(iMetadataAllocator->DeallocatedCount() == 2);  // Should have deallocated old metadata and cached new metadata.
    // Should pull new MsgDecodedStream, with new start offset, as track position has been reported as changed through TrackOffsetChanged() call.
    msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgDecodedStream 0 705600 16 44100 2 CODC 508709376 1323000 Y N N")));

    // Pull previously queued audio.
    msg = iReporter->Pull();
    msg->Process(*iMsgProcessor);
    msg->RemoveRef();
    TEST(iTestPipe->Expect(Brn("MMP::ProcessMsg MsgAudioPcm 983040 0")));
    TEST(iReporter->SubSamples() == samplesExpected);
}



void TestSpotifyReporter()
{
    Runner runner("SpotifyReporter tests\n");
    runner.Add(new SuiteSpotifyReporter());
    runner.Run();
}

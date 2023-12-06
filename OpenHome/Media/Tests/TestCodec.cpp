#include <OpenHome/Media/Tests/TestCodec.h>
#include <OpenHome/Media/Pipeline/EncodedAudioReservoir.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Pipeline/Logger.h>
#include <OpenHome/Media/Codec/CodecController.h>
#include <OpenHome/Media/Codec/Container.h>
#include <OpenHome/Media/Codec/CodecFactory.h>
#include <OpenHome/Media/Codec/Id3v2.h>
#include <OpenHome/Media/Codec/Mpeg4.h>
#include <OpenHome/Media/Codec/MpegTs.h>
#include <OpenHome/Media/Pipeline/Pipeline.h>
#include <OpenHome/Media/Utils/ProcessorAudioUtils.h>
#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Media/Protocol/ProtocolFactory.h>
#include <OpenHome/Private/File.h>
#include <OpenHome/Private/OptionParser.h>
#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/OsWrapper.h>
#include <OpenHome/SocketSsl.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Media/Utils/AllocatorInfoLogger.h>
#include <OpenHome/Private/SuiteUnitTest.h>

#include <vector>

using namespace OpenHome;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Codec;


// AudioFileDescriptor

AudioFileDescriptor::AudioFileDescriptor(const Brx& aFilename, TUint aSampleRate, TUint aSamples, TUint aBitDepth, TUint aChannels, TUint aCodec, TBool aSeekable)
    : iFilename(aFilename)
    , iSampleRate(aSampleRate)
    , iSamples(aSamples)
    , iBitDepth(aBitDepth)
    , iChannels(aChannels)
    , iCodec(aCodec)
    , iSeekable(aSeekable)
{
}
const Brx& AudioFileDescriptor::Filename() const
{
    return iFilename;
}

TUint AudioFileDescriptor::SampleRate() const
{
    return iSampleRate;
}

TUint AudioFileDescriptor::Samples() const
{
    return iSamples;
}

TUint64 AudioFileDescriptor::Jiffies() const
{
    const TUint jiffiesPerSecond = Jiffies::kPerSecond;
    TUint64 jiffies = 0;
    TUint wholeSecs = (iSamples && iSampleRate) ? iSamples/iSampleRate : 0;
    TUint remainingSamples = iSamples - iSampleRate*wholeSecs;
    TUint jiffiesPerSample = (jiffiesPerSecond && iSampleRate) ? jiffiesPerSecond/iSampleRate : 0;

    jiffies = wholeSecs*static_cast<TUint64>(jiffiesPerSecond) + remainingSamples*jiffiesPerSample;
    //LOG(kMedia, "AudioFileDescriptor::Jiffies wholeSecs: %u, remainingSamples: %u, jiffiesPerSample: %u, jiffies: %llu\n", wholeSecs, remainingSamples, jiffiesPerSample, jiffies);
    return jiffies;
}

TUint AudioFileDescriptor::BitDepth() const
{
    return iBitDepth;
}

TUint AudioFileDescriptor::Channels() const
{
    return iChannels;
}

TUint AudioFileDescriptor::Codec() const
{
    return iCodec;
}

TBool AudioFileDescriptor::Seekable() const
{
    return iSeekable;
}


// AudioFileCollection

AudioFileCollection::AudioFileCollection()
{
}

AudioFileCollection::AudioFileCollection(std::vector<AudioFileDescriptor> aReqFiles
                  , std::vector<AudioFileDescriptor> aExtraFiles
                  , std::vector<AudioFileDescriptor> aInvalidFiles
                  , std::vector<AudioFileDescriptor> aStreamOnlyFiles
                  )
    : iReqFiles(aReqFiles)
    , iExtraFiles(aExtraFiles)
    , iInvalidFiles(aInvalidFiles)
    , iStreamOnlyFiles(aStreamOnlyFiles)
{
}


void AudioFileCollection::AddRequiredFile(AudioFileDescriptor aFile)
{
    iReqFiles.push_back(aFile);
}

void AudioFileCollection::AddExtraFile(AudioFileDescriptor aFile)
{
    iExtraFiles.push_back(aFile);
}

void AudioFileCollection::AddInvalidFile(AudioFileDescriptor aFile)
{
    iInvalidFiles.push_back(aFile);
}

void AudioFileCollection::AddStreamOnlyFile(AudioFileDescriptor aFile)
{
    iStreamOnlyFiles.push_back(aFile);
}

std::vector<AudioFileDescriptor>& AudioFileCollection::RequiredFiles()
{
    return iReqFiles;
}

std::vector<AudioFileDescriptor>& AudioFileCollection::ExtraFiles()
{
    return iExtraFiles;
}

std::vector<AudioFileDescriptor>& AudioFileCollection::InvalidFiles()
{
    return iInvalidFiles;
}

std::vector<AudioFileDescriptor>& AudioFileCollection::StreamOnlyFiles()
{   return iStreamOnlyFiles;
}


// TestCodecInfoAggregator

TestCodecInfoAggregator::TestCodecInfoAggregator()
{
}

TestCodecInfoAggregator::~TestCodecInfoAggregator()
{
}

void TestCodecInfoAggregator::Register(IInfoProvider& /*aProvider*/, std::vector<Brn>& /*aSupportedQueries*/)
{
}


//class TestCodecFlushIdProvider

TestCodecFlushIdProvider::TestCodecFlushIdProvider()
    : iFlushId(MsgFlush::kIdInvalid+1)
{
}

TestCodecFlushIdProvider::~TestCodecFlushIdProvider()
{
}

TUint TestCodecFlushIdProvider::NextFlushId()
{
    return iFlushId++;
}


// TestCodecFiller

TestCodecFiller::TestCodecFiller(Environment& aEnv, IPipelineElementDownstream& aDownstream, MsgFactory& aMsgFactory, IFlushIdProvider& aFlushIdProvider, IInfoAggregator& aInfoAggregator)
    : Thread("TCFL")
    , iPipeline(aDownstream)
    , iMsgFactory(aMsgFactory)
    , iNextStreamId(kInvalidPipelineId+1)
{
    iSsl = new SslContext();
    iProtocolManager = new ProtocolManager(aDownstream, aMsgFactory, *this, aFlushIdProvider);
    iProtocolManager->Add(ProtocolFactory::NewHttp(aEnv, *iSsl, Brx::Empty()));
    iProtocolManager->Add(ProtocolFactory::NewHttp(aEnv, *iSsl, Brx::Empty()));    // Second ProtocolHttp to allow out-of-band reads.
    iTrackFactory = new TrackFactory(aInfoAggregator, 1);
}

TestCodecFiller::~TestCodecFiller()
{
    delete iTrackFactory;
    delete iProtocolManager;
    delete iSsl;
}

void TestCodecFiller::Start(const Brx& aUrl)
{
    iUrl.Set(aUrl);
    Thread::Start();
}

TUint TestCodecFiller::StreamId()
{
    return iNextStreamId-1;
}

TBool TestCodecFiller::TryGet(IWriter& aWriter, const Brx& aUrl, TUint64 aOffset, TUint aBytes)
{
    return iProtocolManager->TryGet(aWriter, aUrl, aOffset, aBytes);
}

void TestCodecFiller::Run()
{
    Track* track = iTrackFactory->CreateTrack(iUrl, Brx::Empty());
    ProtocolStreamResult res = iProtocolManager->DoStream(*track);
    track->RemoveRef();
    // send a msgquit here in case of trying to stream an invalid url during tests
    // could cause race conditions if it isn't sent here
    iPipeline.Push(iMsgFactory.CreateMsgQuit());
    TEST(res == EProtocolStreamSuccess);
}

TUint TestCodecFiller::NextStreamId()
{
    return iNextStreamId++;
}

EStreamPlay TestCodecFiller::OkToPlay(TUint /*aStreamId*/)
{
    return ePlayYes;
}


// TestCodecPipelineElementDownstream

TestCodecPipelineElementDownstream::TestCodecPipelineElementDownstream(IMsgProcessor& aMsgProcessor)
    : iMsgProcessor(aMsgProcessor)
{
}

TestCodecPipelineElementDownstream::~TestCodecPipelineElementDownstream()
{
}

void TestCodecPipelineElementDownstream::Push(Msg* aMsg)
{
    //LOG(kMedia, ">TestCodecPipelineElementDownstream::Push\n");
    aMsg = aMsg->Process(iMsgProcessor);
    if (aMsg != nullptr) {
        aMsg->RemoveRef();
    }
}


// TestCodecMinimalPipeline

TestCodecMinimalPipeline::TestCodecMinimalPipeline(Environment& aEnv, IMsgProcessor& aMsgProcessor)
{
    iInfoAggregator = new TestCodecInfoAggregator();
    MsgFactoryInitParams init;
    init.SetMsgAudioEncodedCount(kMsgAudioEncodedCount, kEncodedAudioCount);
    init.SetMsgAudioPcmCount(5, 5);
    init.SetMsgEncodedStreamCount(2);
    init.SetMsgFlushCount(2);
    iMsgFactory = new MsgFactory(*iInfoAggregator, init);
    // iFiller(ProtocolManager) -> iSupply -> iReservoir -> iContainer -> iController -> iElementDownstream(this)
    iFlushIdProvider = new TestCodecFlushIdProvider();
    iElementDownstream = new TestCodecPipelineElementDownstream(aMsgProcessor);
    iReservoir = new EncodedAudioReservoir(*iMsgFactory, *iFlushIdProvider, kReservoirEncodedAudioMsgs, kEncodedReservoirMaxStreams);
    iLoggerEncodedAudioReservoir = new Logger(*iReservoir, "Encoded Audio Reservoir");
    iContainer = new ContainerController(*iMsgFactory, *iLoggerEncodedAudioReservoir, *this, true);
    iLoggerContainer = new Logger(*iContainer, "Codec Container");
    iLoggerCodecController = new Logger("Codec Controller", *iElementDownstream);
    iController = new CodecController(*iMsgFactory, *iLoggerContainer, *iLoggerCodecController, *this, Jiffies::kPerMs * 5, kPriorityNormal, true);
    iFiller = new TestCodecFiller(aEnv, *iReservoir, *iMsgFactory, *iFlushIdProvider, *iInfoAggregator);

    //iLoggerEncodedAudioReservoir->SetEnabled(true);
    //iLoggerContainer->SetEnabled(true);
    //iLoggerCodecController->SetEnabled(true);

    //iLoggerEncodedAudioReservoir->SetFilter(Logger::EMsgAll);
    //iLoggerContainer->SetFilter(Logger::EMsgAll);
    //iLoggerCodecController->SetFilter(Logger::EMsgAll);
}

TestCodecMinimalPipeline::~TestCodecMinimalPipeline()
{
    delete iFiller;
    delete iController;
    delete iLoggerCodecController;
    delete iLoggerContainer;
    delete iContainer;
    delete iLoggerEncodedAudioReservoir;
    delete iReservoir;
    delete iElementDownstream;
    delete iFlushIdProvider;
    delete iMsgFactory;
    delete iInfoAggregator;
}

void TestCodecMinimalPipeline::StartPipeline()
{
    RegisterPlugins();
    iController->Start();
}

void TestCodecMinimalPipeline::StartStreaming(const Brx& aUrl)
{
    iFiller->Start(aUrl);
}

TBool TestCodecMinimalPipeline::SeekCurrentTrack(TUint aSecondsAbsolute, ISeekObserver& aSeekObserver, TUint& aHandle)
{
    ISeeker* seeker = static_cast<ISeeker*>(iController);
    seeker->StartSeek(iFiller->StreamId(), aSecondsAbsolute, aSeekObserver, aHandle);
    return (aHandle != ISeeker::kHandleError);
}

void TestCodecMinimalPipeline::RegisterPlugins()
{
    // Add containers
    iContainer->AddContainer(new Id3v2());
    iContainer->AddContainer(new Mpeg4Container(*this));
    iContainer->AddContainer(new MpegTsContainer(*this));

    // Add codecs
    // These can be re-ordered to check for problems in the recognise function of each codec.
    iController->AddCodec(CodecFactory::NewWav(*this));
    iController->AddCodec(CodecFactory::NewAiff(*this));
    iController->AddCodec(CodecFactory::NewAifc(*this));
    iController->AddCodec(CodecFactory::NewFlac(*this));
    iController->AddCodec(CodecFactory::NewAacFdkAdts(*this));
    iController->AddCodec(CodecFactory::NewAacFdkMp4(*this));
    //iController->AddCodec(CodecFactory::NewAlac(*this));
    iController->AddCodec(CodecFactory::NewAlacApple(*this));
    iController->AddCodec(CodecFactory::NewMp3(*this));
    iController->AddCodec(CodecFactory::NewVorbis(*this));
}

TBool TestCodecMinimalPipeline::TryGet(IWriter& aWriter, const Brx& aUrl, TUint64 aOffset, TUint aBytes)
{
    Log::Print("Codec requesting out-of-band read. aUrl: ");
    Log::Print(aUrl);
    Log::Print(", aOffset: %llu, aBytes: %u\n", aOffset, aBytes);
    return iFiller->TryGet(aWriter, aUrl, aOffset, aBytes);
}

void TestCodecMinimalPipeline::Add(const TChar* /*aMimeType*/)
{
}


// MsgProcessor

MsgProcessor::MsgProcessor(Semaphore& aSem)
    : iSem(aSem)
{
}

MsgProcessor::~MsgProcessor()
{
}

Msg* MsgProcessor::ProcessMsg(MsgMode* aMsg)
{
    return aMsg;
}
Msg* MsgProcessor::ProcessMsg(MsgTrack* aMsg)
{
    //LOG(kMedia, ">MsgProcessor::ProcessMsgTrack\n");
    return aMsg;
}
Msg* MsgProcessor::ProcessMsg(MsgDrain* aMsg)
{
    return aMsg;
}
Msg* MsgProcessor::ProcessMsg(MsgDelay* aMsg)
{
    return aMsg;
}
Msg* MsgProcessor::ProcessMsg(MsgEncodedStream* aMsg)
{
    //LOG(kMedia, ">MsgProcessor::ProcessMsgEncodedStream\n");
    return aMsg;
}
Msg* MsgProcessor::ProcessMsg(MsgStreamSegment* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}
Msg* MsgProcessor::ProcessMsg(MsgAudioEncoded* /*aMsg*/)
{
    //LOG(kMedia, ">MsgProcessor::ProcessMsgAudioEncoded\n");
    ASSERTS();
    return nullptr;
}
Msg* MsgProcessor::ProcessMsg(MsgMetaText* aMsg)
{
    //LOG(kMedia, ">MsgProcessor::ProcessMsgMetaText\n");
    aMsg->RemoveRef();
    return nullptr;
}
Msg* MsgProcessor::ProcessMsg(MsgStreamInterrupted* aMsg)
{
    return aMsg;
}
Msg* MsgProcessor::ProcessMsg(MsgHalt* /*aMsg*/)
{
    //LOG(kMedia, ">MsgProcessor::ProcessMsgHalt\n");
    ASSERTS();
    return nullptr;
}
Msg* MsgProcessor::ProcessMsg(MsgFlush* aMsg)
{
    //LOG(kMedia, ">MsgProcessor::ProcessMsgFlush\n");
    aMsg->RemoveRef();
    return nullptr;
}
Msg* MsgProcessor::ProcessMsg(MsgWait* aMsg)
{
    //LOG(kMedia, ">MsgProcessor::ProcessMsgWait\n");
    aMsg->RemoveRef();
    return nullptr;
}
Msg* MsgProcessor::ProcessMsg(MsgDecodedStream* aMsg)
{
    //LOG(kMedia, ">MsgProcessor::ProcessMsgDecodedStream\n");
    return aMsg;
}
Msg* MsgProcessor::ProcessMsg(MsgAudioPcm* /*aMsg*/)
{
    //LOG(kMedia, ">MsgProcessor::ProcessMsgAudioPcm\n");
    ASSERTS();
    return nullptr;
}
Msg* MsgProcessor::ProcessMsg(MsgAudioDsd* /*aMsg*/)
{
    //LOG(kMedia, ">MsgProcessor::ProcessMsgAudioPcm\n");
    ASSERTS();
    return nullptr;
}
Msg* MsgProcessor::ProcessMsg(MsgSilence* /*aMsg*/)
{
    //LOG(kMedia, ">MsgProcessor::ProcessMsgSilence\n");
    ASSERTS();
    return nullptr;
}
Msg* MsgProcessor::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    //LOG(kMedia, ">MsgProcessor::ProcessMsgPlayable\n");
    ASSERTS();
    return nullptr;
}
Msg* MsgProcessor::ProcessMsg(MsgQuit* aMsg)
{
    //LOG(kMedia, ">MsgProcessor::ProcessMsgQuit\n");
    aMsg->RemoveRef();
    iSem.Signal();
    return nullptr;
}


// SuiteCodecStream

const Brn SuiteCodecStream::kPrefixHttp("http://");;
const TUint SuiteCodecStream::kLenPrefixHttp = sizeof("http://")-1;
const TUint SuiteCodecStream::kMaxUriPathBytes = 32;
const TUint SuiteCodecStream::kMaxUriBytes = Endpoint::kMaxEndpointBytes + kLenPrefixHttp + kMaxUriPathBytes;

SuiteCodecStream::SuiteCodecStream(std::vector<AudioFileDescriptor>& aFiles, Environment& aEnv, CreateTestCodecPipelineFunc aFunc, const Uri& aUri)
    : SuiteUnitTest("Codec stream tests")
    , MsgProcessor(iSem)
    , iJiffies(0)
    , iEnv(aEnv)
    , iUri(aUri)
    , iFileLocation(iUri.AbsoluteUri().Bytes()+kMaxFilenameLen)
    , iSem("TCO1", 0)
    , iPipeline(nullptr)
    , iFiles(aFiles)
    , iFileNum(0)
    , iCreatePipeline(aFunc)
{
    for (auto it = iFiles.begin(); it != iFiles.end(); ++it) {
        AddTest(MakeFunctor(*this, &SuiteCodecStream::TestJiffies));
    }
}

SuiteCodecStream::SuiteCodecStream(const TChar* aSuiteName, std::vector<AudioFileDescriptor>& aFiles, Environment& aEnv, CreateTestCodecPipelineFunc aFunc, const Uri& aUri)
    : SuiteUnitTest(aSuiteName)
    , MsgProcessor(iSem)
    , iJiffies(0)
    , iEnv(aEnv)
    , iUri(aUri)
    , iFileLocation(iUri.AbsoluteUri().Bytes()+kMaxFilenameLen)
    , iSem("TCO1", 0)
    , iPipeline(nullptr)
    , iFiles(aFiles)
    , iFileNum(0)
    , iCreatePipeline(aFunc)
{
}

SuiteCodecStream::~SuiteCodecStream()
{
}

void SuiteCodecStream::Setup()
{
    iJiffies = 0;

    iPipeline = (*iCreatePipeline)(iEnv, *this);
    iPipeline->StartPipeline();
}

void SuiteCodecStream::TearDown()
{
    delete iPipeline;
}

Msg* SuiteCodecStream::ProcessMsg(MsgAudioPcm* aMsg)
{
    iJiffies += aMsg->Jiffies();
    //LOG(kMedia, "iJiffies: %llu\n", iJiffies);
    return aMsg;
}

Brx* SuiteCodecStream::StartStreaming(const Brx& aTestName, const Brx& aFilename)
{
    // Try streaming a full file.
    Log::Print(aTestName);
    Log::Print(": ");
    Log::Print(aFilename);
    Log::Print("\n");

    ASSERT(aFilename.Bytes() <= kMaxFilenameLen);
    Bwh* fileLocation = new Bwh(iUri.AbsoluteUri().Bytes() + aFilename.Bytes() + 1);
    fileLocation->Replace(iUri.AbsoluteUri());
    if ((*fileLocation)[fileLocation->Bytes() - 1] != '/') {
        fileLocation->Append("/");
    }
    fileLocation->Append(aFilename);
    iPipeline->StartStreaming(*fileLocation);
    return fileLocation;
}

void SuiteCodecStream::TestJiffies()
{
    Brn filename(iFiles[iFileNum].Filename());
    TUint64 jiffies = iFiles[iFileNum].Jiffies();
    iFileNum++;

    Brx* fileLocation = StartStreaming(Brn("SuiteCodecStream"), filename);
    iSem.Wait();
    delete fileLocation;

    //LOG(kMedia, "iJiffies: %llu, track jiffies: %llu\n", iJiffies, jiffies);
    Log::Print("iJiffies: %llu, track jiffies: %llu\n", iJiffies, jiffies);
    TEST(iJiffies == jiffies);
}


// SuiteCodecSeek

SuiteCodecSeek::SuiteCodecSeek(std::vector<AudioFileDescriptor>& aFiles, Environment& aEnv, CreateTestCodecPipelineFunc aFunc, const Uri& aUri)
    : SuiteCodecStream("Codec seek tests", aFiles, aEnv, aFunc, aUri)
    , iSeek(true)
    , iSeekPos(0)
    , iSeekSuccess(false)
    , iFileNumStart(0)
    , iFileNumEnd(0)
    , iFileNumBeyondEnd(0)
    , iFileNumBack(0)
    , iFileNumForward(0)
{
    for (auto it = iFiles.begin(); it != iFiles.end(); ++it) {
        AddTest(MakeFunctor(*this, &SuiteCodecSeek::TestSeekingToStart));
        AddTest(MakeFunctor(*this, &SuiteCodecSeek::TestSeekingToEnd));
        AddTest(MakeFunctor(*this, &SuiteCodecSeek::TestSeekingBeyondEnd));
        AddTest(MakeFunctor(*this, &SuiteCodecSeek::TestSeekingBackwards));
        AddTest(MakeFunctor(*this, &SuiteCodecSeek::TestSeekingForwards));
    }
}

SuiteCodecSeek::SuiteCodecSeek(const TChar* aSuiteName, std::vector<AudioFileDescriptor>& aFiles, Environment& aEnv, CreateTestCodecPipelineFunc aFunc, const Uri& aUri)
    : SuiteCodecStream(aSuiteName, aFiles, aEnv, aFunc, aUri)
    , iSeek(true)
    , iSeekPos(0)
    , iSeekSuccess(false)
    , iFileNumStart(0)
    , iFileNumEnd(0)
    , iFileNumBeyondEnd(0)
    , iFileNumBack(0)
    , iFileNumForward(0)
{
}

SuiteCodecSeek::~SuiteCodecSeek()
{
}

Msg* SuiteCodecSeek::ProcessMsg(MsgAudioPcm* aMsg)
{
    aMsg = (MsgAudioPcm*) SuiteCodecStream::ProcessMsg(aMsg);
    if (iSeek && (iJiffies >= iTotalJiffies/2)) {
        iSeekSuccess = iPipeline->SeekCurrentTrack(iSeekPos, *this, iHandle);
        iSeek = false;
        iSemSeek->Signal();
    }
    return aMsg;
}

void SuiteCodecSeek::NotifySeekComplete(TUint aHandle, TUint aFlushId)
{
    iSemSeek->Wait(kSemWaitMs);
    TEST(iHandle == aHandle);
    if (iSeekSuccess) {
        // Synchronous part of seek succeeded. Check asynchronous part.
        if (aFlushId == MsgFlush::kIdInvalid) {
            // Asynchronous part of seek failed.
            iSeekSuccess = false;
        }
    }
}

void SuiteCodecSeek::Setup()
{
    SuiteCodecStream::Setup();
    iSeek = true;
    iSeekSuccess = false;
    iSemSeek = new Semaphore("SCSS", 0);
    iHandle = ISeeker::kHandleError;
    iTotalJiffies = 0;
}

void SuiteCodecSeek::TearDown()
{
    delete iSemSeek;
    SuiteCodecStream::TearDown();
}

TUint64 SuiteCodecSeek::ExpectedJiffies(TUint64 aJiffiesTotal, TUint64 aSeekStartJiffies, TUint aSeekPosSeconds)
{
    TUint64 jiffies = aSeekStartJiffies + (aJiffiesTotal-aSeekPosSeconds*Jiffies::kPerSecond);
    return jiffies;
}

void SuiteCodecSeek::TestSeeking(TUint64 aDurationJiffies, TUint64 aSeekPosJiffies, TUint aCodec, TBool aSeekable)
{
    // Try seeking forward to end of file.
    TUint seekPosSeconds = static_cast<TUint>(aSeekPosJiffies/Jiffies::kPerSecond);
    iSeekPos = seekPosSeconds;
    iSem.Wait();

    if (aSeekable) {
        TUint64 expectedJiffies = ExpectedJiffies(aDurationJiffies, aDurationJiffies/2, seekPosSeconds);
        //LOG(kMedia, "iJiffies: %llu, expectedJiffies: %llu\n", iJiffies, expectedJiffies);
        //Log::Print("iJiffies: %llu, expectedJiffies: %llu\n", iJiffies, expectedJiffies);
        TEST(iSeekSuccess);

        if (aCodec != AudioFileDescriptor::kCodecVorbis) {
            // Vorbis seeking is isn't particularly accurate

            // Seeking isn't entirely accurate, so check within a bounded range of +/- 1 second.
            TEST(iJiffies >= expectedJiffies - Jiffies::kPerSecond);   // Lower bound.
            TEST(iJiffies <= expectedJiffies + Jiffies::kPerSecond);   // Upper bound.
        }
    }
    else {
        // Ignore poor Vorbis seek implementation.
        if (aCodec != AudioFileDescriptor::kCodecVorbis) {
            TEST(!iSeekSuccess);
            TEST(iJiffies == aDurationJiffies);
        }
    }
}

void SuiteCodecSeek::TestSeekingToStart()
{
    Brn filename(iFiles[iFileNumStart].Filename());
    TUint codec = iFiles[iFileNumStart].Codec();
    TBool seekable = iFiles[iFileNumStart].Seekable();
    iTotalJiffies = iFiles[iFileNumStart].Jiffies();
    iFileNumStart++;

    Brx* fileLocation = StartStreaming(Brn("SuiteCodecSeek seeking to start"), filename);
    TestSeeking(iTotalJiffies, 0, codec, seekable);
    delete fileLocation;
}

void SuiteCodecSeek::TestSeekingToEnd()
{
    Brn filename(iFiles[iFileNumEnd].Filename());
    TUint codec = iFiles[iFileNumEnd].Codec();
    TBool seekable = iFiles[iFileNumEnd].Seekable();
    iTotalJiffies = iFiles[iFileNumEnd].Jiffies();
    iFileNumEnd++;

    Brx* fileLocation = StartStreaming(Brn("SuiteCodecSeek seeking to end"), filename);
    // Seek to last playable second.
    TestSeeking(iTotalJiffies, iTotalJiffies-Jiffies::kPerSecond, codec, seekable);
    delete fileLocation;
}

void SuiteCodecSeek::TestSeekingBeyondEnd()
{
    Brn filename(iFiles[iFileNumBeyondEnd].Filename());
    TUint codec = iFiles[iFileNumBeyondEnd].Codec();
    iTotalJiffies = iFiles[iFileNumBeyondEnd].Jiffies();
    iFileNumBeyondEnd++;

    Brx* fileLocation = StartStreaming(Brn("SuiteCodecSeek seeking beyond end"), filename);
    // Seek to 1s beyond end of file.
    TestSeeking(iTotalJiffies, iTotalJiffies+Jiffies::kPerSecond, codec, false);
    delete fileLocation;
}

void SuiteCodecSeek::TestSeekingBackwards()
{
    Brn filename(iFiles[iFileNumBack].Filename());
    TUint codec = iFiles[iFileNumBack].Codec();
    TBool seekable = iFiles[iFileNumBack].Seekable();
    iTotalJiffies = iFiles[iFileNumBack].Jiffies();
    iFileNumBack++;

    Brx* fileLocation = StartStreaming(Brn("SuiteCodecSeek seeking backwards"), filename);
    TestSeeking(iTotalJiffies, iTotalJiffies/4, codec, seekable);
    delete fileLocation;
}

void SuiteCodecSeek::TestSeekingForwards()
{
    Brn filename(iFiles[iFileNumForward].Filename());
    TUint codec = iFiles[iFileNumForward].Codec();
    TBool seekable = iFiles[iFileNumForward].Seekable();
    iTotalJiffies = iFiles[iFileNumForward].Jiffies();
    iFileNumForward++;

    Brx* fileLocation = StartStreaming(Brn("SuiteCodecSeek seeking forwards"), filename);
    TestSeeking(iTotalJiffies, iTotalJiffies - iTotalJiffies/4, codec, seekable);
    delete fileLocation;
}


// SuiteCodecSeekFromStart

SuiteCodecSeekFromStart::SuiteCodecSeekFromStart(std::vector<AudioFileDescriptor>& aFiles, Environment& aEnv, CreateTestCodecPipelineFunc aFunc, const Uri& aUri)
    : SuiteCodecSeek("Codec seek from start tests", aFiles, aEnv, aFunc, aUri)
    , iFileNumMiddle(0)
    , iFileNumEnd(0)
    , iFileNumBeyondEnd(0)
{
    for (auto it = iFiles.begin(); it != iFiles.end(); ++it) {
        AddTest(MakeFunctor(*this, &SuiteCodecSeekFromStart::TestSeekingToMiddle));
        AddTest(MakeFunctor(*this, &SuiteCodecSeekFromStart::TestSeekingToEnd));
        AddTest(MakeFunctor(*this, &SuiteCodecSeekFromStart::TestSeekingBeyondEnd));
    }
}

SuiteCodecSeekFromStart::~SuiteCodecSeekFromStart()
{
}

Msg* SuiteCodecSeekFromStart::ProcessMsg(MsgAudioPcm* aMsg)
{
    aMsg = (MsgAudioPcm*) SuiteCodecStream::ProcessMsg(aMsg);
    if (iSeek) {
        iSeekSuccess = iPipeline->SeekCurrentTrack(iSeekPos, *this, iHandle);
        iSemSeek->Signal();
        iSeek = false;
    }
    return aMsg;
}

void SuiteCodecSeekFromStart::TestSeekingFromStart(TUint64 aDurationJiffies, TUint64 aSeekPosJiffies, TUint aCodec, TBool aSeekable)
{
    TUint seekPosSeconds = static_cast<TUint>(aSeekPosJiffies/Jiffies::kPerSecond);
    iSeekPos = seekPosSeconds;
    iSem.Wait();
    if (aSeekable) {
        TUint64 expectedJiffies = ExpectedJiffies(aDurationJiffies, 0, seekPosSeconds);
        //LOG(kMedia, "iJiffies: %llu, expectedJiffies: %llu\n", iJiffies, expectedJiffies);
        //Log::Print("iJiffies: %llu, expectedJiffies: %llu\n", iJiffies, expectedJiffies);
        TEST(iSeekSuccess);

        if (aCodec != AudioFileDescriptor::kCodecVorbis) {
            // Vorbis seeking isn't particularly accurate

            // Seeking isn't entirely accurate, so check within a bounded range of +/- 1 second.
            TEST(iJiffies >= 0);   // Lower bound.
            TEST(iJiffies <= expectedJiffies + Jiffies::kPerSecond);   // Upper bound.
        }
    }
    else {
        // Ignore poor Vorbis seek implementation.
        if (aCodec != AudioFileDescriptor::kCodecVorbis) {
            TEST(!iSeekSuccess);
            TEST(iJiffies == aDurationJiffies);
        }
    }
}

void SuiteCodecSeekFromStart::TestSeekingToMiddle()
{
    Brn filename(iFiles[iFileNumMiddle].Filename());
    TUint codec = iFiles[iFileNumMiddle].Codec();
    TBool seekable = iFiles[iFileNumMiddle].Seekable();
    iTotalJiffies = iFiles[iFileNumMiddle].Jiffies();
    iFileNumMiddle++;

    Brx* fileLocation = StartStreaming(Brn("SuiteCodecSeekFromStart seeking to middle"), filename);
    TestSeekingFromStart(iTotalJiffies, iTotalJiffies/2, codec, seekable);
    delete fileLocation;
}

void SuiteCodecSeekFromStart::TestSeekingToEnd()
{
    Brn filename(iFiles[iFileNumEnd].Filename());
    TUint codec = iFiles[iFileNumEnd].Codec();
    TBool seekable = iFiles[iFileNumEnd].Seekable();
    iTotalJiffies = iFiles[iFileNumEnd].Jiffies();
    iFileNumEnd++;

    Brx* fileLocation = StartStreaming(Brn("SuiteCodecSeekFromStart seeking to end"), filename);
    // Seek to last playable second.
    TestSeekingFromStart(iTotalJiffies, iTotalJiffies-Jiffies::kPerSecond, codec, seekable);
    delete fileLocation;
}

void SuiteCodecSeekFromStart::TestSeekingBeyondEnd()
{
    Brn filename(iFiles[iFileNumBeyondEnd].Filename());
    TUint codec = iFiles[iFileNumBeyondEnd].Codec();
    iTotalJiffies = iFiles[iFileNumBeyondEnd].Jiffies();
    iFileNumBeyondEnd++;

    Brx* fileLocation = StartStreaming(Brn("SuiteCodecSeekFromStart seeking beyond end"), filename);
    // Seek to 1s beyond end of file.
    TestSeekingFromStart(iTotalJiffies, iTotalJiffies+Jiffies::kPerSecond, codec, false);
    delete fileLocation;
}


// SuiteCodecZeroCrossings

SuiteCodecZeroCrossings::SuiteCodecZeroCrossings(std::vector<AudioFileDescriptor>& aFiles, Environment& aEnv, CreateTestCodecPipelineFunc aFunc, const Uri& aUri)
    : SuiteCodecStream("Codec zero crossing tests", aFiles, aEnv, aFunc, aUri)
    , iSampleRate(0)
    , iBitDepth(0)
    , iChannels(0)
    , iBytesProcessed(0)
    , iLastSubsample(0)
    , iLastCrossingByte(0)
    , iZeroCrossings(0)
    , iUnacceptableCrossingDeltas(0)
    , iCodec(AudioFileDescriptor::kCodecUnknown)
    , iSeekable(false)
{
    for (auto it = iFiles.begin(); it != iFiles.end(); ++it) {
        AddTest(MakeFunctor(*this, &SuiteCodecZeroCrossings::TestZeroCrossings));
    }
}

SuiteCodecZeroCrossings::~SuiteCodecZeroCrossings()
{
}

void SuiteCodecZeroCrossings::Setup()
{
    SuiteCodecStream::Setup();
    iBytesProcessed = 0;
    iLastSubsample = 0;
    iLastCrossingByte = 0;
    iZeroCrossings = 0;
    iUnacceptableCrossingDeltas = 0;
}

void SuiteCodecZeroCrossings::TestCrossingDelta()
{
    const TUint bytesPerSample = (iBitDepth * iChannels) / 8;
    const TUint bytesPerSec = bytesPerSample * iSampleRate;
    const TUint bytesPerSine = bytesPerSec/SuiteCodecStream::kFrequencyHz;
    const TUint bytesPerCrossing = bytesPerSine/2;
    TUint byteDelta = iBytesProcessed - iLastCrossingByte;

    iZeroCrossings++;
    //LOG(kMedia, "byteDelta: %u, bytesPerCrossing: %u\n", byteDelta, bytesPerCrossing);
    if (iLastCrossingByte != 0 && byteDelta != bytesPerCrossing)
    {
        if (byteDelta < bytesPerCrossing-6 || byteDelta > bytesPerCrossing+6) {
            iUnacceptableCrossingDeltas++;
        }
    }
    iLastCrossingByte = iBytesProcessed;
}

Msg* SuiteCodecZeroCrossings::TestSimilarity(MsgAudioPcm* aMsg)
{
    //iLastSubsample = 0;
    MsgPlayable* msg = aMsg->CreatePlayable();
    TUint bytes = msg->Bytes();
    ProcessorPcmBufTest pcmProcessor;
    const TUint increment = (iBitDepth/8) * iChannels;

    msg->Read(pcmProcessor);
    const TByte* ptr = (TByte*)pcmProcessor.Ptr();

    // Measure how many times subsamples pass through zero.
    for (TUint i = 0; i < bytes; i += increment) {
        for (TUint j = 0; j < iChannels; j++) {
            TInt subsample = 0;

            switch (iBitDepth)
            {
            case 16:
                subsample = ((ptr[0] << 24) | (ptr[1] << 16)) >> 16;
                ptr += 2;
                break;
            case 24:
                subsample = ((ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8)) >> 8;
                ptr += 3;
                break;
            default:
                ASSERTS();
            }

            if (j == 0) { // Only do subsample comparison on a single channel.
                if (iLastSubsample >= 0 && subsample < 0) {
                    //iZeroCrossings++;
                    TestCrossingDelta();
                }
                else if (iLastSubsample <= 0 && subsample > 0) {
                    //iZeroCrossings++;
                    TestCrossingDelta();
                }
                iLastSubsample = subsample;
            }
            iBytesProcessed += iBitDepth/8;
        }
    }

    return msg;
}

Msg* SuiteCodecZeroCrossings::ProcessMsg(MsgDecodedStream* aMsg)
{
    const DecodedStreamInfo& info = aMsg->StreamInfo();
    TEST(info.BitDepth() == iBitDepth);
    TEST(info.SampleRate() == iSampleRate);
    TEST(info.NumChannels() == iChannels);
    //TEST(info.Seekable() == iSeekable);
    return aMsg;
}

Msg* SuiteCodecZeroCrossings::ProcessMsg(MsgAudioPcm* aMsg)
{
    aMsg = (MsgAudioPcm*) SuiteCodecStream::ProcessMsg(aMsg);
    Msg* msgOut = TestSimilarity(aMsg);
    msgOut->RemoveRef();
    msgOut = nullptr;
    return msgOut;
}

void SuiteCodecZeroCrossings::TestZeroCrossings()
{
    TUint timeStart;
    TUint timeEnd;
    Brn filename(iFiles[iFileNum].Filename());
    TUint64 jiffies = iFiles[iFileNum].Jiffies();
    iSampleRate = iFiles[iFileNum].SampleRate();
    iBitDepth = iFiles[iFileNum].BitDepth();
    iChannels = iFiles[iFileNum].Channels();
    iCodec = iFiles[iFileNum].Codec();
    iSeekable = iFiles[iFileNum].Seekable();
    iFileNum++;

    const TUint jiffiesPerSine = Jiffies::kPerSecond / SuiteCodecStream::kFrequencyHz;
    const TUint sineWaves = (TUint)jiffies/jiffiesPerSine;
    const TUint expectedZeroCrossings = sineWaves*2 - 1;

    timeStart = Time::Now(iEnv);
    Brx* fileLocation = StartStreaming(Brn("SuiteCodecZeroCrossings"), filename);
    iSem.Wait();
    delete fileLocation;

    timeEnd = Time::Now(iEnv);
    Log::Print("TestCodec ");
    Log::Print(filename);
    Log::Print(" start: %ums, end: %ums, duration: %us (%ums)\n", timeStart, timeEnd, (timeEnd-timeStart)/1000, timeEnd-timeStart);

    Log::Print("iJiffies: %llu, track jiffies: %llu\n", iJiffies, jiffies);
    TEST(iJiffies == jiffies);
    //LOG(kMedia, "iZeroCrossings: %u, expectedZeroCrossings: %u, iUnacceptableCrossingDeltas: %u\n", iZeroCrossings, expectedZeroCrossings, iUnacceptableCrossingDeltas);
    Log::Print("iZeroCrossings: %u, expectedZeroCrossings: %u, iUnacceptableCrossingDeltas: %u\n", iZeroCrossings, expectedZeroCrossings, iUnacceptableCrossingDeltas);
    TEST(iZeroCrossings >= expectedZeroCrossings-200);
    TEST(iZeroCrossings <= expectedZeroCrossings+200);
    // Test that less than 2% of the zero crossings have an unnaceptable spacing.
    TEST(iUnacceptableCrossingDeltas < expectedZeroCrossings/100);
}


// SuiteCodecInvalidType

SuiteCodecInvalidType::SuiteCodecInvalidType(std::vector<AudioFileDescriptor>& aFiles, Environment& aEnv, CreateTestCodecPipelineFunc aFunc, const Uri& aUri)
    : SuiteCodecStream("Codec invalid type tests", aFiles, aEnv, aFunc, aUri)
{
    for (auto it = iFiles.begin(); it != iFiles.end(); ++it) {
        AddTest(MakeFunctor(*this, &SuiteCodecInvalidType::TestInvalidType));
    }
}

SuiteCodecInvalidType::~SuiteCodecInvalidType()
{
}

void SuiteCodecInvalidType::TestInvalidType()
{
    Brn filename(iFiles[iFileNum].Filename());
    TUint64 jiffies = iFiles[iFileNum].Jiffies();
    iFileNum++;

    Brx* fileLocation = StartStreaming(Brn("SuiteCodecInvalidType"), filename);
    iSem.Wait();
    delete fileLocation;

    LOG(kMedia, "iJiffies: %llu, kTotalJiffies: %llu\n", iJiffies, jiffies);
    //Log::Print("iJiffies: %llu, kTotalJiffies: %llu\n", iJiffies, jiffies);
    TEST(iJiffies == 0); // If we don't exit cleanly and with 0 jiffies of output audio, something is misbehaving.
}


void TestCodec(Environment& aEnv, CreateTestCodecPipelineFunc aFunc, GetTestFiles aFileFunc, const std::vector<Brn>& aArgs)
{
    Log::Print("TestCodec\n");

    OptionParser parser;
    OptionString optionServer("-s", "--server", Brn("localhost"), "address of server to connect to");
    parser.AddOption(&optionServer);
    OptionUint optionPort("-p", "--port", 80, "server port to connect on");
    parser.AddOption(&optionPort);
    OptionString optionPath("", "--path", Brn(""), "path to use on server");
    parser.AddOption(&optionPath);
    OptionString optionTestType("-t", "--type", Brn("full"), "type of test (quick | full)");
    parser.AddOption(&optionTestType);
    if (!parser.Parse(aArgs) || parser.HelpDisplayed()) {
        return;
    }
    ASSERT(optionPort.Value() <= 65535);

    Environment::ELoopback loopback;
    if (optionServer.Value().Equals(Brn("127.0.0.1"))) { // using loopback
        loopback = Environment::ELoopbackUse;
    }
    else {
        loopback = Environment::ELoopbackExclude;
    }
    std::vector<NetworkAdapter*>* ifs = Os::NetworkListAdapters(aEnv, loopback, false/*no ipv6*/, "TestCodec");
    ASSERT(ifs->size() > 0);
    TIpAddress addr = (*ifs)[0]->Address(); // assume we are only on one subnet (or using loopback)
    for (TUint i=0; i<ifs->size(); i++) {
        TIpAddress addrTmp = (*ifs)[i]->Address();
        Endpoint endpt(optionPort.Value(), addrTmp);
        Endpoint::AddressBuf buf;
        endpt.AppendAddress(buf);
        (*ifs)[i]->RemoveRef("TestCodec");
    }
    delete ifs;

    Endpoint endptClient(0, addr);
    Endpoint::AddressBuf buf;
    endptClient.AppendAddress(buf);
    Log::Print("Using network interface %s\n", buf.Ptr());

    // set up server uri
    Endpoint endptServer = Endpoint(optionPort.Value(), optionServer.Value());
    Bws<SuiteCodecStream::kMaxUriBytes> uriBuf;
    uriBuf.Append(SuiteCodecStream::kPrefixHttp);
    endptServer.AppendEndpoint(uriBuf);
    uriBuf.Append("/");
    uriBuf.Append(optionPath.Value());
    Uri uri(uriBuf);
    Log::Print("Connecting to server: %.*s\n", PBUF(uri.AbsoluteUri()));

    // set test type
    TBool testFull = true;
    if (optionTestType.Value() == Brn("quick")) {
        testFull = false;
    }

    // set up bare minimum files (and include extra files if full test being run)
    AudioFileCollection* files = (*aFileFunc)();
    std::vector<AudioFileDescriptor> stdFiles(files->RequiredFiles());
    if (testFull) {
        for(auto it = files->ExtraFiles().begin(); it != files->ExtraFiles().end(); ++it) {
            stdFiles.push_back(*it);
        }
    }

    Runner runner("Codec tests\n");
    runner.Add(new SuiteCodecZeroCrossings(stdFiles, aEnv, aFunc, uri));
    if (testFull) {
        //runner.Add(new SuiteCodecStream(stdFiles, aEnv, aFunc, uri));    // now done as part of SuiteCodecZeroCrossings to speed things up
        runner.Add(new SuiteCodecSeek(stdFiles, aEnv, aFunc, uri));
        runner.Add(new SuiteCodecSeekFromStart(stdFiles, aEnv, aFunc, uri));
        runner.Add(new SuiteCodecInvalidType(files->InvalidFiles(), aEnv, aFunc, uri));
        runner.Add(new SuiteCodecStream(files->StreamOnlyFiles(), aEnv, aFunc, uri));
    }
    runner.Run();

    delete files;
}

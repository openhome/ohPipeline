#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Pipeline/RampArray.h>
#include <OpenHome/Media/Utils/AllocatorInfoLogger.h>
#include <OpenHome/Media/Utils/ProcessorAudioUtils.h>

#include <string.h>
#include <vector>

using namespace OpenHome;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Media;

namespace OpenHome {
namespace Media {

class SuiteAllocator : public Suite
{
public:
    SuiteAllocator();
    void Test() override;
private:
    static const TUint kNumTestCells = 10;
    AllocatorInfoLogger iInfoAggregator;
};

class TestCell : public Allocated
{
public:
    TestCell(AllocatorBase& aAllocator);
    void Fill(TChar aVal);
    void CheckIsFilled(TChar aVal) const;
private:
    static const TUint kNumBytes = 10;
    TChar iBytes[kNumBytes];
};

class SuiteMsgAudioEncoded : public Suite
{
    static const TUint kMsgCount = 8;
public:
    SuiteMsgAudioEncoded();
    ~SuiteMsgAudioEncoded();
    void Test() override;
private:
    MsgFactory* iMsgFactory;
    AllocatorInfoLogger iInfoAggregator;
};

class BufferObserver : public IPipelineBufferObserver
{
public:
    BufferObserver() { Reset(); }
    void Reset() { iSize = iNumCalls = 0; }
    TUint Size() const { return iSize; }
    TUint NumCalls() const { return iNumCalls; }
private: // from IPipelineBufferObserver
    void Update(TInt aDelta) override { iSize += aDelta; iNumCalls++; }
private:
    TInt iSize;
    TUint iNumCalls;
};

class SuiteMsgAudio : public Suite
{
    static const TUint kMsgCount = 8;
public:
    SuiteMsgAudio();
    ~SuiteMsgAudio();
    void Test() override;
private:
    MsgFactory* iMsgFactory;
    AllocatorInfoLogger iInfoAggregator;
};

class SuiteMsgPlayable : public Suite
{
    static const TUint kMsgCount = 2;
public:
    SuiteMsgPlayable();
    ~SuiteMsgPlayable();
    void Test() override;
private:
    void ValidateSilence(MsgPlayable* aMsg);
private:
    MsgFactory* iMsgFactory;
    AllocatorInfoLogger iInfoAggregator;
};

class SuiteMsgAudioDsd : public Suite
{
    static const TUint kMsgCount = 8;
public:
    SuiteMsgAudioDsd();
    ~SuiteMsgAudioDsd();
    void Test() override;
private:
    MsgFactory* iMsgFactory;
    AllocatorInfoLogger iInfoAggregator;
};

class SuiteRamp : public Suite
{
    static const TUint kMsgCount = 8;
public:
    SuiteRamp();
    ~SuiteRamp();
    void Test() override;
private:
    MsgFactory* iMsgFactory;
    AllocatorInfoLogger iInfoAggregator;
};

class SuiteAudioStream : public Suite
{
    static const TUint kMsgEncodedStreamCount = 1;
public:
    SuiteAudioStream();
    ~SuiteAudioStream();
    void Test() override;
private:
    MsgFactory* iMsgFactory;
    AllocatorInfoLogger iInfoAggregator;
};

class SuiteMetaText : public Suite
{
    static const TUint kMsgMetaTextCount = 1;
public:
    SuiteMetaText();
    ~SuiteMetaText();
    void Test() override;
private:
    MsgFactory* iMsgFactory;
    AllocatorInfoLogger iInfoAggregator;
};

class SuiteTrack : public Suite
{
    static const TUint kMsgTrackCount = 1;
public:
    SuiteTrack();
    ~SuiteTrack();
    void Test() override;
private:
    MsgFactory* iMsgFactory;
    TrackFactory* iTrackFactory;
    AllocatorInfoLogger iInfoAggregator;
};

class SuiteFlush : public Suite
{
    static const TUint kMsgFlushCount = 1;
public:
    SuiteFlush();
    ~SuiteFlush();
    void Test() override;
private:
    MsgFactory* iMsgFactory;
    AllocatorInfoLogger iInfoAggregator;
};

class SuiteHalt : public Suite
{
    static const TUint kMsgHaltCount = 1;
public:
    SuiteHalt();
    ~SuiteHalt();
    void Test() override;
private:
    void Halted();
private:
    MsgFactory* iMsgFactory;
    AllocatorInfoLogger iInfoAggregator;
    TUint iHaltedCount;
};

class SuiteMode : public Suite
{
    static const TUint kMsgModeCount = 1;
public:
    SuiteMode();
    ~SuiteMode();
    void Test() override;
private:
    MsgFactory* iMsgFactory;
    AllocatorInfoLogger iInfoAggregator;
};

class SuiteDelay : public Suite
{
    static const TUint kMsgDelayCount = 1;
public:
    SuiteDelay();
    ~SuiteDelay();
    void Test() override;
private:
    MsgFactory* iMsgFactory;
    AllocatorInfoLogger iInfoAggregator;
};

class SuiteDecodedStream : public Suite, private IStreamHandler
{
    static const TUint kMsgDecodedStreamCount = 1;
public:
    SuiteDecodedStream();
    ~SuiteDecodedStream();
    void Test() override;
private: // from IStreamHandler
    EStreamPlay OkToPlay(TUint aStreamId) override;
    TUint TrySeek(TUint aStreamId, TUint64 aOffset) override;
    TUint TryDiscard(TUint aJiffies) override;
    TUint TryStop(TUint aStreamId) override;
    void NotifyStarving(const Brx& aMode, TUint aStreamId, TBool aStarving) override;
private:
    MsgFactory* iMsgFactory;
    AllocatorInfoLogger iInfoAggregator;
};

class SuiteMsgProcessor : public Suite
{
public:
    SuiteMsgProcessor();
    ~SuiteMsgProcessor();
    void Test() override;
private:
    MsgFactory* iMsgFactory;
    TrackFactory* iTrackFactory;
    AllocatorInfoLogger iInfoAggregator;
};

class ProcessorMsgType : public IMsgProcessor
{
public:
    enum EMsgType
    {
        ENone
       ,EMsgMode
       ,EMsgTrack
       ,EMsgDrain
       ,EMsgDelay
       ,EMsgEncodedStream
       ,EMsgStreamSegment
       ,EMsgAudioEncoded
       ,EMsgMetaText
       ,EMsgStreamInterrupted
       ,EMsgHalt
       ,EMsgFlush
       ,EMsgWait
       ,EMsgDecodedStream
       ,EMsgAudioPcm
       ,EMsgAudioDsd
       ,EMsgSilence
       ,EMsgPlayable
       ,EMsgQuit
    };
public:
    ProcessorMsgType();
    EMsgType LastMsgType() const;
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
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgAudioDsd* aMsg) override;
    Msg* ProcessMsg(MsgSilence* aMsg) override;
    Msg* ProcessMsg(MsgPlayable* aMsg) override;
    Msg* ProcessMsg(MsgQuit* aMsg) override;
private:
    EMsgType iLastMsgType;
};

class SuiteMsgQueue : public Suite
{
public:
    SuiteMsgQueue();
    ~SuiteMsgQueue();
    void Test() override;
private:
    MsgFactory* iMsgFactory;
    TrackFactory* iTrackFactory;
    AllocatorInfoLogger iInfoAggregator;
};

class SuiteMsgQueueLite : public Suite
{
public:
    SuiteMsgQueueLite();
    ~SuiteMsgQueueLite();
    void Test() override;
private:
    MsgFactory* iMsgFactory;
    TrackFactory* iTrackFactory;
    AllocatorInfoLogger iInfoAggregator;
};

class SuiteMsgReservoir : public Suite
{
    static const TUint kMsgCount = 8;
public:
    SuiteMsgReservoir();
    ~SuiteMsgReservoir();
    void Test() override;
private:
    MsgFactory* iMsgFactory;
    TrackFactory* iTrackFactory;
    AllocatorInfoLogger iInfoAggregator;
};

class TestMsgReservoir : public MsgReservoir
{
public:
    enum EMsgType
    {
        ENone
       ,EMsgAudioPcm
       ,EMsgAudioDsd
       ,EMsgSilence
       ,EMsgMode
       ,EMsgTrack
       ,EMsgDrain
       ,EMsgDelay
       ,EMsgEncodedStream
       ,EMsgStreamSegment
       ,EMsgDecodedStream
       ,EMsgMetaText
       ,EMsgStreamInterrupted
       ,EMsgHalt
       ,EMsgFlush
       ,EMsgWait
       ,EMsgQuit
    };
public:
    TestMsgReservoir();
    void Enqueue(Msg* aMsg)          { DoEnqueue(aMsg); }
    Msg* Dequeue()                   { return DoDequeue(); }
    void EnqueueAtHead(Msg* aMsg)    { MsgReservoir::EnqueueAtHead(aMsg); }
    TUint Jiffies() const            { return MsgReservoir::Jiffies(); }
    TUint EncodedStreamCount() const { return MsgReservoir::EncodedStreamCount(); }
    TUint DecodedStreamCount() const { return MsgReservoir::DecodedStreamCount(); }
    TUint DelayCount() const         { return MsgReservoir::DelayCount(); }
    TUint MetaTextCount() const      { return MsgReservoir::MetaTextCount(); }
    EMsgType LastIn() const          { return iLastMsgIn; }
    EMsgType LastOut() const         { return iLastMsgOut; }
    void SplitNextAudio()            { iSplitNextAudio = true; }
private:
    Msg* ProcessMsgAudioOut(MsgAudio* aMsgAudio);
private: // from MsgQueueFlushable
    void ProcessMsgIn(MsgAudioPcm* aMsg) override;
    void ProcessMsgIn(MsgAudioDsd* aMsg) override;
    void ProcessMsgIn(MsgSilence* aMsg) override;
    void ProcessMsgIn(MsgMode* aMsg) override;
    void ProcessMsgIn(MsgTrack* aMsg) override;
    void ProcessMsgIn(MsgDrain* aMsg) override;
    void ProcessMsgIn(MsgDelay* aMsg) override;
    void ProcessMsgIn(MsgEncodedStream* aMsg) override;
    void ProcessMsgIn(MsgStreamSegment* aMsg) override;
    void ProcessMsgIn(MsgDecodedStream* aMsg) override;
    void ProcessMsgIn(MsgMetaText* aMsg) override;
    void ProcessMsgIn(MsgStreamInterrupted* aMsg) override;
    void ProcessMsgIn(MsgHalt* aMsg) override;
    void ProcessMsgIn(MsgFlush* aMsg) override;
    void ProcessMsgIn(MsgWait* aMsg) override;
    void ProcessMsgIn(MsgQuit* aMsg) override;
    Msg* ProcessMsgOut(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsgOut(MsgAudioDsd* aMsg) override;
    Msg* ProcessMsgOut(MsgSilence* aMsg) override;
    Msg* ProcessMsgOut(MsgMode* aMsg) override;
    Msg* ProcessMsgOut(MsgTrack* aMsg) override;
    Msg* ProcessMsgOut(MsgDrain* aMsg) override;
    Msg* ProcessMsgOut(MsgDelay* aMsg) override;
    Msg* ProcessMsgOut(MsgEncodedStream* aMsg) override;
    Msg* ProcessMsgOut(MsgStreamSegment* aMsg) override;
    Msg* ProcessMsgOut(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsgOut(MsgMetaText* aMsg) override;
    Msg* ProcessMsgOut(MsgStreamInterrupted* aMsg) override;
    Msg* ProcessMsgOut(MsgHalt* aMsg) override;
    Msg* ProcessMsgOut(MsgFlush* aMsg) override;
    Msg* ProcessMsgOut(MsgWait* aMsg) override;
    Msg* ProcessMsgOut(MsgQuit* aMsg) override;
private:
    EMsgType iLastMsgIn;
    EMsgType iLastMsgOut;
    TBool iSplitNextAudio;
};

class DummyElement : public PipelineElement, private INonCopyable
{
public:
    DummyElement(TUint aSupported);
    void Process(Msg* aMsg);
};

class SuitePipelineElement : public Suite
{
public:
    SuitePipelineElement();
    ~SuitePipelineElement();
    void Test() override;
private:
    Msg* CreateMsg(ProcessorMsgType::EMsgType aType);
private:
    MsgFactory* iMsgFactory;
    TrackFactory* iTrackFactory;
    AllocatorInfoLogger iInfoAggregator;
};

} // namespace Media
} // namespace OpenHome


// TestCell

TestCell::TestCell(AllocatorBase& aAllocator)
    : Allocated(aAllocator)
{
    Fill((TByte)0xff);
}

void TestCell::Fill(TChar aVal)
{
    memset(&iBytes[0], aVal, kNumBytes);
}

void TestCell::CheckIsFilled(TChar aVal) const
{
    for (TUint i=0; i<kNumBytes; i++) {
        TEST(iBytes[i] == aVal);
    }
}


// SuiteAllocator

SuiteAllocator::SuiteAllocator()
    : Suite("Allocator tests")
{
}

void SuiteAllocator::Test()
{
    //Print("\nCreate Allocator with 10 TestCells.  Check that 10 TestCells can be allocated\n");
    Allocator<TestCell>* allocator = new Allocator<TestCell>("TestCell", kNumTestCells, iInfoAggregator);
    TestCell* cells[kNumTestCells];
    for (TUint i=0; i<kNumTestCells; i++) {
        cells[i] = allocator->Allocate();
        TEST(cells[i] != nullptr);
    }

    //Print("\nalloc 11th TestCell.  Check this throws\n");
    // currently disabled until allocator throws rather than blocking when full
    //TEST_THROWS(allocator->Allocate(), AllocatorNoMemory);

    //Print("\nuse InfoProvider.  Visually check results\n");
    iInfoAggregator.PrintStats();

    //Print("\nmemset each TestCell to a different value, check that all can be read back (so TestCells don't overlap)\n");
    for (TUint i=0; i<kNumTestCells; i++) {
        cells[i]->Fill((TByte)i);
    }
    for (TUint i=0; i<kNumTestCells; i++) {
        cells[i]->CheckIsFilled((TByte)i);
    }

    //Print("\nfree all TestCells.  Check values of iTestCellsUsed and iTestCellsUsedMax\n");
    TEST(allocator->CellsUsed() == kNumTestCells);
    TEST(allocator->CellsUsedMax() == kNumTestCells);
    for (TUint i=0; i<kNumTestCells; i++) {
        cells[i]->iRefCount--; // clear ref from Allocate() to avoid assertion if we re-Allocate() this object
        allocator->Free(cells[i]);
        TEST(allocator->CellsUsed() == kNumTestCells - i - 1);
        TEST(allocator->CellsUsedMax() == kNumTestCells);
    }

    //Print("\nreallocate all TestCells, confirming that freed TestCells can be reused\n");
    for (TUint i=0; i<kNumTestCells; i++) {
        cells[i] = allocator->Allocate();
        TEST(cells[i] != nullptr);
    }
    TEST(allocator->CellsUsed() == kNumTestCells);

    //Print("\nfree 9 of the 10 TestCells then delete allocator.  Check this asserts (due to the memory leak)\n");
    // disabled until Fifo is updated to enable this assert
    // ...even then we may not want the test if it causes valgrind failures
    /*for (TUint i=0; i<9; i++) {
        allocator->Free(cells[i]);
    }
    TEST(allocator->iTestCellsUsed == 1);
    TEST_THROWS(delete allocator, AssertionFailed);*/

    //Print("Free all cells; check that allocator can now be deleted.\n");
    for (TUint i=0; i<kNumTestCells; i++) {
        allocator->Free(cells[i]);
    }
    delete allocator;
}


// SuiteMsgAudioEncoded

SuiteMsgAudioEncoded::SuiteMsgAudioEncoded()
    : Suite("MsgAudioEncoded tests")
{
    MsgFactoryInitParams init;
    init.SetMsgAudioEncodedCount(kMsgCount, kMsgCount);
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
}

SuiteMsgAudioEncoded::~SuiteMsgAudioEncoded()
{
    delete iMsgFactory;
}

void SuiteMsgAudioEncoded::Test()
{
    // create msg, check it reports the correct number of bytes
    const TUint kNumBytes = 64;
    TByte data[kNumBytes];
    for (TUint i=0; i<sizeof(data)/sizeof(data[0]); i++) {
        data[i] = (TByte)i;
    }
    Brn buf(data, sizeof(data));
    MsgAudioEncoded* msg = iMsgFactory->CreateMsgAudioEncoded(buf);
    TEST(msg->Bytes() == buf.Bytes());

    // check that CopyTo outputs the expected data
    TByte output[128];
    msg->CopyTo(output);
    for (TUint i=0; i<msg->Bytes(); i++) {
        TEST(output[i] == buf[i]);
    }

    // split msg, check its two parts report the same number of bytes
    TUint totalSize = msg->Bytes();
    TUint splitPos = 49;
    MsgAudioEncoded* msg2 = msg->Split(splitPos);
    TEST(msg->Bytes() == splitPos);
    TEST(msg2->Bytes() == totalSize - splitPos);

    // check that each part outputs the expected data
    (void)memset(output, 0xde, sizeof(output));
    msg->CopyTo(output);
    for (TUint i=0; i<msg->Bytes(); i++) {
        TEST(output[i] == buf[i]);
    }
    (void)memset(output, 0xde, sizeof(output));
    msg2->CopyTo(output);
    for (TUint i=0; i<msg2->Bytes(); i++) {
        TEST(output[i] == buf[splitPos + i]);
    }
    msg->RemoveRef();
    msg2->RemoveRef();

    // create two msgs; add them together; check their size and output
    TByte data2[kNumBytes/2];
    for (TUint i=0; i<sizeof(data2)/sizeof(data2[0]); i++) {
        data2[i] = (TByte)(255 - i);
    }
    Brn buf2(data2, sizeof(data2));
    msg = iMsgFactory->CreateMsgAudioEncoded(buf);
    TUint msg1Size = msg->Bytes();
    msg2 = iMsgFactory->CreateMsgAudioEncoded(buf2);
    TUint msg2Size = msg2->Bytes();
    msg->Add(msg2);
    TEST(msg->Bytes() == msg1Size + msg2Size);
    (void)memset(output, 0xde, sizeof(output));
    msg->CopyTo(output);
    for (TUint i=0; i<msg->Bytes(); i++) {
        if (i < buf.Bytes()) {
            TEST(output[i] == buf[i]);
        }
        else {
            TEST(output[i] == buf2[i - buf.Bytes()]);
        }
    }

    // split in second msg; check size/output of both
    splitPos = 10;
    msg2 = msg->Split(msg1Size + splitPos);
    TEST(msg->Bytes() == msg1Size + splitPos);
    TEST(msg2->Bytes() == msg2Size - splitPos);
    (void)memset(output, 0xde, sizeof(output));
    msg->CopyTo(output);
    for (TUint i=0; i<msg->Bytes(); i++) {
        if (i < buf.Bytes()) {
            TEST(output[i] == buf[i]);
        }
        else {
            TEST(output[i] == buf2[i - buf.Bytes()]);
        }
    }
    (void)memset(output, 0xde, sizeof(output));
    msg2->CopyTo(output);
    for (TUint i=0; i<msg2->Bytes(); i++) {
        TEST(output[i] == buf2[i + splitPos]);
    }
    msg2->RemoveRef();

    // split first fragment inside first msg; check size/output of both
    msg1Size = msg->Bytes();
    msg2 = msg->Split(splitPos);
    TEST(msg->Bytes() == splitPos);
    TEST(msg2->Bytes() == msg1Size - splitPos);
    (void)memset(output, 0xde, sizeof(output));
    msg->CopyTo(output);
    for (TUint i=0; i<msg->Bytes(); i++) {
        TEST(output[i] == buf[i]);
    }
    msg->RemoveRef();
    (void)memset(output, 0xde, sizeof(output));
    msg2->CopyTo(output);
    for (TUint i=0; i<msg2->Bytes(); i++) {
        if (i < buf.Bytes() - splitPos) {
            TEST(output[i] == buf[i + splitPos]);
        }
        else {
            TEST(output[i] == buf2[i - buf.Bytes() + splitPos]);
        }
    }
    msg2->RemoveRef();

     // create chained msg, try split at various positions, including message boundaries
    msg = iMsgFactory->CreateMsgAudioEncoded(buf);
    msg1Size = msg->Bytes();
    msg2 = iMsgFactory->CreateMsgAudioEncoded(buf2);
    msg2Size = msg2->Bytes();
    msg->Add(msg2);
    TEST(msg->Bytes() == msg1Size + msg2Size);
    // try split at start of message
    TEST_THROWS(msg->Split(0), AssertionFailed);
    // try split at end of message
    TEST_THROWS(msg->Split(msg->Bytes()), AssertionFailed);
    // try split beyond end of message
    TEST_THROWS(msg->Split(msg->Bytes()+1), AssertionFailed);

    // try split at boundary between two messages
    splitPos = msg1Size;
    msg2 = msg->Split(splitPos);
    TEST(msg->Bytes() == msg1Size);
    TEST(msg2->Bytes() == msg2Size);
    (void)memset(output, 0xde, sizeof(output));
    msg->CopyTo(output);
    for (TUint i=0; i<msg->Bytes(); i++) {
        TEST(output[i] == buf[i]);
    }
    msg->RemoveRef();
    (void)memset(output, 0xde, sizeof(output));
    msg2->CopyTo(output);
    for (TUint i=0; i<msg2->Bytes(); i++) {
        if (i < buf.Bytes() - splitPos) {
            TEST(output[i] == buf[i + splitPos]);
        }
        else {
            TEST(output[i] == buf2[i - buf.Bytes() + splitPos]);
        }
    }
    msg2->RemoveRef();

    // try cloning a message, check size and output of both are same
    msg = iMsgFactory->CreateMsgAudioEncoded(buf);
    msg2 = msg->Clone();

    msg1Size = msg->Bytes();
    msg2Size = msg2->Bytes();
    TEST(msg1Size == msg2Size);

    (void)memset(output, 0xde, sizeof(output));
    msg->CopyTo(output);
    for (TUint i=0; i<msg->Bytes(); i++) {
        TEST(output[i] == buf[i]);
    }
    (void)memset(output, 0xde, sizeof(output));
    msg2->CopyTo(output);
    for (TUint i=0; i<msg2->Bytes(); i++) {
        TEST(output[i] == buf[i]);
    }
    msg->RemoveRef();
    msg2->RemoveRef();

    // try cloning a chained message, check size and output are same
    msg = iMsgFactory->CreateMsgAudioEncoded(buf);
    msg1Size = msg->Bytes();
    msg2 = iMsgFactory->CreateMsgAudioEncoded(buf2);
    msg2Size = msg2->Bytes();
    msg->Add(msg2);
    MsgAudioEncoded* msg3 = msg->Clone();
    TEST(msg3->Bytes() == msg1Size + msg2Size);

    (void)memset(output, 0xde, sizeof(output));
    msg3->CopyTo(output);
    for (TUint i=0; i<msg3->Bytes(); i++) {
        if (i < buf.Bytes()) {
            TEST(output[i] == buf[i]);
        }
        else {
            TEST(output[i] == buf2[i - buf.Bytes()]);
        }
    }
    msg->RemoveRef();
    msg3->RemoveRef();

    // Append adds full buffer when space available
    memset(data, 0, sizeof(data));
    buf.Set(data, sizeof(data));
    msg = iMsgFactory->CreateMsgAudioEncoded(buf);
    memset(data, 1, sizeof(data));
    TUint consumed = msg->Append(buf);
    TEST(consumed == buf.Bytes());
    msg->CopyTo(output);
    TEST(output[buf.Bytes() - 1] == 0);
    TEST(output[buf.Bytes()] == 1);
    msg->RemoveRef();

    // Append truncates buffer when insufficient space
    TByte data3[1023];
    memset(data3, 9, sizeof(data3));
    buf.Set(data3, sizeof(data3));
    msg = iMsgFactory->CreateMsgAudioEncoded(buf);
    do {
        consumed = msg->Append(buf);
    } while (consumed == buf.Bytes());
    TEST(consumed == EncodedAudio::kMaxBytes % buf.Bytes());
    msg->RemoveRef();

    // Append truncates at client-specified point
    msg = iMsgFactory->CreateMsgAudioEncoded(buf);
    consumed = msg->Append(buf, buf.Bytes() + 1);
    TEST(consumed == 1);
    msg->RemoveRef();

    // Append copes with client-specified limit being less than current msg occupancy
    msg = iMsgFactory->CreateMsgAudioEncoded(buf);
    consumed = msg->Append(buf, buf.Bytes() - 1);
    TEST(consumed == 0);
    consumed = msg->Append(buf, buf.Bytes());
    TEST(consumed == 0);
    msg->RemoveRef();

    // validate ref counting of chained msgs (see #5167)
    msg = iMsgFactory->CreateMsgAudioEncoded(buf);
    TEST(msg->RefCount() == 1);
    msg->AddRef();
    TEST(msg->RefCount() == 2);
    msg2 = iMsgFactory->CreateMsgAudioEncoded(buf);
    msg->Add(msg2);
    msg->RemoveRef();
    TEST(msg->RefCount() == 1);
    msg->RemoveRef();

    // clean shutdown implies no leaked msgs
}


// SuiteMsgAudio

SuiteMsgAudio::SuiteMsgAudio()
    : Suite("Basic MsgAudio tests")
{
    MsgFactoryInitParams init;
    init.SetMsgAudioPcmCount(kMsgCount, kMsgCount);
    init.SetMsgSilenceCount(kMsgCount);
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
}

SuiteMsgAudio::~SuiteMsgAudio()
{
    delete iMsgFactory;
}

void SuiteMsgAudio::Test()
{
    static const TUint dataSize = 1200;
    Bwh data(dataSize, dataSize);
    (void)memset((void*)data.Ptr(), 0xde, data.Bytes());

    // Create a pcm msg using the same data at each supported sample rate.
    // Check that lower sample rates report higher numbers of jiffies.
    const TUint sampleRates[] = { 7350, 8000, 11025, 12000, 14700, 16000, 22050, 24000, 29400, 32000, 44100, 48000, 88200, 96000, 176400, 192000 };
    const TUint numRates = sizeof(sampleRates) / sizeof(sampleRates[0]);
    TUint prevJiffies = 0xffffffff;
    TUint jiffies;
    MsgAudio* msg;
    for (TUint i=0; i<numRates; i++) {
        msg = iMsgFactory->CreateMsgAudioPcm(data, 2, sampleRates[i], 8, AudioDataEndian::Little, 0);
        jiffies = msg->Jiffies();
        msg->RemoveRef();
        TEST(prevJiffies > jiffies);
        prevJiffies = jiffies;
    }

    // Create pcm msgs using the same data & sample rates but different bit depths.  Check higher bit depths report lower jiffies
    prevJiffies = 0;
    const TUint bitDepths[] = { 8, 16, 24 };
#define numBitDepths (sizeof(bitDepths) / sizeof(bitDepths[0]))
    MsgAudio* msgbd[numBitDepths];
    for (TUint i=0; i<numBitDepths; i++) {
        msgbd[i] = iMsgFactory->CreateMsgAudioPcm(data, 2, 44100, bitDepths[i], AudioDataEndian::Little, 0);
    }
    TEST(msgbd[0]->Jiffies() == 2 * msgbd[1]->Jiffies());
    TEST(msgbd[0]->Jiffies() == 3 * msgbd[2]->Jiffies());
    for (TUint i=0; i<numBitDepths; i++) {
        msgbd[i]->RemoveRef();
    }

    // Split pcm msg.  Check lengths of both parts are as expected.
    msg = iMsgFactory->CreateMsgAudioPcm(data, 2, 44100, 8, AudioDataEndian::Little, Jiffies::kPerSecond);
    static const TUint kSplitPos = 800;
    jiffies = msg->Jiffies();
    MsgAudio* remaining = msg->Split(kSplitPos);
    TEST(remaining != nullptr);
    TUint newJiffies = msg->Jiffies();
    TUint remainingJiffies = remaining->Jiffies();
    TEST(newJiffies > 0);
    TEST(remainingJiffies > 0);
    TEST(newJiffies < jiffies);
    TEST(remainingJiffies < jiffies);
    TEST(newJiffies + remainingJiffies == jiffies);
    TEST(static_cast<MsgAudioPcm*>(msg)->TrackOffset() == Jiffies::kPerSecond);
    TEST(static_cast<MsgAudioPcm*>(remaining)->TrackOffset() == static_cast<MsgAudioPcm*>(msg)->TrackOffset() + msg->Jiffies());
    remaining->RemoveRef();

    // Split pcm msg at invalid positions (0, > Jiffies()).  Check these assert.
    TEST_THROWS(remaining = msg->Split(0), AssertionFailed);
    TEST_THROWS(remaining = msg->Split(msg->Jiffies()), AssertionFailed);
    TEST_THROWS(remaining = msg->Split(msg->Jiffies()+1), AssertionFailed);

    // split pcm msg whose offset is invalid.  Check both parts have invalid offset
    msg->RemoveRef();
    msg = iMsgFactory->CreateMsgAudioPcm(data, 2, 44100, 8, AudioDataEndian::Little, MsgAudioDecoded::kTrackOffsetInvalid);
    TEST(static_cast<MsgAudioDecoded*>(msg)->TrackOffset() == MsgAudioDecoded::kTrackOffsetInvalid);
    remaining = msg->Split(msg->Jiffies() / 2);
    TEST(static_cast<MsgAudioDecoded*>(msg)->TrackOffset() == MsgAudioDecoded::kTrackOffsetInvalid);
    TEST(static_cast<MsgAudioDecoded*>(remaining)->TrackOffset() == MsgAudioDecoded::kTrackOffsetInvalid);
    remaining->RemoveRef();
    msg->RemoveRef();
    msg = iMsgFactory->CreateMsgAudioPcm(data, 2, 44100, 8, AudioDataEndian::Little, Jiffies::kPerSecond);

    // Clone pcm msg.  Check lengths of clone & parent match
    MsgAudio* clone = msg->Clone();
    jiffies = clone->Jiffies();
    TEST(jiffies == msg->Jiffies());
    TEST(static_cast<MsgAudioPcm*>(msg)->TrackOffset() == static_cast<MsgAudioPcm*>(clone)->TrackOffset());
    msg->RemoveRef();
    // confirm clone is usable after parent is destroyed
    TEST(jiffies == clone->Jiffies());
    clone->RemoveRef();

    // Aggregate 2 msgs. Check their combined lengths are reported.
    TUint dataSizeHalfDecodedAudio = DecodedAudio::kMaxBytes/2;
    dataSizeHalfDecodedAudio -= dataSizeHalfDecodedAudio % 12; // round down to an integer number of samples for 8/16/24 bits with 1/2 channels
    static const TUint secondOffsetSamples = dataSizeHalfDecodedAudio / 2; // iBitDepth = 8 bits = 1 byte
    static const TUint secondsOffsetJiffies = Jiffies::PerSample(44100) * secondOffsetSamples;
    Bwh data1(dataSizeHalfDecodedAudio, dataSizeHalfDecodedAudio);
    (void)memset((void*)data1.Ptr(), 0x01, data1.Bytes());
    Bwh data2(dataSizeHalfDecodedAudio, dataSizeHalfDecodedAudio);
    (void)memset((void*)data2.Ptr(), 0x02, data2.Bytes());

    MsgAudioPcm* msgAggregate1 = iMsgFactory->CreateMsgAudioPcm(data1, 2, 44100, 8, AudioDataEndian::Little, 0);
    MsgAudioPcm* msgAggregate2 = iMsgFactory->CreateMsgAudioPcm(data2, 2, 44100, 8, AudioDataEndian::Little, secondsOffsetJiffies);
    TUint expectedJiffiesAggregated = msgAggregate1->Jiffies() + msgAggregate2->Jiffies();
    msgAggregate1->Aggregate(msgAggregate2); // ref is removed
    TEST(msgAggregate1->Jiffies() == expectedJiffiesAggregated);

    // Check underlying DecodedAudio was also combined (i.e. check that MsgAudioPcm->iSize wasn't just updated).
    MsgPlayable* aggregatedPlayable = msgAggregate1->CreatePlayable();
    TEST(aggregatedPlayable->Bytes() == dataSizeHalfDecodedAudio*2);
    ProcessorPcmBufTest pcmProcessor;
    aggregatedPlayable->Read(pcmProcessor);
    aggregatedPlayable->RemoveRef();
    const TByte* ptr = pcmProcessor.Ptr();
    TUint subsampleVal = 0x01;
    for (TUint i=0; i<data1.Bytes(); i++) {
        TEST(*ptr == subsampleVal);
        ptr++;
    }
    subsampleVal = 0x02;
    for (TUint i=data1.Bytes(); i<data1.Bytes()+data2.Bytes(); i++) {
        TEST(*ptr == subsampleVal);
        ptr++;
    }

    // Try aggregate two msgs with different: #channels
    msgAggregate1 = iMsgFactory->CreateMsgAudioPcm(data1, 2, 44100, 8, AudioDataEndian::Little, 0);
    msgAggregate2 = iMsgFactory->CreateMsgAudioPcm(data2, 1, 44100, 8, AudioDataEndian::Little, secondsOffsetJiffies);
    TEST_THROWS(msgAggregate1->Aggregate(msgAggregate2), AssertionFailed);
    msgAggregate1->RemoveRef();
    msgAggregate2->RemoveRef();

    // Try aggregate two msgs with different: sample rate
    msgAggregate1 = iMsgFactory->CreateMsgAudioPcm(data1, 2, 44100, 8, AudioDataEndian::Little, 0);
    msgAggregate2 = iMsgFactory->CreateMsgAudioPcm(data2, 2, 48000, 8, AudioDataEndian::Little, secondsOffsetJiffies);
    TEST_THROWS(msgAggregate1->Aggregate(msgAggregate2), AssertionFailed);
    msgAggregate1->RemoveRef();
    msgAggregate2->RemoveRef();

    // Try aggregate two msgs with different: bit depth
    msgAggregate1 = iMsgFactory->CreateMsgAudioPcm(data1, 2, 44100, 8, AudioDataEndian::Little, 0);
    msgAggregate2 = iMsgFactory->CreateMsgAudioPcm(data2, 2, 44100, 16, AudioDataEndian::Little, secondsOffsetJiffies);
    TEST_THROWS(msgAggregate1->Aggregate(msgAggregate2), AssertionFailed);
    msgAggregate1->RemoveRef();
    msgAggregate2->RemoveRef();

    // Try aggregate two msgs, where one has a ramp set
    msgAggregate1 = iMsgFactory->CreateMsgAudioPcm(data1, 2, 44100, 8, AudioDataEndian::Little, 0);
    msgAggregate2 = iMsgFactory->CreateMsgAudioPcm(data2, 2, 44100, 8, AudioDataEndian::Little, secondsOffsetJiffies);
    TUint rampRemaining = msgAggregate1->Jiffies() * 3;
    MsgAudio* msgRemaining = nullptr;
    msgAggregate2->SetRamp(0, rampRemaining, Ramp::EUp, msgRemaining);
    TEST_THROWS(msgAggregate1->Aggregate(msgAggregate2), AssertionFailed);
    msgAggregate1->RemoveRef();
    msgAggregate2->RemoveRef();

    // Try aggregate two msgs that will overflow underlying DecodedAudio
    Bwh data3(dataSizeHalfDecodedAudio*2, dataSizeHalfDecodedAudio*2);
    (void)memset((void*)data3.Ptr(), 0x03, data3.Bytes());
    msgAggregate1 = iMsgFactory->CreateMsgAudioPcm(data1, 2, 44100, 8, AudioDataEndian::Little, 0);
    msgAggregate2 = iMsgFactory->CreateMsgAudioPcm(data3, 2, 44100, 8, AudioDataEndian::Little, secondsOffsetJiffies);

    TEST_THROWS(msgAggregate1->Aggregate(msgAggregate2), AssertionFailed);
    msgAggregate1->RemoveRef();
    msgAggregate2->RemoveRef();

    // Check creating zero-length msg asserts
    TEST_THROWS(iMsgFactory->CreateMsgAudioPcm(Brx::Empty(), 2, 44100, 8, AudioDataEndian::Little, 0), AssertionFailed);

    // Create silence msg.  Check its length is as expected
    jiffies = Jiffies::kPerMs;
    msg = iMsgFactory->CreateMsgSilence(jiffies, 44100, 8, 2);
    TEST(jiffies == msg->Jiffies());

    // Split silence msg.  Check lengths of both parts are as expected.
    remaining = msg->Split(jiffies/4);
    TEST(remaining != nullptr);
    TEST(msg->Jiffies() == jiffies/4);
    TEST(remaining->Jiffies() == (3*jiffies)/4);
    remaining->RemoveRef();

    // Split silence msg at invalid positions (0, > Jiffies()).  Check these assert.
    TEST_THROWS(remaining = msg->Split(0), AssertionFailed);
    TEST_THROWS(remaining = msg->Split(msg->Jiffies()), AssertionFailed);
    TEST_THROWS(remaining = msg->Split(msg->Jiffies()+1), AssertionFailed);

    // Clone silence msg.  Check lengths of clone & parent match
    clone = msg->Clone();
    jiffies = clone->Jiffies();
    TEST(jiffies == msg->Jiffies());

    // MsgSilence needs to take a pointer to its iAllocatorPlayable when cloning.
    // As we don't have access to iAllocatorPlayable, try calling CreatePlayable(),
    // which should fail if iAllocatorPlayable hasn't been assigned.
    MsgPlayable* playable = static_cast<MsgSilence*>(clone)->CreatePlayable(); // removes ref from clone
    msg->RemoveRef();
    playable->RemoveRef();

    // Silence msgs in DSD streams should align to client-specified boundaries
    const TUint sr = 2822400;
    const TUint jps = Jiffies::PerSample(sr);
    const TUint sampleBlockWords = 1;
    const TUint minSamples = 16; // assumes 2 channels
    const TUint minJiffies = minSamples * jps;
    jiffies = jps;
    msg = iMsgFactory->CreateMsgSilenceDsd(jiffies, sr, 2, sampleBlockWords, 0);
    TEST(jiffies == msg->Jiffies());
    TEST(jiffies == minJiffies);
    msg->RemoveRef();
    jiffies = jps * (minSamples + 1);
    msg = iMsgFactory->CreateMsgSilenceDsd(jiffies, sr, 2, sampleBlockWords, 0);
    TEST(jiffies == msg->Jiffies());
    TEST(jiffies == minJiffies);
    msg->RemoveRef();

    // Attenuation (RAOP only)
    {
        const TByte b = 0x7f;
        TByte sample[] = { b, b, b, b };
        Brn sampleBuf(sample, sizeof sample);
        auto pcm = iMsgFactory->CreateMsgAudioPcm(sampleBuf, 2, 44100, 16, AudioDataEndian::Little, Jiffies::kPerSecond);
        pcm->SetAttenuation(MsgAudioPcm::kUnityAttenuation / 4);
        playable = pcm->CreatePlayable();
        playable->Read(pcmProcessor);
        playable->RemoveRef();
        ptr = pcmProcessor.Ptr();
        const TInt16 subsample = (ptr[0] << 8) + ptr[1];
        TInt16 expected = ((b << 8) + b) / 4;
        TEST(subsample == expected);
    }

    // IPipelineBufferObserver
    BufferObserver bufferObserver;
    const auto msgSize = 2 * Jiffies::kPerMs;
    msg = iMsgFactory->CreateMsgAudioPcm(data, 2, 44100, 8, AudioDataEndian::Little, msgSize);
    TEST(bufferObserver.Size() == 0);
    TEST(bufferObserver.NumCalls() == 0);
    msg->SetObserver(bufferObserver);
    TEST(bufferObserver.Size() == msg->Jiffies());
    TEST(bufferObserver.NumCalls() == 1);
    msg->RemoveRef();
    TEST(bufferObserver.Size() == 0);
    TEST(bufferObserver.NumCalls() == 2);

    bufferObserver.Reset();
    msg = iMsgFactory->CreateMsgAudioPcm(data, 2, 44100, 8, AudioDataEndian::Little, msgSize);
    msg->SetObserver(bufferObserver);
    TEST(bufferObserver.Size() == msg->Jiffies());
    TEST(bufferObserver.NumCalls() == 1);
    TUint prevBufferSize = bufferObserver.Size();
    remaining = msg->Split(msgSize/2);
    TEST(bufferObserver.Size() == prevBufferSize);
    TEST(bufferObserver.NumCalls() == 1);
    msg->RemoveRef();
    TEST(bufferObserver.Size() == remaining->Jiffies());
    TEST(bufferObserver.NumCalls() == 2);
    remaining->RemoveRef();
    TEST(bufferObserver.Size() == 0);
    TEST(bufferObserver.NumCalls() == 3);

    bufferObserver.Reset();
    msg = iMsgFactory->CreateMsgAudioPcm(data, 2, 44100, 8, AudioDataEndian::Little, msgSize);
    msg->SetObserver(bufferObserver);
    TEST(bufferObserver.Size() == msg->Jiffies());
    TEST(bufferObserver.NumCalls() == 1);
    clone = msg->Clone();
    TEST(bufferObserver.Size() == msg->Jiffies());
    TEST(bufferObserver.NumCalls() == 1);
    clone->RemoveRef();
    TEST(bufferObserver.Size() == msg->Jiffies());
    TEST(bufferObserver.NumCalls() == 1);
    msg->RemoveRef();
    TEST(bufferObserver.Size() == 0);
    TEST(bufferObserver.NumCalls() == 2);

    bufferObserver.Reset();
    jiffies = msgSize;
    msg = iMsgFactory->CreateMsgSilence(jiffies, 44100, 8, 2);
    TEST(bufferObserver.Size() == 0);
    TEST(bufferObserver.NumCalls() == 0);
    msg->SetObserver(bufferObserver);
    TEST(bufferObserver.Size() == msg->Jiffies());
    TEST(bufferObserver.NumCalls() == 1);
    msg->RemoveRef();
    TEST(bufferObserver.Size() == 0);
    TEST(bufferObserver.NumCalls() == 2);

    bufferObserver.Reset();
    msg = iMsgFactory->CreateMsgSilence(jiffies, 44100, 8, 2);
    msg->SetObserver(bufferObserver);
    TEST(bufferObserver.Size() == msg->Jiffies());
    TEST(bufferObserver.NumCalls() == 1);
    prevBufferSize = bufferObserver.Size();
    remaining = msg->Split(msgSize/2);
    TEST(bufferObserver.Size() == prevBufferSize);
    TEST(bufferObserver.NumCalls() == 1);
    msg->RemoveRef();
    TEST(bufferObserver.Size() == remaining->Jiffies());
    TEST(bufferObserver.NumCalls() == 2);
    remaining->RemoveRef();
    TEST(bufferObserver.Size() == 0);
    TEST(bufferObserver.NumCalls() == 3);

    bufferObserver.Reset();
    msg = iMsgFactory->CreateMsgSilence(jiffies, 44100, 8, 2);
    msg->SetObserver(bufferObserver);
    TEST(bufferObserver.Size() == msg->Jiffies());
    TEST(bufferObserver.NumCalls() == 1);
    clone = msg->Clone();
    TEST(bufferObserver.Size() == msg->Jiffies());
    TEST(bufferObserver.NumCalls() == 1);
    clone->RemoveRef();
    TEST(bufferObserver.Size() == msg->Jiffies());
    TEST(bufferObserver.NumCalls() == 1);
    msg->RemoveRef();
    TEST(bufferObserver.Size() == 0);
    TEST(bufferObserver.NumCalls() == 2);

    // clean destruction of class implies no leaked msgs
}


// SuiteMsgPlayable

SuiteMsgPlayable::SuiteMsgPlayable()
    : Suite("Basic MsgPlayable tests")
{
    MsgFactoryInitParams init;
    init.SetMsgAudioPcmCount(kMsgCount, kMsgCount);
    init.SetMsgSilenceCount(kMsgCount);
    init.SetMsgPlayableCount(kMsgCount, kMsgCount, kMsgCount);
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
}

SuiteMsgPlayable::~SuiteMsgPlayable()
{
    delete iMsgFactory;
}

void SuiteMsgPlayable::Test()
{
    static const TUint kDataSize = 256;
    Bws<kDataSize> data(kDataSize);
    for (TUint i=0; i<kDataSize; i++) {
        data.At(i) = 0xff - (TByte)i;
    }

    // Create a pcm msg using the same data at each supported sample rate.
    // Convert to MsgPlayable; check Bytes() for each are identical
    const TUint sampleRates[] = { 7350, 8000, 11025, 12000, 14700, 16000, 22050, 24000, 29400, 32000, 44100, 48000, 88200, 96000, 176400, 192000 };
    const TUint numRates = sizeof(sampleRates) / sizeof(sampleRates[0]);
    TUint prevBytes = 0;
    TUint bytes;
    MsgAudioPcm* audioPcm;
    MsgPlayable* playable;
    for (TUint i=0; i<numRates; i++) {
        audioPcm = iMsgFactory->CreateMsgAudioPcm(data, 2, sampleRates[i], 8, AudioDataEndian::Little, 0);
        playable = audioPcm->CreatePlayable();
        bytes = playable->Bytes();
        playable->RemoveRef();
        if (prevBytes != 0) {
            TEST(prevBytes == bytes);
        }
        prevBytes = bytes;
    }

    // Create pcm msg.  Read/validate its content
    audioPcm = iMsgFactory->CreateMsgAudioPcm(data, 2, 44100, 8, AudioDataEndian::Little, 0);
    playable = audioPcm->CreatePlayable();
    TEST(playable->Bytes() == data.Bytes());
    ProcessorPcmBufTest pcmProcessor;
    playable->Read(pcmProcessor);
    playable->RemoveRef();
    const TByte* ptr = pcmProcessor.Ptr();
    TUint subsampleVal = 0xff;
    for (TUint i=0; i<data.Bytes(); i++) {
        TEST(*ptr == subsampleVal);
        ptr++;
        subsampleVal--;
    }

    // Create pcm msg, split it then convert to playable.  Read/validate contents of both
    audioPcm = iMsgFactory->CreateMsgAudioPcm(data, 2, 44100, 8, AudioDataEndian::Little, 0);
    MsgAudioPcm* remainingPcm = (MsgAudioPcm*)audioPcm->Split(audioPcm->Jiffies()/4);
    playable = audioPcm->CreatePlayable();
    MsgPlayable* remainingPlayable = remainingPcm->CreatePlayable();
    TEST(remainingPlayable->Bytes() == 3 * playable->Bytes());
    playable->Read(pcmProcessor);
    playable->RemoveRef();
    subsampleVal = 0xff;
    Brn buf(pcmProcessor.Buf());
    ptr = buf.Ptr();
    for (TUint i=0; i<buf.Bytes(); i++) {
        TEST(*ptr == subsampleVal);
        ptr++;
        subsampleVal--;
    }
    remainingPlayable->Read(pcmProcessor);
    remainingPlayable->RemoveRef();
    buf.Set(pcmProcessor.Buf());
    ptr = buf.Ptr();
    for (TUint i=0; i<buf.Bytes(); i++) {
        TEST(*ptr == subsampleVal);
        ptr++;
        subsampleVal--;
    }

    // Create pcm msg, convert to playable then split.  Read/validate contents of both
    audioPcm = iMsgFactory->CreateMsgAudioPcm(data, 2, 44100, 8, AudioDataEndian::Little, 0);
    playable = audioPcm->CreatePlayable();
    remainingPlayable = playable->Split(playable->Bytes()/4);
    TEST(remainingPlayable->Bytes() == 3 * playable->Bytes());
    playable->Read(pcmProcessor);
    playable->RemoveRef();
    buf.Set(pcmProcessor.Buf());
    ptr = buf.Ptr();
    subsampleVal = 0xff;
    for (TUint i=0; i<buf.Bytes(); i++) {
        TEST(*ptr == subsampleVal);
        ptr++;
        subsampleVal--;
    }
    remainingPlayable->Read(pcmProcessor);
    remainingPlayable->RemoveRef();
    buf.Set(pcmProcessor.Buf());
    ptr = buf.Ptr();
    for (TUint i=0; i<buf.Bytes(); i++) {
        TEST(*ptr == subsampleVal);
        ptr++;
        subsampleVal--;
    }

    // Create pcm msg, split at non-sample boundary.  Read/validate contents of each fragment
    audioPcm = iMsgFactory->CreateMsgAudioPcm(data, 2, 44100, 8, AudioDataEndian::Little, 0);
    remainingPcm = (MsgAudioPcm*)audioPcm->Split((audioPcm->Jiffies()/4) - 1);
    playable = audioPcm->CreatePlayable();
    remainingPlayable = remainingPcm->CreatePlayable();
    playable->Read(pcmProcessor);
    playable->RemoveRef();
    buf.Set(pcmProcessor.Buf());
    ptr = buf.Ptr();
    subsampleVal = 0xff;
    for (TUint i=0; i<buf.Bytes(); i++) {
        TEST(*ptr == subsampleVal);
        ptr++;
        subsampleVal--;
    }
    remainingPlayable->Read(pcmProcessor);
    remainingPlayable->RemoveRef();
    buf.Set(pcmProcessor.Buf());
    ptr = buf.Ptr();
    for (TUint i=0; i<buf.Bytes(); i++) {
        TEST(*ptr == subsampleVal);
        ptr++;
        subsampleVal--;
    }

    // Create pcm msg, split at 1 jiffy (non-sample boundary).  Check initial msg has 0 Bytes() but can Write() its content
    audioPcm = iMsgFactory->CreateMsgAudioPcm(data, 2, 44100, 8, AudioDataEndian::Little, 0);
    remainingPcm = (MsgAudioPcm*)audioPcm->Split(1);
    playable = audioPcm->CreatePlayable();
    remainingPlayable = remainingPcm->CreatePlayable();
    playable->Read(pcmProcessor);
    playable->RemoveRef();
    buf.Set(pcmProcessor.Buf());
    TEST(buf.Bytes() == 0);
    remainingPlayable->Read(pcmProcessor);
    remainingPlayable->RemoveRef();
    buf.Set(pcmProcessor.Buf());
    TEST(buf.Bytes() == data.Bytes());

    // Test splitting at the end of a message returns nullptr
    audioPcm = iMsgFactory->CreateMsgAudioPcm(data, 2, 44100, 8, AudioDataEndian::Little, 0);
    playable = audioPcm->CreatePlayable();
    TEST(nullptr == playable->Split(playable->Bytes()));
    playable->RemoveRef();

    // Split pcm msg at invalid positions (0, > Jiffies()).  Check these assert.
    audioPcm = iMsgFactory->CreateMsgAudioPcm(data, 2, 44100, 8, AudioDataEndian::Little, 0);
    playable = audioPcm->CreatePlayable();
    TEST_THROWS(remainingPlayable = playable->Split(0), AssertionFailed);
    TEST_THROWS(remainingPlayable = playable->Split(playable->Bytes()+1), AssertionFailed);
    playable->RemoveRef();

    // For each sample rate, create a silence msg using the same size
    // Convert to MsgPlayable; check Bytes() increase as sample rates increase
    prevBytes = 0;
    MsgSilence* silence;
    for (TUint i=0; i<numRates; i++) {
        TUint size = Jiffies::kPerMs * 5;
        silence = iMsgFactory->CreateMsgSilence(size, sampleRates[i], 8, 2);
        playable = silence->CreatePlayable();
        bytes = playable->Bytes();
        playable->RemoveRef();
        if (prevBytes != 0) {
            TEST(prevBytes < bytes);
        }
        prevBytes = bytes;
    }

    // Create silence msg.  Read/validate its content
    TUint size = Jiffies::kPerMs;
    silence = iMsgFactory->CreateMsgSilence(size, 44100, 8, 1);
    playable = silence->CreatePlayable();
    bytes = playable->Bytes();
    ValidateSilence(playable);

    // Create silence msg, convert to playable then split.  Check sizes/contents of each
    silence = iMsgFactory->CreateMsgSilence(size, 44100, 8, 1);
    playable = silence->CreatePlayable();
    remainingPlayable = playable->Split(playable->Bytes() / 4);
    TEST(3 * playable->Bytes() == remainingPlayable->Bytes());
    TEST(playable->Bytes() + remainingPlayable->Bytes() == bytes);
    ValidateSilence(playable);
    ValidateSilence(remainingPlayable);

    // Create silence msg, split at non-sample boundary.  Check that fragments have the correct total length
    silence = iMsgFactory->CreateMsgSilence(size, 44100, 8, 1);
    playable = silence->CreatePlayable();
    remainingPlayable = playable->Split((playable->Bytes() / 4) - 1);
    TEST(playable->Bytes() + remainingPlayable->Bytes() == bytes);
    playable->RemoveRef();
    remainingPlayable->RemoveRef();

    // Create multi-channel silence msg.  Check it can be read correctly.
    size = Jiffies::kPerMs;
    silence = iMsgFactory->CreateMsgSilence(size, 192000, 32, 10);
    playable = silence->CreatePlayable();
    TEST(playable->Bytes() == Jiffies::ToSamples(size, 192000) * 40);
    ValidateSilence(playable);

    // Create silence msg, split at 1 jiffy (non-sample boundary).  Check initial msg has 0 Bytes() but can Write() its content
    silence = iMsgFactory->CreateMsgSilence(size, 44100, 8, 1);
    MsgSilence* remainingSilence = (MsgSilence*)silence->Split(1);
    playable = silence->CreatePlayable();
    remainingPlayable = remainingSilence->CreatePlayable();
    TEST(playable->Bytes() == 0);
    TEST(remainingPlayable->Bytes() == bytes);
    ValidateSilence(playable);
    remainingPlayable->RemoveRef();

    // IPipelineBufferObserver
    BufferObserver bufferObserver;
    const auto msgSize = 2 * Jiffies::kPerMs;
    audioPcm = iMsgFactory->CreateMsgAudioPcm(data, 2, 44100, 8, AudioDataEndian::Little, msgSize);
    audioPcm->SetObserver(bufferObserver);
    TEST(bufferObserver.Size() == audioPcm->Jiffies());
    TEST(bufferObserver.NumCalls() == 1);
    auto prevBufferSize = bufferObserver.Size();
    playable = audioPcm->CreatePlayable();
    TEST(bufferObserver.Size() == prevBufferSize);
    TEST(bufferObserver.NumCalls() == 1);
    playable->RemoveRef();
    TEST(bufferObserver.Size() == 0);
    TEST(bufferObserver.NumCalls() == 2);

    bufferObserver.Reset();
    audioPcm = iMsgFactory->CreateMsgAudioPcm(data, 2, 44100, 8, AudioDataEndian::Little, msgSize);
    audioPcm->SetObserver(bufferObserver);
    TEST(bufferObserver.Size() == audioPcm->Jiffies());
    TEST(bufferObserver.NumCalls() == 1);
    prevBufferSize = bufferObserver.Size();
    playable = audioPcm->CreatePlayable();
    TEST(bufferObserver.Size() == prevBufferSize);
    TEST(bufferObserver.NumCalls() == 1);
    remainingPlayable = playable->Split(playable->Bytes() / 2);
    TEST(bufferObserver.Size() == prevBufferSize);
    TEST(bufferObserver.NumCalls() == 1);
    playable->RemoveRef();
    TEST(bufferObserver.Size() == prevBufferSize / 2);
    TEST(bufferObserver.NumCalls() == 2);
    remainingPlayable->RemoveRef();
    TEST(bufferObserver.Size() == 0);
    TEST(bufferObserver.NumCalls() == 3);

    bufferObserver.Reset();
    audioPcm = iMsgFactory->CreateMsgAudioPcm(data, 2, 44100, 8, AudioDataEndian::Little, msgSize);
    audioPcm->SetObserver(bufferObserver);
    TEST(bufferObserver.Size() == audioPcm->Jiffies());
    TEST(bufferObserver.NumCalls() == 1);
    prevBufferSize = bufferObserver.Size();
    audioPcm->SetMuted();
    TEST(bufferObserver.Size() == prevBufferSize);
    TEST(bufferObserver.NumCalls() == 1);
    playable = audioPcm->CreatePlayable(); // muted => get playableSilence
    TEST(bufferObserver.Size() == prevBufferSize);
    TEST(bufferObserver.NumCalls() == 1);
    playable->RemoveRef();
    TEST(bufferObserver.Size() == 0);
    TEST(bufferObserver.NumCalls() == 2);

    // clean destruction of class implies no leaked msgs
}

void SuiteMsgPlayable::ValidateSilence(MsgPlayable* aMsg)
{
    TUint bytes = aMsg->Bytes();
    ProcessorPcmBufTest pcmProcessor;
    aMsg->Read(pcmProcessor);
    aMsg->RemoveRef();
    const TByte* ptr = pcmProcessor.Ptr();
    for (TUint i=0; i<bytes; i++) {
        TEST(ptr[i] == 0);
    }
}


// SuiteRamp

SuiteRamp::SuiteRamp()
    : Suite("Ramp tests")
{
    MsgFactoryInitParams init;
    init.SetMsgAudioPcmCount(kMsgCount, kMsgCount);
    init.SetMsgSilenceCount(kMsgCount);
    init.SetMsgPlayableCount(kMsgCount, kMsgCount, kMsgCount);
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
}

SuiteRamp::~SuiteRamp()
{
    delete iMsgFactory;
}

void SuiteRamp::Test()
{
    TUint jiffies = Jiffies::kPerMs;

    // start=Ramp::kMax, direction=down, duration=fragmentSize.  Apply, check end is Ramp::kMin
    Ramp ramp;
    Ramp split;
    TUint splitPos;
    TEST(!ramp.Set(Ramp::kMax, jiffies, jiffies, Ramp::EDown, split, splitPos));
    TEST(ramp.Start() == Ramp::kMax);
    TEST(ramp.End() == Ramp::kMin);
    TEST(ramp.Direction() == Ramp::EDown);

    // start=Ramp::kMax, direction=up, duration=fragmentSize.  Check asserts as invalid to ramp up beyond max
    ramp.Reset();
    TEST_THROWS(ramp.Set(Ramp::kMax, jiffies, jiffies, Ramp::EUp, split, splitPos), AssertionFailed);

    // start=Ramp::kMin, direction=up, duration=fragmentSize.  Apply, check end is Ramp::kMax
    ramp.Reset();
    TEST(!ramp.Set(Ramp::kMin, jiffies, jiffies, Ramp::EUp, split, splitPos));
    TEST(ramp.Start() == Ramp::kMin);
    TEST(ramp.End() == Ramp::kMax);
    TEST(ramp.Direction() == Ramp::EUp);

    // start=Ramp::kMax, direction=down, duration=2*fragmentSize.  Apply, check end is 50%
    ramp.Reset();
    TEST(!ramp.Set(Ramp::kMax, jiffies, 2*jiffies, Ramp::EDown, split, splitPos));
    TEST(ramp.Start() == Ramp::kMax);
    TEST(ramp.End() == (Ramp::kMax - Ramp::kMin) / 2);
    TEST(ramp.Direction() == Ramp::EDown);

    // start=Ramp::kMin, direction=up, duration=2*fragmentSize.  Apply, check end is 50%
    ramp.Reset();
    TEST(!ramp.Set(Ramp::kMin, jiffies, 2*jiffies, Ramp::EUp, split, splitPos));
    TEST(ramp.Start() == Ramp::kMin);
    TEST(ramp.End() == (Ramp::kMax - Ramp::kMin) / 2);
    TEST(ramp.Direction() == Ramp::EUp);

    // start=50%, direction=down, duration=4*fragmentSize (so remainingDuration=2*fragmentSize).  Apply, check end is 25%
    ramp.Reset();
    TUint start = (Ramp::kMax - Ramp::kMin) / 2;
    TEST(!ramp.Set(start, jiffies, 2*jiffies, Ramp::EDown, split, splitPos));
    TEST(ramp.Start() == start);
    TEST(ramp.End() == (Ramp::kMax - Ramp::kMin) / 4);
    TEST(ramp.Direction() == Ramp::EDown);

    // start=50%, direction=up, duration=4*fragmentSize (so remainingDuration=2*fragmentSize).  Apply, check end is 75%
    ramp.Reset();
    start = (Ramp::kMax - Ramp::kMin) / 2;
    TEST(!ramp.Set(start, jiffies, 2*jiffies, Ramp::EUp, split, splitPos));
    TEST(ramp.Start() == start);
    TEST(ramp.End() == Ramp::kMax - ((Ramp::kMax - Ramp::kMin) / 4));
    TEST(ramp.Direction() == Ramp::EUp);

    // Apply ramp [Max...Min].  Check start/end values and that subsequent values never rise
//    const TUint kNumChannels = 2;
    const TUint kAudioDataSize = 792;
    TByte audioData[kAudioDataSize];
    (void)memset(audioData, 0x7f, kAudioDataSize);
    Brn audioBuf(audioData, kAudioDataSize);

    ramp.Reset();
    TEST(!ramp.Set(Ramp::kMax, kAudioDataSize, kAudioDataSize, Ramp::EDown, split, splitPos));
    RampApplicator applicator(ramp);
    TUint prevSampleVal = 0x7f, sampleVal = 0;
    TByte sample[DecodedAudio::kMaxNumChannels * 4];
    TUint numSamples = applicator.Start(audioBuf, 8, 2);
    for (TUint i=0; i<numSamples; i++) {
        applicator.GetNextSample(sample);
        sampleVal = sample[0];
        if (i==0) {
            TEST(sampleVal >= 0x7d); // test that start of ramp is close to initial value
        }
        TEST(sampleVal == sample[1]);
        TEST(prevSampleVal >= sampleVal);
        prevSampleVal = sampleVal;
    }
    TEST(sampleVal == 0);

    // Repeat the above test, but for negative subsample values
    TByte audioDataSigned[kAudioDataSize];
    (void)memset(audioDataSigned, 0xff, kAudioDataSize);
    Brn audioBufSigned(audioDataSigned, kAudioDataSize);
    ramp.Reset();
    TEST(!ramp.Set(Ramp::kMax, kAudioDataSize, kAudioDataSize, Ramp::EDown, split, splitPos));
    prevSampleVal = 0xff;
    numSamples = applicator.Start(audioBufSigned, 8, 2);
    for (TUint i=0; i<numSamples; i++) {
        applicator.GetNextSample(sample);
        sampleVal = sample[0];
        if (i==0) {
            TEST(sampleVal >= 0xfd); // test that start of ramp is close to initial value
        }
        TEST((sampleVal & 0x80) != 0 || sampleVal == 0); // all ramped samples must remain <=0
        TEST(sampleVal == sample[1]);
        TEST(prevSampleVal >= sampleVal);
        prevSampleVal = sampleVal;
    }
    TEST(sampleVal == 0);

    // Repeat the above test, but for 16-bit subsamples
    ramp.Reset();
    TEST(!ramp.Set(Ramp::kMax, kAudioDataSize, kAudioDataSize, Ramp::EDown, split, splitPos));
    prevSampleVal = 0x7f7f;
    numSamples = applicator.Start(audioBuf, 16, 2);
    for (TUint i=0; i<numSamples; i++) {
        applicator.GetNextSample(sample);
        sampleVal = (sample[0]<<8) | sample[1];
        TEST(sampleVal == (TUint)(sample[2]<<8 | sample[3]));
        TEST(prevSampleVal >= sampleVal);
        prevSampleVal = sampleVal;
    }

    // Repeat the above test, but for 24-bit subsamples
    ramp.Reset();
    TEST(!ramp.Set(Ramp::kMax, kAudioDataSize, kAudioDataSize, Ramp::EDown, split, splitPos));
    prevSampleVal = 0x7f7f7f;
    numSamples = applicator.Start(audioBuf, 24, 2);
    for (TUint i=0; i<numSamples; i++) {
        applicator.GetNextSample(sample);
        sampleVal = (sample[0]<<16) | (sample[1]<<8) | sample[2];
        TEST(sampleVal == (TUint)((sample[3]<<16) | (sample[4]<<8) | sample[5]));
        TEST(prevSampleVal >= sampleVal);
        prevSampleVal = sampleVal;
    }

    // Repeat the above test, but for 32-bit subsamples
    ramp.Reset();
    TEST(!ramp.Set(Ramp::kMax, kAudioDataSize, kAudioDataSize, Ramp::EDown, split, splitPos));
    prevSampleVal = 0x7f7f7f7f;
    numSamples = applicator.Start(audioBuf, 32, 2);
    for (TUint i=0; i<numSamples; i++) {
        applicator.GetNextSample(sample);
        sampleVal = (sample[0]<<24) | (sample[1]<<16) | (sample[2]<<8) | (sample[3]);
        TEST(sampleVal == (TUint)((sample[4]<<24) | (sample[5]<<16) | (sample[6]<<8) | (sample[7])));
        TEST(prevSampleVal >= sampleVal);
        prevSampleVal = sampleVal;
    }

    // Apply ramp [Min...Max].  Check start/end values and that subsequent values never fall
    ramp.Reset();
    TEST(!ramp.Set(Ramp::kMin, kAudioDataSize, kAudioDataSize, Ramp::EUp, split, splitPos));
    prevSampleVal = 0;
    numSamples = applicator.Start(audioBuf, 8, 2);
    for (TUint i=0; i<numSamples; i++) {
        applicator.GetNextSample(sample);
        sampleVal = sample[0];
        if (i==0) {
            TEST(sampleVal <= 0x02); // test that start of ramp is close to zero
        }
        TEST(sampleVal == sample[1]);
        TEST(prevSampleVal <= sampleVal);
        prevSampleVal = sampleVal;
    }
    TEST(sampleVal >= 0x7d); // test that end of ramp is close to max

    // Apply ramp [Max...50%].  Check start/end values
    ramp.Reset();
    TEST(!ramp.Set(Ramp::kMax, kAudioDataSize, kAudioDataSize*2, Ramp::EDown, split, splitPos));
    prevSampleVal = 0;
    numSamples = applicator.Start(audioBuf, 8, 2);
    for (TUint i=0; i<numSamples; i++) {
        applicator.GetNextSample(sample);
        sampleVal = sample[0];
        if (i==0) {
            TEST(sampleVal >= 0x7d); // test that start of ramp is close to max
        }
    }
    TUint endValGuess = (((TUint64)0x7f * kRampArray[256])>>15);
    TEST(endValGuess - sampleVal <= 0x02);

    // Apply ramp [Min...50%].  Check start/end values
    ramp.Reset();
    TEST(!ramp.Set(Ramp::kMin, kAudioDataSize, kAudioDataSize*2, Ramp::EUp, split, splitPos));
    prevSampleVal = 0;
    numSamples = applicator.Start(audioBuf, 8, 2);
    for (TUint i=0; i<numSamples; i++) {
        applicator.GetNextSample(sample);
        sampleVal = sample[0];
        if (i==0) {
            TEST(sampleVal <= 0x02); // test that start of ramp is close to zero
        }
    }
    endValGuess = (((TUint64)0x7f * kRampArray[256])>>15);
    TEST(endValGuess - sampleVal <= 0x02);

    // Apply ramp [50%...25%].  Check start/end values
    ramp.Reset();
    TEST(!ramp.Set(Ramp::kMax / 2, kAudioDataSize, kAudioDataSize*2, Ramp::EDown, split, splitPos));
    prevSampleVal = 0;
    numSamples = applicator.Start(audioBuf, 8, 2);
    for (TUint i=0; i<numSamples; i++) {
        applicator.GetNextSample(sample);
        sampleVal = sample[0];
        if (i==0) {
            TUint startValGuess = (((TUint64)0x7f * kRampArray[256])>>15);
            TEST(startValGuess - sampleVal < 0x02);
        }
    }
    endValGuess = (((TUint64)0x7f * kRampArray[384])>>15);
    TEST(endValGuess - sampleVal <= 0x02);

    // Create [50%...Min] ramp.  Add [Min...50%] ramp.  Check this splits into [Min...25%], [25%...Min]
    ramp.Reset();
    TEST(!ramp.Set(Ramp::kMax / 2, jiffies, jiffies, Ramp::EDown, split, splitPos));
    TEST(ramp.Set(Ramp::kMin, jiffies, 2 * jiffies, Ramp::EUp, split, splitPos));
    TEST(ramp.Start() == 0);
    TEST(ramp.End() == Ramp::kMax / 4);
    TEST(ramp.Direction() == Ramp::EUp);
    TEST(split.Start() == ramp.End());
    TEST(split.End() == 0);
    TEST(split.Direction() == Ramp::EDown);
    TEST(ramp.IsEnabled());
    TEST(split.IsEnabled());

    // Create [50%...25%] ramp.  Add [70%...30%] ramp.  Check original ramp is retained.
    ramp.Reset();
    TEST(!ramp.Set(Ramp::kMax / 2, jiffies, 4 * jiffies, Ramp::EDown, split, splitPos));
    start = ramp.Start();
    TUint end = ramp.End();
    Ramp::EDirection direction = ramp.Direction();
    TEST(!ramp.Set(((TUint64)10 * Ramp::kMax) / 7, jiffies, (5 * jiffies) / 2, Ramp::EDown, split, splitPos));
    TEST(ramp.Start() == start);
    TEST(ramp.End() == end);
    TEST(ramp.Direction() == direction);

    // Create [50%...25%] ramp.  Add [40%...Min] ramp.  Check new ramp is used.
    ramp.Reset();
    TEST(!ramp.Set(Ramp::kMax / 2, jiffies, 2 * jiffies, Ramp::EDown, split, splitPos));
    start = ramp.Start();
    start = ((TUint64)2 * Ramp::kMax) / 5;
    TEST(!ramp.Set(start, jiffies, jiffies, Ramp::EDown, split, splitPos));
    TEST(ramp.Start() == start);
    TEST(ramp.End() == 0);
    TEST(ramp.Direction() == Ramp::EDown);

    // Create MsgSilence.  Set [Max...Min] ramp.  Convert to playable and check output is all zeros
    MsgSilence* silence = iMsgFactory->CreateMsgSilence(jiffies, 44100, 8, 2);
    MsgAudio* remaining = nullptr;
    TEST(Ramp::kMin == silence->SetRamp(Ramp::kMax, jiffies, Ramp::EDown, remaining));
    TEST(remaining == nullptr);
    MsgPlayable* playable = silence->CreatePlayable();
    TEST(playable != nullptr);
    ProcessorPcmBufTest pcmProcessor;
    playable->Read(pcmProcessor);
    const TByte* ptr = pcmProcessor.Ptr();
    for (TUint i=0; i<playable->Bytes(); i++) {
        TEST(*ptr == 0);
        ptr++;
    }
    playable->RemoveRef();

    // Create MsgAudioPcm.  Set [50%...Min] ramp.  Add [Min...50%] ramp.  Convert to playable and check output
    const TUint kEncodedAudioSize = 768;
    TByte encodedAudioData[kEncodedAudioSize];
    (void)memset(encodedAudioData, 0x7f, kEncodedAudioSize);
    Brn encodedAudio(encodedAudioData, kEncodedAudioSize);
    const TUint kNumChannels = 2;
    MsgAudioPcm* audioPcm = iMsgFactory->CreateMsgAudioPcm(encodedAudio, kNumChannels, 44100, 16, AudioDataEndian::Little, 0);
    jiffies = audioPcm->Jiffies();
    TUint remainingDuration = jiffies;
    TEST(Ramp::kMin == audioPcm->SetRamp(Ramp::kMax / 2, remainingDuration, Ramp::EDown, remaining));
    remainingDuration = jiffies * 2;
    TEST(Ramp::kMin != audioPcm->SetRamp(Ramp::kMin, remainingDuration, Ramp::EUp, remaining));
    TEST(remaining != nullptr);
    TEST(remaining->Ramp().IsEnabled());
    TEST(remaining->Ramp().End() == Ramp::kMin);
    TEST(audioPcm->Jiffies() == jiffies / 2);
    TEST(audioPcm->Jiffies() == remaining->Jiffies());
    playable = audioPcm->CreatePlayable();
    playable->Read(pcmProcessor);
    playable->RemoveRef();
    ptr = pcmProcessor.Ptr();
    TUint bytes = pcmProcessor.Buf().Bytes();
    prevSampleVal = 0;
    TEST(((ptr[0]<<8) | ptr[1]) == 0);
    for (TUint i=0; i<bytes; i+=4) {
        sampleVal = (TUint)((ptr[i]<<8) | ptr[i+1]);
        TEST(sampleVal == (TUint)((ptr[i+2]<<8) | ptr[i+3]));
        if (i > 0) {
            TEST(prevSampleVal <= sampleVal);
        }
        prevSampleVal = sampleVal;
    }
    playable = ((MsgAudioPcm*)remaining)->CreatePlayable();
    playable->Read(pcmProcessor);
    playable->RemoveRef();
    ptr = pcmProcessor.Ptr();
    bytes = pcmProcessor.Buf().Bytes();
    TEST(((ptr[bytes-2]<<8) | ptr[bytes-1]) == 0);
    for (TUint i=0; i<bytes; i+=4) {
        sampleVal = (TUint)((ptr[i]<<8) | ptr[i+1]);
        TEST(sampleVal == (TUint)((ptr[i+2]<<8) | ptr[i+3]));
        if (i > 0) {
            TEST(prevSampleVal >= sampleVal);
        }
        prevSampleVal = sampleVal;
    }

    // Create 2 MsgSilences with different durations.
    // Check can ramp down over them (i.e. there is no rounding error at msg boundary)
    TUint silenceSize = Jiffies::kPerMs * 17;
    silence = iMsgFactory->CreateMsgSilence(silenceSize, 44100, 16, 2);
    silenceSize = Jiffies::kPerMs * 23;
    MsgSilence* silence2 = iMsgFactory->CreateMsgSilence(silenceSize, 44100, 16, 2);
    const TUint duration = silence->Jiffies() + silence2->Jiffies();
    remainingDuration = duration;
    TUint currentRamp = Ramp::kMax;
    currentRamp = silence->SetRamp(currentRamp, remainingDuration, Ramp::EDown, remaining);
    currentRamp = silence2->SetRamp(currentRamp, remainingDuration, Ramp::EDown, remaining);
    TEST(currentRamp == Ramp::kMin);
    silence->RemoveRef();
    silence2->RemoveRef();

#if 0 // test removed - Ramp::kMax is now lower than values from #3118
    // see #3118
    ramp.Reset();
    ramp.iStart = 0x3529a489;
    ramp.iEnd = 0x35cd7b93;
    ramp.iDirection = Ramp::EUp;
    ramp.iEnabled = true;
    ramp.Set(0x33cf3a6c, 0x00044e80, 0x0009c300, Ramp::EDown, split, splitPos); // asserts
#endif

    // muted ramp
    ramp.Reset();
    ramp.SetMuted();
    TEST(ramp.Direction() == Ramp::EMute);
    TEST(ramp.Start() == Ramp::kMin);
    TEST(ramp.End() == Ramp::kMin);

    audioPcm = iMsgFactory->CreateMsgAudioPcm(encodedAudio, 1, 44100, 8, AudioDataEndian::Little, 0);
    audioPcm->SetMuted();
    remainingDuration = Jiffies::kPerMs * 20;
    audioPcm->SetRamp(Ramp::kMax, remainingDuration, Ramp::EDown, remaining);
    playable = audioPcm->CreatePlayable();
    playable->Read(pcmProcessor);
    playable->RemoveRef();
    ptr = pcmProcessor.Ptr();
    bytes = pcmProcessor.Buf().Bytes();
    for (TUint i=0; i<bytes; i++) {
        TEST(*ptr++ == 0);
    }

    audioPcm = iMsgFactory->CreateMsgAudioPcm(encodedAudio, 1, 44100, 8, AudioDataEndian::Little, 0);
    remainingDuration = Jiffies::kPerMs * 20;
    audioPcm->SetRamp(Ramp::kMax, remainingDuration, Ramp::EDown, remaining);
    audioPcm->SetMuted();
    playable = audioPcm->CreatePlayable();
    playable->Read(pcmProcessor);
    playable->RemoveRef();
    ptr = pcmProcessor.Ptr();
    bytes = pcmProcessor.Buf().Bytes();
    for (TUint i=0; i<bytes; i++) {
        TEST(*ptr++ == 0);
    }
}


// SuiteMsgAudioDsd

SuiteMsgAudioDsd::SuiteMsgAudioDsd()
    : Suite("Basic MsgAudio tests")
{
    MsgFactoryInitParams init;
    init.SetMsgAudioPcmCount(kMsgCount, kMsgCount);
    init.SetMsgAudioDsdCount(kMsgCount);
    init.SetMsgSilenceCount(kMsgCount);
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
}

SuiteMsgAudioDsd::~SuiteMsgAudioDsd()
{
    delete iMsgFactory;
}

void SuiteMsgAudioDsd::Test()
{
    static const TUint dataSize = 1200;
    Bwh data(dataSize, dataSize);
    (void)memset((void*)data.Ptr(), 0xde, data.Bytes());

    // Create a dsd msg using the same data at each supported sample rate.
    // Check that lower sample rates report higher numbers of jiffies.
    const TUint sampleRates[] = { 2822400, 5644800, 11289600 };
    const TUint numRates = sizeof(sampleRates) / sizeof(sampleRates[0]);
    TUint prevJiffies = 0xffffffff;
    TUint jiffies;
    MsgAudioDsd* msg;
    for (TUint i = 0; i<numRates; i++) {
        msg = iMsgFactory->CreateMsgAudioDsd(data, 2, sampleRates[i], 2, 0LL, 0);
        jiffies = msg->Jiffies();
        msg->RemoveRef();
        TEST(prevJiffies > jiffies);
        prevJiffies = jiffies;
    }

    // Split dsd msg.  Check lengths of both parts are as expected.
    msg = iMsgFactory->CreateMsgAudioDsd(data, 2, 2822400, 2, Jiffies::kPerSecond, 0);
    static const TUint kSplitPos = 800;
    jiffies = msg->Jiffies();
    MsgAudio* remaining = msg->Split(kSplitPos);
    TEST(remaining != nullptr);
    TUint newJiffies = msg->Jiffies();
    TUint remainingJiffies = remaining->Jiffies();
    TEST(newJiffies > 0);
    TEST(remainingJiffies > 0);
    TEST(newJiffies < jiffies);
    TEST(remainingJiffies < jiffies);
    TEST(newJiffies + remainingJiffies == jiffies);
    TEST(static_cast<MsgAudioDecoded*>(msg)->TrackOffset() == Jiffies::kPerSecond);
    TEST(static_cast<MsgAudioDecoded*>(remaining)->TrackOffset() == static_cast<MsgAudioDecoded*>(msg)->TrackOffset() + msg->Jiffies());
    remaining->RemoveRef();

    // Split pcm msg at invalid positions (0, > Jiffies()).  Check these assert.
    TEST_THROWS(remaining = msg->Split(0), AssertionFailed);
    TEST_THROWS(remaining = msg->Split(msg->Jiffies()), AssertionFailed);
    TEST_THROWS(remaining = msg->Split(msg->Jiffies() + 1), AssertionFailed);

    // Clone dsd msg.  Check lengths of clone & parent match
    MsgAudio* clone = msg->Clone();
    jiffies = clone->Jiffies();
    TEST(jiffies == msg->Jiffies());
    TEST(msg->TrackOffset() == static_cast<MsgAudioDsd*>(clone)->TrackOffset());
    msg->RemoveRef();
    // confirm clone is usable after parent is destroyed
    TEST(jiffies == clone->Jiffies());
    clone->RemoveRef();

    // Check creating zero-length msg asserts
    TEST_THROWS(iMsgFactory->CreateMsgAudioDsd(Brx::Empty(), 2, 2822400, 2, 0LL, 0), AssertionFailed);

    // convert to playable
    msg = iMsgFactory->CreateMsgAudioDsd(data, 2, 2822400, 2, 0LL, 0);
    auto playable = static_cast<MsgAudioDecoded*>(msg)->CreatePlayable();
    ProcessorDsdBufTest processor;
    playable->Read(processor);
    Brn audio = processor.Buf();
    for (TUint i = 0; i < audio.Bytes(); i++) {
        TEST(audio[i] == 0xde);
    }
    playable->RemoveRef();

    static const TByte kDsdSilence = 0x69; // FIXME - duplicated knowledge of format of silence
    // MsgSilence supports dsd
    // Currently does not support Dsd
    jiffies = Jiffies::kPerMs * 3;
    auto silence = iMsgFactory->CreateMsgSilenceDsd(jiffies, 2822400, 1, 2, 0);
    playable = silence->CreatePlayable();
    playable->Read(processor);
    audio.Set(processor.Buf());
    for (TUint i = 0; i < audio.Bytes(); i++) {
        TEST(audio[i] == kDsdSilence);
    }
    playable->RemoveRef();

    // muted dsd converts to PlayableSilence
    msg = iMsgFactory->CreateMsgAudioDsd(data, 2, 2822400, 2, 0LL, 0);
    msg->SetMuted();
    playable = static_cast<MsgAudioDecoded*>(msg)->CreatePlayable();
    playable->Read(processor);
    audio.Set(processor.Buf());
    for (TUint i = 0; i < audio.Bytes(); i++) {
        TEST(audio[i] == kDsdSilence);
    }
    playable->RemoveRef();

    // split at non-block boundary
    TUint numChannels = 2;
    TByte data2[24] = { 0 };
    Brn data2Buf(&data2[0], sizeof data2);
    TUint sampleBlockWords = 1;
    const TUint sr = 2822400;
    const TUint jps = Jiffies::PerSample(sr);
    msg = iMsgFactory->CreateMsgAudioDsd(data2Buf, 2, sr, 1, 0LL, 0);
    TUint samplesPerBlock = ((sampleBlockWords * 4) * 8) / numChannels;
    TUint sampleBlockJiffies = samplesPerBlock * jps; // Taken from MsgAudioDsd::JiffiesPerSampleBlockTotal()
    auto split = msg->Split(sampleBlockJiffies - 1);
    playable = msg->CreatePlayable();
    TEST(playable->Bytes() == 0);
    playable->RemoveRef();
    playable = split->CreatePlayable();
    TEST(playable->Bytes() == sizeof data2);
    playable->RemoveRef();

    // test Split() correctly updates member variables for each msg
    const TUint kData3Size = 320;
    Bwh data3(kData3Size, kData3Size);
    TUint padBytesPerChunk = 0;
    msg = iMsgFactory->CreateMsgAudioDsd(data3, 2, sr, sampleBlockWords, Jiffies::kPerSecond, padBytesPerChunk);
    auto splitDsd = msg->Split(sampleBlockJiffies);
    MsgAudioDsd* audioDsd = static_cast<MsgAudioDsd*>(msg);
    MsgAudioDsd* remainingDsd = static_cast<MsgAudioDsd*>(splitDsd);
    TUint dataBufJiffies = ((kData3Size * 8) / 2) * jps;

    TEST(remainingDsd->iSampleBlockWords == sampleBlockWords);
    TEST(remainingDsd->iBlockWordsNoPad == sampleBlockWords - padBytesPerChunk);
    TEST(audioDsd->iSize == audioDsd->iSizeTotalJiffies);
    TEST(audioDsd->iSizeTotalJiffies == sampleBlockJiffies);
    TEST(audioDsd->iJiffiesNonPlayable == 0);
    TEST(remainingDsd->iSize == remainingDsd->iSizeTotalJiffies);
    TEST(remainingDsd->iSizeTotalJiffies == dataBufJiffies - sampleBlockJiffies);
    TEST(remainingDsd->iJiffiesNonPlayable == 0);

    remainingDsd->RemoveRef();
    audioDsd->RemoveRef();

    // same as above, with variation in sampleBlockSize and padding
    padBytesPerChunk = 2;
    sampleBlockWords = 6;
    TUint blockWordsNoPad = 4;
    samplesPerBlock = ((sampleBlockWords * 4) * 8) / numChannels;
    TUint playableSamplesPerBlock = ((blockWordsNoPad * 4) * 8) / numChannels;
    sampleBlockJiffies = samplesPerBlock * jps;

    msg = iMsgFactory->CreateMsgAudioDsd(data3, 2, sr, sampleBlockWords, Jiffies::kPerSecond, padBytesPerChunk);
    TUint jiffiesBeforeSplit = msg->Jiffies();
    splitDsd = msg->Split(sampleBlockJiffies);
    audioDsd = static_cast<MsgAudioDsd*>(msg);
    remainingDsd = static_cast<MsgAudioDsd*>(splitDsd);
    TUint startingAudioDsdJiffies = (((((kData3Size * 8) * blockWordsNoPad) / sampleBlockWords) / numChannels) * jps);

    TEST(remainingDsd->iSampleBlockWords == sampleBlockWords);
    TEST(remainingDsd->iBlockWordsNoPad == sampleBlockWords - padBytesPerChunk);

    TUint playableSampleBlockJiffies = playableSamplesPerBlock * jps;
    TUint blockCorrectPlayableJiffies = audioDsd->iSize - (audioDsd->iSize % playableSampleBlockJiffies);
    TUint totalJiffies = (blockCorrectPlayableJiffies * sampleBlockWords) / blockWordsNoPad;
    TUint jiffiesNonPlayable = totalJiffies - audioDsd->iSize;

    TEST(audioDsd->iSize == sampleBlockJiffies);
    TEST(audioDsd->iSizeTotalJiffies == totalJiffies);
    TEST(audioDsd->iJiffiesNonPlayable == jiffiesNonPlayable);

    blockCorrectPlayableJiffies = remainingDsd->iSize - (remainingDsd->iSize % playableSampleBlockJiffies);
    totalJiffies = (blockCorrectPlayableJiffies * sampleBlockWords) / blockWordsNoPad;
    jiffiesNonPlayable = totalJiffies - remainingDsd->iSize;

    TEST(remainingDsd->iSize == (jiffiesBeforeSplit - sampleBlockJiffies));
    TEST(remainingDsd->iSizeTotalJiffies == totalJiffies + sampleBlockJiffies);
    TEST(remainingDsd->iJiffiesNonPlayable == jiffiesNonPlayable + sampleBlockJiffies);

    remainingDsd->RemoveRef();
    audioDsd->RemoveRef();

    // test the conversion from jiffies to bytes
    TUint testJiffies = 192000;
    audioDsd = iMsgFactory->CreateMsgAudioDsd(data3, 2, sr, 1, 0, 0);
    TUint bytesFromJiffies = Jiffies::ToBytesSampleBlock(testJiffies, jps, numChannels, 1, samplesPerBlock);
    TUint targetBytes = ((testJiffies / jps) * 2) / 8;
    TEST(bytesFromJiffies == targetBytes);

    // test the conversion from jiffies to bytes, jiffies does not fall on a sample bloock boundary;
    testJiffies = 192000 + jps;
    bytesFromJiffies = Jiffies::ToBytesSampleBlock(testJiffies, jps, numChannels, 1, samplesPerBlock);
    TEST(bytesFromJiffies == targetBytes);
    audioDsd->RemoveRef();

    // test conversion between playable and total jiffies
    // simple conversion, playableJiffies falls on a sample block boundary
    sampleBlockJiffies = ((blockWordsNoPad * 4) * 8) * jps;
    TUint playableJiffies = 128000;
    audioDsd = iMsgFactory->CreateMsgAudioDsd(data3, 2, sr, 6, 0, 2);
    totalJiffies = audioDsd->JiffiesPlayableToJiffiesTotal(playableJiffies, sampleBlockJiffies);
    TUint targetTotalJiffies = (playableJiffies * sampleBlockWords) / blockWordsNoPad;
    TEST(totalJiffies == targetTotalJiffies);
    audioDsd->RemoveRef();

    // playableJiffies does not fall on a sample block boundary, we expect the same result as block above
    playableJiffies += jps; // additon of a single sample from previous test
    audioDsd = iMsgFactory->CreateMsgAudioDsd(data3, 2, sr, 6, 0, 2);
    totalJiffies = audioDsd->JiffiesPlayableToJiffiesTotal(playableJiffies, sampleBlockJiffies);
    TEST(totalJiffies == targetTotalJiffies);
    audioDsd->RemoveRef();

    // test aggregation of 2 MsgAudioDsd, and subsequent call MsgAudioDsd::AggregateComplete()
    const TUint kData4Size = 180;
    Bwh data4(kData4Size, kData4Size);

    audioDsd = iMsgFactory->CreateMsgAudioDsd(data3, numChannels, sr, sampleBlockWords, 0, padBytesPerChunk);
    startingAudioDsdJiffies = (((((kData3Size * 8) * blockWordsNoPad) / sampleBlockWords) / numChannels) * jps);
    auto aggregateDsd = iMsgFactory->CreateMsgAudioDsd(data4, 2, sr, sampleBlockWords, startingAudioDsdJiffies, padBytesPerChunk);
    TUint startingAggregateDsdJiffies = (((((kData4Size * 8) * blockWordsNoPad) / sampleBlockWords) / numChannels) * jps);
    // replicates the behaviour of MsgAudioDsd::SizeJiffiesTotal() and expects to arrive at the same conclusion
    TUint playableAggregatedJiffies = startingAudioDsdJiffies + startingAggregateDsdJiffies;
    TUint blockCorrectPlayableAggregatedJiffies = playableAggregatedJiffies - (playableAggregatedJiffies % playableSampleBlockJiffies);
    TUint totalAggregatedJiffies = (blockCorrectPlayableAggregatedJiffies * sampleBlockWords) / blockWordsNoPad;
    TUint nonPlayableAggregatedJiffies = totalAggregatedJiffies - playableAggregatedJiffies;

    TEST(audioDsd->Jiffies() == startingAudioDsdJiffies);
    TEST(aggregateDsd->Jiffies() == startingAggregateDsdJiffies);
    audioDsd->Aggregate(aggregateDsd);
    TEST(audioDsd->Jiffies() == (startingAudioDsdJiffies + startingAggregateDsdJiffies));
    TEST(audioDsd->iSizeTotalJiffies == totalAggregatedJiffies);
    TEST(audioDsd->iJiffiesNonPlayable == nonPlayableAggregatedJiffies);
    audioDsd->RemoveRef();

    // clean destruction of class implies no leaked msgs
}


// SuiteAudioStream

SuiteAudioStream::SuiteAudioStream()
    : Suite("MsgEncodedStream tests")
{
    MsgFactoryInitParams init;
    init.SetMsgEncodedStreamCount(kMsgEncodedStreamCount);
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
}

SuiteAudioStream::~SuiteAudioStream()
{
    delete iMsgFactory;
}

void SuiteAudioStream::Test()
{
    // create MetaText msg, check its data can be retrieved
    Brn uri("http://255.1.33.76:8734/path?query");
    Brn metaText("metaText");
    TUint totalBytes = 1234;
    TUint streamId = 8;
    TBool seekable = true;
    TBool live = true;
    MsgEncodedStream* msg = iMsgFactory->CreateMsgEncodedStream(uri, metaText, totalBytes, 0, streamId, seekable, live, Multiroom::Allowed, nullptr);
    TEST(msg != nullptr);
    TEST(msg->Uri() == uri);
    TEST(msg->MetaText() == metaText);
    TEST(msg->TotalBytes() == totalBytes);
    TEST(msg->StreamId() == streamId);
    TEST(msg->Seekable() == seekable);
    TEST(msg->Live() == live);
    TEST(msg->StreamHandler() == nullptr);
    msg->RemoveRef();

#ifdef DEFINE_DEBUG
    // access freed msg (doesn't bother valgrind as this is still allocated memory).  Check text has been cleared.
    TEST(msg->Uri() != uri);
    TEST(msg->MetaText() != metaText);
    TEST(msg->TotalBytes() != totalBytes);
    TEST(msg->StreamId() != streamId);
    TEST(msg->Seekable() != seekable);
    TEST(msg->Live() != live);
    TEST(msg->StreamHandler() == nullptr);
#endif

    // create second MetaText msg, check its data can be retrieved
    uri.Set("http://3.4.5.6:8");
    metaText.Set("updated");
    totalBytes = 65537;
    streamId = 99;
    seekable = false;
    live = false;
    msg = iMsgFactory->CreateMsgEncodedStream(uri, metaText, totalBytes, 0, streamId, seekable, live, Multiroom::Allowed, nullptr);
    TEST(msg != nullptr);
    TEST(msg->Uri() == uri);
    TEST(msg->MetaText() == metaText);
    TEST(msg->TotalBytes() == totalBytes);
    TEST(msg->StreamId() == streamId);
    TEST(msg->Seekable() == seekable);
    TEST(msg->Live() == live);
    TEST(msg->StreamHandler() == nullptr);
    msg->RemoveRef();
}


// SuiteMetaText

SuiteMetaText::SuiteMetaText()
    : Suite("MsgMetaText tests")
{
    MsgFactoryInitParams init;
    init.SetMsgMetaTextCount(kMsgMetaTextCount);
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
}

SuiteMetaText::~SuiteMetaText()
{
    delete iMsgFactory;
}

void SuiteMetaText::Test()
{
    // create MetaText msg, check its text can be retrieved
    Brn metaText("metaText");
    MsgMetaText* msg = iMsgFactory->CreateMsgMetaText(metaText);
    TEST(msg != nullptr);
    TEST(msg->MetaText() == metaText);
    msg->RemoveRef();

#ifdef DEFINE_DEBUG
    // access freed msg (doesn't bother valgrind as this is still allocated memory).  Check text has been cleared.
    TEST(msg->MetaText() != metaText);
#endif

    // create second MetaText msg, check its text can be retrieved
    metaText.Set("updated");
    msg = iMsgFactory->CreateMsgMetaText(metaText);
    TEST(msg != nullptr);
    TEST(msg->MetaText() == metaText);
    msg->RemoveRef();
}


// SuiteTrack

SuiteTrack::SuiteTrack()
    : Suite("MsgTrack tests")
{
    MsgFactoryInitParams init;
    init.SetMsgTrackCount(kMsgTrackCount);
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
    iTrackFactory = new TrackFactory(iInfoAggregator, 1);
}

SuiteTrack::~SuiteTrack()
{
    delete iMsgFactory;
    delete iTrackFactory;
}

void SuiteTrack::Test()
{
    // create Track msg, check its uri/id can be retrieved
    Brn uri("http://host:port/folder/file.ext");
    Brn metadata("metadata#1");
    Track* track = iTrackFactory->CreateTrack(uri, metadata);
    TBool startOfStream = true;
    MsgTrack* msg = iMsgFactory->CreateMsgTrack(*track, startOfStream);
    track->RemoveRef();
    TEST(msg != nullptr);
    TEST(msg->Track().Uri() == uri);
    TEST(msg->Track().MetaData() == metadata);
    TEST(msg->StartOfStream() == startOfStream);
    TUint trackId = msg->Track().Id();
    msg->RemoveRef();

#ifdef DEFINE_DEBUG
    // access freed msg (doesn't bother valgrind as this is still allocated memory).  Check uri/id have been cleared.
    TEST_THROWS(msg->Track(), AssertionFailed);
    TEST(track->Uri() != uri);
    TEST(track->MetaData() != metadata);
    TEST(track->Id() != trackId);
    TEST(msg->StartOfStream() != startOfStream);
#endif

    // create second Track msg, check its uri/id can be retrieved
    uri.Set("http://newhost:newport/newfolder/newfile.newext");
    metadata.Set("metadata#2");
    startOfStream = false;
    track = iTrackFactory->CreateTrack(uri, metadata);
    msg = iMsgFactory->CreateMsgTrack(*track, startOfStream);
    TEST(msg != nullptr);
    TEST(msg->Track().Uri() == uri);
    TEST(msg->Track().MetaData() == metadata);
    TEST(msg->Track().Id() != trackId);
    TEST(msg->StartOfStream() == startOfStream);
    trackId = msg->Track().Id();
    msg->RemoveRef();
    TEST(track->Uri() == uri);
    TEST(track->MetaData() == metadata);
    TEST(track->Id() == trackId);
    track->RemoveRef();
}


// SuiteFlush

SuiteFlush::SuiteFlush()
    : Suite("MsgFlush tests")
{
    MsgFactoryInitParams init;
    init.SetMsgFlushCount(kMsgFlushCount);
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
}

SuiteFlush::~SuiteFlush()
{
    delete iMsgFactory;
}

void SuiteFlush::Test()
{
    TUint id = MsgFlush::kIdInvalid + 1;
    MsgFlush* msg = iMsgFactory->CreateMsgFlush(id);
    TEST(msg->Id() == id);
    TEST(msg->Id() != MsgFlush::kIdInvalid);
    msg->RemoveRef();
    TEST(id != msg->Id()); // slightly dodgy to assert that Clear()ing a flush resets its id

    id++;
    msg = iMsgFactory->CreateMsgFlush(id);
    TEST(msg->Id() != MsgFlush::kIdInvalid);
    TEST(msg->Id() == id);
    msg->RemoveRef();
}



// SuiteHalt

SuiteHalt::SuiteHalt()
    : Suite("MsgHalt tests")
    , iHaltedCount(0)
{
    MsgFactoryInitParams init;
    init.SetMsgHaltCount(kMsgHaltCount);
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
}

SuiteHalt::~SuiteHalt()
{
    delete iMsgFactory;
}

void SuiteHalt::Test()
{
    MsgHalt* msg = iMsgFactory->CreateMsgHalt();
    TEST(msg->Id() == MsgHalt::kIdNone);
    msg->RemoveRef();
    TEST(msg->Id() != MsgHalt::kIdNone);

    msg = iMsgFactory->CreateMsgHalt(MsgHalt::kIdInvalid);
    TEST(msg->Id() == MsgHalt::kIdInvalid);
    msg->RemoveRef();

    TUint id = MsgHalt::kIdNone;
    msg = iMsgFactory->CreateMsgHalt();
    TEST(msg->Id() == MsgHalt::kIdNone);
    msg->RemoveRef();

    id++;
    msg = iMsgFactory->CreateMsgHalt(id);
    TEST(msg->Id() == id);
    msg->RemoveRef();
    TEST(msg->Id() != id);

    TEST(iHaltedCount == 0);
    msg = iMsgFactory->CreateMsgHalt(id, MakeFunctor(*this, &SuiteHalt::Halted));
    msg->ReportHalted();
    TEST(iHaltedCount == 1);
    msg->ReportHalted(); // subsequent report attempts should be ignored
    TEST(iHaltedCount == 1);
    msg->RemoveRef();

    // following test disabled - there is no way to recover from Msg::Clear throwing
    //msg = iMsgFactory->CreateMsgHalt(id, MakeFunctor(*this, &SuiteHalt::Halted));
    //TEST_THROWS(msg->RemoveRef(), AssertionFailed); // asserts if we don't run the Halted callback
}

void SuiteHalt::Halted()
{
    ++iHaltedCount;
}


// SuiteMode

SuiteMode::SuiteMode()
    : Suite("MsgMode tests")
{
    MsgFactoryInitParams init;
    init.SetMsgModeCount(kMsgModeCount);
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
}

SuiteMode::~SuiteMode()
{
    delete iMsgFactory;
}

void SuiteMode::Test()
{
    Brn mode("First");
    ModeInfo mi;
    mi.SetLatencyMode(Latency::Internal);
    mi.SetSupportsNextPrev(true, false);
    mi.SetSupportsRepeatRandom(true, false);
    ModeTransportControls transportControls;
    MsgMode* msg = iMsgFactory->CreateMsgMode(mode, mi, nullptr, transportControls);
    TEST(msg->Mode() == mode);
    const ModeInfo& info = msg->Info();
    TEST( info.LatencyMode() == Latency::Internal);
    TEST( info.SupportsNext());
    TEST(!info.SupportsPrev());
    TEST( info.SupportsRepeat());
    TEST(!info.SupportsRandom());
    msg->RemoveRef();
    TEST(msg->Mode() != mode);

    Brn mode2("Second");
    ModeInfo mi2;
    mi.SetLatencyMode(Latency::NotSupported);
    mi2.SetSupportsNextPrev(false, true);
    mi2.SetSupportsRepeatRandom(false, true);
    msg = iMsgFactory->CreateMsgMode(mode2, mi2, nullptr, transportControls);
    const ModeInfo& info2 = msg->Info();
    TEST(msg->Mode() == mode2);
    TEST( info2.LatencyMode() == Latency::NotSupported);
    TEST(!info2.SupportsNext());
    TEST( info2.SupportsPrev());
    TEST(!info2.SupportsRepeat());
    TEST( info2.SupportsRandom());
    msg->RemoveRef();
    TEST(msg->Mode() != mode2);
}


// SuiteDelay

SuiteDelay::SuiteDelay()
    : Suite("MsgDelay tests")
{
    MsgFactoryInitParams init;
    init.SetMsgDelayCount(kMsgDelayCount);
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
}

SuiteDelay::~SuiteDelay()
{
    delete iMsgFactory;
}

void SuiteDelay::Test()
{
    const TUint kDelayJiffies = Jiffies::kPerMs * 100;
    MsgDelay* msg = iMsgFactory->CreateMsgDelay(kDelayJiffies);
    TEST(msg->RemainingJiffies() == kDelayJiffies);
    TEST(msg->TotalJiffies() == kDelayJiffies);
    msg->RemoveRef();
    TEST(msg->RemainingJiffies() != kDelayJiffies);
    TEST(msg->TotalJiffies() != kDelayJiffies);

    msg = iMsgFactory->CreateMsgDelay(0);
    TEST(msg->RemainingJiffies() == 0);
    TEST(msg->TotalJiffies() == 0);
    msg->RemoveRef();
    TEST(msg->RemainingJiffies() != 0);
    TEST(msg->TotalJiffies() != 0);

    msg = iMsgFactory->CreateMsgDelay(kDelayJiffies/2, kDelayJiffies);
    TEST(msg->RemainingJiffies() == kDelayJiffies/2);
    TEST(msg->TotalJiffies() == kDelayJiffies);
    msg->RemoveRef();
    TEST(msg->RemainingJiffies() != kDelayJiffies/2);
    TEST(msg->TotalJiffies() != kDelayJiffies);
}


// SuiteDecodedStream

SuiteDecodedStream::SuiteDecodedStream()
    : Suite("MsgDecodedStream tests")
{
    MsgFactoryInitParams init;
    init.SetMsgDecodedStreamCount(kMsgDecodedStreamCount);
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
}

SuiteDecodedStream::~SuiteDecodedStream()
{
    delete iMsgFactory;
}

void SuiteDecodedStream::Test()
{
    // create AudioFormat msg, check its text can be retrieved
    TUint streamId = 3; // nonsense value but doesn't matter for this test
    TUint bitRate = 128;
    TUint bitDepth = 16;
    TUint sampleRate = 44100;
    TUint numChannels = 2;
    Brn codecName("test codec");
    TUint64 trackLength = 1<<16;
    TUint64 startSample = 1LL<33;
    TBool lossless = true;
    TBool seekable = true;
    TBool live = true;
    AudioFormat format = AudioFormat::Pcm;
    Media::Multiroom multiroom = Multiroom::Forbidden;
    SpeakerProfile profile(2);   // stereo
    IStreamHandler* handler = this;
    RampType ramp = RampType::Volume;
    MsgDecodedStream* msg = iMsgFactory->CreateMsgDecodedStream(streamId, bitRate, bitDepth, sampleRate, numChannels, codecName, trackLength, startSample, lossless, seekable, live, false, format, multiroom, profile, handler, ramp);
    TEST(msg != nullptr);
    TEST(msg->StreamInfo().StreamId() == streamId);
    TEST(msg->StreamInfo().BitRate() == bitRate);
    TEST(msg->StreamInfo().BitDepth() == bitDepth);
    TEST(msg->StreamInfo().SampleRate() == sampleRate);
    TEST(msg->StreamInfo().NumChannels() == numChannels);
    TEST(msg->StreamInfo().CodecName() == codecName);
    TEST(msg->StreamInfo().TrackLength() == trackLength);
    TEST(msg->StreamInfo().SampleStart() == startSample);
    TEST(msg->StreamInfo().Lossless() == lossless);
    TEST(msg->StreamInfo().Seekable() == seekable);
    TEST(msg->StreamInfo().Live() == live);
    TEST(msg->StreamInfo().Format() == format);
    TEST(msg->StreamInfo().Multiroom() == multiroom);
    TEST(msg->StreamInfo().Profile() == profile);
    TEST(msg->StreamInfo().StreamHandler() == handler);
    TEST(msg->StreamInfo().Ramp() == ramp);
    msg->RemoveRef();

#ifdef DEFINE_DEBUG
    // access freed msg (doesn't bother valgrind as this is still allocated memory).  Check text has been cleared.
    TEST(msg->StreamInfo().StreamId() != streamId);
    TEST(msg->StreamInfo().BitRate() != bitRate);
    TEST(msg->StreamInfo().BitDepth() != bitDepth);
    TEST(msg->StreamInfo().SampleRate() != sampleRate);
    TEST(msg->StreamInfo().NumChannels() != numChannels);
    TEST(msg->StreamInfo().CodecName() != codecName);
    TEST(msg->StreamInfo().TrackLength() != trackLength);
    TEST(msg->StreamInfo().SampleStart() != startSample);
    TEST(msg->StreamInfo().Lossless() != lossless);
    TEST(msg->StreamInfo().Seekable() != seekable);
    TEST(msg->StreamInfo().Live() != live);
    TEST(msg->StreamInfo().Multiroom() != multiroom);
    TEST(msg->StreamInfo().StreamHandler() != handler);
    TEST(msg->StreamInfo().Ramp() != ramp);
#endif

    streamId = 4;
    bitRate = 700;
    bitDepth = 24;
    sampleRate = 192000;
    numChannels = 1;
    codecName.Set("new codec name (a bit longer)");
    trackLength = 1<<30;
    startSample += 111;
    lossless = false;
    seekable = false;
    live = false;
    format = AudioFormat::Dsd;
    multiroom = Multiroom::Allowed;
    profile = SpeakerProfile(3, 2, 1);    // 5.1
    ramp = RampType::Sample;
    msg = iMsgFactory->CreateMsgDecodedStream(streamId, bitRate, bitDepth, sampleRate, numChannels, codecName, trackLength, startSample, lossless, seekable, live, false, format, multiroom, profile, handler, ramp);
    TEST(msg != nullptr);
    TEST(msg->StreamInfo().StreamId() == streamId);
    TEST(msg->StreamInfo().BitRate() == bitRate);
    TEST(msg->StreamInfo().BitDepth() == bitDepth);
    TEST(msg->StreamInfo().SampleRate() == sampleRate);
    TEST(msg->StreamInfo().NumChannels() == numChannels);
    TEST(msg->StreamInfo().CodecName() == codecName);
    TEST(msg->StreamInfo().TrackLength() == trackLength);
    TEST(msg->StreamInfo().SampleStart() == startSample);
    TEST(msg->StreamInfo().Lossless() == lossless);
    TEST(msg->StreamInfo().Seekable() == seekable);
    TEST(msg->StreamInfo().Live() == live);
    TEST(msg->StreamInfo().Format() == format);
    TEST(msg->StreamInfo().Multiroom() == multiroom);
    TEST(msg->StreamInfo().Profile() == profile);
    TEST(msg->StreamInfo().StreamHandler() == handler);
    TEST(msg->StreamInfo().Ramp() == ramp);
    msg->RemoveRef();
}

EStreamPlay SuiteDecodedStream::OkToPlay(TUint /*aStreamId*/)
{
    ASSERTS();
    return ePlayNo;
}

TUint SuiteDecodedStream::TrySeek(TUint /*aStreamId*/, TUint64 /*aOffset*/)
{
    ASSERTS();
    return MsgFlush::kIdInvalid;
}

TUint SuiteDecodedStream::TryDiscard(TUint /*aJiffies*/)
{
    ASSERTS();
    return MsgFlush::kIdInvalid;
}

TUint SuiteDecodedStream::TryStop(TUint /*aStreamId*/)
{
    ASSERTS();
    return MsgFlush::kIdInvalid;
}

void SuiteDecodedStream::NotifyStarving(const Brx& /*aMode*/, TUint /*aStreamId*/, TBool /*aStarving*/)
{
    ASSERTS();
}


// SuiteMsgProcessor

SuiteMsgProcessor::SuiteMsgProcessor()
    : Suite("IMsgProcessor tests")
{
    MsgFactoryInitParams init;
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
    iTrackFactory = new TrackFactory(iInfoAggregator, 1);
}

SuiteMsgProcessor::~SuiteMsgProcessor()
{
    delete iMsgFactory;
    delete iTrackFactory;
}

void SuiteMsgProcessor::Test()
{
    ProcessorMsgType processor;
    // lots of code duplication here.
    // If we factored out the repeating block of code, any failures would be in a common method so pretty meaningless
    const TUint kDataBytes = 256;
    TByte audioData[kDataBytes];
    (void)memset(audioData, 0xab, kDataBytes);
    Brn audioBuf(audioData, kDataBytes);

    MsgAudioEncoded* audioEncoded = iMsgFactory->CreateMsgAudioEncoded(audioBuf);
    TEST(audioEncoded == static_cast<Msg*>(audioEncoded)->Process(processor));
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgAudioEncoded);
    audioEncoded->RemoveRef();

    MsgAudioPcm* audioPcm = iMsgFactory->CreateMsgAudioPcm(audioBuf, 2, 44100, 8, AudioDataEndian::Little, 0);
    TEST(audioPcm == static_cast<Msg*>(audioPcm)->Process(processor));
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgAudioPcm);
    MsgPlayable* playable = audioPcm->CreatePlayable();
    TEST(playable == static_cast<Msg*>(playable)->Process(processor));
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgPlayable);
    playable->RemoveRef();

    MsgAudioDsd* audioDsd = iMsgFactory->CreateMsgAudioDsd(audioBuf, 2, 2822400, 2, 0, 0);
    TEST(audioDsd == static_cast<Msg*>(audioDsd)->Process(processor));
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgAudioDsd);
    playable = audioDsd->CreatePlayable();
    TEST(playable == static_cast<Msg*>(playable)->Process(processor));
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgPlayable);
    playable->RemoveRef();

    TUint silenceSize = Jiffies::kPerMs;
    MsgSilence* silence = iMsgFactory->CreateMsgSilence(silenceSize, 44100, 8, 2);
    TEST(silence == static_cast<Msg*>(silence)->Process(processor));
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgSilence);
    playable = silence->CreatePlayable();
    TEST(playable == static_cast<Msg*>(playable)->Process(processor));
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgPlayable);
    playable->RemoveRef();

    Msg* msg = iMsgFactory->CreateMsgDecodedStream(0, 0, 0, 0, 0, Brx::Empty(), 0, 0, false, false, false, false, AudioFormat::Pcm, Multiroom::Allowed, SpeakerProfile(), nullptr, RampType::Sample);
    TEST(msg == msg->Process(processor));
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgDecodedStream);
    msg->RemoveRef();

    msg = iMsgFactory->CreateMsgMode(Brx::Empty());
    TEST(msg == msg->Process(processor));
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgMode);
    msg->RemoveRef();

    Track* track = iTrackFactory->CreateTrack(Brx::Empty(), Brx::Empty());
    msg = iMsgFactory->CreateMsgTrack(*track);
    track->RemoveRef();
    TEST(msg == msg->Process(processor));
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgTrack);
    msg->RemoveRef();

    msg = iMsgFactory->CreateMsgDrain(Functor());
    TEST(msg == msg->Process(processor));
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgDrain);
    msg->RemoveRef();

    msg = iMsgFactory->CreateMsgDelay(0);
    TEST(msg == msg->Process(processor));
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgDelay);
    msg->RemoveRef();

    msg = iMsgFactory->CreateMsgEncodedStream(Brn("http://1.2.3.4:5"), Brn("Test metatext"), 0, 0, 0, false, false, Multiroom::Allowed, nullptr);
    TEST(msg == msg->Process(processor));
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgEncodedStream);
    msg->RemoveRef();

    msg = iMsgFactory->CreateMsgMetaText(Brn("Test metatext"));
    TEST(msg == msg->Process(processor));
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgMetaText);
    msg->RemoveRef();

    msg = iMsgFactory->CreateMsgStreamInterrupted();
    TEST(msg == msg->Process(processor));
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgStreamInterrupted);
    msg->RemoveRef();

    msg = iMsgFactory->CreateMsgHalt();
    TEST(msg == msg->Process(processor));
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgHalt);
    msg->RemoveRef();

    msg = iMsgFactory->CreateMsgFlush(1);
    TEST(msg == msg->Process(processor));
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgFlush);
    msg->RemoveRef();

    msg = iMsgFactory->CreateMsgWait();
    TEST(msg == msg->Process(processor));
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgWait);
    msg->RemoveRef();

    msg = iMsgFactory->CreateMsgQuit();
    TEST(msg == msg->Process(processor));
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgQuit);
    msg->RemoveRef();
}


// ProcessorMsgType

ProcessorMsgType::ProcessorMsgType()
    : iLastMsgType(ENone)
{
}

ProcessorMsgType::EMsgType ProcessorMsgType::LastMsgType() const
{
    return iLastMsgType;
}

Msg* ProcessorMsgType::ProcessMsg(MsgMode* aMsg)
{
    iLastMsgType = ProcessorMsgType::EMsgMode;
    return aMsg;
}

Msg* ProcessorMsgType::ProcessMsg(MsgTrack* aMsg)
{
    iLastMsgType = ProcessorMsgType::EMsgTrack;
    return aMsg;
}

Msg* ProcessorMsgType::ProcessMsg(MsgDrain* aMsg)
{
    iLastMsgType = ProcessorMsgType::EMsgDrain;
    return aMsg;
}

Msg* ProcessorMsgType::ProcessMsg(MsgDelay* aMsg)
{
    iLastMsgType = ProcessorMsgType::EMsgDelay;
    return aMsg;
}

Msg* ProcessorMsgType::ProcessMsg(MsgEncodedStream* aMsg)
{
    iLastMsgType = ProcessorMsgType::EMsgEncodedStream;
    return aMsg;
}

Msg* ProcessorMsgType::ProcessMsg(MsgStreamSegment* aMsg)
{
    iLastMsgType = ProcessorMsgType::EMsgStreamSegment;
    return aMsg;
}

Msg* ProcessorMsgType::ProcessMsg(MsgAudioEncoded* aMsg)
{
    iLastMsgType = ProcessorMsgType::EMsgAudioEncoded;
    return aMsg;
}

Msg* ProcessorMsgType::ProcessMsg(MsgMetaText* aMsg)
{
    iLastMsgType = ProcessorMsgType::EMsgMetaText;
    return aMsg;
}

Msg* ProcessorMsgType::ProcessMsg(MsgStreamInterrupted* aMsg)
{
    iLastMsgType = ProcessorMsgType::EMsgStreamInterrupted;
    return aMsg;
}

Msg* ProcessorMsgType::ProcessMsg(MsgHalt* aMsg)
{
    iLastMsgType = ProcessorMsgType::EMsgHalt;
    return aMsg;
}

Msg* ProcessorMsgType::ProcessMsg(MsgFlush* aMsg)
{
    iLastMsgType = ProcessorMsgType::EMsgFlush;
    return aMsg;
}

Msg* ProcessorMsgType::ProcessMsg(MsgWait* aMsg)
{
    iLastMsgType = ProcessorMsgType::EMsgWait;
    return aMsg;
}

Msg* ProcessorMsgType::ProcessMsg(MsgDecodedStream* aMsg)
{
    iLastMsgType = ProcessorMsgType::EMsgDecodedStream;
    return aMsg;
}

Msg* ProcessorMsgType::ProcessMsg(MsgAudioPcm* aMsg)
{
    iLastMsgType = ProcessorMsgType::EMsgAudioPcm;
    return aMsg;
}

Msg* ProcessorMsgType::ProcessMsg(MsgAudioDsd* aMsg)
{
    iLastMsgType = ProcessorMsgType::EMsgAudioDsd;
    return aMsg;
}

Msg* ProcessorMsgType::ProcessMsg(MsgSilence* aMsg)
{
    iLastMsgType = ProcessorMsgType::EMsgSilence;
    return aMsg;
}

Msg* ProcessorMsgType::ProcessMsg(MsgPlayable* aMsg)
{
    iLastMsgType = ProcessorMsgType::EMsgPlayable;
    return aMsg;
}

Msg* ProcessorMsgType::ProcessMsg(MsgQuit* aMsg)
{
    iLastMsgType = ProcessorMsgType::EMsgQuit;
    return aMsg;
}


// SuiteMsgQueue

SuiteMsgQueue::SuiteMsgQueue()
    : Suite("MsgQueue tests")
{
    MsgFactoryInitParams init;
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
    iTrackFactory = new TrackFactory(iInfoAggregator, 1);
}

SuiteMsgQueue::~SuiteMsgQueue()
{
    delete iMsgFactory;
    delete iTrackFactory;
}

void SuiteMsgQueue::Test()
{
    MsgQueue* queue = new MsgQueue();

    // queue can be populated and read from
    TEST(queue->IsEmpty());
    TUint size = Jiffies::kPerMs;
    Msg* msg = iMsgFactory->CreateMsgSilence(size, 44100, 8, 2);
    queue->Enqueue(msg);
    TEST(!queue->IsEmpty());
    Msg* dequeued = queue->Dequeue();
    TEST(msg == dequeued);
    TEST(queue->IsEmpty());
    dequeued->RemoveRef();

    // queue can be emptied then reused
    Track* track = iTrackFactory->CreateTrack(Brx::Empty(), Brx::Empty());
    msg = iMsgFactory->CreateMsgTrack(*track);
    track->RemoveRef();
    queue->Enqueue(msg);
    TEST(!queue->IsEmpty());
    dequeued = queue->Dequeue();
    TEST(msg == dequeued);
    TEST(queue->IsEmpty());
    dequeued->RemoveRef();

    // queue is fifo by default
    msg = iMsgFactory->CreateMsgMetaText(Brn("Test metatext"));
    queue->Enqueue(msg);
    msg = iMsgFactory->CreateMsgHalt();
    queue->Enqueue(msg);
    msg = iMsgFactory->CreateMsgFlush(1);
    queue->Enqueue(msg);
    msg = iMsgFactory->CreateMsgWait();
    queue->Enqueue(msg);
    msg = iMsgFactory->CreateMsgQuit();
    queue->Enqueue(msg);
    TEST(!queue->IsEmpty());
    ProcessorMsgType processor;
    dequeued = queue->Dequeue();
    TEST(!queue->IsEmpty());
    dequeued->Process(processor);
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgMetaText);
    dequeued->RemoveRef();
    dequeued = queue->Dequeue();
    TEST(!queue->IsEmpty());
    dequeued->Process(processor);
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgHalt);
    dequeued->RemoveRef();
    dequeued = queue->Dequeue();
    TEST(!queue->IsEmpty());
    dequeued->Process(processor);
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgFlush);
    dequeued->RemoveRef();
    dequeued = queue->Dequeue();
    TEST(!queue->IsEmpty());
    dequeued->Process(processor);
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgWait);
    dequeued->RemoveRef();
    dequeued = queue->Dequeue();
    TEST(queue->IsEmpty());
    dequeued->Process(processor);
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgQuit);
    dequeued->RemoveRef();

    // EnqueueAtHead skips existing items
    msg = iMsgFactory->CreateMsgMetaText(Brn("blah"));
    queue->Enqueue(msg);
    msg = iMsgFactory->CreateMsgHalt();
    queue->Enqueue(msg);
    msg = iMsgFactory->CreateMsgFlush(1);
    queue->EnqueueAtHead(msg);
    TEST(!queue->IsEmpty());
    dequeued = queue->Dequeue();
    TEST(!queue->IsEmpty());
    dequeued->Process(processor);
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgFlush);
    dequeued->RemoveRef();
    dequeued = queue->Dequeue();
    TEST(!queue->IsEmpty());
    dequeued->Process(processor);
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgMetaText);
    dequeued->RemoveRef();
    dequeued = queue->Dequeue();
    TEST(queue->IsEmpty());
    dequeued->Process(processor);
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgHalt);
    dequeued->RemoveRef();

    // EnqueueAtHead for empty list correctly sets Head and Tail
    TEST(queue->IsEmpty());
    msg = iMsgFactory->CreateMsgMetaText(Brn("blah"));
    queue->EnqueueAtHead(msg);
    msg = iMsgFactory->CreateMsgHalt();
    queue->Enqueue(msg);
    TEST(!queue->IsEmpty());
    dequeued = queue->Dequeue();
    dequeued->Process(processor);
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgMetaText);
    dequeued->RemoveRef();
    TEST(!queue->IsEmpty());
    dequeued = queue->Dequeue();
    dequeued->Process(processor);
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgHalt);
    dequeued->RemoveRef();
    TEST(queue->IsEmpty());

    // Enqueueing the same msg consecutively fails
    msg = iMsgFactory->CreateMsgFlush(1);
    queue->Enqueue(msg);
    TEST_THROWS(queue->Enqueue(msg), AssertionFailed);
    dequeued = queue->Dequeue();
    dequeued->RemoveRef();
    TEST(queue->IsEmpty());

    // Enqueueing the same msg at head consecutively fails
    msg = iMsgFactory->CreateMsgFlush(1);
    queue->EnqueueAtHead(msg);
    TEST_THROWS(queue->EnqueueAtHead(msg), AssertionFailed);
    dequeued = queue->Dequeue();
    dequeued->RemoveRef();
    TEST(queue->IsEmpty());

    // Enqueueing the same msg at head and tail consecutively fails
    // queue at tail first, then head
    msg = iMsgFactory->CreateMsgMetaText(Brn("blah")); // filler msg so that iHead != iTail
    queue->Enqueue(msg);
    msg = iMsgFactory->CreateMsgFlush(1);
    queue->Enqueue(msg);
    TEST_THROWS(queue->EnqueueAtHead(msg), AssertionFailed);
    dequeued = queue->Dequeue();
    dequeued->RemoveRef();
    dequeued = queue->Dequeue();
    dequeued->RemoveRef();
    TEST(queue->IsEmpty());
    // queue at head first, then tail
    msg = iMsgFactory->CreateMsgMetaText(Brn("blah")); // filler msg so that iHead != iTail
    queue->Enqueue(msg);
    msg = iMsgFactory->CreateMsgFlush(1);
    queue->EnqueueAtHead(msg);
    TEST_THROWS(queue->Enqueue(msg), AssertionFailed);
    dequeued = queue->Dequeue();
    dequeued->RemoveRef();
    dequeued = queue->Dequeue();
    dequeued->RemoveRef();
    TEST(queue->IsEmpty());

#ifdef DEFINE_DEBUG
    // Enqueueing the same msg as a msg already in queue fails
    msg = iMsgFactory->CreateMsgMetaText(Brn("blah")); // filler msg so that iHead != iTail
    queue->Enqueue(msg);
    Msg* flushMsg = iMsgFactory->CreateMsgFlush(1);
    queue->Enqueue(flushMsg);
    msg = iMsgFactory->CreateMsgHalt();
    queue->Enqueue(msg);
    TEST_THROWS(queue->Enqueue(flushMsg), AssertionFailed);
    // try do the same again, but by enqueuing at head
    TEST_THROWS(queue->EnqueueAtHead(flushMsg), AssertionFailed);
    // clear queue
    dequeued = queue->Dequeue();
    dequeued->RemoveRef();
    dequeued = queue->Dequeue();
    dequeued->RemoveRef();
    dequeued = queue->Dequeue();
    dequeued->RemoveRef();
    TEST(queue->IsEmpty());
#endif

    // Clear() removes all items
    msg = iMsgFactory->CreateMsgHalt();
    queue->Enqueue(msg);
    msg = iMsgFactory->CreateMsgFlush(1);
    queue->Enqueue(msg);
    msg = iMsgFactory->CreateMsgWait();
    queue->Enqueue(msg);
    msg = iMsgFactory->CreateMsgQuit();
    queue->Enqueue(msg);
    TEST(!queue->IsEmpty());
    queue->Clear();
    TEST(queue->IsEmpty());

    // FIXME - no check yet that reading from an empty queue blocks

    delete queue;
}


// SuiteMsgQueueLite

SuiteMsgQueueLite::SuiteMsgQueueLite()
    : Suite("MsgQueueLite tests")
{
    MsgFactoryInitParams init;
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
    iTrackFactory = new TrackFactory(iInfoAggregator, 1);
}

SuiteMsgQueueLite::~SuiteMsgQueueLite()
{
    delete iMsgFactory;
    delete iTrackFactory;
}

void SuiteMsgQueueLite::Test()
{
    MsgQueueLite* queue = new MsgQueueLite();

    // queue can be populated and read from
    TEST(queue->IsEmpty());
    TUint size = Jiffies::kPerMs;
    Msg* msg = iMsgFactory->CreateMsgSilence(size, 44100, 8, 2);
    queue->Enqueue(msg);
    TEST(!queue->IsEmpty());
    Msg* dequeued = queue->Dequeue();
    TEST(msg == dequeued);
    TEST(queue->IsEmpty());
    dequeued->RemoveRef();

    // queue can be emptied then reused
    Track* track = iTrackFactory->CreateTrack(Brx::Empty(), Brx::Empty());
    msg = iMsgFactory->CreateMsgTrack(*track);
    track->RemoveRef();
    queue->Enqueue(msg);
    TEST(!queue->IsEmpty());
    dequeued = queue->Dequeue();
    TEST(msg == dequeued);
    TEST(queue->IsEmpty());
    dequeued->RemoveRef();

    // queue is fifo by default
    msg = iMsgFactory->CreateMsgMetaText(Brn("Test metatext"));
    queue->Enqueue(msg);
    msg = iMsgFactory->CreateMsgHalt();
    queue->Enqueue(msg);
    msg = iMsgFactory->CreateMsgFlush(1);
    queue->Enqueue(msg);
    msg = iMsgFactory->CreateMsgWait();
    queue->Enqueue(msg);
    msg = iMsgFactory->CreateMsgQuit();
    queue->Enqueue(msg);
    TEST(!queue->IsEmpty());
    ProcessorMsgType processor;
    dequeued = queue->Dequeue();
    TEST(!queue->IsEmpty());
    dequeued->Process(processor);
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgMetaText);
    dequeued->RemoveRef();
    dequeued = queue->Dequeue();
    TEST(!queue->IsEmpty());
    dequeued->Process(processor);
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgHalt);
    dequeued->RemoveRef();
    dequeued = queue->Dequeue();
    TEST(!queue->IsEmpty());
    dequeued->Process(processor);
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgFlush);
    dequeued->RemoveRef();
    dequeued = queue->Dequeue();
    TEST(!queue->IsEmpty());
    dequeued->Process(processor);
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgWait);
    dequeued->RemoveRef();
    dequeued = queue->Dequeue();
    TEST(queue->IsEmpty());
    dequeued->Process(processor);
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgQuit);
    dequeued->RemoveRef();

    // EnqueueAtHead skips existing items
    msg = iMsgFactory->CreateMsgMetaText(Brn("blah"));
    queue->Enqueue(msg);
    msg = iMsgFactory->CreateMsgHalt();
    queue->Enqueue(msg);
    msg = iMsgFactory->CreateMsgFlush(1);
    queue->EnqueueAtHead(msg);
    TEST(!queue->IsEmpty());
    dequeued = queue->Dequeue();
    TEST(!queue->IsEmpty());
    dequeued->Process(processor);
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgFlush);
    dequeued->RemoveRef();
    dequeued = queue->Dequeue();
    TEST(!queue->IsEmpty());
    dequeued->Process(processor);
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgMetaText);
    dequeued->RemoveRef();
    dequeued = queue->Dequeue();
    TEST(queue->IsEmpty());
    dequeued->Process(processor);
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgHalt);
    dequeued->RemoveRef();

    // EnqueueAtHead for empty list correctly sets Head and Tail
    TEST(queue->IsEmpty());
    msg = iMsgFactory->CreateMsgMetaText(Brn("blah"));
    queue->EnqueueAtHead(msg);
    msg = iMsgFactory->CreateMsgHalt();
    queue->Enqueue(msg);
    TEST(!queue->IsEmpty());
    dequeued = queue->Dequeue();
    dequeued->Process(processor);
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgMetaText);
    dequeued->RemoveRef();
    TEST(!queue->IsEmpty());
    dequeued = queue->Dequeue();
    dequeued->Process(processor);
    TEST(processor.LastMsgType() == ProcessorMsgType::EMsgHalt);
    dequeued->RemoveRef();
    TEST(queue->IsEmpty());

    // Enqueueing the same msg consecutively fails
    msg = iMsgFactory->CreateMsgFlush(1);
    queue->Enqueue(msg);
    TEST_THROWS(queue->Enqueue(msg), AssertionFailed);
    dequeued = queue->Dequeue();
    dequeued->RemoveRef();
    TEST(queue->IsEmpty());

    // Enqueueing the same msg at head consecutively fails
    msg = iMsgFactory->CreateMsgFlush(1);
    queue->EnqueueAtHead(msg);
    TEST_THROWS(queue->EnqueueAtHead(msg), AssertionFailed);
    dequeued = queue->Dequeue();
    dequeued->RemoveRef();
    TEST(queue->IsEmpty());

    // Enqueueing the same msg at head and tail consecutively fails
    // queue at tail first, then head
    msg = iMsgFactory->CreateMsgMetaText(Brn("blah")); // filler msg so that iHead != iTail
    queue->Enqueue(msg);
    msg = iMsgFactory->CreateMsgFlush(1);
    queue->Enqueue(msg);
    TEST_THROWS(queue->EnqueueAtHead(msg), AssertionFailed);
    dequeued = queue->Dequeue();
    dequeued->RemoveRef();
    dequeued = queue->Dequeue();
    dequeued->RemoveRef();
    TEST(queue->IsEmpty());
    // queue at head first, then tail
    msg = iMsgFactory->CreateMsgMetaText(Brn("blah")); // filler msg so that iHead != iTail
    queue->Enqueue(msg);
    msg = iMsgFactory->CreateMsgFlush(1);
    queue->EnqueueAtHead(msg);
    TEST_THROWS(queue->Enqueue(msg), AssertionFailed);
    dequeued = queue->Dequeue();
    dequeued->RemoveRef();
    dequeued = queue->Dequeue();
    dequeued->RemoveRef();
    TEST(queue->IsEmpty());

#ifdef DEFINE_DEBUG
    // Enqueueing the same msg as a msg already in queue fails
    msg = iMsgFactory->CreateMsgMetaText(Brn("blah")); // filler msg so that iHead != iTail
    queue->Enqueue(msg);
    Msg* flushMsg = iMsgFactory->CreateMsgFlush(1);
    queue->Enqueue(flushMsg);
    msg = iMsgFactory->CreateMsgHalt();
    queue->Enqueue(msg);
    TEST_THROWS(queue->Enqueue(flushMsg), AssertionFailed);
    // try do the same again, but by enqueuing at head
    TEST_THROWS(queue->EnqueueAtHead(flushMsg), AssertionFailed);
    // clear queue
    dequeued = queue->Dequeue();
    dequeued->RemoveRef();
    dequeued = queue->Dequeue();
    dequeued->RemoveRef();
    dequeued = queue->Dequeue();
    dequeued->RemoveRef();
    TEST(queue->IsEmpty());
#endif

    // Clear() removes all items
    msg = iMsgFactory->CreateMsgHalt();
    queue->Enqueue(msg);
    msg = iMsgFactory->CreateMsgFlush(1);
    queue->Enqueue(msg);
    msg = iMsgFactory->CreateMsgWait();
    queue->Enqueue(msg);
    msg = iMsgFactory->CreateMsgQuit();
    queue->Enqueue(msg);
    TEST(!queue->IsEmpty());
    queue->Clear();
    TEST(queue->IsEmpty());

    // reading from an empty queue asserts
    TEST_THROWS(queue->Dequeue(), AssertionFailed);

    delete queue;
}


// SuiteMsgReservoir

SuiteMsgReservoir::SuiteMsgReservoir()
    : Suite("MsgReservoir tests")
{
    MsgFactoryInitParams init;
    init.SetMsgAudioPcmCount(2, 1);
    init.SetMsgSilenceCount(2);
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
    iTrackFactory = new TrackFactory(iInfoAggregator, 1);
}

SuiteMsgReservoir::~SuiteMsgReservoir()
{
    delete iMsgFactory;
    delete iTrackFactory;
}

void SuiteMsgReservoir::Test()
{
    // Add msg of each type.  After each addition, check type of last in and that only audio increases Jiffies()
    // Dequeue msgs.  After each, check type of last out and that only audio decreases Jiffies()

    TestMsgReservoir* queue = new TestMsgReservoir();
    TUint jiffies = queue->Jiffies();
    TEST(jiffies == 0);
    TEST(queue->LastIn() == TestMsgReservoir::ENone);
    TEST(queue->LastOut() == TestMsgReservoir::ENone);

    Msg* msg = iMsgFactory->CreateMsgMode(Brx::Empty());
    queue->Enqueue(msg);
    jiffies = queue->Jiffies();
    TEST(jiffies == 0);
    TEST(queue->LastIn() == TestMsgReservoir::EMsgMode);
    TEST(queue->LastOut() == TestMsgReservoir::ENone);

    Track* track = iTrackFactory->CreateTrack(Brx::Empty(), Brx::Empty());
    msg = iMsgFactory->CreateMsgTrack(*track);
    track->RemoveRef();
    queue->Enqueue(msg);
    jiffies = queue->Jiffies();
    TEST(jiffies == 0);
    TEST(queue->LastIn() == TestMsgReservoir::EMsgTrack);
    TEST(queue->LastOut() == TestMsgReservoir::ENone);

    msg = iMsgFactory->CreateMsgDelay(0);
    TEST(queue->DelayCount() == 0);
    queue->Enqueue(msg);
    jiffies = queue->Jiffies();
    TEST(jiffies == 0);
    TEST(queue->LastIn() == TestMsgReservoir::EMsgDelay);
    TEST(queue->DelayCount() == 1);
    TEST(queue->LastOut() == TestMsgReservoir::ENone);

    msg = iMsgFactory->CreateMsgEncodedStream(Brn("http://1.2.3.4:5"), Brn("metatext"), 0, 0, 0, false, false, Multiroom::Allowed, nullptr);
    TEST(queue->EncodedStreamCount() == 0);
    queue->Enqueue(msg);
    TEST(queue->Jiffies() == 0);
    TEST(queue->LastIn() == TestMsgReservoir::EMsgEncodedStream);
    TEST(queue->EncodedStreamCount() == 1);
    TEST(queue->LastOut() == TestMsgReservoir::ENone);

    msg = iMsgFactory->CreateMsgDecodedStream(3, 128, 16, 44100, 2, Brn("test codec"), 1<<16, 0, true, true, false, false, AudioFormat::Pcm, Multiroom::Allowed, SpeakerProfile(), nullptr, RampType::Sample);
    TEST(queue->DecodedStreamCount() == 0);
    queue->Enqueue(msg);
    TEST(queue->Jiffies() == 0);
    TEST(queue->LastIn() == TestMsgReservoir::EMsgDecodedStream);
    TEST(queue->DecodedStreamCount() == 1);
    TEST(queue->LastOut() == TestMsgReservoir::ENone);

    TUint silenceSize = Jiffies::kPerMs;
    MsgAudio* audio = iMsgFactory->CreateMsgSilence(silenceSize, 44100, 8, 2);
    queue->Enqueue(audio);
    TEST(queue->Jiffies() == jiffies + audio->Jiffies());
    jiffies = queue->Jiffies();
    TEST(queue->LastIn() == TestMsgReservoir::EMsgSilence);
    TEST(queue->LastOut() == TestMsgReservoir::ENone);

    msg = iMsgFactory->CreateMsgMetaText(Brn("foo"));
    TEST(queue->MetaTextCount() == 0);
    queue->Enqueue(msg);
    TEST(queue->Jiffies() == jiffies);
    TEST(queue->LastIn() == TestMsgReservoir::EMsgMetaText);
    TEST(queue->MetaTextCount() == 1);
    TEST(queue->LastOut() == TestMsgReservoir::ENone);

    msg = iMsgFactory->CreateMsgFlush(5); // arbitrary flush id
    queue->Enqueue(msg);
    TEST(queue->Jiffies() == jiffies);
    TEST(queue->LastIn() == TestMsgReservoir::EMsgFlush);
    TEST(queue->LastOut() == TestMsgReservoir::ENone);

    msg = iMsgFactory->CreateMsgWait();
    queue->Enqueue(msg);
    TEST(queue->Jiffies() == jiffies);
    TEST(queue->LastIn() == TestMsgReservoir::EMsgWait);
    TEST(queue->LastOut() == TestMsgReservoir::ENone);

    msg = iMsgFactory->CreateMsgQuit();
    queue->Enqueue(msg);
    TEST(queue->Jiffies() == jiffies);
    TEST(queue->LastIn() == TestMsgReservoir::EMsgQuit);
    TEST(queue->LastOut() == TestMsgReservoir::ENone);

    const TUint kDataBytes = 256;
    TByte encodedAudioData[kDataBytes];
    (void)memset(encodedAudioData, 0xab, kDataBytes);
    Brn encodedAudioBuf(encodedAudioData, kDataBytes);
    audio = iMsgFactory->CreateMsgAudioPcm(encodedAudioBuf, 2, 44100, 8, AudioDataEndian::Little, 0);
    const TUint audioPcmJiffies = audio->Jiffies();
    queue->Enqueue(audio);
    TEST(queue->Jiffies() == jiffies + audioPcmJiffies);
    jiffies = queue->Jiffies();
    TEST(queue->LastIn() == TestMsgReservoir::EMsgAudioPcm);
    TEST(queue->LastOut() == TestMsgReservoir::ENone);

    audio = iMsgFactory->CreateMsgAudioDsd(encodedAudioBuf, 2, 2822400, 2, 0, 0);
    const TUint audioDsdJiffies = audio->Jiffies();
    queue->Enqueue(audio);
    TEST(queue->Jiffies() == jiffies + audioDsdJiffies);
    jiffies = queue->Jiffies();
    TEST(queue->LastIn() == TestMsgReservoir::EMsgAudioDsd);
    TEST(queue->LastOut() == TestMsgReservoir::ENone);

    msg = iMsgFactory->CreateMsgHalt();
    queue->Enqueue(msg);
    TEST(queue->Jiffies() == jiffies);
    TEST(queue->LastIn() == TestMsgReservoir::EMsgHalt);
    TEST(queue->LastOut() == TestMsgReservoir::ENone);

    msg = queue->Dequeue();
    TEST(queue->LastIn() == TestMsgReservoir::EMsgHalt);
    TEST(queue->LastOut() == TestMsgReservoir::EMsgMode);
    TEST(queue->Jiffies() == jiffies);
    msg->RemoveRef();

    msg = queue->Dequeue();
    TEST(queue->LastIn() == TestMsgReservoir::EMsgHalt);
    TEST(queue->LastOut() == TestMsgReservoir::EMsgTrack);
    TEST(queue->Jiffies() == jiffies);
    msg->RemoveRef();

    TEST(queue->DelayCount() == 1);
    msg = queue->Dequeue();
    TEST(queue->LastIn() == TestMsgReservoir::EMsgHalt);
    TEST(queue->LastOut() == TestMsgReservoir::EMsgDelay);
    TEST(queue->DelayCount() == 0);
    TEST(queue->Jiffies() == jiffies);
    msg->RemoveRef();

    TEST(queue->EncodedStreamCount() == 1);
    msg = queue->Dequeue();
    TEST(queue->LastIn() == TestMsgReservoir::EMsgHalt);
    TEST(queue->LastOut() == TestMsgReservoir::EMsgEncodedStream);
    TEST(queue->EncodedStreamCount() == 0);
    TEST(queue->Jiffies() == jiffies);
    msg->RemoveRef();

    TEST(queue->DecodedStreamCount() == 1);
    msg = queue->Dequeue();
    TEST(queue->LastIn() == TestMsgReservoir::EMsgHalt);
    TEST(queue->LastOut() == TestMsgReservoir::EMsgDecodedStream);
    TEST(queue->DecodedStreamCount() == 0);
    TEST(queue->Jiffies() == jiffies);
    msg->RemoveRef();

    msg = queue->Dequeue();
    TEST(queue->LastIn() == TestMsgReservoir::EMsgHalt);
    TEST(queue->LastOut() == TestMsgReservoir::EMsgSilence);
    TEST(queue->Jiffies() == jiffies - silenceSize);
    jiffies = queue->Jiffies();
    msg->RemoveRef();

    TEST(queue->MetaTextCount() == 1);
    msg = queue->Dequeue();
    TEST(queue->LastIn() == TestMsgReservoir::EMsgHalt);
    TEST(queue->LastOut() == TestMsgReservoir::EMsgMetaText);
    TEST(queue->MetaTextCount() == 0);
    TEST(queue->Jiffies() == jiffies);
    msg->RemoveRef();

    msg = queue->Dequeue();
    TEST(queue->LastIn() == TestMsgReservoir::EMsgHalt);
    TEST(queue->LastOut() == TestMsgReservoir::EMsgFlush);
    TEST(queue->Jiffies() == jiffies);
    msg->RemoveRef();

    msg = queue->Dequeue();
    TEST(queue->LastIn() == TestMsgReservoir::EMsgHalt);
    TEST(queue->LastOut() == TestMsgReservoir::EMsgWait);
    TEST(queue->Jiffies() == jiffies);
    msg->RemoveRef();

    msg = queue->Dequeue();
    TEST(queue->LastIn() == TestMsgReservoir::EMsgHalt);
    TEST(queue->LastOut() == TestMsgReservoir::EMsgQuit);
    TEST(queue->Jiffies() == jiffies);
    msg->RemoveRef();

    queue->SplitNextAudio();
    msg = queue->Dequeue();
    TEST(queue->LastIn() == TestMsgReservoir::EMsgHalt);
    TEST(queue->LastOut() == TestMsgReservoir::EMsgAudioPcm);
    TEST(queue->Jiffies() == jiffies - (audioPcmJiffies/2));
    jiffies = queue->Jiffies();
    msg->RemoveRef();
    msg = queue->Dequeue();
    TEST(queue->LastIn() == TestMsgReservoir::EMsgHalt);
    TEST(queue->LastOut() == TestMsgReservoir::EMsgAudioPcm);
    TEST(queue->Jiffies() == audioDsdJiffies);
    msg->RemoveRef();

    msg = queue->Dequeue();
    TEST(queue->LastIn() == TestMsgReservoir::EMsgHalt);
    TEST(queue->LastOut() == TestMsgReservoir::EMsgAudioDsd);
    TEST(queue->Jiffies() == 0);
    msg->RemoveRef();

    msg = queue->Dequeue();
    TEST(queue->LastIn() == TestMsgReservoir::EMsgHalt);
    TEST(queue->LastOut() == TestMsgReservoir::EMsgHalt);
    TEST(queue->Jiffies() == 0);
    msg->RemoveRef();

    delete queue;
}


// TestMsgReservoir

TestMsgReservoir::TestMsgReservoir()
    : iLastMsgIn(ENone)
    , iLastMsgOut(ENone)
    , iSplitNextAudio(false)
{
}

Msg* TestMsgReservoir::ProcessMsgAudioOut(MsgAudio* aMsgAudio)
{
    if (iSplitNextAudio) {
        iSplitNextAudio = false;
        MsgAudio* remaining = aMsgAudio->Split(aMsgAudio->Jiffies() / 2);
        EnqueueAtHead(remaining);
    }
    return aMsgAudio;
}

void TestMsgReservoir::ProcessMsgIn(MsgAudioPcm* /*aMsg*/)
{
    iLastMsgIn = EMsgAudioPcm;
}

void TestMsgReservoir::ProcessMsgIn(MsgAudioDsd* /*aMsg*/)
{
    iLastMsgIn = EMsgAudioDsd;
}

void TestMsgReservoir::ProcessMsgIn(MsgSilence* /*aMsg*/)
{
    iLastMsgIn = EMsgSilence;
}

void TestMsgReservoir::ProcessMsgIn(MsgMode* /*aMsg*/)
{
    iLastMsgIn = EMsgMode;
}

void TestMsgReservoir::ProcessMsgIn(MsgTrack* /*aMsg*/)
{
    iLastMsgIn = EMsgTrack;
}

void TestMsgReservoir::ProcessMsgIn(MsgDrain* /*aMsg*/)
{
    iLastMsgIn = EMsgDrain;
}

void TestMsgReservoir::ProcessMsgIn(MsgDelay* /*aMsg*/)
{
    iLastMsgIn = EMsgDelay;
}

void TestMsgReservoir::ProcessMsgIn(MsgEncodedStream* /*aMsg*/)
{
    iLastMsgIn = EMsgEncodedStream;
}

void TestMsgReservoir::ProcessMsgIn(MsgStreamSegment* /*aMsg*/)
{
    iLastMsgIn = EMsgStreamSegment;
}

void TestMsgReservoir::ProcessMsgIn(MsgDecodedStream* /*aMsg*/)
{
    iLastMsgIn = EMsgDecodedStream;
}

void TestMsgReservoir::ProcessMsgIn(MsgMetaText* /*aMsg*/)
{
    iLastMsgIn = EMsgMetaText;
}

void TestMsgReservoir::ProcessMsgIn(MsgStreamInterrupted* /*aMsg*/)
{
    iLastMsgIn = EMsgStreamInterrupted;
}

void TestMsgReservoir::ProcessMsgIn(MsgHalt* /*aMsg*/)
{
    iLastMsgIn = EMsgHalt;
}

void TestMsgReservoir::ProcessMsgIn(MsgFlush* /*aMsg*/)
{
    iLastMsgIn = EMsgFlush;
}

void TestMsgReservoir::ProcessMsgIn(MsgWait* /*aMsg*/)
{
    iLastMsgIn = EMsgWait;
}

void TestMsgReservoir::ProcessMsgIn(MsgQuit* /*aMsg*/)
{
    iLastMsgIn = EMsgQuit;
}

Msg* TestMsgReservoir::ProcessMsgOut(MsgAudioPcm* aMsg)
{
    iLastMsgOut = EMsgAudioPcm;
    return ProcessMsgAudioOut(aMsg);
}

Msg* TestMsgReservoir::ProcessMsgOut(MsgAudioDsd* aMsg)
{
    iLastMsgOut = EMsgAudioDsd;
    return ProcessMsgAudioOut(aMsg);
}

Msg* TestMsgReservoir::ProcessMsgOut(MsgSilence* aMsg)
{
    iLastMsgOut = EMsgSilence;
    return ProcessMsgAudioOut(aMsg);
}

Msg* TestMsgReservoir::ProcessMsgOut(MsgMode* aMsg)
{
    iLastMsgOut = EMsgMode;
    return aMsg;
}

Msg* TestMsgReservoir::ProcessMsgOut(MsgTrack* aMsg)
{
    iLastMsgOut = EMsgTrack;
    return aMsg;
}

Msg* TestMsgReservoir::ProcessMsgOut(MsgDrain* aMsg)
{
    iLastMsgOut = EMsgDrain;
    return aMsg;
}

Msg* TestMsgReservoir::ProcessMsgOut(MsgDelay* aMsg)
{
    iLastMsgOut = EMsgDelay;
    return aMsg;
}

Msg* TestMsgReservoir::ProcessMsgOut(MsgEncodedStream* aMsg)
{
    iLastMsgOut = EMsgEncodedStream;
    return aMsg;
}

Msg* TestMsgReservoir::ProcessMsgOut(MsgStreamSegment* aMsg)
{
    iLastMsgOut = EMsgStreamSegment;
    return aMsg;
}

Msg* TestMsgReservoir::ProcessMsgOut(MsgDecodedStream* aMsg)
{
    iLastMsgOut = EMsgDecodedStream;
    return aMsg;
}

Msg* TestMsgReservoir::ProcessMsgOut(MsgMetaText* aMsg)
{
    iLastMsgOut = EMsgMetaText;
    return aMsg;
}

Msg* TestMsgReservoir::ProcessMsgOut(MsgStreamInterrupted* aMsg)
{
    iLastMsgOut = EMsgStreamInterrupted;
    return aMsg;
}

Msg* TestMsgReservoir::ProcessMsgOut(MsgHalt* aMsg)
{
    iLastMsgOut = EMsgHalt;
    return aMsg;
}

Msg* TestMsgReservoir::ProcessMsgOut(MsgFlush* aMsg)
{
    iLastMsgOut = EMsgFlush;
    return aMsg;
}

Msg* TestMsgReservoir::ProcessMsgOut(MsgWait* aMsg)
{
    iLastMsgOut = EMsgWait;
    return aMsg;
}

Msg* TestMsgReservoir::ProcessMsgOut(MsgQuit* aMsg)
{
    iLastMsgOut = EMsgQuit;
    return aMsg;
}


// DummyElement

DummyElement::DummyElement(TUint aSupported)
    : PipelineElement(aSupported)
{
}

void DummyElement::Process(Msg* aMsg)
{
    auto msg = aMsg->Process(*this);
    TEST(msg == aMsg);
    msg->RemoveRef();
}


// SuitePipelineElement

SuitePipelineElement::SuitePipelineElement()
    : Suite("PipelineElement tests")
{
    MsgFactoryInitParams init;
    iMsgFactory = new MsgFactory(iInfoAggregator, init);
    iTrackFactory = new TrackFactory(iInfoAggregator, 1);
}

SuitePipelineElement::~SuitePipelineElement()
{
    delete iMsgFactory;
    delete iTrackFactory;
}

void SuitePipelineElement::Test()
{
    for (TInt s = ProcessorMsgType::EMsgMode; s <= ProcessorMsgType::EMsgQuit; s++) {
        const TUint supported = 1<<(s-1); // dodgy mapping that relies on ProcessorMsgType and PipelineElement declaring types in the same order
        auto element = new DummyElement(supported);
        for (TInt t=ProcessorMsgType::EMsgMode; t <= ProcessorMsgType::EMsgQuit; t++) {
            auto msg = CreateMsg((ProcessorMsgType::EMsgType)t);
            if (t == s) {
                element->Process(msg);
            }
            else {
                TEST_THROWS(element->Process(msg), AssertionFailed);
                msg->RemoveRef();
            }
        }
        delete element;
    }

    auto element = new DummyElement(0xffffffff);
    for (TInt t=ProcessorMsgType::EMsgMode; t <= ProcessorMsgType::EMsgQuit; t++) {
        auto msg = CreateMsg((ProcessorMsgType::EMsgType)t);
        element->Process(msg);
    }
    delete element;
}

Msg* SuitePipelineElement::CreateMsg(ProcessorMsgType::EMsgType aType)
{
    switch (aType)
    {
    default:
    case ProcessorMsgType::ENone:
        break;
    case ProcessorMsgType::EMsgMode:
        return iMsgFactory->CreateMsgMode(Brx::Empty());
    case ProcessorMsgType::EMsgTrack:
    {
        Track* track = iTrackFactory->CreateTrack(Brx::Empty(), Brx::Empty());
        auto msg = iMsgFactory->CreateMsgTrack(*track);
        track->RemoveRef();
        return msg;
    }
    case ProcessorMsgType::EMsgDrain:
        return iMsgFactory->CreateMsgDrain(Functor());
    case ProcessorMsgType::EMsgDelay:
        return iMsgFactory->CreateMsgDelay(0);
    case ProcessorMsgType::EMsgEncodedStream:
        return iMsgFactory->CreateMsgEncodedStream(Brn("http://1.2.3.4:5"), Brn("Test metatext"), 0, 0, 0, false, false, Multiroom::Allowed, nullptr);
    case ProcessorMsgType::EMsgStreamSegment:
        return iMsgFactory->CreateMsgStreamSegment(Brn("http://1.2.3.4:5/1.ext"));
    case ProcessorMsgType::EMsgAudioEncoded:
    {
        const TUint kDataBytes = 256;
        TByte audioData[kDataBytes];
        (void)memset(audioData, 0xab, kDataBytes);
        Brn audioBuf(audioData, kDataBytes);
        return iMsgFactory->CreateMsgAudioEncoded(audioBuf);
    }
    case ProcessorMsgType::EMsgMetaText:
        return iMsgFactory->CreateMsgMetaText(Brn("Test metatext"));
    case ProcessorMsgType::EMsgStreamInterrupted:
        return iMsgFactory->CreateMsgStreamInterrupted();
    case ProcessorMsgType::EMsgHalt:
        return iMsgFactory->CreateMsgHalt();
    case ProcessorMsgType::EMsgFlush:
        return iMsgFactory->CreateMsgFlush(1);
    case ProcessorMsgType::EMsgWait:
        return iMsgFactory->CreateMsgWait();
    case ProcessorMsgType::EMsgDecodedStream:
        return iMsgFactory->CreateMsgDecodedStream(0, 0, 0, 0, 0, Brx::Empty(), 0, 0, false, false, false, false, AudioFormat::Pcm, Multiroom::Allowed, SpeakerProfile(), nullptr, RampType::Sample);
    case ProcessorMsgType::EMsgAudioPcm:
    {
        const TUint kDataBytes = 256;
        TByte audioData[kDataBytes];
        (void)memset(audioData, 0xab, kDataBytes);
        Brn audioBuf(audioData, kDataBytes);
        return iMsgFactory->CreateMsgAudioPcm(audioBuf, 2, 44100, 8, AudioDataEndian::Little, 0);
    }
    case ProcessorMsgType::EMsgAudioDsd:
    {
        const TUint kDataBytes = 256;
        TByte audioData[kDataBytes];
        (void)memset(audioData, 0xab, kDataBytes);
        Brn audioBuf(audioData, kDataBytes);
        return iMsgFactory->CreateMsgAudioDsd(audioBuf, 2, 2822400, 2, 0LL, 0);
    }
    case ProcessorMsgType::EMsgSilence:
    {
        TUint size = Jiffies::kPerMs;
        return iMsgFactory->CreateMsgSilence(size, 44100, 8, 2);
    }
    case ProcessorMsgType::EMsgPlayable:
    {
        const TUint kDataBytes = 256;
        TByte audioData[kDataBytes];
        (void)memset(audioData, 0xab, kDataBytes);
        Brn audioBuf(audioData, kDataBytes);
        MsgAudioPcm* audioPcm = iMsgFactory->CreateMsgAudioPcm(audioBuf, 2, 44100, 8, AudioDataEndian::Little, 0);
        return audioPcm->CreatePlayable();
    }
    case ProcessorMsgType::EMsgQuit:
        return iMsgFactory->CreateMsgQuit();
    }
    ASSERTS();
    return nullptr;
}



void TestMsg()
{
    Runner runner("Basic Msg tests\n");
    runner.Add(new SuiteAllocator());
    runner.Add(new SuiteMsgAudioEncoded());
    runner.Add(new SuiteRamp());
    runner.Add(new SuiteMsgAudio());
    runner.Add(new SuiteMsgPlayable());
    runner.Add(new SuiteMsgAudioDsd());
    runner.Add(new SuiteAudioStream());
    runner.Add(new SuiteMetaText());
    runner.Add(new SuiteTrack());
    runner.Add(new SuiteFlush());
    runner.Add(new SuiteHalt());
    runner.Add(new SuiteMode());
    runner.Add(new SuiteDelay());
    runner.Add(new SuiteDecodedStream());
    runner.Add(new SuiteMsgProcessor());
    runner.Add(new SuiteMsgQueue());
    runner.Add(new SuiteMsgQueueLite());
    runner.Add(new SuiteMsgReservoir());
    runner.Add(new SuitePipelineElement());
    runner.Run();
}

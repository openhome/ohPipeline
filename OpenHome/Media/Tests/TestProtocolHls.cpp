#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Media/Protocol/ProtocolHls.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Media/Utils/AllocatorInfoLogger.h>
#include <OpenHome/Net/Private/Globals.h>
#include <OpenHome/OsWrapper.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/SuiteUnitTest.h>
#include <OpenHome/Tests/TestPipe.h>
#include <OpenHome/Tests/Mock.h>

#include <limits>

namespace OpenHome {
namespace Media {
namespace Test {

// class TestHttpReader : public IHlsReader, public IHttpSocket, public IReader
// {
// public:
//     enum EConnectionBehaviour
//     {
//         eSuccess,
//         eNotFound,
//         eUnspecifiedError,
//     };
// public:
//     typedef std::pair<const Uri*, EConnectionBehaviour> UriConnectPair;
//     typedef std::vector<UriConnectPair> UriList;
//     typedef std::vector<const Brx*> BufList;
// public:
//     TestHttpReader(Semaphore& aObserverSem, Semaphore& aWaitSem);
//     void SetContent(const UriList& aUris, const BufList& aContent);
//     void BlockAtOffset(TUint aOffset);
//     void ThrowReadErrorAtOffset(TUint aOffset);
//     void WaitAtOffset(TUint aOffset); // waits on aWaitSem being signalled
//     TUint ConnectCount() const;
// public: // from IHlsReader
//     IHttpSocket& Socket() override;
//     IReader& Reader() override;
// public: // from IHttpSocket
//     TUint Connect(const Uri& aUri) override;
//     void Close() override;
//     TUint ContentLength() const override;
// public: // from IReader
//     Brn Read(TUint aBytes) override;
//     void ReadFlush() override;
//     void ReadInterrupt() override;
// private:
//     const UriList* iUris;
//     const BufList* iContent;
//     Brn iCurrent;
//     TUint iIndex;
//     TUint iOffset;
//     TUint iBlockOffset;
//     Semaphore& iObserverSem;
//     Semaphore iBlockSem;
//     TUint iThrowOffset;
//     Semaphore& iWaitSem;
//     TUint iWaitOffset;
//     TUint iConnectCount;
//     TBool iConnected;
// };

class SuiteHlsSegmentDescriptor : public OpenHome::TestFramework::SuiteUnitTest
{
public:
    SuiteHlsSegmentDescriptor();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    void TestIndex();
    void TestSegmentUri();
    void TestDurationMs();
    void TestAbsoluteUri();
    void TestRelativeUri();
};

class SuiteHlsPlaylistParser : public OpenHome::TestFramework::SuiteUnitTest
{
public:
    SuiteHlsPlaylistParser();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    void TestPlaylistNoMediaSequence();
    void TestPlaylistMediaSequenceStartZero();
    void TestPlaylistMediaSequenceStartNonZero();
    void TestPlaylistRelativeUris();
    void TestEndlistAtEnd();
    void TestEndlistAtStart();
    void TestPlaylistCrLf();
    void TestUnsupportedTag();
    void TestInvalidAttributes();
private:
    HlsPlaylistParser* iParser;
};

class MockHlsPlaylistProvider : public IHlsPlaylistProvider
{
public:
    MockHlsPlaylistProvider();
    ~MockHlsPlaylistProvider();
    void QueuePlaylist(const Brn aUri, const Brn aPlaylist);
public: // from IHlsPlaylistProvider
    IReader& Reload() override;
    const Uri& GetUri() const override;
    void InterruptPlaylistProvider(TBool aInterrupt) override;
private:
    ReaderBuffer iReader;
    std::vector<Uri*> iUris;
    std::vector<Brn> iPlaylists;
    TUint iCurrentIdx;
    TUint iNextIdx;
    TBool iInterrupted;
};

class MockReloadTimer : public IHlsReloadTimer
{
public:
    MockReloadTimer(OpenHome::Test::ITestPipeWritable& aTestPipe);
public: // from IHlsReloadTimer
    void Restart() override;
    void Wait(TUint aWaitMs) override;
    void InterruptReloadTimer() override;
private:
    OpenHome::Test::ITestPipeWritable& iTestPipe;
};

class SuiteHlsM3uReader : public OpenHome::TestFramework::SuiteUnitTest
{
public:
    SuiteHlsM3uReader();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    void TestPlaylistNoMediaSequence();
    void TestPlaylistMediaSequenceStartZero();
    void TestPlaylistMediaSequenceStartNonZero();
    void TestPlaylistClientDefinedStart();
    void TestPlaylistClientDefinedStartBeforeSequenceStart();
    void TestReload();
    void TestReloadNoChange();
    void TestReloadNonContinuous();
    void TestEndlist();
    void TestUnsupportedTag();
    void TestInvalidPlaylist();
private:
    OpenHome::Test::TestPipeDynamic* iTestPipe;
    MockHlsPlaylistProvider* iProvider;
    MockReloadTimer* iReloadTimer;
    HlsM3uReader* iM3uReader;
};

class MockHlsSegmentProvider : public ISegmentProvider
{
public:
    MockHlsSegmentProvider();
    void QueueSegment(const Brn aSegment);
    void SetStreamEnd();
public: // from ISegmentProvider
    IReader& NextSegment() override;
    void InterruptSegmentProvider(TBool aInterrupt) override;
private:
    ReaderBuffer iReader;
    std::vector<Brn> iSegments;
    TUint iSegment;
    TBool iInterrupted;
    TBool iStreamEndSet;
};

class SuiteHlsSegmentStreamer : public OpenHome::TestFramework::SuiteUnitTest
{
public:
    SuiteHlsSegmentStreamer();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    void TestSingleSegmentReadFull();
    void TestSingleSegmentReadIncrements();
    void TestMultipleSegmentsReadFullExact();
    void TestMultipleSegmentsReadFullMoreThan();
    void TestMultipleSegmentsReadIncrements();
    void TestEndOfStreamReadExact();
    void TestEndOfStreamReadMoreThan();
private:
    MockHlsSegmentProvider* iProvider;
    SegmentStreamer* iStreamer;
};

} // namespace Test
} // namespace Media
} // namespace OpenHome


using namespace OpenHome;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Test;
using namespace OpenHome::Test;
using namespace OpenHome::TestFramework;


// // TestHttpReader

// TestHttpReader::TestHttpReader(Semaphore& aObserverSem, Semaphore& aWaitSem)
//     : iObserverSem(aObserverSem)
//     , iBlockSem("THRS", 0)
//     , iWaitSem(aWaitSem)
//     , iConnectCount(0)
//     , iConnected(false)
// {
// }

// void TestHttpReader::SetContent(const UriList& aUris, const BufList& aContent)
// {
//     ASSERT(!iConnected);
//     iUris = &aUris;
//     iContent = &aContent;
//     iIndex = 0;
//     iBlockOffset = std::numeric_limits<TUint>::max();
//     iThrowOffset = std::numeric_limits<TUint>::max();
//     iWaitOffset = std::numeric_limits<TUint>::max();
// }

// void TestHttpReader::BlockAtOffset(TUint aOffset)
// {
//     ASSERT(iThrowOffset == std::numeric_limits<TUint>::max());
//     ASSERT(iWaitOffset == std::numeric_limits<TUint>::max());
//     iBlockOffset = aOffset;
// }

// void TestHttpReader::ThrowReadErrorAtOffset(TUint aOffset)
// {
//     ASSERT(iBlockOffset == std::numeric_limits<TUint>::max());
//     ASSERT(iWaitOffset == std::numeric_limits<TUint>::max());
//     iThrowOffset = aOffset;
// }

// void TestHttpReader::WaitAtOffset(TUint aOffset)
// {
//     ASSERT(iThrowOffset == std::numeric_limits<TUint>::max());
//     ASSERT(iBlockOffset == std::numeric_limits<TUint>::max());
//     iWaitOffset = aOffset;
// }

// TUint TestHttpReader::ConnectCount() const
// {
//     return iConnectCount;
// }

// IHttpSocket& TestHttpReader::Socket()
// {
//     return *this;
// }

// IReader& TestHttpReader::Reader()
// {
//     return *this;
// }

// TUint TestHttpReader::Connect(const Uri& aUri)
// {
//     iConnectCount++;
//     const TUint idx = iIndex++;
//     if (idx < iUris->size() && aUri.AbsoluteUri() == (*iUris)[idx].first->AbsoluteUri()) {
//         if ((*iUris)[idx].second == eSuccess) {
//             iCurrent.Set(*(*iContent)[idx]);
//             iBlockSem.Clear();
//             iOffset = 0;
//             iConnected = true;
//             return HttpStatus::kOk.Code();
//         }
//         else if ((*iUris)[idx].second == eNotFound) {
//             return HttpStatus::kNotFound.Code();
//         }
//         else if ((*iUris)[idx].second == eUnspecifiedError) {
//             return 0;
//         }
//         else {
//             ASSERTS();
//         }
//     }
//     ASSERTS();
//     return 0;   // unreachable
// }

// void TestHttpReader::Close()
// {
//     ASSERT(iConnected);
//     iConnected = false;
// }

// TUint TestHttpReader::ContentLength() const
// {
//     return (*iContent)[iIndex-1]->Bytes();
// }

// Brn TestHttpReader::Read(TUint aBytes)
// {
//     if (iOffset == iCurrent.Bytes()) {
//         THROW(ReaderError);
//     }
//     TUint offsetNew = iOffset + aBytes;
//     if (offsetNew > iCurrent.Bytes()) {
//         aBytes -= (offsetNew - iCurrent.Bytes());
//         offsetNew = iCurrent.Bytes();
//     }

//     if (offsetNew >= iBlockOffset) {
//         if (offsetNew > iBlockOffset && iOffset < iBlockOffset) {
//             aBytes -= offsetNew - iBlockOffset;
//             offsetNew = iBlockOffset;
//         }
//         else {
//             iObserverSem.Signal();
//             iBlockSem.Wait();
//             iBlockOffset = std::numeric_limits<TUint>::max();
//             THROW(ReaderError);
//         }
//     }
//     else if (offsetNew >= iWaitOffset) {
//         if (offsetNew > iWaitOffset && iOffset < iWaitOffset) {
//             aBytes -= offsetNew - iWaitOffset;
//             offsetNew = iWaitOffset;
//         }
//         else {
//             iObserverSem.Signal();
//             iWaitSem.Wait();
//             iWaitOffset = std::numeric_limits<TUint>::max();
//         }
//     }
//     else if (offsetNew >= iThrowOffset) {
//         if (offsetNew > iThrowOffset && iOffset < iThrowOffset) {
//             aBytes -= offsetNew - iThrowOffset;
//             offsetNew = iThrowOffset;
//         }
//         else {
//             iThrowOffset = std::numeric_limits<TUint>::max();
//             THROW(ReaderError);
//         }
//     }

//     Brn buf = iCurrent.Split(iOffset, aBytes);
//     iOffset = offsetNew;
//     return buf;
// }

// void TestHttpReader::ReadFlush()
// {
//     //iOffset = 0;
// }

// void TestHttpReader::ReadInterrupt()
// {
//     ASSERT(iConnected);
//     iBlockSem.Signal();
// }






// SuiteHlsSegmentDescriptor

SuiteHlsSegmentDescriptor::SuiteHlsSegmentDescriptor()
    : SuiteUnitTest("SuiteHlsSegmentDescriptor")
{
    AddTest(MakeFunctor(*this, &SuiteHlsSegmentDescriptor::TestIndex), "TestIndex");
    AddTest(MakeFunctor(*this, &SuiteHlsSegmentDescriptor::TestSegmentUri), "TestSegmentUri");
    AddTest(MakeFunctor(*this, &SuiteHlsSegmentDescriptor::TestDurationMs), "TestDurationMs");
    AddTest(MakeFunctor(*this, &SuiteHlsSegmentDescriptor::TestAbsoluteUri), "TestAbsoluteUri");
    AddTest(MakeFunctor(*this, &SuiteHlsSegmentDescriptor::TestRelativeUri), "TestRelativeUri");
}

void SuiteHlsSegmentDescriptor::Setup()
{
}

void SuiteHlsSegmentDescriptor::TearDown()
{
}

void SuiteHlsSegmentDescriptor::TestIndex()
{
    const Brn kUri("http://www.example.com/a.ts");
    const SegmentDescriptor sd(5, kUri, 6);
    TEST(sd.Index() == 5);
}

void SuiteHlsSegmentDescriptor::TestSegmentUri()
{
    const Brn kUri("http://www.example.com/a.ts");
    const SegmentDescriptor sd(5, kUri, 6);
    TEST(sd.SegmentUri() == kUri);
}

void SuiteHlsSegmentDescriptor::TestDurationMs()
{
    const Brn kUri("http://www.example.com/a.ts");
    const SegmentDescriptor sd(5, kUri, 6);
    TEST(sd.DurationMs() == 6);
}

void SuiteHlsSegmentDescriptor::TestAbsoluteUri()
{
    const Uri kUriBase(Brn("http://www.example.com"));
    const Brn kUri("http://www.example.com/a.ts");
    const SegmentDescriptor sd(5, kUri, 6);
    TEST(sd.SegmentUri() == kUri);
    Uri uri;
    sd.AbsoluteUri(kUriBase, uri);
    TEST(uri.AbsoluteUri() == kUri);
}

void SuiteHlsSegmentDescriptor::TestRelativeUri()
{
    const Uri kUriBase(Brn("http://www.example.com"));
    const Brn kUri("a.ts");
    const SegmentDescriptor sd(5, kUri, 6);
    TEST(sd.SegmentUri() == kUri);
    Uri uri;
    sd.AbsoluteUri(kUriBase, uri);
    TEST(uri.AbsoluteUri() == Brn("http://www.example.com/a.ts"));
}


// SuiteHlsPlaylistParser

SuiteHlsPlaylistParser::SuiteHlsPlaylistParser()
    : SuiteUnitTest("SuiteHlsPlaylistParser")
{
    AddTest(MakeFunctor(*this, &SuiteHlsPlaylistParser::TestPlaylistNoMediaSequence), "TestPlaylistNoMediaSequence");
    AddTest(MakeFunctor(*this, &SuiteHlsPlaylistParser::TestPlaylistMediaSequenceStartZero), "TestPlaylistMediaSequenceStartZero");
    AddTest(MakeFunctor(*this, &SuiteHlsPlaylistParser::TestPlaylistMediaSequenceStartNonZero), "TestPlaylistMediaSequenceStartNonZero");
    AddTest(MakeFunctor(*this, &SuiteHlsPlaylistParser::TestPlaylistRelativeUris), "TestPlaylistRelativeUris");
    AddTest(MakeFunctor(*this, &SuiteHlsPlaylistParser::TestEndlistAtEnd), "TestEndlistAtEnd");
    AddTest(MakeFunctor(*this, &SuiteHlsPlaylistParser::TestEndlistAtStart), "TestEndlistAtStart");
    AddTest(MakeFunctor(*this, &SuiteHlsPlaylistParser::TestPlaylistCrLf), "TestPlaylistCrLf");
    AddTest(MakeFunctor(*this, &SuiteHlsPlaylistParser::TestUnsupportedTag), "TestUnsupportedTag");
    AddTest(MakeFunctor(*this, &SuiteHlsPlaylistParser::TestInvalidAttributes), "TestInvalidAttributes");
}

void SuiteHlsPlaylistParser::Setup()
{
    iParser = new HlsPlaylistParser();
}

void SuiteHlsPlaylistParser::TearDown()
{
    delete iParser;
}

void SuiteHlsPlaylistParser::TestPlaylistNoMediaSequence()
{
    // A playlist with no EXT-X-MEDIA-SEQUENCE should assume it starts from 0.
    // (Can infer that this means the playlist will NOT have segments removed
    // and will only ever have segments added.)
    // Media segments do NOT have to contain their sequence number.
    // Segment durations must be >= EXT-X-TARGETDURATION
    const Brn kFileNoMediaSeq(
    "#EXTM3U\n"
    "#EXT-X-VERSION:2\n"
    "#EXT-X-TARGETDURATION:6\n"
    "\n"
    "#EXTINF:6,\n"
    "https://priv.example.com/a.ts\n"
    "#EXTINF:5,\n"
    "https://priv.example.com/b.ts\n"
    "#EXTINF:4,\n"
    "https://priv.example.com/c.ts\n"
    );
    ReaderBuffer reader(kFileNoMediaSeq);
    iParser->Parse(reader);

    TEST(iParser->TargetDurationMs() == 6000);
    TEST(iParser->StreamEnded() == false);

    auto sd1 = iParser->GetNextSegmentUri();
    TEST(sd1.Index() == 0);
    TEST(sd1.SegmentUri() == Brn("https://priv.example.com/a.ts"));
    TEST(sd1.DurationMs() == 6000);

    auto sd2 = iParser->GetNextSegmentUri();
    TEST(sd2.Index() == 1);
    TEST(sd2.SegmentUri() == Brn("https://priv.example.com/b.ts"));
    TEST(sd2.DurationMs() == 5000);

    auto sd3 = iParser->GetNextSegmentUri();
    TEST(sd3.Index() == 2);
    TEST(sd3.SegmentUri() == Brn("https://priv.example.com/c.ts"));
    TEST(sd3.DurationMs() == 4000);

    TEST_THROWS(iParser->GetNextSegmentUri(), HlsNoMoreSegments);
    TEST(iParser->StreamEnded() == false);
}

void SuiteHlsPlaylistParser::TestPlaylistMediaSequenceStartZero()
{
    // Test a variant playlist that starts at seq 0.
    const Brn kFileMediaSeqStartZero(
    "#EXTM3U\n"
    "#EXT-X-VERSION:2\n"
    "#EXT-X-TARGETDURATION:6\n"
    "#EXT-X-MEDIA-SEQUENCE:0\n"
    "\n"
    "#EXTINF:6,\n"
    "https://priv.example.com/a.ts\n"
    "#EXTINF:5,\n"
    "https://priv.example.com/b.ts\n"
    "#EXTINF:4,\n"
    "https://priv.example.com/c.ts\n"
    );

    ReaderBuffer reader(kFileMediaSeqStartZero);
    iParser->Parse(reader);

    TEST(iParser->TargetDurationMs() == 6000);
    TEST(iParser->StreamEnded() == false);

    auto sd1 = iParser->GetNextSegmentUri();
    TEST(sd1.Index() == 0);
    TEST(sd1.SegmentUri() == Brn("https://priv.example.com/a.ts"));
    TEST(sd1.DurationMs() == 6000);

    auto sd2 = iParser->GetNextSegmentUri();
    TEST(sd2.Index() == 1);
    TEST(sd2.SegmentUri() == Brn("https://priv.example.com/b.ts"));
    TEST(sd2.DurationMs() == 5000);

    auto sd3 = iParser->GetNextSegmentUri();
    TEST(sd3.Index() == 2);
    TEST(sd3.SegmentUri() == Brn("https://priv.example.com/c.ts"));
    TEST(sd3.DurationMs() == 4000);

    TEST_THROWS(iParser->GetNextSegmentUri(), HlsNoMoreSegments);
    TEST(iParser->StreamEnded() == false);
}

void SuiteHlsPlaylistParser::TestPlaylistMediaSequenceStartNonZero()
{
    // Test a variant playlist that starts at a non-zero seq no.
    const Brn kFileMediaSeqStartNonZero(
    "#EXTM3U\n"
    "#EXT-X-VERSION:2\n"
    "#EXT-X-TARGETDURATION:6\n"
    "#EXT-X-MEDIA-SEQUENCE:1234\n"
    "\n"
    "#EXTINF:6,\n"
    "https://priv.example.com/a.ts\n"
    "#EXTINF:5,\n"
    "https://priv.example.com/b.ts\n"
    "#EXTINF:4,\n"
    "https://priv.example.com/c.ts\n"
    );
    ReaderBuffer reader(kFileMediaSeqStartNonZero);
    iParser->Parse(reader);

    TEST(iParser->TargetDurationMs() == 6000);
    TEST(iParser->StreamEnded() == false);

    auto sd1 = iParser->GetNextSegmentUri();
    TEST(sd1.Index() == 1234);
    TEST(sd1.SegmentUri() == Brn("https://priv.example.com/a.ts"));
    TEST(sd1.DurationMs() == 6000);

    auto sd2 = iParser->GetNextSegmentUri();
    TEST(sd2.Index() == 1235);
    TEST(sd2.SegmentUri() == Brn("https://priv.example.com/b.ts"));
    TEST(sd2.DurationMs() == 5000);

    auto sd3 = iParser->GetNextSegmentUri();
    TEST(sd3.Index() == 1236);
    TEST(sd3.SegmentUri() == Brn("https://priv.example.com/c.ts"));
    TEST(sd3.DurationMs() == 4000);

    TEST_THROWS(iParser->GetNextSegmentUri(), HlsNoMoreSegments);
    TEST(iParser->StreamEnded() == false);
}

void SuiteHlsPlaylistParser::TestPlaylistRelativeUris()
{
    // Test a variant playlist that uses relative URIs.
    // Relative URIs are considered relative to the URI of the playlist that
    // contains it.
    const Brn kFileRelative(
    "#EXTM3U\n"
    "#EXT-X-VERSION:2\n"
    "#EXT-X-TARGETDURATION:6\n"
    "#EXT-X-MEDIA-SEQUENCE:1234\n"
    "\n"
    "#EXTINF:6,\n"
    "a.ts\n"
    "#EXTINF:5,\n"
    "b.ts\n"
    "#EXTINF:4,\n"
    "c.ts\n"
    );
    ReaderBuffer reader(kFileRelative);
    iParser->Parse(reader);

    TEST(iParser->TargetDurationMs() == 6000);
    TEST(iParser->StreamEnded() == false);

    auto sd1 = iParser->GetNextSegmentUri();
    TEST(sd1.Index() == 1234);
    TEST(sd1.SegmentUri() == Brn("a.ts"));
    TEST(sd1.DurationMs() == 6000);

    auto sd2 = iParser->GetNextSegmentUri();
    TEST(sd2.Index() == 1235);
    TEST(sd2.SegmentUri() == Brn("b.ts"));
    TEST(sd2.DurationMs() == 5000);

    auto sd3 = iParser->GetNextSegmentUri();
    TEST(sd3.Index() == 1236);
    TEST(sd3.SegmentUri() == Brn("c.ts"));
    TEST(sd3.DurationMs() == 4000);

    TEST_THROWS(iParser->GetNextSegmentUri(), HlsNoMoreSegments);
    TEST(iParser->StreamEnded() == false);
}

void SuiteHlsPlaylistParser::TestEndlistAtEnd()
{
    // Test a file with the EXT-X-ENDLIST tag, which indicates that no more
    // media segments will be added to the existing playlist.
    // The tag may occur anywhere in the playlist file.

    // Test tag at end of playlist.
    const Brn kFileEndlistEnd(
    "#EXTM3U\n"
    "#EXT-X-TARGETDURATION:10\n"
    "#EXTINF:9.009,\n"
    "http://media.example.com/first.ts\n"
    "#EXTINF:9.009,\n"
    "http://media.example.com/second.ts\n"
    "#EXTINF:3.003,\n"
    "http://media.example.com/third.ts\n"
    "#EXT-X-ENDLIST\n");
    ReaderBuffer reader(kFileEndlistEnd);
    iParser->Parse(reader);

    TEST(iParser->TargetDurationMs() == 10000);
    TEST(iParser->StreamEnded() == false);

    auto sd1 = iParser->GetNextSegmentUri();
    TEST(sd1.Index() == 0);
    TEST(sd1.SegmentUri() == Brn("http://media.example.com/first.ts"));
    TEST(sd1.DurationMs() == 9009);

    auto sd2 = iParser->GetNextSegmentUri();
    TEST(sd2.Index() == 1);
    TEST(sd2.SegmentUri() == Brn("http://media.example.com/second.ts"));
    TEST(sd2.DurationMs() == 9009);

    auto sd3 = iParser->GetNextSegmentUri();
    TEST(sd3.Index() == 2);
    TEST(sd3.SegmentUri() == Brn("http://media.example.com/third.ts"));
    TEST(sd3.DurationMs() == 3003);

    TEST_THROWS(iParser->GetNextSegmentUri(), HlsEndOfStream);
    TEST(iParser->StreamEnded() == true);
}

void SuiteHlsPlaylistParser::TestEndlistAtStart()
{
    // Test tag at start of playlist and that all media is still played.
    const Brn kFileEndlistStart(
    "#EXTM3U\n"
    "#EXT-X-TARGETDURATION:10\n"
    "#EXT-X-ENDLIST\n"
    "#EXTINF:9.009,\n"
    "http://media.example.com/first.ts\n"
    "#EXTINF:9.009,\n"
    "http://media.example.com/second.ts\n"
    "#EXTINF:3.003,\n"
    "http://media.example.com/third.ts\n"
    );
    ReaderBuffer reader(kFileEndlistStart);
    iParser->Parse(reader);

    TEST(iParser->TargetDurationMs() == 10000);
    TEST(iParser->StreamEnded() == false);

    auto sd1 = iParser->GetNextSegmentUri();
    TEST(sd1.Index() == 0);
    TEST(sd1.SegmentUri() == Brn("http://media.example.com/first.ts"));
    TEST(sd1.DurationMs() == 9009);

    auto sd2 = iParser->GetNextSegmentUri();
    TEST(sd2.Index() == 1);
    TEST(sd2.SegmentUri() == Brn("http://media.example.com/second.ts"));
    TEST(sd2.DurationMs() == 9009);

    auto sd3 = iParser->GetNextSegmentUri();
    TEST(sd3.Index() == 2);
    TEST(sd3.SegmentUri() == Brn("http://media.example.com/third.ts"));
    TEST(sd3.DurationMs() == 3003);

    TEST_THROWS(iParser->GetNextSegmentUri(), HlsEndOfStream);
    TEST(iParser->StreamEnded() == true);
}

void SuiteHlsPlaylistParser::TestPlaylistCrLf()
{
    // Test a variant playlist that uses "\r\n" as line terminators.
    const Brn kFileMediaCrLf(
    "#EXTM3U\r\n"
    "#EXT-X-VERSION:2\r\n"
    "#EXT-X-TARGETDURATION:6\r\n"
    "#EXT-X-MEDIA-SEQUENCE:1234\r\n"
    "\r\n"
    "#EXTINF:6,\r\n"
    "https://priv.example.com/a.ts\n"
    "#EXTINF:5,\r\n"
    "https://priv.example.com/b.ts\n"
    "#EXTINF:4,\r\n"
    "https://priv.example.com/c.ts\n"
    );
    ReaderBuffer reader(kFileMediaCrLf);
    iParser->Parse(reader);

    TEST(iParser->TargetDurationMs() == 6000);
    TEST(iParser->StreamEnded() == false);

    auto sd1 = iParser->GetNextSegmentUri();
    TEST(sd1.Index() == 1234);
    TEST(sd1.SegmentUri() == Brn("https://priv.example.com/a.ts"));
    TEST(sd1.DurationMs() == 6000);

    auto sd2 = iParser->GetNextSegmentUri();
    TEST(sd2.Index() == 1235);
    TEST(sd2.SegmentUri() == Brn("https://priv.example.com/b.ts"));
    TEST(sd2.DurationMs() == 5000);

    auto sd3 = iParser->GetNextSegmentUri();
    TEST(sd3.Index() == 1236);
    TEST(sd3.SegmentUri() == Brn("https://priv.example.com/c.ts"));
    TEST(sd3.DurationMs() == 4000);

    TEST_THROWS(iParser->GetNextSegmentUri(), HlsNoMoreSegments);
    TEST(iParser->StreamEnded() == false);
}

void SuiteHlsPlaylistParser::TestUnsupportedTag()
{
    // Test version 3 playlist with EXT-X-KEY tags. Should skip over tags
    // (would fail to decrypt in real-world use, but just want to check
    // unrecognised tags are successfully skipped here).
    const Brn kFileVersion3Encrypted(
    "#EXTM3U\n"
    "#EXT-X-VERSION:3\n"
    "#EXT-X-MEDIA-SEQUENCE:7794\n"
    "#EXT-X-TARGETDURATION:15\n"
    "\n"
    "#EXT-X-KEY:METHOD=AES-128,URI=\"https://priv.example.com/key.php?r=52\"\n"
    "\n"
    "#EXTINF:2.833,\n"
    "http://media.example.com/fileSequence52-A.ts\n"
    "#EXTINF:15.0,\n"
    "http://media.example.com/fileSequence52-B.ts\n"
    "#EXTINF:13.333,\n"
    "http://media.example.com/fileSequence52-C.ts\n"
    "\n"
    "#EXT-X-KEY:METHOD=AES-128,URI=\"https://priv.example.com/key.php?r=53\"\n"
    "\n"
    "#EXTINF:15.0,\n"
    "http://media.example.com/fileSequence53-A.ts\n");
    ReaderBuffer reader2(kFileVersion3Encrypted);
    iParser->Parse(reader2);

    TEST(iParser->TargetDurationMs() == 15000);
    TEST(iParser->StreamEnded() == false);

    auto sd1 = iParser->GetNextSegmentUri();
    TEST(sd1.Index() == 7794);
    TEST(sd1.SegmentUri() == Brn("http://media.example.com/fileSequence52-A.ts"));
    TEST(sd1.DurationMs() == 2833);

    auto sd2 = iParser->GetNextSegmentUri();
    TEST(sd2.Index() == 7795);
    TEST(sd2.SegmentUri() == Brn("http://media.example.com/fileSequence52-B.ts"));
    TEST(sd2.DurationMs() == 15000);

    auto sd3 = iParser->GetNextSegmentUri();
    TEST(sd3.Index() == 7796);
    TEST(sd3.SegmentUri() == Brn("http://media.example.com/fileSequence52-C.ts"));
    TEST(sd3.DurationMs() == 13333);

    // Should skip over EXT-X-KEY tag here.
    auto sd4 = iParser->GetNextSegmentUri();
    TEST(sd4.Index() == 7797);
    TEST(sd4.SegmentUri() == Brn("http://media.example.com/fileSequence53-A.ts"));
    TEST(sd4.DurationMs() == 15000);

    TEST_THROWS(iParser->GetNextSegmentUri(), HlsNoMoreSegments);
    TEST(iParser->StreamEnded() == false);
}

void SuiteHlsPlaylistParser::TestInvalidAttributes()
{
    // Test attempting to load a malformed playlist where EXT-X-TARGETDURATION
    // is not a numeric value.
    const Brn kFileInvalidTargetDuration(
    "#EXTM3U\r\n"
    "#EXT-X-VERSION:2\r\n"
    "#EXT-X-TARGETDURATION:abc\r\n"
    "#EXT-X-MEDIA-SEQUENCE:1234\r\n"
    "\r\n"
    "#EXTINF:6,\r\n"
    "https://priv.example.com/a.ts\n"
    "#EXTINF:5,\r\n"
    "https://priv.example.com/b.ts\n"
    "#EXTINF:4,\r\n"
    "https://priv.example.com/c.ts\n"
    );
    ReaderBuffer reader(kFileInvalidTargetDuration);
    TEST_THROWS(iParser->Parse(reader), HlsPlaylistInvalid);
    TEST(iParser->TargetDurationMs() == 0);
    TEST(iParser->StreamEnded() == false);
    TEST_THROWS(iParser->GetNextSegmentUri(), HlsPlaylistInvalid);
}


// MockHlsPlaylistProvider

MockHlsPlaylistProvider::MockHlsPlaylistProvider()
    : iCurrentIdx(0)
    , iNextIdx(0)
    , iInterrupted(false)
{
}

MockHlsPlaylistProvider::~MockHlsPlaylistProvider()
{
    for (auto u : iUris) {
        delete u;
    }
}

void MockHlsPlaylistProvider::QueuePlaylist(const Brn aUri, const Brn aPlaylist)
{
    iUris.push_back(new Uri(aUri));
    iPlaylists.push_back(aPlaylist);
}

IReader& MockHlsPlaylistProvider::Reload()
{
    if (iInterrupted) {
        THROW(HlsPlaylistProviderError);
    }
    if (iNextIdx >= iPlaylists.size()) {
        THROW(HlsPlaylistProviderError);
    }

    iReader.Set(iPlaylists[iNextIdx]);
    iCurrentIdx = iNextIdx;
    iNextIdx++;
    return iReader;
}

const Uri& MockHlsPlaylistProvider::GetUri() const
{
    if (iInterrupted) {
        THROW(HlsPlaylistProviderError);
    }
    if (iCurrentIdx >= iUris.size()) {
        THROW(HlsPlaylistProviderError);
    }
    return *iUris[iCurrentIdx];
}

void MockHlsPlaylistProvider::InterruptPlaylistProvider(TBool aInterrupt)
{
    iInterrupted = aInterrupt;
    iReader.ReadInterrupt();
}


// MockReloadTimer

MockReloadTimer::MockReloadTimer(OpenHome::Test::ITestPipeWritable& aTestPipe)
    : iTestPipe(aTestPipe)
{
}

void MockReloadTimer::Restart()
{
    iTestPipe.Write(Brn("MRT::Restart"));
}

void MockReloadTimer::Wait(TUint aWaitMs)
{
    Bws<128> buf("MRT::Wait ");
    Ascii::AppendDec(buf, aWaitMs);
    iTestPipe.Write(buf);
}

void MockReloadTimer::InterruptReloadTimer()
{
    iTestPipe.Write(Brn("MRT::InterruptReloadTimer"));
}


// SuiteHlsM3uReader

SuiteHlsM3uReader::SuiteHlsM3uReader()
    : SuiteUnitTest("SuiteHlsM3uReader")
{
    AddTest(MakeFunctor(*this, &SuiteHlsM3uReader::TestPlaylistNoMediaSequence), "TestPlaylistNoMediaSequence");
    AddTest(MakeFunctor(*this, &SuiteHlsM3uReader::TestPlaylistMediaSequenceStartZero), "TestPlaylistMediaSequenceStartZero");
    AddTest(MakeFunctor(*this, &SuiteHlsM3uReader::TestPlaylistMediaSequenceStartNonZero), "TestPlaylistMediaSequenceStartNonZero");
    AddTest(MakeFunctor(*this, &SuiteHlsM3uReader::TestPlaylistClientDefinedStart), "TestPlaylistClientDefinedStart");
    AddTest(MakeFunctor(*this, &SuiteHlsM3uReader::TestPlaylistClientDefinedStartBeforeSequenceStart), "TestPlaylistClientDefinedStartBeforeSequenceStart");
    AddTest(MakeFunctor(*this, &SuiteHlsM3uReader::TestReload), "TestReload");
    AddTest(MakeFunctor(*this, &SuiteHlsM3uReader::TestReloadNoChange), "TestReloadNoChange");
    AddTest(MakeFunctor(*this, &SuiteHlsM3uReader::TestReloadNonContinuous), "TestReloadNonContinuous");
    AddTest(MakeFunctor(*this, &SuiteHlsM3uReader::TestEndlist), "TestEndlist");
    AddTest(MakeFunctor(*this, &SuiteHlsM3uReader::TestUnsupportedTag), "TestUnsupportedTag");
    AddTest(MakeFunctor(*this, &SuiteHlsM3uReader::TestInvalidPlaylist), "TestInvalidPlaylist");
}

void SuiteHlsM3uReader::Setup()
{
    iTestPipe = new TestPipeDynamic();
    iProvider = new MockHlsPlaylistProvider();
    iReloadTimer = new MockReloadTimer(*iTestPipe);
    iM3uReader = new HlsM3uReader(*iProvider, *iReloadTimer);
}

void SuiteHlsM3uReader::TearDown()
{
    TEST(iTestPipe->ExpectEmpty());
    delete iM3uReader;
    delete iReloadTimer;
    delete iProvider;
    delete iTestPipe;
}

void SuiteHlsM3uReader::TestPlaylistNoMediaSequence()
{
    const Brn kUri("http://www.example.com/playlist.m3u8");
    const Brn kFileNoMediaSeq(
    "#EXTM3U\n"
    "#EXT-X-VERSION:2\n"
    "#EXT-X-TARGETDURATION:6\n"
    "\n"
    "#EXTINF:6,\n"
    "https://priv.example.com/a.ts\n"
    "#EXTINF:5,\n"
    "https://priv.example.com/b.ts\n"
    "#EXTINF:4,\n"
    "https://priv.example.com/c.ts\n"
    );
    iProvider->QueuePlaylist(kUri, kFileNoMediaSeq);

    Uri uri;
    TUint durationMs = iM3uReader->NextSegmentUri(uri);
    // First load of playlist, so should have reset reload timer.
    TEST(iTestPipe->Expect(Brn("MRT::Restart")));
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/a.ts"));
    TEST(durationMs == 6000);
    TEST(iM3uReader->LastSegment() == 0);

    durationMs = iM3uReader->NextSegmentUri(uri);
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/b.ts"));
    TEST(durationMs == 5000);
    TEST(iM3uReader->LastSegment() == 1);

    durationMs = iM3uReader->NextSegmentUri(uri);
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/c.ts"));
    TEST(durationMs == 4000);
    TEST(iM3uReader->LastSegment() == 2);

    TEST_THROWS(iM3uReader->NextSegmentUri(uri), HlsSegmentUriError);
    // Should have attempted to reload playlist, so should have waited.
    TEST(iTestPipe->Expect(Brn("MRT::Wait 6000")));
}

void SuiteHlsM3uReader::TestPlaylistMediaSequenceStartZero()
{
    const Brn kUri("http://www.example.com/playlist.m3u8");
    const Brn kFileMediaSeqStartZero(
    "#EXTM3U\n"
    "#EXT-X-VERSION:2\n"
    "#EXT-X-TARGETDURATION:6\n"
    "#EXT-X-MEDIA-SEQUENCE:0\n"
    "\n"
    "#EXTINF:6,\n"
    "https://priv.example.com/a.ts\n"
    "#EXTINF:5,\n"
    "https://priv.example.com/b.ts\n"
    "#EXTINF:4,\n"
    "https://priv.example.com/c.ts\n"
    );
    iProvider->QueuePlaylist(kUri, kFileMediaSeqStartZero);

    Uri uri;
    TUint durationMs = iM3uReader->NextSegmentUri(uri);
    // First load of playlist, so should have reset reload timer.
    TEST(iTestPipe->Expect(Brn("MRT::Restart")));
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/a.ts"));
    TEST(durationMs == 6000);
    TEST(iM3uReader->LastSegment() == 0);

    durationMs = iM3uReader->NextSegmentUri(uri);
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/b.ts"));
    TEST(durationMs == 5000);
    TEST(iM3uReader->LastSegment() == 1);

    durationMs = iM3uReader->NextSegmentUri(uri);
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/c.ts"));
    TEST(durationMs == 4000);
    TEST(iM3uReader->LastSegment() == 2);

    TEST_THROWS(iM3uReader->NextSegmentUri(uri), HlsSegmentUriError);
    // Should have attempted to reload playlist, so should have waited.
    TEST(iTestPipe->Expect(Brn("MRT::Wait 6000")));
}

void SuiteHlsM3uReader::TestPlaylistMediaSequenceStartNonZero()
{
    const Brn kUri("http://www.example.com/playlist.m3u8");
    const Brn kFileMediaSeqStartNonZero(
    "#EXTM3U\n"
    "#EXT-X-VERSION:2\n"
    "#EXT-X-TARGETDURATION:6\n"
    "#EXT-X-MEDIA-SEQUENCE:1234\n"
    "\n"
    "#EXTINF:6,\n"
    "https://priv.example.com/a.ts\n"
    "#EXTINF:5,\n"
    "https://priv.example.com/b.ts\n"
    "#EXTINF:4,\n"
    "https://priv.example.com/c.ts\n"
    );
    iProvider->QueuePlaylist(kUri, kFileMediaSeqStartNonZero);

    Uri uri;
    TUint durationMs = iM3uReader->NextSegmentUri(uri);
    // First load of playlist, so should have reset reload timer.
    TEST(iTestPipe->Expect(Brn("MRT::Restart")));
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/a.ts"));
    TEST(durationMs == 6000);
    TEST(iM3uReader->LastSegment() == 1234);

    durationMs = iM3uReader->NextSegmentUri(uri);
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/b.ts"));
    TEST(durationMs == 5000);
    TEST(iM3uReader->LastSegment() == 1235);

    durationMs = iM3uReader->NextSegmentUri(uri);
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/c.ts"));
    TEST(durationMs == 4000);
    TEST(iM3uReader->LastSegment() == 1236);

    TEST_THROWS(iM3uReader->NextSegmentUri(uri), HlsSegmentUriError);
    // Should have attempted to reload playlist, so should have waited.
    TEST(iTestPipe->Expect(Brn("MRT::Wait 6000")));
}

void SuiteHlsM3uReader::TestPlaylistClientDefinedStart()
{
    const Brn kUri("http://www.example.com/playlist.m3u8");
    const Brn kFileMediaSeqStartNonZero(
    "#EXTM3U\n"
    "#EXT-X-VERSION:2\n"
    "#EXT-X-TARGETDURATION:6\n"
    "#EXT-X-MEDIA-SEQUENCE:1234\n"
    "\n"
    "#EXTINF:6,\n"
    "https://priv.example.com/a.ts\n"
    "#EXTINF:5,\n"
    "https://priv.example.com/b.ts\n"
    "#EXTINF:4,\n"
    "https://priv.example.com/c.ts\n"
    );
    iProvider->QueuePlaylist(kUri, kFileMediaSeqStartNonZero);

    // Set a client-defined start that is within this playlist.
    iM3uReader->SetStartSegment(1235);  // Second entry in playlist.

    Uri uri;
    TUint durationMs = iM3uReader->NextSegmentUri(uri);
    // First load of playlist, so should have reset reload timer.
    TEST(iTestPipe->Expect(Brn("MRT::Restart")));
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/b.ts"));
    TEST(durationMs == 5000);
    TEST(iM3uReader->LastSegment() == 1235);

    durationMs = iM3uReader->NextSegmentUri(uri);
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/c.ts"));
    TEST(durationMs == 4000);
    TEST(iM3uReader->LastSegment() == 1236);

    TEST_THROWS(iM3uReader->NextSegmentUri(uri), HlsSegmentUriError);
    // Should have attempted to reload playlist, so should have waited.
    TEST(iTestPipe->Expect(Brn("MRT::Wait 6000")));
}

void SuiteHlsM3uReader::TestPlaylistClientDefinedStartBeforeSequenceStart()
{
    const Brn kUri("http://www.example.com/playlist.m3u8");
    const Brn kFileMediaSeqStartNonZero(
    "#EXTM3U\n"
    "#EXT-X-VERSION:2\n"
    "#EXT-X-TARGETDURATION:6\n"
    "#EXT-X-MEDIA-SEQUENCE:1234\n"
    "\n"
    "#EXTINF:6,\n"
    "https://priv.example.com/a.ts\n"
    "#EXTINF:5,\n"
    "https://priv.example.com/b.ts\n"
    "#EXTINF:4,\n"
    "https://priv.example.com/c.ts\n"
    );
    iProvider->QueuePlaylist(kUri, kFileMediaSeqStartNonZero);

    // Set a client-defined start that is before this playlist.
    // As client-defined start is before playlist, should just start returning from first entry.
    iM3uReader->SetStartSegment(1233);  // Entry before first in playlist.

    Uri uri;
    TUint durationMs = iM3uReader->NextSegmentUri(uri);
    // First load of playlist, so should have reset reload timer.
    TEST(iTestPipe->Expect(Brn("MRT::Restart")));
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/a.ts"));
    TEST(durationMs == 6000);
    TEST(iM3uReader->LastSegment() == 1234);

    durationMs = iM3uReader->NextSegmentUri(uri);
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/b.ts"));
    TEST(durationMs == 5000);
    TEST(iM3uReader->LastSegment() == 1235);

    durationMs = iM3uReader->NextSegmentUri(uri);
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/c.ts"));
    TEST(durationMs == 4000);
    TEST(iM3uReader->LastSegment() == 1236);

    TEST_THROWS(iM3uReader->NextSegmentUri(uri), HlsSegmentUriError);
    // Should have attempted to reload playlist, so should have waited.
    TEST(iTestPipe->Expect(Brn("MRT::Wait 6000")));
}

void SuiteHlsM3uReader::TestReload()
{
    const Brn kUri1("http://www.example.com/playlist.m3u8");
    const Brn kFileMediaSeqStartNonZero1(
    "#EXTM3U\n"
    "#EXT-X-VERSION:2\n"
    "#EXT-X-TARGETDURATION:6\n"
    "#EXT-X-MEDIA-SEQUENCE:1234\n"
    "\n"
    "#EXTINF:6,\n"
    "https://priv.example.com/a.ts\n"
    "#EXTINF:5,\n"
    "https://priv.example.com/b.ts\n"
    "#EXTINF:4,\n"
    "https://priv.example.com/c.ts\n"
    );

    const Brn kUri2("http://www.example.com/playlist.m3u8");
    const Brn kFileMediaSeqStartNonZero2(
    "#EXTM3U\n"
    "#EXT-X-VERSION:2\n"
    "#EXT-X-TARGETDURATION:6\n"
    "#EXT-X-MEDIA-SEQUENCE:1235\n"
    "\n"
    "#EXTINF:5,\n"
    "https://priv.example.com/b.ts\n"
    "#EXTINF:4,\n"
    "https://priv.example.com/c.ts\n"
    "#EXTINF:3,\n"
    "https://priv.example.com/d.ts\n"
    );
    iProvider->QueuePlaylist(kUri1, kFileMediaSeqStartNonZero1);
    iProvider->QueuePlaylist(kUri2, kFileMediaSeqStartNonZero2);

    Uri uri;
    TUint durationMs = iM3uReader->NextSegmentUri(uri);
    // First load of playlist, so should have reset reload timer.
    TEST(iTestPipe->Expect(Brn("MRT::Restart")));
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/a.ts"));
    TEST(durationMs == 6000);
    TEST(iM3uReader->LastSegment() == 1234);

    durationMs = iM3uReader->NextSegmentUri(uri);
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/b.ts"));
    TEST(durationMs == 5000);
    TEST(iM3uReader->LastSegment() == 1235);

    durationMs = iM3uReader->NextSegmentUri(uri);
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/c.ts"));
    TEST(durationMs == 4000);
    TEST(iM3uReader->LastSegment() == 1236);

    durationMs = iM3uReader->NextSegmentUri(uri);
    // Should have attempted to reload playlist, so should have waited.
    TEST(iTestPipe->Expect(Brn("MRT::Wait 6000")));
    // Should also have reset reload timer.
    TEST(iTestPipe->Expect(Brn("MRT::Restart")));
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/d.ts"));
    TEST(durationMs == 3000);
    TEST(iM3uReader->LastSegment() == 1237);

    TEST_THROWS(iM3uReader->NextSegmentUri(uri), HlsSegmentUriError);
    // Should have attempted to reload playlist, so should have waited.
    TEST(iTestPipe->Expect(Brn("MRT::Wait 6000")));
}

void SuiteHlsM3uReader::TestReloadNoChange()
{
    const Brn kUri1("http://www.example.com/playlist.m3u8");
    const Brn kFileMediaSeqStartNonZero1(
    "#EXTM3U\n"
    "#EXT-X-VERSION:2\n"
    "#EXT-X-TARGETDURATION:6\n"
    "#EXT-X-MEDIA-SEQUENCE:1234\n"
    "\n"
    "#EXTINF:6,\n"
    "https://priv.example.com/a.ts\n"
    "#EXTINF:5,\n"
    "https://priv.example.com/b.ts\n"
    "#EXTINF:4,\n"
    "https://priv.example.com/c.ts\n"
    );

    const Brn kUri2("http://www.example.com/playlist.m3u8");
    const Brn kFileMediaSeqStartNonZero2(
    "#EXTM3U\n"
    "#EXT-X-VERSION:2\n"
    "#EXT-X-TARGETDURATION:6\n"
    "#EXT-X-MEDIA-SEQUENCE:1235\n"
    "\n"
    "#EXTINF:5,\n"
    "https://priv.example.com/b.ts\n"
    "#EXTINF:4,\n"
    "https://priv.example.com/c.ts\n"
    "#EXTINF:3,\n"
    "https://priv.example.com/d.ts\n"
    );
    // Queue up same playlist twice.
    iProvider->QueuePlaylist(kUri1, kFileMediaSeqStartNonZero1);
    iProvider->QueuePlaylist(kUri1, kFileMediaSeqStartNonZero1);
    iProvider->QueuePlaylist(kUri2, kFileMediaSeqStartNonZero2);

    Uri uri;
    TUint durationMs = iM3uReader->NextSegmentUri(uri);
    // First load of playlist, so should have reset reload timer.
    TEST(iTestPipe->Expect(Brn("MRT::Restart")));
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/a.ts"));
    TEST(durationMs == 6000);
    TEST(iM3uReader->LastSegment() == 1234);

    durationMs = iM3uReader->NextSegmentUri(uri);
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/b.ts"));
    TEST(durationMs == 5000);
    TEST(iM3uReader->LastSegment() == 1235);

    durationMs = iM3uReader->NextSegmentUri(uri);
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/c.ts"));
    TEST(durationMs == 4000);
    TEST(iM3uReader->LastSegment() == 1236);

    // Should attempt to reload unchanged playlist on first attempt.
    durationMs = iM3uReader->NextSegmentUri(uri);
    // Should have attempted to reload (same) playlist, so should have waited.
    TEST(iTestPipe->Expect(Brn("MRT::Wait 6000")));
    // Should also have reset reload timer.
    TEST(iTestPipe->Expect(Brn("MRT::Restart")));
    // When attempting next try, should have halved wait duration for next playlist, as previous playlist was unchanged.
    TEST(iTestPipe->Expect(Brn("MRT::Wait 3000")));
    // Should also have reset reload timer.
    TEST(iTestPipe->Expect(Brn("MRT::Restart")));

    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/d.ts"));
    TEST(durationMs == 3000);
    TEST(iM3uReader->LastSegment() == 1237);

    TEST_THROWS(iM3uReader->NextSegmentUri(uri), HlsSegmentUriError);
    // Should have attempted to reload playlist, so should have waited.
    TEST(iTestPipe->Expect(Brn("MRT::Wait 6000")));
}

void SuiteHlsM3uReader::TestReloadNonContinuous()
{
    const Brn kUri1("http://www.example.com/playlist.m3u8");
    const Brn kFileMediaSeqStartNonZero1(
    "#EXTM3U\n"
    "#EXT-X-VERSION:2\n"
    "#EXT-X-TARGETDURATION:6\n"
    "#EXT-X-MEDIA-SEQUENCE:1234\n"
    "\n"
    "#EXTINF:6,\n"
    "https://priv.example.com/a.ts\n"
    "#EXTINF:5,\n"
    "https://priv.example.com/b.ts\n"
    "#EXTINF:4,\n"
    "https://priv.example.com/c.ts\n"
    );

    // Reload of playlist will have a EXT-X-MEDIA-SEQUENCE which is 1 greater than expected, following on from last playlist.
    const Brn kUri2("http://www.example.com/playlist.m3u8");
    const Brn kFileMediaSeqStartNonZero2(
    "#EXTM3U\n"
    "#EXT-X-VERSION:2\n"
    "#EXT-X-TARGETDURATION:6\n"
    "#EXT-X-MEDIA-SEQUENCE:1238\n"
    "\n"
    "#EXTINF:3,\n"
    "https://priv.example.com/e.ts\n"
    "#EXTINF:2,\n"
    "https://priv.example.com/f.ts\n"
    "#EXTINF:1,\n"
    "https://priv.example.com/g.ts\n"
    );
    iProvider->QueuePlaylist(kUri1, kFileMediaSeqStartNonZero1);
    iProvider->QueuePlaylist(kUri2, kFileMediaSeqStartNonZero2);

    Uri uri;
    TUint durationMs = iM3uReader->NextSegmentUri(uri);
    // First load of playlist, so should have reset reload timer.
    TEST(iTestPipe->Expect(Brn("MRT::Restart")));
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/a.ts"));
    TEST(durationMs == 6000);
    TEST(iM3uReader->LastSegment() == 1234);

    durationMs = iM3uReader->NextSegmentUri(uri);
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/b.ts"));
    TEST(durationMs == 5000);
    TEST(iM3uReader->LastSegment() == 1235);

    durationMs = iM3uReader->NextSegmentUri(uri);
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/c.ts"));
    TEST(durationMs == 4000);
    TEST(iM3uReader->LastSegment() == 1236);

    // Exhausted last playlist. New playlist should be loaded, with discontinuity encountered.
    TEST_THROWS(iM3uReader->NextSegmentUri(uri), HlsSegmentUriError);
    // Should have attempted to reload playlist, so should have waited.
    TEST(iTestPipe->Expect(Brn("MRT::Wait 6000")));
    TEST(iTestPipe->Expect(Brn("MRT::Restart")));
}

void SuiteHlsM3uReader::TestEndlist()
{
    const Brn kUri1("http://www.example.com/playlist.m3u8");
    const Brn kFileMediaSeqStartNonZero1(
    "#EXTM3U\n"
    "#EXT-X-VERSION:2\n"
    "#EXT-X-TARGETDURATION:6\n"
    "#EXT-X-MEDIA-SEQUENCE:1234\n"
    "\n"
    "#EXTINF:6,\n"
    "https://priv.example.com/a.ts\n"
    "#EXTINF:5,\n"
    "https://priv.example.com/b.ts\n"
    "#EXTINF:4,\n"
    "https://priv.example.com/c.ts\n"
    );
    const Brn kUri2("http://www.example.com/playlist.m3u8");
    const Brn kFileMediaSeqStartNonZero2(
    "#EXTM3U\n"
    "#EXT-X-VERSION:2\n"
    "#EXT-X-TARGETDURATION:6\n"
    "#EXT-X-MEDIA-SEQUENCE:1235\n"
    "\n"
    "#EXTINF:5,\n"
    "https://priv.example.com/b.ts\n"
    "#EXTINF:4,\n"
    "https://priv.example.com/c.ts\n"
    "#EXTINF:3,\n"
    "https://priv.example.com/d.ts\n"
    "#EXT-X-ENDLIST\n"
    );
    iProvider->QueuePlaylist(kUri1, kFileMediaSeqStartNonZero1);
    iProvider->QueuePlaylist(kUri2, kFileMediaSeqStartNonZero2);

    Uri uri;
    TUint durationMs = iM3uReader->NextSegmentUri(uri);
    // First load of playlist, so should have reset reload timer.
    TEST(iTestPipe->Expect(Brn("MRT::Restart")));
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/a.ts"));
    TEST(durationMs == 6000);
    TEST(iM3uReader->LastSegment() == 1234);

    durationMs = iM3uReader->NextSegmentUri(uri);
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/b.ts"));
    TEST(durationMs == 5000);
    TEST(iM3uReader->LastSegment() == 1235);

    durationMs = iM3uReader->NextSegmentUri(uri);
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/c.ts"));
    TEST(durationMs == 4000);
    TEST(iM3uReader->LastSegment() == 1236);

    durationMs = iM3uReader->NextSegmentUri(uri);
    // Should have attempted to reload playlist, so should have waited.
    TEST(iTestPipe->Expect(Brn("MRT::Wait 6000")));
    // Should also have reset reload timer.
    TEST(iTestPipe->Expect(Brn("MRT::Restart")));
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/d.ts"));
    TEST(durationMs == 3000);
    TEST(iM3uReader->LastSegment() == 1237);

    // Exhausted last playlist and should have encountered end-of-stream.
    TEST_THROWS(iM3uReader->NextSegmentUri(uri), HlsEndOfStream);
}

void SuiteHlsM3uReader::TestUnsupportedTag()
{
    const Brn kUri("http://www.example.com/playlist.m3u8");
    // Test version 3 playlist with EXT-X-KEY tags. Should skip over tags
    // (would fail to decrypt in real-world use, but just want to check
    // unrecognised tags are successfully skipped here).
    const Brn kFileEncrypted(
    "#EXTM3U\n"
    "#EXT-X-VERSION:2\n"
    "#EXT-X-TARGETDURATION:6\n"
    "#EXT-X-MEDIA-SEQUENCE:1234\n"
    "\n"
    "#EXT-X-KEY:METHOD=AES-128,URI=\"https://priv.example.com/key.php?r=52\"\n"
    "\n"
    "#EXTINF:6,\n"
    "https://priv.example.com/a.ts\n"
    "#EXTINF:5,\n"
    "https://priv.example.com/b.ts\n"
    "\n"
    "#EXT-X-KEY:METHOD=AES-128,URI=\"https://priv.example.com/key.php?r=53\"\n"
    "\n"
    "#EXTINF:4,\n"
    "https://priv.example.com/c.ts\n"
    );
    iProvider->QueuePlaylist(kUri, kFileEncrypted);

    Uri uri;
    TUint durationMs = iM3uReader->NextSegmentUri(uri);
    // First load of playlist, so should have reset reload timer.
    TEST(iTestPipe->Expect(Brn("MRT::Restart")));
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/a.ts"));
    TEST(durationMs == 6000);
    TEST(iM3uReader->LastSegment() == 1234);

    durationMs = iM3uReader->NextSegmentUri(uri);
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/b.ts"));
    TEST(durationMs == 5000);
    TEST(iM3uReader->LastSegment() == 1235);

    durationMs = iM3uReader->NextSegmentUri(uri);
    TEST(uri.AbsoluteUri() == Brn("https://priv.example.com/c.ts"));
    TEST(durationMs == 4000);
    TEST(iM3uReader->LastSegment() == 1236);

    TEST_THROWS(iM3uReader->NextSegmentUri(uri), HlsSegmentUriError);
    // Should have attempted to reload playlist, so should have waited.
    TEST(iTestPipe->Expect(Brn("MRT::Wait 6000")));
}

void SuiteHlsM3uReader::TestInvalidPlaylist()
{
    // Test attempting to load a malformed playlist where EXT-X-TARGETDURATION
    // is not a numeric value.
    const Brn kUri("http://www.example.com/playlist.m3u8");
    const Brn kFileInvalidTargetDuration(
    "#EXTM3U\r\n"
    "#EXT-X-VERSION:2\r\n"
    "#EXT-X-TARGETDURATION:abc\r\n"
    "#EXT-X-MEDIA-SEQUENCE:1234\r\n"
    "\r\n"
    "#EXTINF:6,\r\n"
    "https://priv.example.com/a.ts\n"
    "#EXTINF:5,\r\n"
    "https://priv.example.com/b.ts\n"
    "#EXTINF:4,\r\n"
    "https://priv.example.com/c.ts\n"
    );
    iProvider->QueuePlaylist(kUri, kFileInvalidTargetDuration);
    Uri uri;
    TEST_THROWS(iM3uReader->NextSegmentUri(uri), HlsSegmentUriError);
}


// MockHlsSegmentProvider

MockHlsSegmentProvider::MockHlsSegmentProvider()
    : iSegment(0)
    , iInterrupted(false)
    , iStreamEndSet(false)
{
}

void MockHlsSegmentProvider::QueueSegment(const Brn aSegment)
{
    ASSERT(!iStreamEndSet);
    iSegments.push_back(aSegment);
}

void MockHlsSegmentProvider::SetStreamEnd()
{
    iStreamEndSet = true;
}

IReader& MockHlsSegmentProvider::NextSegment()
{
    if (iInterrupted) {
        THROW(HlsSegmentError);
    }
    if (iSegment >= iSegments.size()) {
        if (iStreamEndSet) {
            THROW(HlsEndOfStream);
        }
        THROW(HlsSegmentError);
    }

    iReader.Set(iSegments[iSegment]);
    iSegment++;
    return iReader;
}

void MockHlsSegmentProvider::InterruptSegmentProvider(TBool aInterrupt)
{
    iInterrupted = aInterrupt;
    iReader.ReadInterrupt();
}


// SuiteHlsSegmentStreamer

SuiteHlsSegmentStreamer::SuiteHlsSegmentStreamer()
    : SuiteUnitTest("SuiteHlsSegmentStreamer")
{
    AddTest(MakeFunctor(*this, &SuiteHlsSegmentStreamer::TestSingleSegmentReadFull), "TestSingleSegmentReadFull");
    AddTest(MakeFunctor(*this, &SuiteHlsSegmentStreamer::TestSingleSegmentReadIncrements), "TestSingleSegmentReadIncrements");
    AddTest(MakeFunctor(*this, &SuiteHlsSegmentStreamer::TestMultipleSegmentsReadFullExact), "TestMultipleSegmentsReadFullExact");
    AddTest(MakeFunctor(*this, &SuiteHlsSegmentStreamer::TestMultipleSegmentsReadFullMoreThan), "TestMultipleSegmentsReadFullMoreThan");
    AddTest(MakeFunctor(*this, &SuiteHlsSegmentStreamer::TestMultipleSegmentsReadIncrements), "TestMultipleSegmentsReadIncrements");
    AddTest(MakeFunctor(*this, &SuiteHlsSegmentStreamer::TestEndOfStreamReadExact), "TestEndOfStreamReadExact");
    AddTest(MakeFunctor(*this, &SuiteHlsSegmentStreamer::TestEndOfStreamReadMoreThan), "TestEndOfStreamReadMoreThan");
}

void SuiteHlsSegmentStreamer::Setup()
{
    iProvider = new MockHlsSegmentProvider();
    iStreamer = new SegmentStreamer(*iProvider);
}

void SuiteHlsSegmentStreamer::TearDown()
{
    delete iStreamer;
    delete iProvider;
}

void SuiteHlsSegmentStreamer::TestSingleSegmentReadFull()
{
    const Brn kSegment1("123123123");
    iProvider->QueueSegment(kSegment1);

    auto buf = iStreamer->Read(9);
    TEST(buf == kSegment1);

    buf = iStreamer->Read(9);
    TEST(buf.Bytes() == 0); // End-of-stream condition.

    TEST_THROWS(iStreamer->Read(9), ReaderError);
}

void SuiteHlsSegmentStreamer::TestSingleSegmentReadIncrements()
{
    const Brn kSegment1("123123123");
    iProvider->QueueSegment(kSegment1);

    auto buf = iStreamer->Read(4);
    TEST(buf == Brn("1231"));

    buf = iStreamer->Read(4);
    TEST(buf == Brn("2312"));

    buf = iStreamer->Read(4);
    TEST(buf == Brn("3"));

    buf = iStreamer->Read(4);
    TEST(buf.Bytes() == 0); // End-of-stream condition.

    TEST_THROWS(iStreamer->Read(4), ReaderError);
}

void SuiteHlsSegmentStreamer::TestMultipleSegmentsReadFullExact()
{
    const Brn kSegment1("123123123");
    const Brn kSegment2("456456456");
    const Brn kSegment3("789789789");
    iProvider->QueueSegment(kSegment1);
    iProvider->QueueSegment(kSegment2);
    iProvider->QueueSegment(kSegment3);

    // Request exactly number of bytes in stream.
    // SegmentStreamer will return only what is available from the current segment.
    auto buf = iStreamer->Read(27);
    TEST(buf == Brn("123123123"));
    buf = iStreamer->Read(27);
    TEST(buf.Bytes() == 0); // End-of-stream condition.
    TEST_THROWS(iStreamer->Read(27), ReaderError);

    iStreamer->Reset();
    buf = iStreamer->Read(27);
    TEST(buf == Brn("456456456"));
    buf = iStreamer->Read(27);
    TEST(buf.Bytes() == 0); // End-of-stream condition.
    TEST_THROWS(iStreamer->Read(27), ReaderError);

    iStreamer->Reset();
    buf = iStreamer->Read(27);
    TEST(buf == Brn("789789789"));
    buf = iStreamer->Read(27);
    TEST(buf.Bytes() == 0); // End-of-stream condition.
    TEST_THROWS(iStreamer->Read(27), ReaderError);
}

void SuiteHlsSegmentStreamer::TestMultipleSegmentsReadFullMoreThan()
{
    const Brn kSegment1("123123123");
    const Brn kSegment2("456456456");
    const Brn kSegment3("789789789");
    iProvider->QueueSegment(kSegment1);
    iProvider->QueueSegment(kSegment2);
    iProvider->QueueSegment(kSegment3);

    // Request more than the number of bytes in stream.
    auto buf = iStreamer->Read(28);
    TEST(buf == Brn("123123123"));
    buf = iStreamer->Read(28);
    TEST(buf.Bytes() == 0); // End-of-stream condition.

    iStreamer->Reset();
    buf = iStreamer->Read(28);
    TEST(buf == Brn("456456456"));
    buf = iStreamer->Read(28);
    TEST(buf.Bytes() == 0); // End-of-stream condition.

    iStreamer->Reset();
    buf = iStreamer->Read(28);
    TEST(buf == Brn("789789789"));
    buf = iStreamer->Read(28);
    TEST(buf.Bytes() == 0); // End-of-stream condition.

    TEST_THROWS(iStreamer->Read(28), ReaderError);
}

void SuiteHlsSegmentStreamer::TestMultipleSegmentsReadIncrements()
{
    const Brn kSegment1("123123123");
    const Brn kSegment2("456456456");
    const Brn kSegment3("789789789");
    iProvider->QueueSegment(kSegment1);
    iProvider->QueueSegment(kSegment2);
    iProvider->QueueSegment(kSegment3);

    auto buf = iStreamer->Read(4);
    TEST(buf == Brn("1231"));
    buf = iStreamer->Read(4);
    TEST(buf == Brn("2312"));
    buf = iStreamer->Read(4);
    TEST(buf == Brn("3"));
    buf = iStreamer->Read(4);
    TEST(buf.Bytes() == 0); // End-of-stream condition.

    iStreamer->Reset();
    buf = iStreamer->Read(4);
    TEST(buf == Brn("4564"));
    buf = iStreamer->Read(4);
    TEST(buf == Brn("5645"));
    buf = iStreamer->Read(4);
    TEST(buf == Brn("6"));
    buf = iStreamer->Read(4);
    TEST(buf.Bytes() == 0); // End-of-stream condition.

    iStreamer->Reset();
    buf = iStreamer->Read(4);
    TEST(buf == Brn("7897"));
    buf = iStreamer->Read(4);
    TEST(buf == Brn("8978"));
    buf = iStreamer->Read(4);
    TEST(buf == Brn("9"));
    buf = iStreamer->Read(4);
    TEST(buf.Bytes() == 0); // End-of-stream condition.

    TEST_THROWS(iStreamer->Read(4), ReaderError);
}

void SuiteHlsSegmentStreamer::TestEndOfStreamReadExact()
{
    const Brn kSegment1("123123123");
    const Brn kSegment2("456456456");
    iProvider->QueueSegment(kSegment1);
    iProvider->QueueSegment(kSegment2);
    iProvider->SetStreamEnd();

    // Request more than the number of bytes in stream.
    auto buf = iStreamer->Read(27);
    TEST(buf == Brn("123123123"));
    buf = iStreamer->Read(27);
    TEST(buf.Bytes() == 0); // End-of-stream condition.

    iStreamer->Reset();
    buf = iStreamer->Read(27);
    TEST(buf == Brn("456456456"));
    buf = iStreamer->Read(27);
    TEST(buf.Bytes() == 0); // End-of-stream condition.

    TEST_THROWS(iStreamer->Read(27), ReaderError);
}

void SuiteHlsSegmentStreamer::TestEndOfStreamReadMoreThan()
{
    const Brn kSegment1("123123123");
    const Brn kSegment2("456456456");
    iProvider->QueueSegment(kSegment1);
    iProvider->QueueSegment(kSegment2);
    iProvider->SetStreamEnd();

    // Request more than the number of bytes in stream.
    auto buf = iStreamer->Read(28);
    TEST(buf == Brn("123123123"));
    buf = iStreamer->Read(28);
    TEST(buf.Bytes() == 0); // End-of-stream condition.

    iStreamer->Reset();
    buf = iStreamer->Read(28);
    TEST(buf == Brn("456456456"));
    buf = iStreamer->Read(28);
    TEST(buf.Bytes() == 0);

    TEST_THROWS(iStreamer->Read(28), ReaderError);
}



void TestProtocolHls(Environment& /*aEnv*/)
{
    Runner runner("HLS tests\n");
    runner.Add(new SuiteHlsSegmentDescriptor());
    runner.Add(new SuiteHlsPlaylistParser());
    runner.Add(new SuiteHlsM3uReader());
    runner.Add(new SuiteHlsSegmentStreamer());
    runner.Run();
}

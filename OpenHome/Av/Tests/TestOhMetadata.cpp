#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/SuiteUnitTest.h>

#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Av/OhMetadata.h>

#include <array>
#include <functional>

using namespace OpenHome;
using namespace OpenHome::TestFramework;

namespace OpenHome {
namespace Av {

using WriterCallback = std::function<void (WriterDIDLLite&, const Brx&)>;

// SuiteTestWriterDIDLLite
class SuiteWriterDIDLLite : public SuiteUnitTest
{
public:
    static const Brn kItemId;
    static const Brn kParentId;

public:
    SuiteWriterDIDLLite();

private:
    void Setup() override;
    void TearDown() override;

private:
    void TestWriteNothing();
    void TestWriteTitle();
    void TestWriteArtist();
    void TestWriteAlbum();
    void TestWriteGenre();
    void TestWriteArtwork();

    void TestWriteEmptyDoesNothing();

    void TestWriteOnceCalls(const Brx& aValueToWrite, WriterCallback aWriteCallback);
};

const Brn SuiteWriterDIDLLite::kItemId("itemId");
const Brn SuiteWriterDIDLLite::kParentId("parentId");

SuiteWriterDIDLLite::SuiteWriterDIDLLite()
    : SuiteUnitTest("SuiteWriterDIDLLite")
{
    AddTest(MakeFunctor(*this, &SuiteWriterDIDLLite::TestWriteNothing), "TestWriteNothing");
    AddTest(MakeFunctor(*this, &SuiteWriterDIDLLite::TestWriteEmptyDoesNothing), "TestWriteEmptyDoesNothing");
    AddTest(MakeFunctor(*this, &SuiteWriterDIDLLite::TestWriteTitle), "TestWriteTitle");
    AddTest(MakeFunctor(*this, &SuiteWriterDIDLLite::TestWriteAlbum), "TestWriteAlbum");
    AddTest(MakeFunctor(*this, &SuiteWriterDIDLLite::TestWriteArtist), "TestWriteArtist");
    AddTest(MakeFunctor(*this, &SuiteWriterDIDLLite::TestWriteGenre), "TestWriteGenre");
}

void SuiteWriterDIDLLite::Setup()
{ }

void SuiteWriterDIDLLite::TearDown()
{ }


void SuiteWriterDIDLLite::TestWriteNothing()
{
    WriterBwh writer(512);
    WriterDIDLLite subject(kItemId, DIDLLite::kItemTypeTrack, writer);

    subject.WriteEnd();

    const Brx& didl = writer.Buffer();
    TEST(Ascii::Contains(didl, kItemId));
    TEST(Ascii::Contains(didl, DIDLLite::kItemTypeTrack));

    TEST(Ascii::Contains(didl, DIDLLite::kTagTitle) == false);
    TEST(Ascii::Contains(didl, DIDLLite::kTagArtist) == false);
    TEST(Ascii::Contains(didl, DIDLLite::kTagDescription) == false);
}

void SuiteWriterDIDLLite::TestWriteTitle()
{
    const Brn kTitle("A Title");

    WriterCallback cb = [] (WriterDIDLLite& writer, const Brx& aValue) {
        writer.WriteTitle(aValue);
    };

    TestWriteOnceCalls(kTitle, cb);
}

void SuiteWriterDIDLLite::TestWriteAlbum()
{
    const Brn kAlbum("A Album");
    WriterCallback cb = [] (WriterDIDLLite& writer, const Brx& aValue) {
        writer.WriteAlbum(aValue);
    };

    TestWriteOnceCalls(kAlbum, cb);
}

void SuiteWriterDIDLLite::TestWriteArtist()
{
    const Brn kArtist("Artist");
    WriterCallback cb = [] (WriterDIDLLite& writer, const Brx& aValue) {
        writer.WriteArtist(aValue);
    };

    TestWriteOnceCalls(kArtist, cb);
}

void SuiteWriterDIDLLite::TestWriteGenre()
{
    const Brn kGenre("Genre A");
    WriterCallback cb = [] (WriterDIDLLite& writer, const Brx& aValue) {
        writer.WriteGenre(aValue);
    };

    TestWriteOnceCalls(kGenre, cb);
}

void SuiteWriterDIDLLite::TestWriteOnceCalls(const Brx& aValueToWrite,
                                             WriterCallback aWriteCallback)
{
    WriterBwh writer(512);
    WriterDIDLLite subject(kItemId, DIDLLite::kItemTypeTrack, writer);

    aWriteCallback(subject, aValueToWrite);

    const Brx& didl = writer.Buffer();
    TEST(Ascii::Contains(didl, kItemId));
    TEST(Ascii::Contains(didl, DIDLLite::kItemTypeTrack));
    TEST(Ascii::Contains(didl, aValueToWrite));

    TEST_THROWS(aWriteCallback(subject, Brx::Empty()), AssertionFailed);
    TEST_THROWS(aWriteCallback(subject, Brn("GARBAGE")), AssertionFailed);
    TEST_THROWS(aWriteCallback(subject, aValueToWrite), AssertionFailed);

    subject.WriteEnd();
    TEST_THROWS(aWriteCallback(subject, Brx::Empty()), AssertionFailed);
    TEST_THROWS(aWriteCallback(subject, Brn("GARBAGE")), AssertionFailed);
    TEST_THROWS(aWriteCallback(subject, aValueToWrite), AssertionFailed);
}

void SuiteWriterDIDLLite::TestWriteEmptyDoesNothing()
{
    WriterBwh writer(512);
    WriterDIDLLite subject(kItemId, DIDLLite::kItemTypeTrack, writer);

    subject.WriteTitle(Brx::Empty());
    subject.WriteArtist(Brx::Empty());
    subject.WriteAlbum(Brx::Empty());
    subject.WriteGenre(Brx::Empty());
    subject.WriteDescription(Brx::Empty());
    subject.WriteTrackNumber(Brx::Empty());
    subject.WriteArtwork(Brx::Empty());

    std::array<const Brn, 7> tags = {
        Brn(DIDLLite::kTagTitle),
        Brn(DIDLLite::kTagArtist),
        Brn(DIDLLite::kTagAlbumTitle),
        Brn(DIDLLite::kTagGenre),
        Brn(DIDLLite::kTagDescription),
        Brn(DIDLLite::kTagOriginalTrackNumber),
        Brn(DIDLLite::kTagArtwork),
    };

    const Brx& didl = writer.Buffer();
    for(const auto& val : tags) {
        TEST(Ascii::Contains(didl, val) == false);
    }
}


} // namespace Av
} // namespace OpenHome



void TestOhMetadata()
{
    Runner runner("ohMetadata tests\n");

    runner.Add(new OpenHome::Av::SuiteWriterDIDLLite());

    runner.Run();
}




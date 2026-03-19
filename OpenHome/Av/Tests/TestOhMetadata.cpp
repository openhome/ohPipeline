#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/SuiteUnitTest.h>

#include <OpenHome/Private/Ascii.h>
#include <OpenHome/OhMetadata.h>
#include <OpenHome/DidlLite.h>
#include <OpenHome/Net/Private/XmlParser.h>

#include <array>
#include <functional>

using namespace OpenHome;
using namespace OpenHome::TestFramework;

namespace OpenHome {

using WriterCallback = std::function<void (WriterDIDLLite&, const Brx&)>;

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

class SuiteDIDLLiteTruncator : public SuiteUnitTest
{
public:
    SuiteDIDLLiteTruncator();
private: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    void TestRegularMetadataUnchanged();
    void TestTooLongMetadataTruncated();
    void TestTooLongInvalidXmlRejected();
    void TestTooLongDidlRejected();
};

// SuiteWriterDIDLLite

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

    std::array<const Brn, 7> tags = { {
        DIDLLite::kTagTitle,
        DIDLLite::kTagArtist,
        DIDLLite::kTagAlbumTitle,
        DIDLLite::kTagGenre,
        DIDLLite::kTagDescription,
        DIDLLite::kTagOriginalTrackNumber,
        DIDLLite::kTagArtwork,
    } };

    const Brx& didl = writer.Buffer();
    for(const auto& val : tags) {
        TEST(Ascii::Contains(didl, val) == false);
    }
}


// SuiteDIDLLiteTruncator

SuiteDIDLLiteTruncator::SuiteDIDLLiteTruncator()
    : SuiteUnitTest("SuiteDIDLLiteTruncator")
{
    AddTest(MakeFunctor(*this, &SuiteDIDLLiteTruncator::TestRegularMetadataUnchanged), "TestRegularMetadataUnchanged");
    AddTest(MakeFunctor(*this, &SuiteDIDLLiteTruncator::TestTooLongMetadataTruncated), "TestTooLongMetadataTruncated");
    AddTest(MakeFunctor(*this, &SuiteDIDLLiteTruncator::TestTooLongInvalidXmlRejected), "TestTooLongInvalidXmlRejected");
    AddTest(MakeFunctor(*this, &SuiteDIDLLiteTruncator::TestTooLongDidlRejected), "TestTooLongDidlRejected");
}

void SuiteDIDLLiteTruncator::Setup()
{
}

void SuiteDIDLLiteTruncator::TearDown()
{
}

void SuiteDIDLLiteTruncator::TestRegularMetadataUnchanged()
{
    const Brn src("shortbitoftext");
    Bws<128> dest;
    DIDLLiteTruncator::CheckTruncate(src, dest);
    TEST(src == dest);
}

void SuiteDIDLLiteTruncator::TestTooLongMetadataTruncated()
{
    const char* src =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\""
        " xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\""
        " xmlns:dc=\"http://purl.org/dc/elements/1.1/\""
        " xmlns:oh=\"http://www.openhome.org\""
        ">"
        "<item id=\"12345\" parentID=\"-1\" restricted=\"1\">"
        "<dc:title>my title</dc:title>"
        "<upnp:artist>some artist</upnp:artist>"
        "<upnp:genre>aReallyReallyReallyReallyReallyReallyReallyReallyReallyReallyReallyReallyReallyReallyReallyReallyReallyReallyReallyLongGenre</upnp:genre>"
        "<upnp:albumArtURI>http://www.linn.co.uk</upnp:albumArtURI>"
        "<upnp:album>album title</upnp:album>"
        "</item>"
        "</DIDL-Lite>";

    Brn srcBuf(src);
    //LOG(kEssential, "\n\tsrcBuf.Bytes()=%u\n", srcBuf.Bytes());
    Bws<470> dest;
    DIDLLiteTruncator::CheckTruncate(srcBuf, dest);
    TEST(dest.Bytes() > 0);
    TEST(dest.Bytes() < srcBuf.Bytes());

    const auto didl = Net::XmlParserBasic::Find(Brn("DIDL-Lite"), dest);
    const auto item = Net::XmlParserBasic::Find(Brn("item"), didl);
    const auto title = Net::XmlParserBasic::Find(Brn("title"), item);
    TEST(title.Bytes() > 0);
    const auto artist = Net::XmlParserBasic::Find(Brn("artist"), item);
    TEST(artist.Bytes() > 0);
    const auto artwork = Net::XmlParserBasic::Find(Brn("albumArtURI"), item);
    TEST(artwork.Bytes() > 0);
    const auto album = Net::XmlParserBasic::Find(Brn("album"), item);
    TEST(album.Bytes() > 0);
}

void SuiteDIDLLiteTruncator::TestTooLongInvalidXmlRejected()
{
    const Brn src("shortbitoftext");
    Bws<8> dest;
    DIDLLiteTruncator::CheckTruncate(src, dest);
    TEST(src != dest);
    TEST(dest.Bytes() == 0);
}

void SuiteDIDLLiteTruncator::TestTooLongDidlRejected()
{
    const char* src =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>"
        "<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\""
        " xmlns:upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\""
        " xmlns:dc=\"http://purl.org/dc/elements/1.1/\""
        " xmlns:oh=\"http://www.openhome.org\""
        ">"
        "<item id=\"12345\" parentID=\"-1\" restricted=\"1\">"
        "<dc:title>my title</dc:title>"
        "<upnp:artist>some artist</upnp:artist>"
        "<upnp:genre>aReallyReallyReallyReallyReallyReallyReallyReallyReallyReallyReallyReallyReallyReallyReallyReallyReallyReallyReallyLongGenre</upnp:genre>"
        "<upnp:albumArtURI>http://www.linn.co.uk</upnp:albumArtURI>"
        "<upnp:album>album title</upnp:album>"
        "</item>"
        "</DIDL-Lite>";

    Brn srcBuf(src);
    Bws<256> dest;
    DIDLLiteTruncator::CheckTruncate(srcBuf, dest);
    TEST(dest.Bytes() == 0);
}


} // namespace OpenHome



void TestOhMetadata()
{
    Runner runner("ohMetadata tests\n");

    runner.Add(new OpenHome::SuiteWriterDIDLLite());
    runner.Add(new OpenHome::SuiteDIDLLiteTruncator());

    runner.Run();
}




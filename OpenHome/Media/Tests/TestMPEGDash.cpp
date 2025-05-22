#include <OpenHome/Private/Env.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/UnixTimestamp.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/SuiteUnitTest.h>
#include <OpenHome/Media/Protocol/MPEGDash.h>

using namespace OpenHome;
using namespace OpenHome::Media;
using namespace OpenHome::TestFramework;

// SuiteISO8601Duration
class SuiteISO8601Duration : public Suite
{
public:
    SuiteISO8601Duration() : Suite("SuiteISO8601Duration") {}

private: // Suite
    void Test() override;

private:
    void TestHours();
    void TestMins();
    void TestSeconds();
    void TestMultipart();
    void TestInvalidForms();
};

void SuiteISO8601Duration::Test()
{
    TestInvalidForms();
    TestHours();
    TestMins();
    TestSeconds();
    TestMultipart();
}

void SuiteISO8601Duration::TestHours()
{
    ISO8601Duration subject;

    TEST(subject.TryParse(Brn("PT1H")));
    TEST(subject.TotalSeconds() == Time::kSecondsPerHour);

    TEST(subject.TryParse(Brn("PT2H")));
    TEST(subject.TotalSeconds() == (2 * Time::kSecondsPerHour));

    TEST(subject.TryParse(Brn("PT1.5H")));
    TEST(subject.TotalSeconds() == (Time::kSecondsPerHour + (Time::kSecondsPerHour / 2)));

    // NOTE: European ',' can be used as well as '.' for seperators
    TEST(subject.TryParse(Brn("PT1,5H")));
    TEST(subject.TotalSeconds() == (Time::kSecondsPerHour + (Time::kSecondsPerHour / 2)));

    TEST(subject.TryParse(Brn("PT5.0H")));
    TEST(subject.TotalSeconds() == (5 * Time::kSecondsPerHour));

    TEST(subject.TryParse(Brn("PT1.0025H")));
    TEST(subject.TotalSeconds() == 3609); // Time::kSecondsPerHour + 9 (0.0025 * Time::kSecondsPerHour)
}

void SuiteISO8601Duration::TestMins()
{
    ISO8601Duration subject;

    TEST(subject.TryParse(Brn("PT1M")));
    TEST(subject.TotalSeconds() == Time::kSecondsPerMinute);

    TEST(subject.TryParse(Brn("PT3M")));
    TEST(subject.TotalSeconds() == (3 * Time::kSecondsPerMinute));

    TEST(subject.TryParse(Brn("PT4,5M")));
    TEST(subject.TotalSeconds() == ((4 * Time::kSecondsPerMinute) + (Time::kSecondsPerMinute / 2)));

    TEST(subject.TryParse(Brn("PT8.0M")));
    TEST(subject.TotalSeconds() == (8 * Time::kSecondsPerMinute));
}

void SuiteISO8601Duration::TestSeconds()
{
    ISO8601Duration subject;

    TEST(subject.TryParse(Brn("PT0S")));
    TEST(subject.TotalSeconds() == 0);

    TEST(subject.TryParse(Brn("PT9S")));
    TEST(subject.TotalSeconds() == 9);


    TEST(subject.TryParse(Brn("PT698S")));
    TEST(subject.TotalSeconds() == 698);

    TEST(subject.TryParse(Brn("PT8.669S")) == false);

    TEST(subject.TryParse(Brn("PT345,0S")) == false);
    TEST(subject.TotalSeconds() == 0);
}

void SuiteISO8601Duration::TestMultipart()
{
    ISO8601Duration subject;

    TEST(subject.TryParse(Brn("PT0H0M0S")));
    TEST(subject.TotalSeconds() == 0);

    TEST(subject.TryParse(Brn("PT5H4M")));
    TEST(subject.TotalSeconds() == (5 * Time::kSecondsPerHour) + (4 * Time::kSecondsPerMinute));

    TEST(subject.TryParse(Brn("PT3M9S")));
    TEST(subject.TotalSeconds() == (3 * Time::kSecondsPerMinute) + 9);

    TEST(subject.TryParse(Brn("PT0.5H30M")));
    TEST(subject.TotalSeconds() == Time::kSecondsPerHour);
}

void SuiteISO8601Duration::TestInvalidForms()
{
    ISO8601Duration subject;

    TEST(false == subject.TryParse(Brx::Empty()));
    TEST(false == subject.TryParse(Brn("")));
    TEST(false == subject.TryParse(Brn("P")));
    TEST(false == subject.TryParse(Brn("XT")));
    TEST(false == subject.TryParse(Brn("PE")));
    TEST(false == subject.TryParse(Brn("PT")));
    TEST(false == subject.TryParse(Brn("pt")));
    TEST(false == subject.TryParse(Brn("lower_case")));
    TEST(false == subject.TryParse(Brn("Something")));
    TEST(false == subject.TryParse(Brn("10-20-30")));
    TEST(false == subject.TryParse(Brn("~{}-=@")));

    // Lower case / malformed strings
    TEST(false == subject.TryParse(Brn("PT0.5h")));
    TEST(false == subject.TryParse(Brn("PT69m")));
    TEST(false == subject.TryParse(Brn("PT5s")));

    TEST(false == subject.TryParse(Brn("PTDF")));
    TEST(false == subject.TryParse(Brn("PT45b")));
    TEST(false == subject.TryParse(Brn("PT0000000000000000000D")));
}



// SuiteSegmentTemplate
class SuiteSegmentTemplate : public Suite
{
public:
    SuiteSegmentTemplate() : Suite("SuiteSegmentTemplate") {}

private: // Suite
    void Test() override;

private:
    void TestParsing();
    void TestFormatting();
};

void SuiteSegmentTemplate::Test()
{
    TestParsing();
    TestFormatting();
}

void SuiteSegmentTemplate::TestParsing()
{
    // Empty XML, nothing present, all defaults
    {
        const Brn kXml(Brx::Empty());
        SegmentTemplate subject(kXml);

        TEST(subject.Initialization().Bytes() == 0);
        TEST(subject.Media().Bytes() == 0);

        TEST(subject.Duration() == 0);
        TEST(subject.StartNumber() == 1);
        TEST(subject.Timescale() == 1);
    }

    // SegmentTemplate tag but no attribtues
    {
        const Brn kXml("<SegmentTemplate />");
        SegmentTemplate subject(kXml);

        TEST(subject.Initialization().Bytes() == 0);
        TEST(subject.Media().Bytes() == 0);

        TEST(subject.Duration() == 0);
        TEST(subject.StartNumber() == 1);
        TEST(subject.Timescale() == 1);
    }

    // SegmentTemplate, with only a media value
    {
        const Brn kXml("<SegmentTemplate media=\"something.m4a\" />");
        SegmentTemplate subject(kXml);

        TEST(subject.Initialization().Bytes() == 0);
        TEST(subject.Media() == Brn("something.m4a"));

        TEST(subject.Duration() == 0);
        TEST(subject.StartNumber() == 1);
        TEST(subject.Timescale() == 1);
    }

    // SegmentTemplate with a start number & media
    {
        const Brn kXml("<SegmentTemplate startNumber=\"14\" media=\"something.m4a\" />");
        SegmentTemplate subject(kXml);

        TEST(subject.Initialization().Bytes() == 0);
        TEST(subject.Media() == Brn("something.m4a"));

        TEST(subject.StartNumber() == 14);

        TEST(subject.Duration() == 0);
        TEST(subject.Timescale() == 1);
    }

    // SegmentTemplate with duration & media
    {
        const Brn kXml("<SegmentTemplate media=\"test.m4a\" duration=\"120\"/>");
        SegmentTemplate subject(kXml);

        TEST(subject.Initialization().Bytes() == 0);
        TEST(subject.Media() == Brn("test.m4a"));

        TEST(subject.Duration() == 120);

        TEST(subject.StartNumber() == 1);
        TEST(subject.Timescale() == 1);
    }

    // SegmentTemplate with duration & timescale
    {
        const Brn kXml("<SegmentTemplate timescale=\"10\" duration=\"1200\" media=\"$Number$.m4a\" />");
        SegmentTemplate subject(kXml);

        TEST(subject.Initialization().Bytes() == 0);
        TEST(subject.Media() == Brn("$Number$.m4a"));


        TEST(subject.StartNumber() == 1);
        TEST(subject.Duration() == 1200);
        TEST(subject.Timescale() == 10);
    }

    // SegmentTemplate with all the things
    {
        const Brn kXml("<SegmentTemplate initialization=\"$RepresentationID$.dash\" duration=\"307200\" media=\"$Number$.m4a\" timescale=\"48000\" startNumber=\"39\"/>");
        SegmentTemplate subject(kXml);

        TEST(subject.Initialization() == Brn("$RepresentationID$.dash"));
        TEST(subject.Media() == Brn("$Number$.m4a"));


        TEST(subject.StartNumber() == 39);
        TEST(subject.Duration() == 307200);
        TEST(subject.Timescale() == 48000);
    }

}

void SuiteSegmentTemplate::TestFormatting()
{
    Bws<1024> urlBuf;

    // Empty template
    {
        const Brn kTemplate("url/to/something.m4a");
        SegmentTemplateParams p = {
            Brx::Empty(),  // RepresentationId
            0,             // Bandwidth
            0,             // Time
            0,             // Number
            0,             // SubNumber
        };

        TEST(SegmentTemplate::TryFormatTemplateUrl(urlBuf, kTemplate, p));
        TEST(urlBuf == Brn("url/to/something.m4a"));
        urlBuf.SetBytes(0);
    }

    // Template with an unknown param
    {
        const Brn kTemplate("path/with/$Unknown$/present");
        SegmentTemplateParams p = {
            Brx::Empty(),  // RepresentationId
            0,             // Bandwidth
            0,             // Time
            0,             // Number
            0,             // SubNumber
        };

        TEST(SegmentTemplate::TryFormatTemplateUrl(urlBuf, kTemplate, p) == false);
        urlBuf.SetBytes(0);
    }

    // Template with know case in a known template param
    {
        const Brn kTemplate("$Representationid$.m4a");
        SegmentTemplateParams p = {
            Brn("representation"), // RepresentationId
            0,                     // Bandwidth
            0,                     // Time
            0,                     // Number
            0,                     // SubNumber
        };

        TEST(SegmentTemplate::TryFormatTemplateUrl(urlBuf, kTemplate, p) == false);
        urlBuf.SetBytes(0);
    }

    // Template with RepresentationId
    {
        const Brn kTemplate("$RepresentationID$.m4a");
        SegmentTemplateParams p = {
            Brn("representation"), // RepresentationId
            0,                     // Bandwidth
            0,                     // Time
            0,                     // Number
            0,                     // SubNumber
        };

        TEST(SegmentTemplate::TryFormatTemplateUrl(urlBuf, kTemplate, p));
        TEST(urlBuf == Brn("representation.m4a"));
        urlBuf.SetBytes(0);
    }

    // Template with Bandwidth
    {
        const Brn kTemplate("$Bandwidth$.m4a");
        SegmentTemplateParams p = {
            Brx::Empty(), // RepresentationId
            192500,       // Bandwidth
            0,            // Time
            0,            // Number
            0,            // SubNumber
        };

        TEST(SegmentTemplate::TryFormatTemplateUrl(urlBuf, kTemplate, p));
        TEST(urlBuf == Brn("192500.m4a"));
        urlBuf.SetBytes(0);
    }

    // Template with Time
    {
        const Brn kTemplate("$Time$.m4a");
        SegmentTemplateParams p = {
            Brx::Empty(), // RepresentationId
            0,            // Bandwidth
            13034431,     // Time
            0,            // Number
            0,            // SubNumber
        };

        TEST(SegmentTemplate::TryFormatTemplateUrl(urlBuf, kTemplate, p));
        TEST(urlBuf == Brn("13034431.m4a"));
        urlBuf.SetBytes(0);
    }

    // Template with Number
    {
        const Brn kTemplate("$Number$.m4a");
        SegmentTemplateParams p = {
            Brx::Empty(), // RepresentationId
            0,            // Bandwidth
            0,            // Time
            69,           // Number
            0,            // SubNumber
        };

        TEST(SegmentTemplate::TryFormatTemplateUrl(urlBuf, kTemplate, p));
        TEST(urlBuf == Brn("69.m4a"));
        urlBuf.SetBytes(0);
    }

    // Template with Number & SubNumber
    {
        const Brn kTemplate("$Number$-$SubNumber$.m4a");
        SegmentTemplateParams p = {
            Brx::Empty(), // RepresentationId
            0,            // Bandwidth
            0,            // Time
            2,            // Number
            1,            // SubNumber
        };

        TEST(SegmentTemplate::TryFormatTemplateUrl(urlBuf, kTemplate, p));
        TEST(urlBuf == Brn("2-1.m4a"));
        urlBuf.SetBytes(0);
    }

    // Template with multiple parts
    {
        const Brn kTemplate("bbc/radio/radio2/$RepresentationID$-$Number$.m4s");
        SegmentTemplateParams p = {
            Brn("audio-48000"), // RepresentationId
            0,                  // Bandwidth
            0,                  // Time
            101112,             // Number
            0,                  // SubNumber
        };

        TEST(SegmentTemplate::TryFormatTemplateUrl(urlBuf, kTemplate, p));
        TEST(urlBuf == Brn("bbc/radio/radio2/audio-48000-101112.m4s"));
        urlBuf.SetBytes(0);
    }

    // NOTE: We don't currently support widths on the params yet...
}


// SuiteMPDRepresentation
class SuiteMPDRepresentation : public Suite
{
public:
    SuiteMPDRepresentation() : Suite("SuiteMPDRepresentation") {}

private: // Suite
    void Test() override;

private:
    void TestParsing();
};

void SuiteMPDRepresentation::Test()
{
    TestParsing();
}

void SuiteMPDRepresentation::TestParsing()
{
    MPDRepresentation subject;

    // Empty XML
    {
        const Brn kXml(0, 0);
        TEST(subject.TrySet(kXml) == false);
    }

    // Xml that's not a representation
    {
        const Brn kXml("<element></element>");
        TEST(subject.TrySet(kXml) == false);
    }

    // Xml, but is missing required properties
    {
        const Brn kXml("<Representation></Representation>");
        TEST(subject.TrySet(kXml) == false);
    }

    // Xml, with required Id, but no bandwidth present
    {
        const Brn kXml("<Representation id=\"id\" />");
        TEST(subject.TrySet(kXml) == false);
    }

    // Xml, with required Id, but bandwidth is a string, not a number
    {
        const Brn kXml("<Representation id=\"id\" bandwidth=\"bandy-boi\"/>");
        TEST(subject.TrySet(kXml) == false);
    }

    // Xml, with the required properties present
    {
        const Brn kXml("<Representation id=\"id\" bandwidth=\"48000\"/>");
        TEST(subject.TrySet(kXml));
        TEST(subject.Id() == Brn("id"));
        TEST(subject.Bandwidth() == 48000);
        TEST(subject.QualityRanking() == MPDRepresentation::kDefaultQualityRanking);
        TEST(subject.ElementXml().Bytes() == 0);
    }

    // Xml, with the required properties present and a body
    {
        const Brn kXml("<Representation id=\"id\" bandwidth=\"48000\"><BaseURL>https://example.com</BaseURL></Representation>");
        TEST(subject.TrySet(kXml));
        TEST(subject.Id() == Brn("id"));
        TEST(subject.Bandwidth() == 48000);
        TEST(subject.QualityRanking() == MPDRepresentation::kDefaultQualityRanking);
        TEST(subject.ElementXml() == Brn("<BaseURL>https://example.com</BaseURL>"));
    }

    // Xml, with required properties & a quality ranking attribute
    {
        const Brn kXml("<Representation id=\"id\" bandwidth=\"48000\" qualityRanking=\"1234\"><BaseURL>https://example.com</BaseURL></Representation>");
        TEST(subject.TrySet(kXml));
        TEST(subject.Id() == Brn("id"));
        TEST(subject.Bandwidth() == 48000);
        TEST(subject.QualityRanking() == 1234);
        TEST(subject.ElementXml() == Brn("<BaseURL>https://example.com</BaseURL>"));
    }
}


// SuiteMPDAdaptationSet
class SuiteMPDAdaptationSet : public Suite
{
public:
    SuiteMPDAdaptationSet() : Suite("SuiteMPDAdaptationSet") {}

private: // Suite
    void Test() override;

private:
    void TestParsing();
    void TestVisiting();
    void TestSelection();
};

void SuiteMPDAdaptationSet::Test()
{
    TestParsing();
    TestVisiting();
    TestSelection();
}

void SuiteMPDAdaptationSet::TestParsing()
{
    MPDAdaptationSet subject;

    // No Xml
    {
        const Brn kXml(0,0);
        TEST(subject.TrySet(kXml) == false);
    }

    // Xml, but of wrong element type
    {
        const Brn kXml("<Element></Element>");
        TEST(subject.TrySet(kXml) == false);
    }

    // Xml, no attributes
    {
        const Brn kXml("<AdaptationSet />");
        TEST(subject.TrySet(kXml) == false);
    }

    // Xml, attributes, but no body contents
    {
        const Brn kXml("<AdaptationSet contentType=\"audio\"></AdaptationSet>");
        TEST(subject.TrySet(kXml) == false);
    }

    // Xml, no attributes, but some body contents
    {
        const Brn kXml("<AdaptationSet><Representation id=\"id\" bandwidth=\"100\" /></AdaptationSet>");
        TEST(subject.TrySet(kXml));
        TEST(subject.IsAudio() == false);
        TEST(subject.SelectionPriority() == MPDAdaptationSet::kDefaultSelectionPriority);
        TEST(subject.ElementXml() == Brn("<Representation id=\"id\" bandwidth=\"100\" />"));
        TEST(subject.Representation().Id() == Brn("id"));
        TEST(subject.Representation().Bandwidth() == 100);
    }

    // Xml, mimeType=audio
    {
        const Brn kXml("<AdaptationSet mimeType=\"audio\"><Representation id=\"id\" bandwidth=\"100\" /></AdaptationSet>");
        TEST(subject.TrySet(kXml));
        TEST(subject.IsAudio() == true);
        TEST(subject.SelectionPriority() == MPDAdaptationSet::kDefaultSelectionPriority);
        TEST(subject.ElementXml() == Brn("<Representation id=\"id\" bandwidth=\"100\" />"));
        TEST(subject.Representation().Id() == Brn("id"));
        TEST(subject.Representation().Bandwidth() == 100);
    }

    // Xml, mimeType=audio/mp4
    {
        const Brn kXml("<AdaptationSet mimeType=\"audio/mp4\"><Representation id=\"id\" bandwidth=\"100\" /></AdaptationSet>");
        TEST(subject.TrySet(kXml));
        TEST(subject.IsAudio() == true);
        TEST(subject.SelectionPriority() == MPDAdaptationSet::kDefaultSelectionPriority);
        TEST(subject.ElementXml() == Brn("<Representation id=\"id\" bandwidth=\"100\" />"));
        TEST(subject.Representation().Id() == Brn("id"));
        TEST(subject.Representation().Bandwidth() == 100);
    }

    // Xml, contentType=audio
    {
        const Brn kXml("<AdaptationSet contentType=\"audio\"><Representation id=\"id\" bandwidth=\"100\" /></AdaptationSet>");
        TEST(subject.TrySet(kXml));
        TEST(subject.IsAudio() == true);
        TEST(subject.SelectionPriority() == MPDAdaptationSet::kDefaultSelectionPriority);
        TEST(subject.ElementXml() == Brn("<Representation id=\"id\" bandwidth=\"100\" />"));
        TEST(subject.Representation().Id() == Brn("id"));
        TEST(subject.Representation().Bandwidth() == 100);
    }

    // Xml, has a selection priority
    {
        const Brn kXml("<AdaptationSet contentType=\"audio\" selectionPriority=\"345\"><Representation id=\"id\" bandwidth=\"100\" /></AdaptationSet>");
        TEST(subject.TrySet(kXml));
        TEST(subject.IsAudio() == true);
        TEST(subject.SelectionPriority() == 345);
        TEST(subject.ElementXml() == Brn("<Representation id=\"id\" bandwidth=\"100\" />"));
        TEST(subject.Representation().Id() == Brn("id"));
        TEST(subject.Representation().Bandwidth() == 100);
    }

    // Xml, has multiple representations and we pick based on the default selection rules
    {
        const Brn kXml("<AdaptationSet><Representation id=\"id-A\" bandwidth=\"100\" /><Representation id=\"id-B\" bandwidth=\"200\" /></AdaptationSet>");
        TEST(subject.TrySet(kXml));
        TEST(subject.IsAudio() == false);
        TEST(subject.SelectionPriority() == MPDAdaptationSet::kDefaultSelectionPriority);
        TEST(subject.ElementXml() == Brn("<Representation id=\"id-A\" bandwidth=\"100\" /><Representation id=\"id-B\" bandwidth=\"200\" />"));
        TEST(subject.Representation().Id() == Brn("id-B"));
        TEST(subject.Representation().Bandwidth() == 200);
    }
}


class SuiteMPDAdaptationSetRepresentationVisitor : public IRepresentationVisitor
{
private: // IAdaptationSetVisitor
    void VisitRepresentation(const Brx& aId, TUint aBandwidth, TUint aQualityRanking, const Brx& aRepresentationXml) override;
};

void SuiteMPDAdaptationSetRepresentationVisitor::VisitRepresentation(const Brx& aId, TUint aBandwidth, TUint aQualityRanking, const Brx& aRepresentationXml)
{
    if (aId == Brn("id-A")) {
        TEST(aBandwidth == 100);
        TEST(aQualityRanking == 2);
        TEST(aRepresentationXml == Brn("<Representation id=\"id-A\" bandwidth=\"100\" qualityRanking=\"2\" />"));
    }
    else if (aId == Brn("id-B")) {
        TEST(aBandwidth == 250);
        TEST(aQualityRanking == MPDRepresentation::kDefaultQualityRanking);
        TEST(aRepresentationXml == Brn("<Representation id=\"id-B\" bandwidth=\"250\" />"));
    }
    else {
        TEST(false);
    }
}


void SuiteMPDAdaptationSet::TestVisiting()
{
    const Brn kXml("<AdaptationSet><Representation id=\"id-A\" bandwidth=\"100\" qualityRanking=\"2\" /><Representation id=\"id-B\" bandwidth=\"250\" /></AdaptationSet>");
    MPDAdaptationSet subject;
    SuiteMPDAdaptationSetRepresentationVisitor visitor;

    TEST(subject.TrySet(kXml));
    subject.Visit(visitor);
}

void SuiteMPDAdaptationSet::TestSelection()
{
    const Brn kXml("<AdaptationSet><Representation id=\"id-A\" bandwidth=\"100\"/><Representation id=\"id-B\" bandwidth=\"250\" /></AdaptationSet>");
    MPDAdaptationSet subject;

    TEST(subject.TrySet(kXml));
    TEST(subject.Representation().Id() == Brn("id-B"));

    TEST(subject.TrySelectRepresentation(Brx::Empty()) == false);
    TEST(subject.TrySelectRepresentation(Brn("Unknown")) == false);
    TEST(subject.TrySelectRepresentation(Brn("ANOTHER-Unknown-One")) == false);

    TEST(subject.TrySelectRepresentation(Brn("id-A")));
    TEST(subject.Representation().Id() == Brn("id-A"));
}


// SuiteMPDPeriod
class SuiteMPDPeriod : public Suite
{
public:
    SuiteMPDPeriod() : Suite("SuiteMPDPeriod") {}

private: // Suite
    void Test() override;

private:
    void TestParsing();
    void TestVisiting();
    void TestSelection();
};

void SuiteMPDPeriod::Test()
{
    TestParsing();
    TestVisiting();
    TestSelection();
}

void SuiteMPDPeriod::TestParsing()
{
    MPDPeriod subject;

    // No Xml
    {
        const Brn kXml(0, 0);
        TEST(subject.TrySet(kXml) == false);
    }

    // Different Xml element
    {
        const Brn kXml("<Element></Element>");
        TEST(subject.TrySet(kXml) == false);
    }

    // Xml, but no child elements
    {
        const Brn kXml("<Period></Period>");
        TEST(subject.TrySet(kXml) == false);
    }

    // Xml with only an AdaptationSet child
    {
        const Brn kXml("<Period><AdaptationSet /></Period>");
        TEST(subject.TrySet(kXml) == false);
    }

    // Xml with AdaptationSet, but not an audio one & Representation children
    {
        const Brn kXml("<Period><AdaptationSet><Representation id=\"id\" bandwidth=\"99\"/></AdaptationSet></Period>");
        TEST(subject.TrySet(kXml) == false);
    }

    // Xml with AdaptationSet & Representation children
    {
        const Brn kXml("<Period><AdaptationSet contentType=\"audio\"><Representation id=\"id\" bandwidth=\"99\"/></AdaptationSet></Period>");
        TEST(subject.TrySet(kXml));
        TEST(subject.ElementXml() == Brn("<AdaptationSet contentType=\"audio\"><Representation id=\"id\" bandwidth=\"99\"/></AdaptationSet>"));
        TEST(subject.AdaptationSet().IsAudio() == true);
        TEST(subject.AdaptationSet().Representation().Id() == Brn("id"));
        TEST(subject.AdaptationSet().Representation().Bandwidth() == 99);
    }
}

class SuiteMPDPeriodAdaptationSetVisitor : public IAdaptationSetVisitor
{
private: // IAdaptationSetVisitor
    void VisitAdaptationSet(TUint aIndex, TUint aSelectionPriority, TBool aIsAudio, const Brx& aXml) override;
};

void SuiteMPDPeriodAdaptationSetVisitor::VisitAdaptationSet(TUint aIndex, TUint aSelectionPriority, TBool aIsAudio, const Brx& aXml)
{
    if (aIndex == 0) {
        TEST(aSelectionPriority == 2);
        TEST(aIsAudio == false);
        TEST(aXml == Brn("<AdaptationSet selectionPriority=\"2\"><Representation id=\"id-A\" bandwidth=\"800\"/></AdaptationSet>"));
    }
    else if (aIndex == 1) {
        TEST(aSelectionPriority == 1);
        TEST(aIsAudio == true);
        TEST(aXml == Brn("<AdaptationSet contentType=\"audio\" selectionPriority=\"1\"><Representation id=\"id-B\" bandwidth=\"1000\"/></AdaptationSet>"));
    }
    else if (aIndex == 2) {
        TEST(aSelectionPriority == MPDAdaptationSet::kDefaultSelectionPriority);
        TEST(aIsAudio == true);
        TEST(aXml == Brn("<AdaptationSet contentType=\"audio\"><Representation id=\"id-C\" bandwidth=\"2500\"/></AdaptationSet>"));
    }
    else {
        TEST(false);
    }
}


void SuiteMPDPeriod::TestVisiting()
{
    const Brn kXml("<Period><AdaptationSet selectionPriority=\"2\"><Representation id=\"id-A\" bandwidth=\"800\"/></AdaptationSet><AdaptationSet contentType=\"audio\" selectionPriority=\"1\"><Representation id=\"id-B\" bandwidth=\"1000\"/></AdaptationSet><AdaptationSet contentType=\"audio\"><Representation id=\"id-C\" bandwidth=\"2500\"/></AdaptationSet></Period>");
    MPDPeriod subject;
    SuiteMPDPeriodAdaptationSetVisitor visitor;

    TEST(subject.TrySet(kXml));
    subject.Visit(visitor);
}

void SuiteMPDPeriod::TestSelection()
{
    const Brn kXml("<Period><AdaptationSet selectionPriority=\"2\"><Representation id=\"id-A\" bandwidth=\"800\"/></AdaptationSet><AdaptationSet contentType=\"audio\" selectionPriority=\"1\"><Representation id=\"id-B\" bandwidth=\"1000\"/></AdaptationSet><AdaptationSet contentType=\"audio\"><Representation id=\"id-C\" bandwidth=\"2500\"/></AdaptationSet></Period>");
    MPDPeriod subject;

    TEST(subject.TrySet(kXml));
    TEST(subject.AdaptationSet().Representation().Id() == Brn("id-C"));

    TEST(subject.TrySelectAdaptationSet(2000) == false);
    TEST(subject.TrySelectAdaptationSet(150)  == false);
    TEST(subject.TrySelectAdaptationSet(4)    == false);

    TEST(subject.TrySelectAdaptationSet(1));
    TEST(subject.AdaptationSet().Representation().Id() == Brn("id-B"));

    TEST(subject.TrySelectAdaptationSet(0));
    TEST(subject.AdaptationSet().Representation().Id() == Brn("id-A"));
}


// SuiteMPDDocument
class SuiteMPDDocument : public Suite
{
public:
    SuiteMPDDocument() : Suite("SuiteMPDDocument") { }

private: // Suite
    void Test() override;

private:
    void TestParsing();
    void TestExpiry();
};

void SuiteMPDDocument::Test()
{
    TestParsing();
    TestExpiry();
}

void SuiteMPDDocument::TestParsing()
{
    MPDDocument subject;

    // No Xml
    {
        const Brn kXml(0,0);
        TEST(subject.TrySet(kXml) == false);
    }

    // Xml, but wrong element
    {
        const Brn kXml("<RandomElement></RandomElement>");
        TEST(subject.TrySet(kXml) == false);
    }

    // Xml, MPD only, no children
    {
        const Brn kXml("<MPD />");
        TEST(subject.TrySet(kXml) == false);
    }

    // Xml, MPD but no periods
    {
        const Brn kXml("<MPD><Element></Element></MPD>");
        TEST(subject.TrySet(kXml) == false);
    }

    // Xml, MPD with a period but nothing else
    {
        const Brn kXml("<MPD><Period /></MPD>");
        TEST(subject.TrySet(kXml) == false);
    }

    // Xml - MPD, Period & AdaptationSet but no representation
    {
        const Brn kXml("<MPD><Period><AdaptationSet /></Period></MPD>");
        TEST(subject.TrySet(kXml) == false);
    }

    // Xml - MPD with all the children present, but a non-audio AdaptationSet
    {
        const Brn kXml("<MPD><Period><AdaptationSet><Representation id=\"id\" bandwidth=\"10\"/></AdaptationSet></Period></MPD>");
        TEST(subject.TrySet(kXml) == false);
    }

    // Xml - MPD with all the children present, and an audio AdaptationSet
    {
        const Brn kXml("<MPD><Period><AdaptationSet mimeType=\"audio\"><Representation id=\"id\" bandwidth=\"10\"/></AdaptationSet></Period></MPD>");
        TEST(subject.TrySet(kXml));
        TEST(subject.IsStatic());
        TEST(subject.IsContentProtected() == false);
    }

    // Xml - MPD with all chlildren present, an audio AdaptationSet as well as it being a dynamic type
    {
        const Brn kXml("<MPD type=\"dynamic\"><Period><AdaptationSet mimeType=\"audio\"><Representation id=\"id\" bandwidth=\"10\"/></AdaptationSet></Period></MPD>");
        TEST(subject.TrySet(kXml));
        TEST(subject.IsStatic() == false);
        TEST(subject.IsContentProtected() == false);
    }
}

void SuiteMPDDocument::TestExpiry()
{
    MPDDocument subject;

    TEST(subject.TrySet(Brn("<MPD type=\"dynamic\"><Period><AdaptationSet mimeType=\"audio\"><Representation id=\"id\" bandwidth=\"10\"/></AdaptationSet></Period></MPD>")));
    TEST(subject.HasExpired() == false);

    subject.SetExpired();
    TEST(subject.HasExpired());

    TEST(subject.TrySet(Brn("<MPD type=\"dynamic\"><Period><AdaptationSet mimeType=\"audio\"><Representation id=\"id\" bandwidth=\"10\"/></AdaptationSet></Period></MPD>")));
    TEST(subject.HasExpired() == false);
    TEST(subject.HasExpired() == false);

    subject.SetExpired();
    TEST(subject.HasExpired());
}


// SuiteBaseUrlCollection
class SuiteBaseUrlCollection : public Suite
{
public:
    SuiteBaseUrlCollection() : Suite("SuiteBaseUrlCollection") {}

private: // Suite
    void Test() override;

private:
    void TestParsing();
};


void SuiteBaseUrlCollection::Test()
{
    TestParsing();
}

class SuiteBaseUrlCallCountVisitor : public IBaseUrlVisitor
{
public:
    SuiteBaseUrlCallCountVisitor() { Reset(); }

public:
    TUint CallCount() const { return iCallCount; }
    void Reset()            { iCallCount = 0; }

private: // IBaseUrlVisitor
    void VisitBaseUrl(const Brx& /*aLevel*/, TUint /*aIndex*/, TUint /*aSelectionPriority*/,
                      TUint /*aWeight*/, const Brx& /*aServiceLocation*/, const Brx& /*aUrl*/,
                      const Brx& /*aElementXml*/) override { iCallCount += 1; }

private:
    TUint iCallCount;
};


void SuiteBaseUrlCollection::TestParsing()
{
    const Brn kLevel("test");
    SuiteBaseUrlCallCountVisitor visitor;

    TEST(BaseUrlCollection::TryVisit(Brx::Empty(), kLevel, visitor) == false);
    TEST(visitor.CallCount() == 0);

    visitor.Reset();

    TEST(BaseUrlCollection::TryVisit(Brn("<Element></Element>"), kLevel, visitor) == false);
    TEST(visitor.CallCount() == 0);

    visitor.Reset();

    TEST(BaseUrlCollection::TryVisit(Brn("<Element><SubElementA></SubElementA><SubElementB></SubElementB></Element>"), kLevel, visitor) == false);
    TEST(visitor.CallCount() == 0);

    visitor.Reset();

    {
        const Brn kXml("<BaseURL>https://example.com/stream/1</BaseURL>");
        TEST(BaseUrlCollection::TryVisit(kXml, kLevel, visitor));
        TEST(visitor.CallCount() == 1);

        visitor.Reset();
    }

    {
        const Brn kXml("<BaseURL>https://example.com/stream/1</BaseURL><Element><SubElement /></Element><BaseURL>https://example.com/stream/2</BaseURL>");
        TEST(BaseUrlCollection::TryVisit(kXml, kLevel, visitor));
        TEST(visitor.CallCount() == 2);

        visitor.Reset();
    }

    {
        const Brn kXml("<BaseURL>https://example.com/stream/1</BaseURL><BaseURL>https://example.com/stream/2</BaseURL><BaseURL>https://example.com/stream/3</BaseURL>");
        TEST(BaseUrlCollection::TryVisit(kXml, kLevel, visitor));
        TEST(visitor.CallCount() == 3);

        visitor.Reset();
    }
}


// SuiteContentProtection
class SuiteContentProtection : public Suite
{
public:
    SuiteContentProtection() : Suite("SuiteContentProtection") {}

private: // Suite
    void Test() override;
};

void SuiteContentProtection::Test()
{
    ContentProtection subject;

    // No Xml
    {
        const Brn kXml(0, 0);
        TEST(subject.TrySet(kXml) == false);
    }

    // Xml, but wrong elements
    {
        const Brn kXml("<Element /><AnotherElement></AnotherElement>");
        TEST(subject.TrySet(kXml) == false);
    }

    // Xml, with a single content protection containing supplimentary details
    {
        const Brn kXml("<ContentProtection schemeIdUri=\"test-protection\"><pssh>hssp</pssh></ContentProtection>");
        TEST(subject.TrySet(kXml));
        TEST(subject.iSchemeIdUri == Brn("test-protection"));
        TEST(subject.iValue.Bytes() == 0);
        TEST(subject.iDefaultKID.Bytes() == 0);
        TEST(subject.iPropertiesSchemeIdUri.Bytes() == 0);
        TEST(subject.iPropertiesXML.Bytes() == 0);
        TEST(subject.IsMPEG4Protection() == false);
    }

    // Xml, with a single content protection of a known type
    {
        const Brn kXml("<ContentProtection schemeIdUri=\"urn:mpeg:dash:mp4protection:2011\" value=\"cenc\" />");
        TEST(subject.TrySet(kXml));
        TEST(subject.iSchemeIdUri == Brn("urn:mpeg:dash:mp4protection:2011"));
        TEST(subject.iValue == Brn("cenc"));
        TEST(subject.iDefaultKID.Bytes() == 0);
        TEST(subject.iPropertiesSchemeIdUri.Bytes() == 0);
        TEST(subject.iPropertiesXML.Bytes() == 0);
        TEST(subject.IsMPEG4Protection());
    }

    // Xml, with content protection of a known type and some suppliementary properties
    {
        const Brn kXml("<ContentProtection schemeIdUri=\"urn:mpeg:dash:mp4protection:2011\" value=\"cenc\" /><ContentProtection schemeIdUri=\"urn:uuid:abc-123\"><pssh>hssp</pssh><licenseUrl>https://example.com</licenseUrl></ContentProtection>");
        TEST(subject.TrySet(kXml));
        TEST(subject.iSchemeIdUri == Brn("urn:mpeg:dash:mp4protection:2011"));
        TEST(subject.iValue == Brn("cenc"));
        TEST(subject.iDefaultKID.Bytes() == 0);
        TEST(subject.iPropertiesSchemeIdUri == Brn("urn:uuid:abc-123"));
        TEST(subject.iPropertiesXML == Brn("<ContentProtection schemeIdUri=\"urn:uuid:abc-123\"><pssh>hssp</pssh><licenseUrl>https://example.com</licenseUrl></ContentProtection>"));
        TEST(subject.IsMPEG4Protection());
    }

    // XMl, with a cenc:default_KID
    {
        const Brn kXml("<ContentProtection schemeIdUri=\"urn:mpeg:dash:mp4protection:2011\" cenc:default_KID=\"key\" />");
        TEST(subject.TrySet(kXml));
        TEST(subject.iSchemeIdUri == Brn("urn:mpeg:dash:mp4protection:2011"));
        TEST(subject.iValue.Bytes() == 0);
        TEST(subject.iDefaultKID == Brn("key"));
        TEST(subject.iPropertiesSchemeIdUri.Bytes() == 0);
        TEST(subject.iPropertiesXML.Bytes() == 0);
        TEST(subject.IsMPEG4Protection());
    }
}


// SuiteMPDSegmentStream
class SuiteMPDSegmentStream : public Suite
{
public:
    SuiteMPDSegmentStream() : Suite("SuiteMPDSegmentStream") {}

private:
    void Test() override;

private:
    void TestStaticSegmentList();
    void TestDynamicSegmentTemplate();
    void TestSeeking();
};

class FixedUnixTimestamp : public IUnixTimestamp
{
public:
    FixedUnixTimestamp(TUint aTimestamp) : iTimestamp(aTimestamp) {}

private:
    TUint Now() override { return iTimestamp; }
    void Reset() override {}

private:
    TUint iTimestamp;
};

void SuiteMPDSegmentStream::Test()
{
    TestStaticSegmentList();
    TestDynamicSegmentTemplate();
    TestSeeking();
}

void SuiteMPDSegmentStream::TestStaticSegmentList()
{
    const TUint kTimestamp = 1723638296; // Wed Aug 14 2024 12:24:56 (GMT)
    const Brn kManifest("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><MPD mediaPresentationDuration=\"PT154.63926696777344S\" minBufferTime=\"PT2S\" profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\" type=\"static\" xmlns=\"urn:mpeg:dash:schema:mpd:2011\" xmlns:amz=\"urn:amazon:music:3p:music:2020\" xmlns:amz-music=\"urn:amazon:music:drm:2019\" xmlns:cenc=\"urn:mpeg:cenc:2013\" xmlns:mspr=\"urn:microsoft:playready\"><Period id=\"0\"><AdaptationSet contentType=\"audio\" id=\"1\" selectionPriority=\"1000\" subsegmentAlignment=\"true\"><ContentProtection cenc:default_KID=\"5e8ae77a-5b13-4eca-a354-9164f1d30567\" schemeIdUri=\"urn:mpeg:dash:mp4protection:2011\" value=\"cenc\"/><ContentProtection schemeIdUri=\"urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed\"><cenc:pssh>AABBCCDD__WWXXYYZZ</cenc:pssh><amz:LicenseUrl>https://example.com/drm/widevine/license</amz:LicenseUrl></ContentProtection><SupplementalProperty schemeIdUri=\"urn:mpeg:mpegB:cicp:ProgramLoudness\" value=\"-8.3 LUFS\"/><SupplementalProperty schemeIdUri=\"amz-music:trackType\" value=\"SD\"/><SupplementalProperty schemeIdUri=\"urn:mpeg:mpegB:cicp:AnchorLoudness\" value=\"-8.3 LUFS\"/><Representation audioSamplingRate=\"48000\" bandwidth=\"51352\" codecs=\"opus\" id=\"1\" mimeType=\"audio/mp4\" qualityRanking=\"3\"><AudioChannelConfiguration schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\" value=\"2\"/><SupplementalProperty schemeIdUri=\"tag:amazon.com,2019:dash:StreamName\" value=\"SD\"/><BaseURL>https://example.come/audio/stream?id=ABCED&amp;ql=SD_LOW</BaseURL><SegmentList duration=\"480000\" timescale=\"48000\"><Initialization range=\"0-1031\"/><SegmentURL mediaRange=\"1256-61511\"/><SegmentURL mediaRange=\"61512-121647\"/><SegmentURL mediaRange=\"121648-181783\"/><SegmentURL mediaRange=\"181784-245972\"/><SegmentURL mediaRange=\"245973-310161\"/><SegmentURL mediaRange=\"310162-374350\"/><SegmentURL mediaRange=\"374351-438539\"/><SegmentURL mediaRange=\"438540-502728\"/><SegmentURL mediaRange=\"502729-566917\"/><SegmentURL mediaRange=\"566918-631106\"/></SegmentList></Representation></AdaptationSet></Period></MPD>");
    const Brn kExpectedStreamUri("https://example.come/audio/stream?id=ABCED&amp;ql=SD_LOW");

    Bws<Uri::kMaxUriBytes> uriBuffer;
    MPDDocument document;
    MPDSegment segment(uriBuffer);
    FixedUnixTimestamp timestamp(kTimestamp);
    MPDSegmentStream subject(timestamp);

    TEST(document.TrySet(kManifest));
    TEST(subject.TrySet(document, false));

    // Initialisation Segment...
    TEST(subject.TryGetNextSegment(segment));
    TEST(segment.iUrlBuffer  == kExpectedStreamUri);
    TEST(segment.iRangeStart == 0);
    TEST(segment.iRangeEnd   == 1031);


    // Remaining Audio segments...
    TEST(subject.TryGetNextSegment(segment));
    TEST(segment.iUrlBuffer  == kExpectedStreamUri);
    TEST(segment.iRangeStart == 1032); // We adjust the start to be at the end of the "init" segment
    TEST(segment.iRangeEnd   == 61511);

    TEST(subject.TryGetNextSegment(segment));
    TEST(segment.iUrlBuffer  == kExpectedStreamUri);
    TEST(segment.iRangeStart == 61512);
    TEST(segment.iRangeEnd   == 121647);

    TEST(subject.TryGetNextSegment(segment));
    TEST(segment.iUrlBuffer  == kExpectedStreamUri);
    TEST(segment.iRangeStart == 121648);
    TEST(segment.iRangeEnd   == 181783);

    TEST(subject.TryGetNextSegment(segment));
    TEST(segment.iUrlBuffer  == kExpectedStreamUri);
    TEST(segment.iRangeStart == 181784);
    TEST(segment.iRangeEnd   == 245972);

    TEST(subject.TryGetNextSegment(segment));
    TEST(segment.iUrlBuffer  == kExpectedStreamUri);
    TEST(segment.iRangeStart == 245973);
    TEST(segment.iRangeEnd   == 310161);

    TEST(subject.TryGetNextSegment(segment));
    TEST(segment.iUrlBuffer  == kExpectedStreamUri);
    TEST(segment.iRangeStart == 310162);
    TEST(segment.iRangeEnd   == 374350);

    TEST(subject.TryGetNextSegment(segment));
    TEST(segment.iUrlBuffer  == kExpectedStreamUri);
    TEST(segment.iRangeStart == 374351);
    TEST(segment.iRangeEnd   == 438539);

    TEST(subject.TryGetNextSegment(segment));
    TEST(segment.iUrlBuffer  == kExpectedStreamUri);
    TEST(segment.iRangeStart == 438540);
    TEST(segment.iRangeEnd   == 502728);

    TEST(subject.TryGetNextSegment(segment));
    TEST(segment.iUrlBuffer  == kExpectedStreamUri);
    TEST(segment.iRangeStart == 502729);
    TEST(segment.iRangeEnd   == 566917);

    TEST(subject.TryGetNextSegment(segment));
    TEST(segment.iUrlBuffer  == kExpectedStreamUri);
    TEST(segment.iRangeStart == 566918);
    TEST(segment.iRangeEnd   == 631106);

    // No more segments left, so no matter how many times we request a segment
    // we don't have any left!
    TEST(subject.TryGetNextSegment(segment) == false);
    TEST(subject.TryGetNextSegment(segment) == false);
    TEST(subject.TryGetNextSegment(segment) == false);
}

void SuiteMPDSegmentStream::TestDynamicSegmentTemplate()
{
    const TUint kTimestamp = 1723638296; // Wed Aug 14 2024 12:24:56 (GMT)
    const Brn kManifest("<?xml version=\"1.0\" encoding=\"utf-8\"?><MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns=\"urn:mpeg:dash:schema:mpd:2011\" xmlns:dvb=\"urn:dvb:dash:dash-extensions:2014-1\" xsi:schemaLocation=\"urn:mpeg:dash:schema:mpd:2011 http://standards.iso.org/ittf/PubliclyAvailableStandards/MPEG-DASH_schema_files/DASH-MPD.xsd\" type=\"dynamic\" availabilityStartTime=\"1969-12-31T23:59:44Z\" minimumUpdatePeriod=\"PT6H\" timeShiftBufferDepth=\"PT6H\" maxSegmentDuration=\"PT7S\" minBufferTime=\"PT3.200S\" profiles=\"urn:dvb:dash:profile:dvb-dash:2014,urn:dvb:dash:profile:dvb-dash:isoff-ext-live:2014\" publishTime=\"1850-05-10T09:00:00\"><UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-iso:2014\" value=\"http://time.akamai.com/?iso\" /><BaseURL dvb:weight=\"1\" serviceLocation=\"ak\">http://dash.uk.live.example.com/radio/station/dash/</BaseURL><Period id=\"1\" start=\"PT0S\"><AdaptationSet group=\"1\" contentType=\"audio\" lang=\"en\" minBandwidth=\"48000\" maxBandwidth=\"96000\" segmentAlignment=\"true\" audioSamplingRate=\"48000\" mimeType=\"audio/mp4\" codecs=\"mp4a.40.5\" startWithSAP=\"1\"><AudioChannelConfiguration schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\" value=\"2\"/><Role schemeIdUri=\"urn:mpeg:dash:role:2011\" value=\"main\"/><SegmentTemplate timescale=\"48000\" initialization=\"stream-$RepresentationID$.dash\" media=\"stream-$RepresentationID$-$Number$.m4s\" startNumber=\"1\" duration=\"307200\"/><Representation id=\"audio=48000\" bandwidth=\"48000\"/><Representation id=\"audio=96000\" bandwidth=\"96000\"/></AdaptationSet><AdaptationSet group=\"1\" contentType=\"audio\" lang=\"en\" minBandwidth=\"128000\" maxBandwidth=\"320000\" segmentAlignment=\"true\" audioSamplingRate=\"48000\" mimeType=\"audio/mp4\" codecs=\"mp4a.40.2\" startWithSAP=\"1\"><AudioChannelConfiguration schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\" value=\"2\"/><Role schemeIdUri=\"urn:mpeg:dash:role:2011\" value=\"main\"/><SegmentTemplate timescale=\"48000\" initialization=\"stream-$RepresentationID$.dash\" media=\"stream-$RepresentationID$-$Number$.m4s\" startNumber=\"1\" duration=\"307200\"/><Representation id=\"audio=128000\" bandwidth=\"128000\"/><Representation id=\"audio=320000\" bandwidth=\"320000\"/></AdaptationSet></Period></MPD>");
    const Brn kExpectedBaseUri("http://dash.uk.live.example.com/radio/station/dash/");

    Bws<Uri::kMaxUriBytes> uriBuffer;
    MPDDocument document;
    MPDSegment segment(uriBuffer);
    FixedUnixTimestamp timestamp(kTimestamp);
    MPDSegmentStream subject(timestamp);

    TEST(document.TrySet(kManifest));
    TEST(subject.TrySet(document, false));

    // Initialisation Segment...
    TEST(subject.TryGetNextSegment(segment));
    TEST(segment.iUrlBuffer.BeginsWith(kExpectedBaseUri));
    TEST(Ascii::Contains(segment.iUrlBuffer, Brn("/stream-audio=320000.dash")));
    TEST(segment.iRangeStart == -1);
    TEST(segment.iRangeEnd   == -1);

    // Audio Segments
    TEST(subject.TryGetNextSegment(segment));
    TEST(segment.iUrlBuffer.BeginsWith(kExpectedBaseUri));
    TEST(Ascii::Contains(segment.iUrlBuffer, Brn("/stream-audio=320000-269318486.m4s")));
    TEST(segment.iRangeStart == -1);
    TEST(segment.iRangeEnd   == -1);

    TEST(subject.TryGetNextSegment(segment));
    TEST(segment.iUrlBuffer.BeginsWith(kExpectedBaseUri));
    TEST(Ascii::Contains(segment.iUrlBuffer, Brn("/stream-audio=320000-269318487.m4s")));
    TEST(segment.iRangeStart == -1);
    TEST(segment.iRangeEnd   == -1);

    TEST(subject.TryGetNextSegment(segment));
    TEST(segment.iUrlBuffer.BeginsWith(kExpectedBaseUri));
    TEST(Ascii::Contains(segment.iUrlBuffer, Brn("/stream-audio=320000-269318488.m4s")));
    TEST(segment.iRangeStart == -1);
    TEST(segment.iRangeEnd   == -1);

    TEST(subject.TryGetNextSegment(segment));
    TEST(segment.iUrlBuffer.BeginsWith(kExpectedBaseUri));
    TEST(Ascii::Contains(segment.iUrlBuffer, Brn("/stream-audio=320000-269318489.m4s")));
    TEST(segment.iRangeStart == -1);
    TEST(segment.iRangeEnd   == -1);

    TEST(subject.TryGetNextSegment(segment));
    TEST(segment.iUrlBuffer.BeginsWith(kExpectedBaseUri));
    TEST(Ascii::Contains(segment.iUrlBuffer, Brn("/stream-audio=320000-269318490.m4s")));
    TEST(segment.iRangeStart == -1);
    TEST(segment.iRangeEnd   == -1);
}

void SuiteMPDSegmentStream::TestSeeking()
{
    const TUint kTimestamp = 1723638296; // Wed Aug 14 2024 12:24:56 (GMT)


    Bws<Uri::kMaxUriBytes> uriBuffer;
    MPDDocument document;
    MPDSegment segment(uriBuffer);
    FixedUnixTimestamp timestamp(kTimestamp);
    MPDSegmentStream subject(timestamp);

    {
        // TEMPLATE MANIFEST
        // NO SEEKING
        const Brn kManifest("<?xml version=\"1.0\" encoding=\"utf-8\"?><MPD xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" xmlns=\"urn:mpeg:dash:schema:mpd:2011\" xmlns:dvb=\"urn:dvb:dash:dash-extensions:2014-1\" xsi:schemaLocation=\"urn:mpeg:dash:schema:mpd:2011 http://standards.iso.org/ittf/PubliclyAvailableStandards/MPEG-DASH_schema_files/DASH-MPD.xsd\" type=\"dynamic\" availabilityStartTime=\"1969-12-31T23:59:44Z\" minimumUpdatePeriod=\"PT6H\" timeShiftBufferDepth=\"PT6H\" maxSegmentDuration=\"PT7S\" minBufferTime=\"PT3.200S\" profiles=\"urn:dvb:dash:profile:dvb-dash:2014,urn:dvb:dash:profile:dvb-dash:isoff-ext-live:2014\" publishTime=\"1850-05-10T09:00:00\"><UTCTiming schemeIdUri=\"urn:mpeg:dash:utc:http-iso:2014\" value=\"http://time.akamai.com/?iso\" /><BaseURL dvb:weight=\"1\" serviceLocation=\"ak\">http://dash.uk.live.example.com/radio/station/dash/</BaseURL><Period id=\"1\" start=\"PT0S\"><AdaptationSet group=\"1\" contentType=\"audio\" lang=\"en\" minBandwidth=\"48000\" maxBandwidth=\"96000\" segmentAlignment=\"true\" audioSamplingRate=\"48000\" mimeType=\"audio/mp4\" codecs=\"mp4a.40.5\" startWithSAP=\"1\"><AudioChannelConfiguration schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\" value=\"2\"/><Role schemeIdUri=\"urn:mpeg:dash:role:2011\" value=\"main\"/><SegmentTemplate timescale=\"48000\" initialization=\"stream-$RepresentationID$.dash\" media=\"stream-$RepresentationID$-$Number$.m4s\" startNumber=\"1\" duration=\"307200\"/><Representation id=\"audio=48000\" bandwidth=\"48000\"/><Representation id=\"audio=96000\" bandwidth=\"96000\"/></AdaptationSet><AdaptationSet group=\"1\" contentType=\"audio\" lang=\"en\" minBandwidth=\"128000\" maxBandwidth=\"320000\" segmentAlignment=\"true\" audioSamplingRate=\"48000\" mimeType=\"audio/mp4\" codecs=\"mp4a.40.2\" startWithSAP=\"1\"><AudioChannelConfiguration schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\" value=\"2\"/><Role schemeIdUri=\"urn:mpeg:dash:role:2011\" value=\"main\"/><SegmentTemplate timescale=\"48000\" initialization=\"stream-$RepresentationID$.dash\" media=\"stream-$RepresentationID$-$Number$.m4s\" startNumber=\"1\" duration=\"307200\"/><Representation id=\"audio=128000\" bandwidth=\"128000\"/><Representation id=\"audio=320000\" bandwidth=\"320000\"/></AdaptationSet></Period></MPD>");

        TEST(document.TrySet(kManifest));
        TEST(subject.TrySet(document, false));

        TEST(subject.TrySeekByOffset(0) == false);
        TEST(subject.TrySeekByOffset(13043431) == false);
    }

    {
        // LIST MANIFEST
        const Brn kManifest("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?><MPD mediaPresentationDuration=\"PT154.63926696777344S\" minBufferTime=\"PT2S\" profiles=\"urn:mpeg:dash:profile:isoff-on-demand:2011\" type=\"static\" xmlns=\"urn:mpeg:dash:schema:mpd:2011\" xmlns:amz=\"urn:amazon:music:3p:music:2020\" xmlns:amz-music=\"urn:amazon:music:drm:2019\" xmlns:cenc=\"urn:mpeg:cenc:2013\" xmlns:mspr=\"urn:microsoft:playready\"><Period id=\"0\"><AdaptationSet contentType=\"audio\" id=\"1\" selectionPriority=\"1000\" subsegmentAlignment=\"true\"><ContentProtection cenc:default_KID=\"5e8ae77a-5b13-4eca-a354-9164f1d30567\" schemeIdUri=\"urn:mpeg:dash:mp4protection:2011\" value=\"cenc\"/><ContentProtection schemeIdUri=\"urn:uuid:edef8ba9-79d6-4ace-a3c8-27dcd51d21ed\"><cenc:pssh>AABBCCDD__WWXXYYZZ</cenc:pssh><amz:LicenseUrl>https://example.com/drm/widevine/license</amz:LicenseUrl></ContentProtection><SupplementalProperty schemeIdUri=\"urn:mpeg:mpegB:cicp:ProgramLoudness\" value=\"-8.3 LUFS\"/><SupplementalProperty schemeIdUri=\"amz-music:trackType\" value=\"SD\"/><SupplementalProperty schemeIdUri=\"urn:mpeg:mpegB:cicp:AnchorLoudness\" value=\"-8.3 LUFS\"/><Representation audioSamplingRate=\"48000\" bandwidth=\"51352\" codecs=\"opus\" id=\"1\" mimeType=\"audio/mp4\" qualityRanking=\"3\"><AudioChannelConfiguration schemeIdUri=\"urn:mpeg:dash:23003:3:audio_channel_configuration:2011\" value=\"2\"/><SupplementalProperty schemeIdUri=\"tag:amazon.com,2019:dash:StreamName\" value=\"SD\"/><BaseURL>https://example.come/audio/stream?id=ABCED&amp;ql=SD_LOW</BaseURL><SegmentList duration=\"480000\" timescale=\"48000\"><Initialization range=\"0-1031\"/><SegmentURL mediaRange=\"1256-61511\"/><SegmentURL mediaRange=\"61512-121647\"/><SegmentURL mediaRange=\"121648-181783\"/><SegmentURL mediaRange=\"181784-245972\"/><SegmentURL mediaRange=\"245973-310161\"/><SegmentURL mediaRange=\"310162-374350\"/><SegmentURL mediaRange=\"374351-438539\"/><SegmentURL mediaRange=\"438540-502728\"/><SegmentURL mediaRange=\"502729-566917\"/><SegmentURL mediaRange=\"566918-631106\"/></SegmentList></Representation></AdaptationSet></Period></MPD>");

        TEST(document.TrySet(kManifest));
        TEST(subject.TrySet(document, false));

        // Get the "init" segment out of the way
        TEST(subject.TryGetNextSegment(segment));

        // Seek right to the start of the audio in the file
        TEST(subject.TrySeekByOffset(1256));
        TEST(subject.TryGetNextSegment(segment));
        TEST(segment.iRangeStart == 1256);
        TEST(segment.iRangeEnd   == 61511);

        // Middle of a random fragment
        TEST(subject.TrySeekByOffset(246691));
        TEST(subject.TryGetNextSegment(segment));
        TEST(segment.iRangeStart == 246691);
        TEST(segment.iRangeEnd   == 310161);

        // Very end of a previous fragment. Ensures we request data from the
        // next fragment to prevent very small byte requests and that we have
        // enough audio in order to continue playing.
        TEST(subject.TrySeekByOffset(438539));
        TEST(subject.TryGetNextSegment(segment));
        TEST(segment.iRangeStart == 438539);
        TEST(segment.iRangeEnd   == 502728);
    }
}


extern void TestMPEGDash(Environment& /*aEnv*/)
{
    Runner runner("TestMPEGDash");
    runner.Add(new SuiteISO8601Duration());
    runner.Add(new SuiteSegmentTemplate());
    runner.Add(new SuiteMPDRepresentation());
    runner.Add(new SuiteMPDAdaptationSet());
    runner.Add(new SuiteMPDPeriod());
    runner.Add(new SuiteMPDDocument());
    runner.Add(new SuiteBaseUrlCollection());
    runner.Add(new SuiteContentProtection());
    runner.Add(new SuiteMPDSegmentStream());

    runner.Run();
}

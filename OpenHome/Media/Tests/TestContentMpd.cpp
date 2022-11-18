#include <OpenHome/Types.h>
#include <OpenHome/Private/TestFramework.h>

#include <OpenHome/Net/Private/XmlParser.h>
#include <OpenHome/Media/Protocol/ContentMpd.h>

namespace OpenHome
{
namespace Media
{
namespace Mpd
{
namespace Test
{

class SuiteMpdRootParser : public TestFramework::Suite
{
public:
    SuiteMpdRootParser();

public: // Suite
    void Test() override;
};

class SuiteMpdElementParser : public TestFramework::Suite
{
public:
    SuiteMpdElementParser();

public: //Suite
    void Test() override;

private:
    void TestAttributes();
    void TestFetchingChildren();
};

class SuiteMpdSupplementalPropertyParser : public TestFramework::Suite
{
public:
    SuiteMpdSupplementalPropertyParser();

public: // Suite
    void Test() override;
};


} // namespace Test
} // namespace Mpd
} // namespace Media
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Mpd;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Media::Mpd::Test;

// SuiteMpdRootParser
SuiteMpdRootParser::SuiteMpdRootParser()
    : Suite("SuiteMpdRootParser")
{ }

void SuiteMpdRootParser::Test()
{
    // Empty XMl
    {
        Brn xml(Brx::Empty());
        Brn result;

        TEST(MpdRootParser::HasRootTag(xml) == false);
        TEST(MpdRootParser::TryGetRootTag(xml, result) == false);
        TEST(result.Bytes() == 0);
    }

    // Root tag present, but is empty
    {
        Brn xml("<MPD></MPD>");
        Brn result;

        TEST(MpdRootParser::HasRootTag(xml) == true);
        TEST(MpdRootParser::TryGetRootTag(xml, result) == true);
        TEST(result == xml);
    }

    // Root tag present, but has some contents.
    {
        Brn xml("<MPD>test</MPD>");
        Brn result;

        TEST(MpdRootParser::HasRootTag(xml) == true);
        TEST(MpdRootParser::TryGetRootTag(xml, result) == true);
        TEST(result == xml);
    }

    // Root tag presnt, but also with Doctype
    {
        Brn xml("<?xml version=\"1.0\" encoding=\"UTF-8\"?><MPD>test</MPD>");
        Brn result;

        TEST(MpdRootParser::HasRootTag(xml) == true);
        TEST(MpdRootParser::TryGetRootTag(xml, result) == true);
        TEST(result == Brn("<MPD>test</MPD>"));
    }
}


// SuiteMpdElementParser
SuiteMpdElementParser::SuiteMpdElementParser()
    : Suite("SuiteMpdElementParser")
{ }

void SuiteMpdElementParser::Test()
{
    TestAttributes();
    TestFetchingChildren();
}

void SuiteMpdElementParser::TestAttributes()
{
    // No attributes
    {
        Brn xml("<Tag></Tag>");
        int callCount = 0;

        AttributeCallback cb = [&](const Brx&, const Brx&) {
            callCount++;
            return EIterationDecision::Continue;
        };

        TEST(MpdElementParser::TryGetAttributes(xml, Brn("Tag"), cb));
        TEST(callCount == 0);
    }

    // Single, unknown attribute
    {
        Brn xml("<Tag test=\"true\"></Tag>");

        AttributeCallback cb = [&](const Brx& name, const Brx& value) {
            TEST(name == Brn("test"));
            TEST(value == Brn("true"));
            return EIterationDecision::Continue;
        };

        TEST(MpdElementParser::TryGetAttributes(xml, Brn("Tag"), cb));
    }

    // Multiple attributes
    {
        Brn xml("<Tag test=\"true\" context=\"none\" name=\"whitespace\"></Tag>");

        AttributeCallback cb = [&](const Brx& name, const Brx& value) {
            if (name == Brn("test")) {
                TEST(value == Brn("true"));
            }
            else if (name == Brn("context")) {
                TEST(value == Brn("none"));
            }
            else if (name == Brn("name")) {
                TEST(value == Brn("whitespace"));
            }
            else {
                TEST(false);
            }

            return EIterationDecision::Continue;
        };

        TEST(MpdElementParser::TryGetAttributes(xml, Brn("Tag"), cb));
    }
}

void SuiteMpdElementParser::TestFetchingChildren()
{
    // No children present
    {
        Brn xml("<Parent></Parent>");
        ChildElementCallback cb = [] (const Brx&, const Brx&, EMpdElementType) {
            TEST(false); // SHOULD NOT BE REACHED
            return EIterationDecision::Stop;
        };

        TEST(MpdElementParser::TryGetChildElements(xml, cb));
    }


    // Single Child
    {
        TBool isFirstChild = true;
        Brn xml("<Parent><Child1></Child1></Parent>");
        ChildElementCallback cb = [&isFirstChild] (const Brx& childName, const Brx& childXml, EMpdElementType type) {
            TEST(isFirstChild);
            isFirstChild = false;

            TEST(childName == Brn("Child1"));
            TEST(childXml == Brn("<Child1></Child1>"));
            TEST(type == EMpdElementType::Unknown);

            return EIterationDecision::Continue;
        };

        TEST(MpdElementParser::TryGetChildElements(xml, cb));
    }

    // Single Child with it's own children
    // Root
    //  Child1
    //   SubChild
    //   SubChild
    {
        TUint callCount = 0;
        Brn xml("<Parent><Child1><SubChild1></SubChild1><SubChild2></SubChild2></Child1></Parent>");
        ChildElementCallback cb = [&callCount, &cb]( const Brx& childTag, const Brx& childXml, EMpdElementType type) {
            ++callCount;

            if (callCount == 1) {
                TEST(childTag == Brn("Child1"));
                TEST(childXml == Brn("<Child1><SubChild1></SubChild1><SubChild2></SubChild2></Child1>"));
                TEST(type == EMpdElementType::Unknown);

                TEST(MpdElementParser::TryGetChildElements(childXml, cb));
            }
            else if (callCount == 2) {
                TEST(childTag == Brn("SubChild1"));
                TEST(childXml == Brn("<SubChild1></SubChild1>"));
                TEST(type == EMpdElementType::Unknown);
            }
            else if (callCount == 3) {
                TEST(childTag == Brn("SubChild2"));
                TEST(childXml == Brn("<SubChild2></SubChild2>"));
                TEST(type == EMpdElementType::Unknown);
            }
            else {
                TEST(false);
            }

            return EIterationDecision::Continue;
        };


        TEST(MpdElementParser::TryGetChildElements(xml, cb));
    }

    // Multiple children
    {
        TUint callCount = 0;
        Brn xml("<Parent><Child1></Child1><Period></Period></Parent>");
        ChildElementCallback cb = [&callCount] (const Brx& childTag, const Brx& childXml, EMpdElementType type) {
            ++callCount;

            if (callCount == 1) {
                TEST(childTag == Brn("Child1"));
                TEST(childXml == Brn("<Child1></Child1>"));
                TEST(type == EMpdElementType::Unknown);
            }
            else if (callCount == 2) {
                TEST(childTag == Brn("Period"));
                TEST(childXml == Brn("<Period></Period>"));
                TEST(type == EMpdElementType::Period);
            }
            else {
                TEST(false);
            }

            return EIterationDecision::Continue;
        };

        TEST(MpdElementParser::TryGetChildElements(xml, cb));
    }

    // Multiple children, but we stop after the first!
    {
        TUint callCount = 0;
        Brn xml("<Parent><Child1></Child1><Period></Period></Parent>");
        ChildElementCallback cb = [&callCount] (const Brx& childTag, const Brx& childXml, EMpdElementType type) {
            ++callCount;

            if (callCount == 1) {
                TEST(childTag == Brn("Child1"));
                TEST(childXml == Brn("<Child1></Child1>"));
                TEST(type == EMpdElementType::Unknown);
            }
            else {
                TEST(false);
            }

            return EIterationDecision::Stop;
        };

        TEST(MpdElementParser::TryGetChildElements(xml, cb));
    }
}



// SuiteMpdSupplementalPropertyParser
SuiteMpdSupplementalPropertyParser::SuiteMpdSupplementalPropertyParser()
    : Suite("SuiteMpdSupplementalPropertyParser")
{ }

void SuiteMpdSupplementalPropertyParser::Test()
{
    Brn key;
    Brn value;

    // No content
    {
        Brn xml("");
        TEST(MpdSupplementalPropertyParser::TryParse(xml, key, value) == false);
        TEST(key.Bytes() == 0);
        TEST(value.Bytes() == 0);
    }

    // SupplementalProperty, but no matching attributes
    {
        Brn xml("<SupplementalProperty></SupplementalProperty>");
        TEST(MpdSupplementalPropertyParser::TryParse(xml, key, value) == false);
        TEST(key.Bytes() == 0);
        TEST(value.Bytes() == 0);

        Brn xml2("<SupplementalProperty a=\"b\"></SupplementalProperty>");
        TEST(MpdSupplementalPropertyParser::TryParse(xml2, key, value) == false);
        TEST(key.Bytes() == 0);
        TEST(value.Bytes() == 0);
    }

    // SupplementalProperty, but only has a value.
    {
        Brn xml("<SupplementalProperty value=\"test\"></SupplementalProperty>");
        TEST(MpdSupplementalPropertyParser::TryParse(xml, key, value) == false);
        TEST(key.Bytes() == 0);
        TEST(value.Bytes() == 0);
    }

    // SupplementalProperty with both key & value
    {
        Brn xml("<SupplementalProperty schemeIdUri=\"urn:test\" value=\"a value\"></SupplementalProperty>");
        TEST(MpdSupplementalPropertyParser::TryParse(xml, key, value));
        TEST(key == Brn("urn:test"));
        TEST(value == Brn("a value"));
    }

    key.Set(Brx::Empty());
    value.Set(Brx::Empty());

    // ContentProtection with only value
    {
        Brn xml("<ContentProtection value=\"test\"></ContentProtection>");
        TEST(MpdSupplementalPropertyParser::TryParseOfType(xml, Brn("ContentProtection"), key, value) == false);
        TEST(key.Bytes() == 0);
        TEST(value.Bytes() == 0);
    }

    // ContentProection with key & value
    {
        Brn xml("<ContentProtection schemeIdUri=\"urn:test\" value=\"a value\"></ContentProtection>");
        TEST(MpdSupplementalPropertyParser::TryParseOfType(xml, Brn("ContentProtection"), key, value));
        TEST(key == Brn("urn:test"));
        TEST(value == Brn("a value"));
    }
}



extern void TestContentMpd()
{
    Runner runner("ContentMpd tests\n");
    runner.Add(new SuiteMpdRootParser());
    runner.Add(new SuiteMpdElementParser());
    runner.Add(new SuiteMpdSupplementalPropertyParser());
    runner.Run();
}

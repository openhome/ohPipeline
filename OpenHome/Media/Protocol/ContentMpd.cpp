#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Net/Private/XmlParser.h>

#include "ContentMpd.h"

#include <map>
#include <limits>
#include <algorithm>


using namespace OpenHome;
using namespace OpenHome::Net;
using namespace OpenHome::Media;
using namespace OpenHome::Media::Mpd;


// ContentMpd
ContentMpd::ContentMpd()
{ }

ContentMpd::~ContentMpd()
{ }

TBool ContentMpd::Recognise(const Brx& /*aUri*/, const Brx& aMimeType, const Brx& aData)
{
    static const Brn kContentType("application/dash+xml");

    // If we have no parsers for the manifest then there is nothing we can do. No point in checking the content type
    if (iParsers.size() == 0) {
        return false;
    }

    // Just incase, we'll copy the 'Data' param here. At the moment our parsing functions expect to work
    // on the complete XML document which is not currently available in 'Stream()' without re-reading everything
    // which creates a duplicate copy.
    iData = Optional<const Brx>(aData);

    // Assume that the MIME Type is just a single value...
    if (aMimeType == kContentType) {
        return true;
    }

    // Some services provide multiple header values to define encoding + content type.
    Parser p(aMimeType);
    Brn val = p.Next(';');

    while(val.Bytes() > 0) {
        if (val == kContentType) {
            return true;
        }

        val = p.Next(';');
    }

    val = p.Remaining();
    if (val == kContentType) {
        return true;
    }

    return false;
}

ProtocolStreamResult ContentMpd::Stream(IReader& /*aReader*/, TUint64 /*aTotalBytes*/)
{
    // We need at least one Parser available in order to stream
    if (iParsers.size() == 0) {
        return ProtocolStreamResult::EProtocolStreamErrorUnrecoverable;
    }

    if (!iData.Ok()) {
        return ProtocolStreamResult::EProtocolStreamErrorUnrecoverable;
    }

    const Brx& manifest = iData.Unwrap();
    ProtocolStreamResult res = EProtocolStreamErrorUnrecoverable;

    for(auto& entry : iParsers) {
        if (entry->CanProcess(manifest)) {
            Log::Print("ContentMpd::Stream - Selected Parser '%.*s'\n", PBUF(entry->Id()));
            res = entry->Process(manifest);
            break;
        }
    }

    // Clear off our data to we're not going to access the potentially changed buffer in the future....
    iData = {};
    return res;
}

void ContentMpd::AddParser(IMpdParser* aParser)
{
    iParsers.push_back(std::move(aParser));
}





// MpdElements
const Brn MpdElements::kRoot("MPD");
const Brn MpdElements::kPeriod("Period");
const Brn MpdElements::kAdaptationSet("AdaptationSet");
const Brn MpdElements::kRepresentation("Representation");
const Brn MpdElements::kBaseUrl("BaseURL");
const Brn MpdElements::kSegmentList("SegmentList");
const Brn MpdElements::kSegmentUrl("SegmentURL");
const Brn MpdElements::kInitialization("Initialization");
const Brn MpdElements::kSupplementalProperty("SupplementalProperty");
const Brn MpdElements::kContentProtection("ContentProtection");

static const std::map<Brn, EMpdElementType, BufferCmp> kElementTypeLookup = {
    { MpdElements::kPeriod, EMpdElementType::Period },
    { MpdElements::kAdaptationSet, EMpdElementType::AdaptationSet },
    { MpdElements::kRepresentation, EMpdElementType::Representation },
    { MpdElements::kBaseUrl, EMpdElementType::BaseUrl },
    { MpdElements::kSegmentList, EMpdElementType::SegmentList },
    { MpdElements::kInitialization, EMpdElementType::Initialization },
    { MpdElements::kSegmentUrl, EMpdElementType::SegmentUrl },
    { MpdElements::kSupplementalProperty, EMpdElementType::SupplementalProperty },
    { MpdElements::kContentProtection, EMpdElementType::ContentProtection },
};


// MpdRoot
const Brn MpdRoot::kTypeStatic("static");
const Brn MpdRoot::kTypeDynamic("dynamic");

const Brn MpdRoot::kAttributeType("type");
const Brn MpdRoot::kAttributeProfiles("profiles");
const Brn MpdRoot::kAttributeMinBufferTime("minBufferTime");

// MpdPeriod
const Brn MpdPeriod::kAttributeId("id");

// MpdAdaptationSet
const Brn MpdAdaptationSet::kAttributeId("id");
const Brn MpdAdaptationSet::kAttributeContentType("contentType");
const Brn MpdAdaptationSet::kAttributeSelectionPriority("selectionPriority");

// MpdRepresentation
const Brn MpdRepresentation::kAttributeId("id");
const Brn MpdRepresentation::kAttributeBandwidth("bandwidth");

const Brn MpdRepresentation::kAttributeCodecs("codecs");
const Brn MpdRepresentation::kAttributeMimeType("mimeType");
const Brn MpdRepresentation::kAttributeQualityRanking("qualityRanking");

// MpdSupplementalProperty
const Brn MpdSupplementalProperty::kAttributeKey("schemeIdUri");
const Brn MpdSupplementalProperty::kAttributeValue("value");


// MpdSupplementalPropertyParser
TBool MpdSupplementalPropertyParser::TryParse(const Brx& aXml, Brn& aKey, Brn& aValue)
{
    return TryParseOfType(aXml, MpdElements::kSupplementalProperty, aKey, aValue);
}

TBool MpdSupplementalPropertyParser::TryParseOfType(const Brx& aXml, const Brx& aElementType, Brn& aKey, Brn& aValue)
{
    if (aXml.Bytes() == 0) {
        return false;
    }

    Brn key;
    Brn value;
    AttributeCallback cb = [&key, &value] (const Brx& attributeKey, const Brx& attributeValue) {
        if (attributeKey == MpdSupplementalProperty::kAttributeKey) {
            key.Set(attributeValue);
        }
        else if (attributeKey == MpdSupplementalProperty::kAttributeValue) {
            value.Set(attributeValue);
        }

        return EIterationDecision::Continue;
    };

    if (!MpdElementParser::TryGetAttributes(aXml, aElementType, cb)) {
        return false;
    }

    // The 'Key' of a SupplementalProperty is manditory. If this isn't present, we're not correctly formed.
    if (key.Bytes() == 0) {
        return false;
    }

    aKey.Set(key);
    aValue.Set(value);
    return true;
}

// MpdElementParser
TBool MpdElementParser::TryGetAttributes(const Brx& aXml, const Brx& aExpectedStartTag, AttributeCallback aCallback)
{
    if (aXml.Bytes() == 0) {
        return false;
    }

    if (aXml.At(0) != '<') {
        return false;
    }

    const Brn actualStartTag(aXml.Ptr() + 1,
                             std::min(aXml.Bytes(), aExpectedStartTag.Bytes()));
    if (actualStartTag != aExpectedStartTag) {
        return false;
    }

    const TUint closingTagIndex = Ascii::IndexOf(aXml, '>');
    Parser p(Brn(aXml.Ptr(), closingTagIndex));

    p.Next(' '); // Clear tag name...

    while(!p.Finished()) {
        const Brn attributeName = p.Next('=');
        p.Next('\"');
        const Brn attributeValue = p.Next('\"');

        const EIterationDecision shouldContinue = aCallback(attributeName, attributeValue);
        if (shouldContinue == EIterationDecision::Stop) {
            break;
        }
    }

    return true;
}

TBool MpdElementParser::TryGetChildElements(const Brx& aXml, ChildElementCallback aCallback)
{
    if (aXml.Bytes() == 0) {
        return false;
    }

    // First - we want to trim off the starting tag, so we can access the child elements!
    const TUint endTagIndex = Ascii::IndexOf(aXml, '>');
    if (endTagIndex == aXml.Bytes()) {
        return false;
    }

    const TByte* start = aXml.Ptr() + endTagIndex + 1;  // +1 to skip the trailing '>' value
    const TUint length = aXml.Bytes() - endTagIndex - 1;

    Brn xml(start, length);

    Brn child;
    Brn tagName;
    EMpdElementType type = EMpdElementType::Unknown;

    // Get next XML element...
    while (XmlParserBasic::TryNext(xml, tagName, xml, child)) {

        if (tagName.Bytes() == 0) {
            return false;
        }

        // Try and assign to a known type...
        auto it = kElementTypeLookup.find(tagName);
        type = it != kElementTypeLookup.end() ? it->second
                                              : EMpdElementType::Unknown;

        const EIterationDecision shouldContinue = aCallback(tagName, child, type);
        if (shouldContinue == EIterationDecision::Stop) {
            break;
        }
    }

    return true;
}

// MpdRootParser
TBool MpdRootParser::HasRootTag(const Brx& aXml)
{
    Brn ignore;
    return XmlParserBasic::TryFind(MpdElements::kRoot, aXml, ignore);
}

TBool MpdRootParser::TryGetRootTag(const Brx& aXml, Brn& aTag)
{
    Brn tagName;
    Brn tagContents;

    while(tagName != MpdElements::kRoot) {
        if (!XmlParserBasic::TryNext(aXml, tagName, tagContents)) {
            return false;
        }

        if (tagName == MpdElements::kRoot) {

            // If the document contains a doctype, we'll need to strip that off.
            if (Ascii::Contains(tagContents, '?')) {
                Parser p(tagContents);
                p.Next('?');    // Strips opening '<'
                p.Next('?');    // Strips contents of doctype header between the 2 '?' values
                p.Next('>'); // Strips final '>'
                aTag.Set(p.Remaining());
            } else {
                aTag.Set(tagContents);
            }
            return true;
        }
    }

    return false;
}



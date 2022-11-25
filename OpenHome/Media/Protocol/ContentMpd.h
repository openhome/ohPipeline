#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Media/Protocol/Protocol.h>

#include <vector>
#include <functional>

namespace OpenHome {
namespace Media {

class IMpdParser
{
public:
    virtual ~IMpdParser() { };
    virtual const Brx& Id() const = 0;
    virtual TBool CanProcess(const Brx& aMpd) = 0;
    virtual ProtocolStreamResult Process(const Brx& aMpd) = 0;
};


class ContentMpd : public ContentProcessor
{
public:
    ContentMpd();
    ~ContentMpd();

public: // ContentProcessor
    TBool Recognise(const Brx& aUri, const Brx& aMimeType, const Brx& aData);
    ProtocolStreamResult Stream(IReader& aReader, TUint64 aTotalBytes);

public:
    void AddParser(IMpdParser* aParser);

private:
    Optional<const Brx> iData;
    std::vector<IMpdParser*> iParsers;
};




/* This namespace contains a set of helper classes for parsing MPEG-DASH manifest files.
 *
 * Given the spec is sufficiently wooly, each service we consume provides a different set of tags
 * attributes & extensions. Therefore, we provide a number of helpful parsing classes which can be
 * used as the building blocks for service specific implementations.
 *
 * Spec used for implementation can be found:
 * - https://standards.iso.org/ittf/PubliclyAvailableStandards/index.html
 * - Search for `DASH` or `ISO/IEC 23009-1:2022`
 * - We are using the 5th edition */
namespace Mpd {

enum class EMpdElementType
{
    Period,
    AdaptationSet,
    Representation,
    BaseUrl,
    SegmentList,
    Initialization,
    SegmentUrl,
    SupplementalProperty,
    ContentProtection,
    Unknown,
};

enum class EIterationDecision {
    Continue,
    Stop,
};

using AttributeCallback = std::function<EIterationDecision (const Brx& aName, const Brx& aValue)>;
using ChildElementCallback = std::function<EIterationDecision (const Brx& aElementName, const Brx& aElementXml, EMpdElementType aElementType)>;


class MpdElements
{
public:
    static const Brn kRoot;
    static const Brn kPeriod;
    static const Brn kAdaptationSet;
    static const Brn kRepresentation;
    static const Brn kBaseUrl;
    static const Brn kSegmentList;
    static const Brn kInitialization;
    static const Brn kSegmentUrl;
    static const Brn kSupplementalProperty;
    static const Brn kContentProtection;
};

// Spec Link: 5.3.1.2
class MpdRoot
{
public:
    // Required
    static const Brn kAttributeType;
    static const Brn kAttributeProfiles;
    static const Brn kAttributeMinBufferTime;

    static const Brn kTypeStatic;
    static const Brn kTypeDynamic;
};

// Spec Link: 5.3.2.2
class MpdPeriod
{
public:
    // Optional
    static const Brn kAttributeId;
};

// Spec Link: 5.3.3.2
class MpdAdaptationSet
{
public:
    // Optional
    static const Brn kAttributeId;
    static const Brn kAttributeContentType;
    static const Brn kAttributeSelectionPriority;   // Higher = Better. Should be ordered Highest -> Lowest
};

// Spec Link: 5.3.5.2
class MpdRepresentation
{
public:
    // Required
    static const Brn kAttributeId;
    static const Brn kAttributeBandwidth;

    // Optional
    static const Brn kAttributeQualityRanking;  // Spec says Lower = Better, but many services actually use Higher = Better
    static const Brn kAttributeCodecs;
    static const Brn kAttributeMimeType;
};

// Spec Link: 5.8.4.9
// NOTE: 'ContentProtection' (5.8.4.1) element is also defined as a SupplementalProperty upon first definition.
//       This defines the type of protection applied and indicates how subsequent 'ContentProtection' elements
//       should be interpreted.
class MpdSupplementalProperty
{
public:
    // Required
    static const Brn kAttributeKey;
    static const Brn kAttributeValue;
};


class MpdSupplementalPropertyParser
{
public:
    // Tries to parse an element of type 'SupplementalProperty'
    static TBool TryParse(const Brx& aXml, Brn& aKey, Brn& aValue);

    // Tries to parse a element of type 'aElementType' - e.g. SupplementalProperty, ContentProtection, AudioChannelConfiguration
    static TBool TryParseOfType(const Brx& aXml, const Brx& aElementType, Brn& aKey, Brn& aValue);
};



class MpdElementParser
{
public:
    // NOTE: Should both of these have an optional 'InterationDecsion' value passed back to them
    //       so the underlying loop knows if we need to exit? Eg - if we happen to have found what we are looking for
    //       then should we keep going??
    // Streams the attributes to the callback function
    static TBool TryGetAttributes(const Brx& aXml, const Brx& aExpectedStartTag, AttributeCallback aCallback);

    // Streams the child elements to the callback function.
    static TBool TryGetChildElements(const Brx& aXml, ChildElementCallback aCallback);
};


class MpdRootParser
{
public:
    // Checks for the presense of a MPD tag
    static TBool HasRootTag(const Brx& aXml);

    // Places the MPD tag into aRoot if present
    static TBool TryGetRootTag(const Brx& aXml, Brn& aRoot);
};

} // namespace Mpd



} // namespace Media
} // namespace OpenHome

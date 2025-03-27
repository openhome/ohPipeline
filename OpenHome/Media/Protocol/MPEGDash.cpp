#include <OpenHome/Functor.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Protocol/MPEGDash.h>
#include <OpenHome/Net/Private/XmlParser.h>
#include <OpenHome/Media/Supply.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Media/Protocol/ProtocolFactory.h>
#include <OpenHome/Media/Pipeline/Pipeline.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Time.h>
#include <OpenHome/Private/Timer.h>

#include <math.h>
#include <limits.h>

/* MPEG Dash Support
 * -----------------
 * Largely based off the 2022 version of the DASH spec as well as some supplimentary documents.
 *
 * MPEG DASH ISO23009-01: https://standards.iso.org/ittf/PubliclyAvailableStandards/index.html
 * DVB Extensions:        https://dvb.org/?standard=dvb-mpeg-dash-profile-for-transport-of-iso-bmff-based-dvb-services-over-ip-based-networks
 *
 * The intent of this is to implemen the bare minimum required for us to stream static & dynamic MPD documents
 * provided by the service(s) we integrate with. This is by no means a complete implementation supporting every
 * feature & manifest type of DASH. Furthermore, an attempt has been made to keep the memory footprint as low
 * as possible and as such some shortcuts have been taken. */


#define TRY_ASCII_UINT(val, res) do {                                  \
                                    try { res = Ascii::Uint(val); }    \
                                    catch (AsciiError&) { }            \
                                 } while (0);


using namespace OpenHome;
using namespace OpenHome::Net;
using namespace OpenHome::Media;

// Spec Link: Annex C: C.2
static const Brn kMimeType("application/dash+xml");


// MPD Manifest Common Tag Names
// ------------------------------

// As CStrings
static const TChar* kMPDTagRootName("MPD");
static const TChar* kMPDTagPeriodName("Period");
static const TChar* kMPDTagAdaptationSetName("AdaptationSet");
static const TChar* kMPDTagRepresentationName("Representation");

static const TChar* kMPDTagSegmentBaseName("SegmentBase");
static const TChar* kMPDTagSegmentListName("SegmentList");
static const TChar* kMPDTagSegmentTemplateName("SegmentTemplate");

static const TChar* kMPDTagSegmentUrlName("SegmentUrl");
static const TChar* kMPDTagSegmentInitalisationName("Initialization");

// As Brn
static const Brn kMPDTagRoot(kMPDTagRootName);
static const Brn kMPDTagPeriod(kMPDTagPeriodName);
static const Brn kMPDTagAdaptationSet(kMPDTagAdaptationSetName);
static const Brn kMPDTagRepresentation(kMPDTagRepresentationName);

static const Brn kMPDTagSegmentBase(kMPDTagSegmentBaseName);
static const Brn kMPDTagSegmentList(kMPDTagSegmentListName);
static const Brn kMPDTagSegmentTemplate(kMPDTagSegmentTemplateName);

static const Brn kMPDTagSegmentUrl(kMPDTagSegmentUrlName);


// XML Parsing Helpers - iterating through all a tag's attributes via a Parser class.
// Usage:
//      Parser p(...);
//      (void)TryCreateAttributeParser(<xml>, <tag name>, p);
//      while (TryReadAttribute(p, <attribute name>, <attribute value>) {
//          ...
//      }
static TBool TryCreateAttributeParser(const Brx& aXml, const Brx& aTagName, Parser& p)
{
    if (aXml.Bytes() == 0) {
        return false;
    }

    Brn trimmed = Ascii::Trim(aXml);

    if (trimmed.At(0) != '<') {
        return false;
    }

    const Brn actualStartTag(trimmed.Ptr() + 1,
                             std::min(trimmed.Bytes(), aTagName.Bytes()));
    if (actualStartTag != aTagName) {
        return false;
    }

    const TUint closingTagIndex = Ascii::IndexOf(trimmed, '>');
    p.Set(Brn(trimmed.Ptr(), closingTagIndex));

    p.Next(' '); // Clear tag name...

    return true;
}

static TBool TryReadAttribute(Parser& p, Brn& aName, Brn& aValue)
{
    if (!p.Finished()) {
        aName.Set(p.Next('='));
        p.Next('\"');
        aValue.Set(p.Next('\"'));
        return true;
    }

    return false;
}


// Helper to parse ISO8601 Timestamps in the format:
// PYYYY-MM-DDThh:mm:ssZ
static PointInTime TryParseMPDTime(const Brx& aTimeStr)
{
    if (aTimeStr.Bytes() == 0) {
        THROW(AsciiError); // Empty string, so nothing to parse
    }

    if (aTimeStr.At(aTimeStr.Bytes() - 1) != 'Z') {
        THROW(AsciiError); // Non-UTC timezone
    }

    Parser p(aTimeStr);
    TUint year   = Ascii::Uint(p.Next('-'));
    TByte month  = static_cast<TByte>(Ascii::Uint(p.Next('-')));
    TByte day    = static_cast<TByte>(Ascii::Uint(p.Next('T')));
    TByte hour   = static_cast<TByte>(Ascii::Uint(p.Next(':')));
    TByte min    = static_cast<TByte>(Ascii::Uint(p.Next(':')));
    TByte second = static_cast<TByte>(Ascii::Uint(p.Next('Z')));

    return PointInTime(day, month, year, hour, min, second);
}


// ISO8601Duration
ISO8601Duration::ISO8601Duration()
{
    iSeconds = 0;
}

TUint64 ISO8601Duration::TotalSeconds() const
{
    return iSeconds;
}

TBool ISO8601Duration::TryParse(const Brx& aDurationStr)
{
    if (aDurationStr.Bytes() <= 2) {
        return false;
    }

    // Unsupported format
    if (aDurationStr.At(0) != 'P' || aDurationStr.At(1) != 'T') {
        return false;
    }

    iSeconds = 0;

    TUint index = 2;
    TUint start = index;
    TUint scale = 0;
    TBool process = false;
    Brn parsed;
    Brn wholePart;
    Brn fractionalPart;

    while (index < aDurationStr.Bytes()) {
        TByte val = aDurationStr.At(index);

        if (val == 'H') {
            scale = Time::kSecondsPerHour;
            process = true;
        }
        else if (val == 'M') {
            scale = Time::kSecondsPerMinute;
            process = true;
        }
        else if (val == 'S') {
            scale = 1;
            process = true;
        }
        else if (val != '.' && val != ',' && !Ascii::IsDigit(val)) {
            LOG_ERROR(kMedia, "ISO8601Duration::TrySet - Unexpected character '%c' found\n", val);
            return false;
        }

        if (process) {
            parsed.Set(aDurationStr.Ptr() + start, index - 1);

            wholePart.Set(0, 0);
            fractionalPart.Set(0, 0);

            SplitNumberIntoParts(parsed, wholePart, fractionalPart);

            if (val == 'S' && fractionalPart.Bytes() > 0) {
                LOG_ERROR(kMedia, "ISO8601Duration::TrySet - We don't support fractional seconds.\n");
                return false;
            }

            try {
                iSeconds += ConvertPartsToSeconds(wholePart, fractionalPart, scale);
            }
            catch (AssertionFailed&) {
                throw;
            }
            catch (Exception&) {
                return false;
            }

            process = false;
            start   = index + 1;
        }

        index += 1;
    }

    return true;
}

void ISO8601Duration::SplitNumberIntoParts(const Brx& aNumberStr,
                                           Brn& aWholePart,
                                           Brn& aFractionalPart)
{
    TUint i = 0;
    TUint len = 0;
    TByte value = 0;
    const TUint maxBytes = aNumberStr.Bytes();

    // Consume up until we hit a non numerical character...
    while (i < maxBytes) {
        value = aNumberStr.At(i);
        if (!Ascii::IsDigit(value)) {
            aWholePart.Set(aNumberStr.Ptr(), len);
            break;
        }
        i += 1;
        len += 1;
    }


    // Consume the non numerical charcter...
    i += 1;

    // Check if we have a fractional indicator, and if so then consume until we hit the end
    if (value == '.' || value == ',') {
        len = 0;

        while (i < maxBytes) {
            value = aNumberStr.At(i);
            if (!Ascii::IsDigit(value)) {
                aFractionalPart.Set(aWholePart.Ptr() + aWholePart.Bytes() + 1, len);
                break;
            }

            i += 1;
            len += 1;
        }
    }
}

TUint64 ISO8601Duration::ConvertPartsToSeconds(const Brx& aWholePart, const Brx& aFractionalPart, TUint aComponentSeconds)
{
    TUint cursor      = 0;
    TUint multiFactor = 10;
    TUint64 result    = 0;

    TUint wholeInt = Ascii::Uint(aWholePart);
    result += (wholeInt * aComponentSeconds);

    while (cursor < aFractionalPart.Bytes()) {
        const TUint v  = Ascii::DecValue(aFractionalPart.At(cursor));
        const float x  = (static_cast<float>(v * aComponentSeconds)) / static_cast<float>(multiFactor);
        result        += static_cast<TUint64>(round(x)); // CAUTION!! See header note on this class about accuracy
        cursor        += 1;
        multiFactor   *= 10;
    }

    return result;
}



// BaseUrlVisitor
class DefaultBaseUrlSelector : public IBaseUrlSelector
{
public:
    DefaultBaseUrlSelector();

public: // IBaseUrlSelector
    const Brx& SelectedBaseUrl() const override;

private: // IBaseUrlVisitor
    void VisitBaseUrl(const Brx& aLevel, TUint aIndex, TUint aSelectionPriority, TUint aWeight, const Brx& aServiceLocation, const Brx& aUrl, const Brx& aElementXml) override;

private:
    Brn   iCurrentUrl;
    TUint iCurrentSelectionWeight;
    TUint iCurrentSelectionPriority;
    Brn   iCurrentServiceLocation;
};

DefaultBaseUrlSelector::DefaultBaseUrlSelector()
    : iCurrentUrl(Brx::Empty())
    , iCurrentSelectionWeight(0)
    , iCurrentSelectionPriority(UINT_MAX)
    , iCurrentServiceLocation(Brx::Empty())
{ }

const Brx& DefaultBaseUrlSelector::SelectedBaseUrl() const
{
    return iCurrentUrl;
}

void DefaultBaseUrlSelector::VisitBaseUrl(const Brx& /*aLevel*/, TUint /*aIndex*/, TUint aSelectionPriority, TUint aWeight, const Brx& aServiceLocation, const Brx& aUrl, const Brx& /*aElementXml*/)
{
    if (aUrl.Bytes() == 0) {
        return;
    }

    /* BaseURL Selection
     * ------------------------
     * Spec Link: 5.6.4
     * DVB Spec Link:  10.8.2.1
     * ------------------------
     * By default, MPD BaseUrl entries should be listed in selection order.
     *
     * Optionally, BaseURLs can be grouped by "serviceLocation" to provide additional information to the client selection.
     * For example, BaseURLs from the same CDN might be grouped together so if one fails, it might be best to try another CDN
     * as it's likely URLs from the same CDN will suffer the same problem(s).
     *
     * DVB introduces the concept where BaseURLs can be prioritised/weighted to aid with load balancing or to provide
     * hints to the client which server(s) are closer to them based on the request for the MPD
     * ------------------------
     * What we do:
     *  - We select the first BaseURL element listed in the XML
     *  - If multiple BaseURL elements are present we'll:
     *      - Compare ONLY those with the same serviceLocation
     *      - Pick the one with the best priority && highest weighting */

    TBool selectUrl = iCurrentUrl.Bytes() == 0;
    if (!selectUrl) {
        if (aServiceLocation == iCurrentServiceLocation) {
            selectUrl = aSelectionPriority < iCurrentSelectionPriority;   // Lower = better
            selectUrl = selectUrl && (aWeight > iCurrentSelectionWeight); // Higher = better
        }
    }

    if (selectUrl) {
        iCurrentUrl.Set(aUrl);
        iCurrentSelectionWeight = aWeight;
        iCurrentSelectionPriority = aSelectionPriority;
    }
}

// BaseUrlCollection
const Brn BaseUrlCollection::kTagBaseUrl("BaseURL");

const Brn BaseUrlCollection::kAttributeDVBWeight("dvb:weight");
const Brn BaseUrlCollection::kAttributeServiceLocation("serviceLocation");
const Brn BaseUrlCollection::kAttributeDVBSelectionPriority("dvb:priority");

const TUint BaseUrlCollection::kDefaultWeight = 1;
const TUint BaseUrlCollection::kDefaultSelectionPriority = 1;

TBool BaseUrlCollection::TryVisit(const Brx& aXml, const Brx& aLevel, IBaseUrlVisitor& aVisitor)
{
    if (aXml.Bytes() == 0) {
        return false;
    }

    Brn tag;
    Brn url;
    Brn value;

    Brn xmlToParse(aXml);

    TUint index = 0;
    TUint weight;
    Brn serviceLocation;
    TUint selectionPriority;

    while(XmlParserBasic::TryNext(xmlToParse, tag, xmlToParse, value)) {
        if (tag == kTagBaseUrl) {
            Brn attributeKey;
            Brn attributeValue;
            weight            = kDefaultWeight;
            selectionPriority = kDefaultSelectionPriority;
            serviceLocation.Set(Brx::Empty());

            // Get the actual URL value
            if (!XmlParserBasic::TryFind(kTagBaseUrl, value, url)) {
                return false;
            }

            // Parse the attributes...
            Parser p;
            if (!TryCreateAttributeParser(value, kTagBaseUrl, p)) {
                return false;
            }


            while(TryReadAttribute(p, attributeKey, attributeValue)) {
                if (attributeKey.Equals(kAttributeServiceLocation)) {
                    serviceLocation.Set(attributeValue);
                }
                else if (attributeKey.Equals(kAttributeDVBWeight)) {
                    TRY_ASCII_UINT(value, weight);
                }
                else if (attributeKey.Equals(kAttributeDVBSelectionPriority)) {
                    TRY_ASCII_UINT(value, selectionPriority);
                }
            }

            aVisitor.VisitBaseUrl(aLevel, index, selectionPriority, weight, serviceLocation, url, value);
            index += 1;
        }
    }

    // Only return true if at least one BaseURL has been processed
    return index > 0;
}


// ContentProtection
// Spec Link: 5.8.4.1.
const Brn ContentProtection::kTagContentProtection("ContentProtection");

const Brn ContentProtection::kAttributeValue("value");
const Brn ContentProtection::kAttributeSchemeIdUri("schemeIdUri");
const Brn ContentProtection::kAttributeCencDefaultKID("cenc:default_KID");

const Brn ContentProtection::kProtectionTypeMPEG4("urn:mpeg:dash:mp4protection:2011");

TBool ContentProtection::IsMPEG4Protection() const
{
    return iSchemeIdUri.Bytes() > 0 && iSchemeIdUri == kProtectionTypeMPEG4;
}

TBool ContentProtection::TrySet(const Brx& aXml)
{
    if (aXml.Bytes() == 0) {
        return false;
    }

    // Reset internals...
    iSchemeIdUri.Set(Brx::Empty());
    iValue.Set(Brx::Empty());
    iDefaultKID.Set(Brx::Empty());

    iPropertiesSchemeIdUri.Set(Brx::Empty());
    iPropertiesXML.Set(Brx::Empty());

    Brn result;
    Brn attributeName;
    Brn attributeValue;
    Brn xmlToParse(aXml);

    while(XmlParserBasic::TryGetElement(kTagContentProtection, xmlToParse, xmlToParse, result)) {
        if (!XmlParserBasic::TryFindAttribute(kTagContentProtection, kAttributeSchemeIdUri, result, attributeValue)) {
            Log::Print("ContentProtection::TrySet - Failed to find schemeIdUri on ContentProtection element. Element is malformed\n");
            return false;
        }

        if (attributeValue.BeginsWith(Brn("urn:uuid"))) {
            // Got the supplimentary properties thingy
            iPropertiesSchemeIdUri.Set(attributeValue);
            iPropertiesXML.Set(result);
        }
        else {
            // Got the main thing outlining the actual protection mechanism
            Parser p;
            if (!TryCreateAttributeParser(result, kTagContentProtection, p)) {
                Log::Print("ContentProtection::TrySet - Failed to construct attribute parser around a ContentProtection element. Likely element is malformed.\n");
                return false;
            }

            while (TryReadAttribute(p, attributeName, attributeValue)) {
                if (attributeName.Equals(kAttributeSchemeIdUri)) {
                    iSchemeIdUri.Set(attributeValue);
                }
                else if (attributeName.Equals(kAttributeValue)) {
                    iValue.Set(attributeValue);
                }
                else if (attributeName.Equals(kAttributeCencDefaultKID)) {
                    iDefaultKID.Set(attributeValue);
                }
            }
        }
    }

    return iSchemeIdUri.Bytes() > 0;
}

// SegmentTemplate
const Brn SegmentTemplate::kTemplateParameterTime("Time");
const Brn SegmentTemplate::kTemplateParameterNumber("Number");
const Brn SegmentTemplate::kTemplateParameterSubNumber("SubNumber");
const Brn SegmentTemplate::kTemplateParameterRepresentationId("RepresentationID");
const Brn SegmentTemplate::kTemplateParameterRepresentationBandwidth("Bandwidth");

const Brn SegmentTemplate::kAttributeInitialization("initialization");
const Brn SegmentTemplate::kAttributeMedia("media");
const Brn SegmentTemplate::kAttributeDuration("duration");
const Brn SegmentTemplate::kAttributeTimescale("timescale");
const Brn SegmentTemplate::kAttributeStartNumber("startNumber");

SegmentTemplate::SegmentTemplate(const Brx& aXml)
{
    iStartNumber = 1; // Default value, if not present. Spec Link: 5.3.9.5.3
    iDuration    = 0;
    iTimescale   = 1; // Default value, if not present. Spec link: 5.10.2.2 (Table 38)

    Brn key;
    Brn value;
    Parser p;
    if (TryCreateAttributeParser(aXml, kMPDTagSegmentTemplate, p)) {
        while(TryReadAttribute(p, key, value)) {
            if (key.Equals(kAttributeInitialization)) {
                iInitialization.Set(value);
            }
            else if (key.Equals(kAttributeMedia)) {
                iMedia.Set(value);
            }
            else if (key.Equals(kAttributeDuration)) {
                TRY_ASCII_UINT(value, iDuration);
            }
            else if (key.Equals(kAttributeTimescale)) {
                TRY_ASCII_UINT(value, iTimescale);
            }
            else if (key.Equals(kAttributeStartNumber)) {
                TRY_ASCII_UINT(value, iStartNumber);
            }
        }
    }
}

const Brx& SegmentTemplate::Initialization() const
{
    return iInitialization;
}

const Brx& SegmentTemplate::Media() const
{
    return iMedia;
}

TUint SegmentTemplate::Duration() const
{
    return iDuration;
}

TUint SegmentTemplate::Timescale() const
{
    return iTimescale;
}

TUint SegmentTemplate::StartNumber() const
{
    return iStartNumber;
}


// static
TBool SegmentTemplate::TryFormatTemplateUrl(Bwx& aUrlBuf, const Brx& aTemplateUrl, const SegmentTemplateParams& aTemplateParams)
{
    for (TUint i = 0; i < aTemplateUrl.Bytes(); i += 1) {
        if (aTemplateUrl.At(i) != '$') {
            aUrlBuf.Append(aTemplateUrl.At(i));
        }
        else {
            for (TUint j = i + 1; j < aTemplateUrl.Bytes(); j += 1) {
                if (aTemplateUrl.At(j) == '$') {
                    Brn templateParam(aTemplateUrl.Ptr() + i + 1,
                                      j - i - 1);
                    LOG_TRACE(kMedia, "MPD::TryFormatTemplateUrl - Found template param! %.*s\n", PBUF(templateParam));

                    // FIXME: Need to actually parse and handle widths here if they are specified in the parameter string
                    //        Default = 1
                    TUint width = 1;

                    // NOTE: All comparisons here are case senstive
                    if (templateParam == kTemplateParameterRepresentationId) {
                        aUrlBuf.Append(aTemplateParams.iRepresentationId);
                    }
                    else if (templateParam == kTemplateParameterRepresentationBandwidth) {
                        aUrlBuf.AppendPrintf("%*u", width, aTemplateParams.iBandwidth);
                    }
                    else if (templateParam == kTemplateParameterNumber) {
                        aUrlBuf.AppendPrintf("%*u", width, aTemplateParams.iNumber);
                    }
                    else if (templateParam == kTemplateParameterSubNumber) {
                        aUrlBuf.AppendPrintf("%*u", width, aTemplateParams.iSubNumber);
                    }
                    else if (templateParam == kTemplateParameterTime) {
                        aUrlBuf.AppendPrintf("%*u", width, aTemplateParams.iTime);
                    }
                    else {
                        LOG_ERROR(kMedia, "MPD::TyrFormatTemplateUrl - Unknown template value: '%.*s' found.\n", PBUF(templateParam));
                        return false;
                    }

                    i = j;
                    break;
                }
            }
        }
    }

    return true;
}



// MPDDocument
const Brn MPDDocument::kAttributeAvailabilityStartTime("availabilityStartTime");

const Brx& MPDDocument::Xml() const
{
    return iXml;
}

const Brx& MPDDocument::ElementXml() const
{
    return iElementXml;
}

TBool MPDDocument::IsStatic() const
{
    return iIsStatic;
}

TBool MPDDocument::HasExpired() const
{
    return iExpired;
}

void MPDDocument::SetExpired()
{
    iExpired = true;
}


const MPDPeriod& MPDDocument::Period() const
{
    return iPeriod;
}

TUint64 MPDDocument::MinimumUpdatePeriod() const
{
    return iMinimumUpdatePeriod.TotalSeconds();
}


TBool MPDDocument::IsContentProtected() const
{
    return iContentProtection.iSchemeIdUri.Bytes() > 0;
}

const ContentProtection& MPDDocument::ContentProtectionDetails() const
{
    return iContentProtection;
}


void MPDDocument::GetBaseUrl(Bwx& aUrlBuffer)
{
    ASSERT(aUrlBuffer.MaxBytes() >= Uri::kMaxUriBytes);

    iBaseUrl.Clear();

    DefaultBaseUrlSelector selector;

    /* BaseURL Selection
     * -----------------
     * Spec Link: 5.6.4
     * -----------------
     * When selecting a baseURL we must work down the document as child BaseURL elements might be relative to their parent.
     * However, when child's BaseURL element is an absolute URI, this must trump any previously constructed BaseURL.
     *
     * We do this by treating everything as an absolute URI. If we attempt to call Uri.Replace() with a relative URI then
     * we'll throw and therefore use the Replace(abs, rel) overload to form the new Uri. */

    Brn rootXml;
    (void)XmlParserBasic::TryFind(kMPDTagRoot, iXml, rootXml);

    std::pair<const Brx&, const Brx&> levels[4] = {
        { kMPDTagRoot,           rootXml },
        { kMPDTagPeriod,         iPeriod.ElementXml() },
        { kMPDTagAdaptationSet,  iPeriod.AdaptationSet().ElementXml() },
        { kMPDTagRepresentation, iPeriod.AdaptationSet().Representation().ElementXml() }
    };

    for(TUint i = 0; i < (sizeof(levels)/sizeof(levels[0])); ++i) {
        const Brx& level = levels[i].first;
        const Brx& xml   = levels[i].second;

        if (BaseUrlCollection::TryVisit(xml, level, selector)) {
            // NOTE: Replace() internally calls Clear() first even on error. We must capture the current
            //       URI before we attempt to do any modifications on it
            iUrlBuf.Replace(iBaseUrl.AbsoluteUri());

            try {
                iBaseUrl.Replace(selector.SelectedBaseUrl()); // If absolute, this will work correctly...
            }
            catch (UriError&) {
                iBaseUrl.Replace(iUrlBuf, selector.SelectedBaseUrl()); // Otherwise, this is a relative URL we need to append
            }
        }
    }

    aUrlBuffer.Replace(iBaseUrl.AbsoluteUri());
}



TBool MPDDocument::TrySet(const Brx& aXml)
{
    iExpired = false;

    iXml.Set(aXml);
    iElementXml.Set(Brx::Empty());
    iPeriod.TrySet(Brx::Empty());

    if (iXml.Bytes() == 0) {
        return false;
    }

    iIsStatic = true; // Static is default if not present. Spec link: 5.3.1.2 (Table 3)

    Brn attributeValue;
    if (XmlParserBasic::TryFindAttribute(kMPDTagRootName, "type", iXml, attributeValue)) {
        iIsStatic = attributeValue.Equals(Brn("static"));
    }

    if (XmlParserBasic::TryFindAttribute(kMPDTagRootName, "minimumUpdatePeriod", iXml, attributeValue)) {
        (void)iMinimumUpdatePeriod.TryParse(attributeValue);
    }

    if (!XmlParserBasic::TryFind(kMPDTagRoot, iXml, iElementXml)) {
        return false;
    }

    if (!iPeriod.TrySet(iElementXml)) {
        return false;
    }

    TryDetectContentProtection();

    return true;
}


void MPDDocument::Visit(IBaseUrlVisitor& aVisitor)
{
    Brn rootXml;
    (void)XmlParserBasic::TryFind(kMPDTagRoot, iXml, rootXml);

    std::pair<const Brx&, const Brx&> levels[4] = {
        { kMPDTagRoot,           rootXml },
        { kMPDTagPeriod,         iPeriod.ElementXml() },
        { kMPDTagAdaptationSet,  iPeriod.AdaptationSet().ElementXml() },
        { kMPDTagRepresentation, iPeriod.AdaptationSet().Representation().ElementXml() }
    };

    for(TUint i = 0; i < (sizeof(levels)/sizeof(levels[0])); ++i) {
        const Brx& level = levels[i].first;
        const Brx& xml   = levels[i].second;

        (void)BaseUrlCollection::TryVisit(xml, level, aVisitor);
    }
}

void MPDDocument::TryDetectContentProtection()
{
    // Content protection search is conducted from the bottom up, from representation -> MPD Root
    if (iContentProtection.TrySet(iPeriod.AdaptationSet().Representation().ElementXml())) {
        return;
    }

    if (iContentProtection.TrySet(iPeriod.AdaptationSet().ElementXml())) {
        return;
    }

    if (iContentProtection.TrySet(iPeriod.ElementXml())) {
        return;
    }

    Brn rootXml;
    (void)XmlParserBasic::TryFind(kMPDTagRoot, iXml, rootXml);
    (void)iContentProtection.TrySet(rootXml);
}



// DefaultAdaptationSetVisitor
//     Selects the AdaptionSet with the highest selection priority as specified by the default
//     processing rules defined in the DASH spec
class DefaultAdaptationSetVisitor : public IAdaptationSetVisitor
{
public:
    DefaultAdaptationSetVisitor();

public:
    TInt AdaptationSetIndex() const;
    const Brx& AdaptationSetXml() const;

private: // IAdaptationSetVisitor
    void VisitAdaptationSet(TUint aIndex, TUint aSelectionPriority, TBool aIsAudio, const Brx& aXml) override;

private:
    TInt iSelectedIndex;
    TUint iSelectedPriority;
    Brn iXml;
};


DefaultAdaptationSetVisitor::DefaultAdaptationSetVisitor()
    : iSelectedIndex(-1)
    , iSelectedPriority(0)
    , iXml(Brx::Empty())
{  }


TInt DefaultAdaptationSetVisitor::AdaptationSetIndex() const
{
    return iSelectedIndex;
}

const Brx& DefaultAdaptationSetVisitor::AdaptationSetXml() const
{
    return iXml;
}

void DefaultAdaptationSetVisitor::VisitAdaptationSet(TUint aIndex, TUint aSelectionPriority, TBool aIsAudio, const Brx& aXml)
{
    if (!aIsAudio) {
        return;
    }

    TBool shouldReplace = false;
    shouldReplace = aSelectionPriority > iSelectedPriority; // For selection priorities, the higher the value, the better.

    // In the case where the selection priorities are equal (this is likely if it hasn't been specified in the MPD file
    // they should be listed in ascending order of quality.
    // TODO: In the future, we should maybe do more to verify this, such as looking at the min/max bandwidth params!
    shouldReplace |= aSelectionPriority == iSelectedPriority;

    if (shouldReplace) {
        iSelectedIndex    = static_cast<TInt>(aIndex);
        iSelectedPriority = aSelectionPriority;
        iXml.Set(aXml);
    }
}




// MPDPeriod
const Brx& MPDPeriod::Xml() const
{
    return iXml;
}

const Brx& MPDPeriod::ElementXml() const
{
    return iElementXml;
}

const MPDAdaptationSet& MPDPeriod::AdaptationSet() const
{
    return iAdaptationSet;
}

TBool MPDPeriod::TrySet(const Brx& aXml)
{
    iXml.Set(aXml);
    iElementXml.Set(Brx::Empty());
    iAdaptationSet.TrySet(Brx::Empty());

    if (iXml.Bytes() == 0) {
        return false;
    }

    if (!XmlParserBasic::TryFind(kMPDTagPeriod, aXml, iElementXml)) {
        return false;
    }

    DefaultAdaptationSetVisitor visitor;
    Visit(visitor);

    if (visitor.AdaptationSetIndex() == -1) {
        return false;
    }
    else {
        return TrySelectAdaptationSet(visitor.AdaptationSetIndex());
    }
}

TBool MPDPeriod::TrySelectAdaptationSet(TUint aIndex)
{
    if (iXml.Bytes() == 0) {
        return false;
    }

    TUint index = 0;
    Brn elementXml;
    Brn xmlToParse(iXml);

    while(XmlParserBasic::TryGetElement(kMPDTagAdaptationSet, xmlToParse, xmlToParse, elementXml)) {
        if (index == aIndex) {
            return iAdaptationSet.TrySet(elementXml);
        }
        else {
            index += 1;
        }
    }

    return false;
}

void MPDPeriod::Visit(IAdaptationSetVisitor& aVisitor)
{
    if (iXml.Bytes() == 0) {
        return;
    }

    TUint index = 0;
    Brn elementXml;
    Brn xmlToParse(iXml);
    MPDAdaptationSet adaptationSet;

    while(XmlParserBasic::TryGetElement(kMPDTagAdaptationSet, xmlToParse, xmlToParse, elementXml)) {
        if (adaptationSet.TrySet(elementXml)) {
            aVisitor.VisitAdaptationSet(index, adaptationSet.SelectionPriority(), adaptationSet.IsAudio(), elementXml);
        }
        index += 1;
    }
}



// DefaultRepresentationVisitor
//     Selects the Representation with the highest bandwidth and/or quality ranking
//     as specified by the default processing rules defined in the DASH spec.
class DefaultRepresentationVisitor : public IRepresentationVisitor
{
public:
    DefaultRepresentationVisitor();

public:
    const Brx& RepresentationId() const;
    const Brx& RepresentationXml() const;

private: // IRepresentationVisitor
    void VisitRepresentation(const Brx& aId, TUint aBandwidth, TUint aQualityRanking, const Brx& aXml) override;

private:
    Brn iXml;
    Brn iSelectedId;
    TUint iSelectedBandwidth;
    TUint iQualityRanking;
};


DefaultRepresentationVisitor::DefaultRepresentationVisitor()
    : iXml(Brx::Empty())
    , iSelectedId(Brx::Empty())
    , iSelectedBandwidth(0)
    , iQualityRanking(MPDRepresentation::kDefaultQualityRanking)
{ }


const Brx& DefaultRepresentationVisitor::RepresentationId() const
{
    return iSelectedId;
}

const Brx& DefaultRepresentationVisitor::RepresentationXml() const
{
    return iXml;
}

void DefaultRepresentationVisitor::VisitRepresentation(const Brx& aId, TUint aBandwidth, TUint aQualityRanking, const Brx& aXml)
{
    // NOTE: For quality ranking, lower = better

    const TBool isBetter = iSelectedId.Bytes() == 0                     // First time we've been visited, so always pick
                           || aQualityRanking < iQualityRanking         // The incoming has a better quality ranking than us
                           || (aQualityRanking == iQualityRanking       // The incoming has the same quality ranking, but a better bandwidth
                                && aBandwidth > iSelectedBandwidth);

    if (isBetter) {
        iSelectedId.Set(aId);
        iSelectedBandwidth = aBandwidth;
        iQualityRanking = aQualityRanking;
        iXml.Set(aXml);
    }
}

// MPDAdaptationSet
const Brn MPDAdaptationSet::kAttributeMimeType("mimeType");
const Brn MPDAdaptationSet::kAttributeContentType("contentType");
const Brn MPDAdaptationSet::kAttributeSelectionPriority("selectionPriority");

const TUint MPDAdaptationSet::kDefaultSelectionPriority = 1; // Higher = Better

const MPDRepresentation& MPDAdaptationSet::Representation() const
{
    return iRepresentation;
}

const Brx& MPDAdaptationSet::Xml() const
{
    return iXml;
}

const Brx& MPDAdaptationSet::ElementXml() const
{
    return iElementXml;
}

TBool MPDAdaptationSet::IsAudio() const
{
    return iIsAudio;
}

TUint MPDAdaptationSet::SelectionPriority() const
{
    return iSelectionPriority;
}


TBool MPDAdaptationSet::TrySet(const Brx& aXml)
{
    Parser p;
    Brn key;
    Brn value;

    iIsAudio = false;
    iSelectionPriority = kDefaultSelectionPriority;

    iXml.Set(aXml);
    iElementXml.Set(Brx::Empty());
    iRepresentation.TrySet(Brx::Empty());

    if (iXml.Bytes() == 0) {
        return false;
    }

    if (!XmlParserBasic::TryFind(kMPDTagAdaptationSet, iXml, iElementXml)) {
        return false;
    }

    if (!TryCreateAttributeParser(iXml, kMPDTagAdaptationSet, p)) {
        return false;
    }

    while (TryReadAttribute(p, key, value)) {
        if (key.Equals(kAttributeMimeType) || key.Equals(kAttributeContentType)) {
            iIsAudio = value.BeginsWith(Brn("audio"));
        }
        else if (key.Equals(kAttributeSelectionPriority)) {
            TRY_ASCII_UINT(value, iSelectionPriority);
        }
    }

    DefaultRepresentationVisitor visitor;
    Visit(visitor);

    if (visitor.RepresentationId().Bytes() == 0) {
        return false;
    }
    else {
        return TrySelectRepresentation(visitor.RepresentationId());
    }
}

TBool MPDAdaptationSet::TrySelectRepresentation(const Brx& aRepresentationId)
{
    Brn id;
    Brn elementXml;
    Brn xmlToParse(iXml);

    while(XmlParserBasic::TryGetElement(kMPDTagRepresentation, xmlToParse, xmlToParse, elementXml)) {
        if (XmlParserBasic::TryFindAttribute(kMPDTagRepresentation, MPDRepresentation::kAttributeId, elementXml, id)) {
            if (id == aRepresentationId) {
                return iRepresentation.TrySet(elementXml);
            }
        }
    }

    return false;
}

void MPDAdaptationSet::Visit(IRepresentationVisitor& aVisitor)
{
    if (iXml.Bytes() == 0) {
        return;
    }

    Brn elementXml;
    Brn xmlToParse(iXml);
    MPDRepresentation representation;

    while(XmlParserBasic::TryGetElement(kMPDTagRepresentation, xmlToParse, xmlToParse, elementXml)) {
        if (representation.TrySet(elementXml)) {
            aVisitor.VisitRepresentation(representation.Id(), representation.Bandwidth(), representation.QualityRanking(), elementXml);
        }
    }
}

// MPDRepresentation
const Brn MPDRepresentation::kAttributeId("id");
const Brn MPDRepresentation::kAttributeBandwidth("bandwidth");
const Brn MPDRepresentation::kAttributeQualityRanking("qualityRanking");

const TUint MPDRepresentation::kDefaultBandwidth = 0;
const TUint MPDRepresentation::kDefaultQualityRanking = UINT_MAX; // Lower = Better

const Brx& MPDRepresentation::Id() const
{
    return iId;
}

TUint MPDRepresentation::Bandwidth() const
{
    return iBandwidth;
}

TUint MPDRepresentation::QualityRanking() const
{
    return iQualityRanking;
}

const Brx& MPDRepresentation::Xml() const
{
    return iXml;
}

const Brx& MPDRepresentation::ElementXml() const
{
    return iElementXml;
}


TBool MPDRepresentation::TrySet(const Brx& aXml)
{
    iId.Set(Brx::Empty());
    iElementXml.Set(Brx::Empty());

    iBandwidth = kDefaultBandwidth;
    iQualityRanking = kDefaultQualityRanking;

    iXml.Set(aXml);

    if (iXml.Bytes() == 0) {
        return false;
    }

    if (!XmlParserBasic::TryFind(kMPDTagRepresentation, iXml, iElementXml)) {
        return false;
    }

    // Attempt to read desired attributes...
    Brn key;
    Brn value;
    Parser p;

    if (!TryCreateAttributeParser(iXml, kMPDTagRepresentation, p)) {
        return false;
    }

    while (TryReadAttribute(p, key, value)) {
        if (key.Equals(kAttributeId)) {
            iId.Set(value);
        }
        else if (key.Equals(kAttributeBandwidth)) {
            TRY_ASCII_UINT(value, iBandwidth);
        }
        else if (key.Equals(kAttributeQualityRanking)) {
            TRY_ASCII_UINT(value, iQualityRanking);
        }
    }

    // Both of these are manditory in the spec.
    // Spec Link: 5.3.5.2 (Table 9)
    return iId.Bytes() > 0 && iBandwidth > 0;
}


// MPDSegmentStream
static const TUint kBoundaryThreshold = 128;

MPDSegmentStream::MPDSegmentStream(IUnixTimestamp& aUnixTimestamp)
    : iTimestamp(aUnixTimestamp)
    , iCurrentDocument(nullptr)
    , iSegmentType(ESegmentType::Unknown)
    , iSeek(false)
    , iSeekPosition(0)
{ }

TBool MPDSegmentStream::TryGetNextSegment(MPDSegment& aSegment)
{
    if (iSegmentXml.Bytes() == 0 || iSegmentType == ESegmentType::Unknown || !iCurrentDocument) {
        return false;
    }

    // TODO: Need to check that our document hasn't changed beneath us and we're trying to get segments out the wrong thing??

    if (iCurrentDocument->HasExpired()) {
        THROW(SegmentStreamExpired);
    }

    if (iNeedsInitialisationSegment) {
        iNeedsInitialisationSegment = false;

        if (TryGetInitialisationSegment(aSegment)) {
            return true;
        }

        // Otherwise, we'll fall through and get the next segment
    }

    if (iSeek) {
        // NOTE: This code currently assumes we are streaming with a 'List' type
        if (iSegmentType != ESegmentType::List) {
            return false;
        }

        // Reset us back to the initial segment to allow us to find the containing segment.
        iSegmentNumber = 0;

        TBool success = false;

        while(true) {
            if (!TryGetMediaSegment(aSegment)) {
                break;
            }

            iSegmentNumber += 1;

            const TBool hasRangeStart      = aSegment.iRangeStart != -1;
            const TBool hasRangeEnd        = aSegment.iRangeEnd   != -1;
            const TBool hasRanges          = hasRangeStart && hasRangeEnd;
            const TBool isWithinLowerBound = hasRangeStart && (TUint64)aSegment.iRangeStart <= iSeekPosition;
            const TBool isWithinUpperBound = hasRangeEnd   && iSeekPosition <= (TUint64)aSegment.iRangeEnd;

            const TBool segmentContainsSeekPosition =     !hasRanges
                                                      || (!hasRangeStart     && isWithinUpperBound)
                                                      || (!hasRangeEnd       && isWithinLowerBound)
                                                      || (isWithinLowerBound && isWithinUpperBound);

            if (segmentContainsSeekPosition) {
                success = true;

                aSegment.iRangeStart = (TInt64)iSeekPosition;

                // If we happen to be right at the very end of a segment, we should start to request the next
                // part right away to ensure we have enough audio to keep playing
                const TUint64 diff = aSegment.iRangeEnd - aSegment.iRangeStart;
                if (diff <= kBoundaryThreshold) {
                    success = TryGetMediaSegment(aSegment);
                    if (success) {
                        aSegment.iRangeStart = (TUint)iSeekPosition;
                    }
                }

                break;
            }
        }

        iSeek         = false;
        iSeekPosition = 0;

        if (!success) {
            LOG_ERROR(kMedia, "MPDSegmentStream::TryGetNextSegment - Failed to seek to desired position\n");
            return false;
        }
        else {
            return true;
        }
    }
    else {
        const TBool result = TryGetMediaSegment(aSegment);
        iSegmentNumber += 1;
        return result;
    }
}

TBool MPDSegmentStream::TrySeekByOffset(TUint64 aOffset)
{
    const TBool hasXml             = iSegmentXml.Bytes() > 0;
    const TBool hasDocument        = iCurrentDocument != nullptr;
    if (!hasXml || !hasDocument) {
        LOG_ERROR(kMedia, "MPDSegmentStream::TrySeekByOffset - Unable to seek as no document or xml present\n");
        return false;
    }

    const TBool isSeekableByOffset = iSegmentType == ESegmentType::List;
    if (!isSeekableByOffset) {
        LOG_ERROR(kMedia, "MPDSegmentStream::TrySeekByOffset - Segment not of type 'List' so doesn't support seeking by offset.\n");
        return false;
    }

    iSeek         = true;
    iSeekPosition = aOffset;

    return true;
}


TBool MPDSegmentStream::TryGetInitialisationSegment(MPDSegment& aSegment)
{
    switch(iSegmentType) {
        case ESegmentType::Base: {
            // NOTE: This appears to be VIDEO content only ??
            // See answer: https://stackoverflow.com/questions/32327137/read-contents-of-initialization-range-and-segmentbase-indexrange-in-a-dash-strea
            //  which links to here suggesting it's video only?? https://gpac.io/2012/02/01/dash-support/
            THROW(SegmentStreamUnsupported);
        }
        case ESegmentType::List: {
            Brn attributeValue;
            Brn elementXml;
            Brn xmlToParse(iSegmentXml);

            if (!XmlParserBasic::TryGetElement(kMPDTagSegmentInitalisationName, xmlToParse, xmlToParse, elementXml)) {
                return false;
            }

            // If there is a 'media' attribute, then this is an ABSOLUTE URL pointing to the data for this given segment.
            if (XmlParserBasic::TryFindAttribute(kMPDTagSegmentInitalisationName, "media", elementXml, attributeValue)) {
                aSegment.iUrlBuffer.ReplaceThrow(attributeValue);
            }
            else {
                // Otherwise, we need to walk the chain to find a suitable base URL for the segment...
                iCurrentDocument->GetBaseUrl(aSegment.iUrlBuffer);
            }

            if (XmlParserBasic::TryFindAttribute(kMPDTagSegmentInitalisationName, "range", elementXml, attributeValue)) {
                Parser p(attributeValue);
                aSegment.iRangeStart = Ascii::Int(p.Next('-'));
                aSegment.iRangeEnd   = Ascii::Int(p.Remaining());
            }

            return true;
        }
        case ESegmentType::Template: {
            // TODO: Check that we are of the crrect type / have no timeline present??

            // TODO: Is it worth keeping these as free functions or having the code inline here like we do?
            //       Perhaps just having member functions are reasonable. Otherwise we might be forces to split
            //       this out into an interface + a bunch of other classed used to get the segments (which _could_ work).

            // Static documents using segment templates contain a 'SegmentTimeline' child element which outlines each of the segments.
            if (iCurrentDocument->IsStatic()) {
                LOG_ERROR(kMedia, "MPD::HandleInitialisationSegmentTemplate - 'Static' type manifests with segment templates are not supported.\n");
                return false;
            }

            SegmentTemplate segment(iSegmentXml);
            if (segment.Initialization().Bytes() == 0) {
                LOG_ERROR(kMedia, "MPD::HandleInitialisationSegmentTemplate - No 'initialisation' element found.\n");
                return false;
            }

            iCurrentDocument->GetBaseUrl(aSegment.iUrlBuffer);

            const MPDRepresentation& representation = iCurrentDocument->Period().AdaptationSet().Representation();
            SegmentTemplateParams templateParams {
                representation.Id(),          // RepresentationId
                representation.Bandwidth(),   // Bandwidth
                0,                             // Time, Not currently supported.
                0,                             // Number, for initialisation segments there should be no number value required
                0,                             // SubNumber, for initialisation segments there should be no subnumber value required
            };

            // FIXME: Do we need to check the ENTIRE Url including all the previous BaseURl segments, or can we just assume that it's only
            //        the portion of the URL present in the Template element that needs formatted??
            if (!SegmentTemplate::TryFormatTemplateUrl(aSegment.iUrlBuffer, segment.Initialization(), templateParams)) {
                LOG_ERROR(kMedia, "MPD::HandleInitialisationSegmentTemplate - Failed to populate templated URL.\n");
                return false;
            }
            return true;
        }
        default: {
            return false; // Not reached
        }
    }
}

TBool MPDSegmentStream::TryGetMediaSegment(MPDSegment& aSegment)
{
    switch(iSegmentType) {
        case ESegmentType::Base: {
            // NOTE: This appears to be VIDEO content only ??
            // See answer: https://stackoverflow.com/questions/32327137/read-contents-of-initialization-range-and-segmentbase-indexrange-in-a-dash-strea
            //  which links to here suggesting it's video only?? https://gpac.io/2012/02/01/dash-support/
            THROW(SegmentStreamUnsupported);
        }
        case ESegmentType::List: {
            Brn attributeValue;
            Brn elementXml;
            Brn xmlToParse(iSegmentXml);

            TUint index = 0;
            while(index < iSegmentNumber && XmlParserBasic::TryGetElement(kMPDTagSegmentUrlName, xmlToParse, xmlToParse, elementXml)) {
                index += 1;
            }

            // No segment found for the required index. Likely we've gone off the end of the list and so reached the end of the available segments!
            if (!XmlParserBasic::TryGetElement(kMPDTagSegmentUrlName, xmlToParse, elementXml)) {
                return false;
            }

            // If there is a 'media' attribute, then this is an ABSOLUTE URL pointing to the data for this given segment.
            if (XmlParserBasic::TryFindAttribute(kMPDTagSegmentUrlName, "media", elementXml, attributeValue)) {
                aSegment.iUrlBuffer.ReplaceThrow(attributeValue);
            }
            else {
                // Otherwise, we need to walk the chain to find a suitable base URL for the segment...
                iCurrentDocument->GetBaseUrl(aSegment.iUrlBuffer);
            }

            if (XmlParserBasic::TryFindAttribute(kMPDTagSegmentUrlName, "mediaRange", elementXml, attributeValue)) {
                Parser p(attributeValue);
                aSegment.iRangeStart = Ascii::Int(p.Next('-'));
                aSegment.iRangeEnd   = Ascii::Int(p.Remaining());
            }

            return true;
        }
        case ESegmentType::Template: {
            // Static documents using segment templates contain a 'SegmentTimeline' child element which outlines each of the segments.
            if (iCurrentDocument->IsStatic()) {
                LOG_ERROR(kMedia, "MPD::HandleSegmentTemplate - 'Static' type manifests with segment templates are not supported.\n");
                return false;
            }

            SegmentTemplate segment(iSegmentXml);
            if (segment.Media().Bytes() == 0) {
                LOG_ERROR(kMedia, "MPD::HandleSegmentTemplate - No 'media' element found.\n");
                return false;
            }

            iCurrentDocument->GetBaseUrl(aSegment.iUrlBuffer);

            const MPDRepresentation& representation = iCurrentDocument->Period().AdaptationSet().Representation();
            SegmentTemplateParams templateParams {
                representation.Id(),            // RepresentationId
                representation.Bandwidth(),     // Bandwidth
                0,                              // Time, Not currently supported.
                iSegmentNumber,                 // Number
                0,                              // SubNumber, Not currently supported.
            };

            // FIXME: I guess we need to work backwards on the already resolved Base URL to ensure that there is no template params left there???
            if (!SegmentTemplate::TryFormatTemplateUrl(aSegment.iUrlBuffer, segment.Media(), templateParams)) {
                LOG_ERROR(kMedia, "MPD::HandleSegmentTemplate - Failed to populate templated URL.\n");
                return false;
            }
            return true;
        }
        default: {
            return false;
        }
    }
}




TBool MPDSegmentStream::TrySet(MPDDocument& aDocument)
{
    iCurrentDocument            = nullptr;
    iSegmentType                = ESegmentType::Unknown;
    iSegmentNumber              = 0;
    iNeedsInitialisationSegment = true;

    iSegmentXml.Set(Brx::Empty());

    if (aDocument.HasExpired()) {
        LOG_ERROR(kMedia, "MPDSegmentStream::TryStream - Passed an expired manifest!\n");
        return false;
    }

    LOG_INFO(kMedia, "MPDSegmentStream::TryStream - Provided document type: %ss\n", aDocument.IsStatic() ? "Static" : "Dynamic");

    // TODO: Need to check we only have a single period and exit otherwise as this
    //       is something we don't currently support.

    // TODO: Do we need to ensure that we set the starting number, or is this done on the initial setup thing??

    iCurrentDocument = &aDocument;

    // Now we need to decide what the type of stream we require from the given manifest. This'll be one of the 3 supported types:
    // Types marked with a (*) are not currently supported.
    //  -     List (each segment is provided in a list, optionally as ranges within a single URL)
    //  -     Template [No Timeline] (Segments are defined as tenmplated URL where you must substitute parameter values
    //  - (*) Template [   Timeline] (Segments are defined on a fixed timeline)
    //  - (*) Base (all information for segments is contained within a single URL)
    std::pair<const Brx&, const Brx&> searchList[3] {
        { kMPDTagRepresentation, iCurrentDocument->Period().AdaptationSet().Representation().Xml() },
        { kMPDTagAdaptationSet,  iCurrentDocument->Period().AdaptationSet().Xml() },
        { kMPDTagPeriod,         iCurrentDocument->Period().Xml() }
    };

    for (TUint i = 0; i < (sizeof(searchList)/sizeof(searchList[0])); ++i) {
        if (XmlParserBasic::TryGetElement(kMPDTagSegmentBase, searchList[i].second, iSegmentXml)) {
            iSegmentType = ESegmentType::Base;
            break;
        }

        if (XmlParserBasic::TryGetElement(kMPDTagSegmentList, searchList[i].second, iSegmentXml)) {
            iSegmentType = ESegmentType::List;
            break;
        }

        if (XmlParserBasic::TryGetElement(kMPDTagSegmentTemplate, searchList[i].second, iSegmentXml)) {
            iSegmentType = ESegmentType::Template;
            // TODO: Work out if we're a segment template with timeline or not??
            break;
        }
    }

    if (iSegmentXml.Bytes() == 0 || iSegmentType == ESegmentType::Unknown) {
        Log::Print("!! MPD: Unknown segment type found.\n");
        return false;
    }


    return TrySetInitialSegmentNumber();
}


TBool MPDSegmentStream::TrySetInitialSegmentNumber()
{
    // All Static documents should start from the first segment, assuming we are starting from the beginning!
    // TODO: Check if all segment types only specify a "StartNumber" when they are dynamic...
    if (iCurrentDocument->IsStatic()) {
        iSegmentNumber = 0;
        return true;
    }


    // Dynamic documents are a little tricker. Their starting segment number if based off a number of factors
    // provided by the MPD Document.
    // Spec Link: 5.3.9.5.3

    // NOTE: We currently restrict this to 'SegmentTemplate' stream types as I don't think it makes sense for the other
    //       types to by dynamic. We might need to revisit this in the future
    if (iSegmentType != ESegmentType::Template) {
        return false;
    }

    // TODO: Perhaps this should be placed on the Document itself???
    //       This could also implement the code to handle a start time.
    Brn attributeValue;
    if (!XmlParserBasic::TryFindAttribute(kMPDTagRoot, MPDDocument::kAttributeAvailabilityStartTime, iCurrentDocument->Xml(), attributeValue)) {
        // required in dynamic documents!!
        return false;
    }

    PointInTime pit;
    try {
        pit = TryParseMPDTime(attributeValue);
    }
    catch (AsciiError&) {
        Log::Print("!!! Failed to parse MPD time. !!!\n");
        return false;
    }

    const TInt64 availabilityStartTime = pit.ConvertToUnixTimestamp();

    SegmentTemplate segment(iSegmentXml);

    // Sepc Link: Annex A (Specificially A.3.2 onwards)
    TUint timeNow             = iTimestamp.Now();
    TUint timeDifference      = static_cast<TUint>(timeNow - availabilityStartTime);
    double segmentScaleFactor = ((double)segment.Duration()) / ((double)segment.Timescale());
    iSegmentNumber            = static_cast<TUint>(floor((segment.StartNumber() + timeDifference) / segmentScaleFactor));

    return true;
}



// ContentMPD
class ContentMPD : public Media::ContentProcessor
{
public:
    ContentMPD(ITimerFactory& aTimerFactory);
    ~ContentMPD();

public:
    MPDDocument& MPD();

    void Initialise(IProtocolManager* aProtocolManager);

private: // ContentProcessor
    TBool Recognise(const Brx& aUri, const Brx& aMimeType, const Brx& aData) override;
    ProtocolStreamResult Stream(IReader& aReader, TUint64 aTotalBytes) override;
    void Reset() override;

private:
    void OnMPDDocumentExpiryTimerFired();

private:
    ITimer* iExpiryTimer;
    WriterBwh iBuffer;
    MPDDocument iDocument;
    IProtocolManager* iProtocolManager;   // Not owned
    TUint iDocumentId;
};



ContentMPD::ContentMPD(ITimerFactory& aTimerFactory)
    : iBuffer(1024)
    , iProtocolManager(nullptr)
    , iDocumentId(0)
{
    iExpiryTimer = aTimerFactory.CreateTimer(MakeFunctor(*this, &ContentMPD::OnMPDDocumentExpiryTimerFired), "ContentMPD-Expiry");
}

ContentMPD::~ContentMPD()
{
    iExpiryTimer->Cancel();
    delete iExpiryTimer;
}


MPDDocument& ContentMPD::MPD()
{
    return iDocument;
}

void ContentMPD::Initialise(IProtocolManager* aProtocolManager)
{
    iProtocolManager = aProtocolManager;
}

TBool ContentMPD::Recognise(const Brx& aUri, const Brx& aMimeType, const Brx& aData)
{
    (void)aUri;
    (void)aData;

    // Some servers provide a straight up content type which is nice of them.
    if (aMimeType == kMimeType) {
        return true;
    }

    // Some servers provide multiple header values to define the encoding + content type.
    Parser p(aMimeType);
    Brn val = p.Next(';');

    while(val.Bytes() > 0) {
        if (val == kMimeType) {
            return true;
        }

        val.Set(p.Next(';'));
    }

    return p.Remaining() == kMimeType;
}

ProtocolStreamResult ContentMPD::Stream(IReader& aReader, TUint64 aTotalBytes)
{
    // MPD requires us to have the entire document in memory for us to parse and extract the bits and pieces we need out from it.
    try {
        while(iBuffer.Buffer().Bytes() < aTotalBytes) {
            iBuffer.Write(aReader.Read(1024));
        }
    }
    catch (ReaderError&) {
        LOG_ERROR(kMedia, "ContentMPD::Stream - ReaderError when downloading MPD.\n");
        return ProtocolStreamResult::EProtocolStreamErrorUnrecoverable;
    }

    // Next, we need to check that the data returned is infact an actual MPD document and something that we can parse!
    if (!iDocument.TrySet(iBuffer.Buffer())) {
        LOG_ERROR(kMedia, "ContentMPD::Stream - Failed to parse MPD document.\n");
        return ProtocolStreamResult::EProtocolStreamErrorUnrecoverable;
    }

    // If we have a dynamic manifest, then we must set a timer to expire if after the specific time.
    if (!iDocument.IsStatic()) {
        LOG(kMedia, "ContentMPD::Stream - Manifest type: Dynamic\n");

        TUint expirySeconds = static_cast<TUint>(iDocument.MinimumUpdatePeriod());
        if (expirySeconds == 0) {
            LOG(kMedia, "ContentMPD::Stream - WARN: Manifest did not specify a minimum update period!\n");
        }
        else {
            LOG(kMedia, "ContentMPD::Stream - Minimum Update Period: %lus\n", expirySeconds);
            iExpiryTimer->FireIn(expirySeconds * 1000);
        }
    }

    // If the Document is content protected, we probably should grab the details here
    // and seed the DRMProvider with all the required information
    if (iDocument.IsContentProtected()) {
        LOG(kMedia, "ContentMPD::Stream - MPD reports DRM protection\n");

        const ContentProtection& cp = iDocument.ContentProtectionDetails();
        if (!cp.IsMPEG4Protection()) {
            LOG_ERROR(kMedia, "ContentMPD::Stream  - Unknown DRM scheme: %.*s. Content not playable.\n", PBUF(cp.iSchemeIdUri));
            return ProtocolStreamResult::EProtocolStreamErrorUnrecoverable;
        }

        LOG(kMedia, "ContentMPD::Stream - DRM Type: MP4 (Kind:'%.*s')\n", PBUF(cp.iValue));

        ASSERT(iProtocolManager);
        const std::vector<IDashDRMProvider*>& drmProviders = iProtocolManager->GetDashDRMProviders();
        IDashDRMProvider* activeDRMProvider = nullptr;

        for(TUint i = 0; i < drmProviders.size(); i += 1) {
            if (drmProviders[i]->TryRecognise(cp)) {
                activeDRMProvider = drmProviders[i];
                break;
            }
        }

        if (activeDRMProvider == nullptr) {
            LOG_ERROR(kMedia, "ContentMPD::Stream - MPD is content protected, but were unable to find a DRM provider that could handle it.\n");
            return ProtocolStreamResult::EProtocolStreamErrorUnrecoverable;
        }
    }
    else {
        LOG(kMedia, "ContentMPD::Stream - MPD contains no DRM protection\n");
    }


    iDocumentId += 1;

    Bws<32> streamUrl;
    streamUrl.Append("dash://");
    Ascii::AppendDec(streamUrl, iDocumentId);

    return iProtocolSet->Stream(streamUrl);
}

void ContentMPD::Reset()
{
    ContentProcessor::Reset();

    iExpiryTimer->Cancel();
    (void)iDocument.TrySet(Brx::Empty());
    iBuffer.Reset();
}

void ContentMPD::OnMPDDocumentExpiryTimerFired()
{
    LOG(kMedia, "ContentMPD - Document Expiry Timer Fired!\n");
    iDocument.SetExpired();
}




// ProtocolDash
namespace OpenHome {
namespace Media {

class ProtocolDash : public ProtocolNetworkSsl
                   , public IReader
{
public:
    ProtocolDash(Environment& aEnv, SslContext& aSsl, Av::IMediaPlayer& aMediaPlayer);
    ~ProtocolDash();

private: // Protocol
    void Initialise(MsgFactory& aMsgFactory, IPipelineElementDownstream& aDownstream) override;
    ProtocolStreamResult Stream(const Brx& aUri) override;
    ProtocolGetResult Get(IWriter& aWriter, const Brx& aUri, TUint64 aOffset, TUint aBytes) override;
    void Deactivated() override;
    void Interrupt(TBool aInterrupt) override;

private: // IStreamHandler
    Media::EStreamPlay OkToPlay(TUint aStreamId) override;
    TUint TrySeek(TUint aStreamId, TUint64 aOffset) override;
    TUint TryStop(TUint aStreamId) override;

private: // IReader
    Brn Read(TUint aBytes) override;
    void ReadFlush() override;
    void ReadInterrupt() override;

private:
    ProtocolStreamResult StreamSegment(MPDSegment& aSegment);

private:
    ContentMPD* iContentProcessor; // NOT OWNED
    MPDSegmentStream iSegmentStream;
    ISupply* iSupply;
    Bwh iSegmentUrlBuffer;
    Uri iUri;
    Uri iUriNext;
    TBool iStarted;
    TBool iStopped;
    TUint iNextFlushId;
    TUint iCurrentStreamId;

    // Required HTTP stuff...
    WriterHttpRequest iWriterRequest;
    ReaderUntilS<2048> iReaderUntil;
    ReaderHttpResponse iReaderResponse;
    ReaderHttpChunked iDechunker;
    HttpHeaderConnection iHeaderConnection;
    HttpHeaderContentType iHeaderContentType;
    HttpHeaderContentLength iHeaderContentLength;
    HttpHeaderTransferEncoding iHeaderTransferEncoding;
};

}; // namespace Media
}; // namespace OpenHome




// ProtocolFactory
Protocol* ProtocolFactory::NewDash(Environment& aEnv, SslContext& aSsl, Av::IMediaPlayer& aMediaPlayer)
{ // static
    return new ProtocolDash(aEnv, aSsl, aMediaPlayer);
}

// ProtocolDash
ProtocolDash::ProtocolDash(Environment& aEnv, SslContext& aSsl, Av::IMediaPlayer& aMediaPlayer)
    : ProtocolNetworkSsl(aEnv, aSsl)
    , iSegmentStream(aMediaPlayer.UnixTimestamp())
    , iSupply(nullptr)
    , iSegmentUrlBuffer(1024)
    , iWriterRequest(iSocket)
    , iReaderUntil(iReaderBuf)
    , iReaderResponse(aEnv, iReaderUntil)
    , iDechunker(iReaderUntil)
{
    TimerFactory timerFactory(aEnv);

    iContentProcessor = new ContentMPD(timerFactory);

    iReaderResponse.AddHeader(iHeaderContentType);
    iReaderResponse.AddHeader(iHeaderContentLength);
    iReaderResponse.AddHeader(iHeaderTransferEncoding);
    iReaderResponse.AddHeader(iHeaderConnection);

    // TODO: Perhaps we should expose this as a property we get rather than register internally?
    //       Having said that, this Protocol is a bit useless without the content processor working
    // FIXME: Could this class be a protocol AND a content processor??
    aMediaPlayer.Pipeline()
                .Add(iContentProcessor); // NOTE: Ownership transfered
}

ProtocolDash::~ProtocolDash()
{
    delete iSupply;
}

void ProtocolDash::Initialise(MsgFactory& aMsgFactory, IPipelineElementDownstream& aDownstream)
{
    iSupply = new Supply(aMsgFactory, aDownstream);
    iContentProcessor->Initialise(iProtocolManager);
}

void ProtocolDash::Interrupt(TBool aInterrupt)
{
    iLock.Wait();
    if (iActive) {
        LOG(kMedia, "ProtocolDash::Interrupt(%u)\n", aInterrupt);
        if (aInterrupt) {
            iStopped = true;
        }
        iSocket.Interrupt(aInterrupt);
    }
    iLock.Signal();
}

ProtocolStreamResult ProtocolDash::Stream(const Brx& aUri)
{
    iUri.Replace(aUri);
    if (iUri.Scheme() != Brn("dash")) {
        return EProtocolErrorNotSupported;
    }

    iStarted         = false;
    iStopped         = false;
    iNextFlushId     = MsgFlush::kIdInvalid;
    iCurrentStreamId = IPipelineIdProvider::kStreamIdInvalid;

    // Check that the Uri matches that of the content processor (? Not sure if this is actually required cause I think
    // the way the pipeline works, this would be the same?)
    if (!iContentProcessor) {
        LOG_ERROR(kMedia, "ProtocolDash::Stream - No content processor!\n");
        return ProtocolStreamResult::EProtocolStreamErrorUnrecoverable;
    }

    // NOTE: This needs to be here to ensure that we have a consistent messaging for the entire MPD file
    iCurrentStreamId = iIdProvider->NextStreamId();
    iSupply->OutputStream(iUri.AbsoluteUri(), 0, 0, false, true, Multiroom::Allowed, *this, iCurrentStreamId);

    MPDSegment segment(iSegmentUrlBuffer);
    MPDDocument& document = iContentProcessor->MPD();

    if (!iSegmentStream.TrySet(document)) {
        LOG_ERROR(kMedia, "ProtocolDash::Stream - Failed to construct segment stream around provided MPD document\n");
        return ProtocolStreamResult::EProtocolStreamErrorUnrecoverable;
    }

    LOG(kMedia, "ProtocolDash::Stream - Manifest Type: '%s'\n", document.IsStatic() ? "Static" : "Dynamic");

    ProtocolStreamResult streamResult = EProtocolStreamSuccess;

    while (!iStopped && streamResult == EProtocolStreamSuccess) {
        try {
            if (!iSegmentStream.TryGetNextSegment(segment)) {
                break;
            }
        }
        catch (SegmentStreamError&) {
            LOG_ERROR(kMedia, "ProtocolDash::Stream - SegmentStream error when fetching next segment\n");
            streamResult = ProtocolStreamResult::EProtocolStreamErrorUnrecoverable;
            continue;
        }
        catch (SegmentStreamExpired&) {
            LOG(kMedia, "ProtocolDash::Stream - SegmentStream indicated that our MPD has expired.\n");
            streamResult = ProtocolStreamResult::EProtocolStreamErrorRecoverable;
            continue;
        }
        catch (SegmentStreamUnsupported&) {
            LOG_ERROR(kMedia, "ProtocolDash::Stream - Given MPD document provides segments in an unsupported format.\n");
            streamResult =  ProtocolStreamResult::EProtocolStreamErrorUnrecoverable;
            continue;
        }

        // Segment present - lets stream!
        LOG(kMedia, "ProtocolDash::Stream - Next segment...\n");

        const TBool isRangeRequest = segment.iRangeEnd != -1;
        if (isRangeRequest) {
            LOG_TRACE(kMedia, "ProtocolDash::Stream - Segment Url: %.*s (%u - %u)", PBUF(segment.iUrlBuffer), segment.iRangeStart, segment.iRangeEnd);
        }
        else {
            LOG_TRACE(kMedia, "ProtocolDash::Steam - Segment Url: %.*s\n", PBUF(segment.iUrlBuffer));
        }

        streamResult = StreamSegment(segment);
    }

    // End of stream. Also check for the stopped condition. This trumps all
    TBool wasStopped = false;

    iLock.Wait();
    if (iStopped) {
        wasStopped = true;

        if (iNextFlushId != MsgFlush::kIdInvalid) {
            iSupply->OutputFlush(iNextFlushId);
        }
    }

    iCurrentStreamId = IPipelineIdProvider::kStreamIdInvalid;
    iLock.Signal();

    if (wasStopped) {
        return ProtocolStreamResult::EProtocolStreamStopped;
    }

    // Expired, so need to fetch a new one
    if (document.HasExpired()) {
        return ProtocolStreamResult::EProtocolStreamErrorRecoverable;
    }

    return streamResult;
}

ProtocolGetResult ProtocolDash::Get(IWriter& aWriter, const Brx& aUri, TUint64 aOffset, TUint aBytes)
{
    (void)aWriter;
    (void)aUri;
    (void)aOffset;
    (void)aBytes;
    return ProtocolGetResult::EProtocolGetErrorNotSupported;
}

void ProtocolDash::Deactivated()
{
    iProtocolManager->GetAudioProcessor()->Reset();
    iDechunker.ReadFlush();
    Close();
}

EStreamPlay ProtocolDash::OkToPlay(TUint aStreamId)
{
    return iIdProvider->OkToPlay(aStreamId);
}

TUint ProtocolDash::TrySeek(TUint aStreamId, TUint64 aOffset)
{
    (void)aStreamId;
    (void)aOffset;
    return MsgFlush::kIdInvalid;
}

TUint ProtocolDash::TryStop(TUint aStreamId)
{
    iLock.Wait();

    const TBool stop = iCurrentStreamId == aStreamId && aStreamId != IPipelineIdProvider::kStreamIdInvalid;
    if (stop) {
        if (iNextFlushId == MsgFlush::kIdInvalid) {
            /* If a valid flushId is set then We've previously promised to send a Flush but haven't
               got round to it yet.  Re-use the same id for any other requests that come in before
               our main thread gets a chance to issue a Flush */
            iNextFlushId = iFlushIdProvider->NextFlushId();
        }
        iStopped = true;
        iSocket.Interrupt(true);
    }

    iLock.Signal();

    return stop ? iNextFlushId
                : MsgFlush::kIdInvalid;
}

Brn ProtocolDash::Read(TUint aBytes)
{
    return iDechunker.Read(aBytes);
}

void ProtocolDash::ReadFlush()
{
    iDechunker.ReadFlush();
}

void ProtocolDash::ReadInterrupt()
{
    iDechunker.ReadInterrupt();
}

ProtocolStreamResult ProtocolDash::StreamSegment(MPDSegment& aSegment)
{
    iDechunker.ReadFlush();

    iUriNext.Replace(aSegment.iUrlBuffer);

    const TBool isEndpointSame    = iUri.Host() == iUriNext.Host();
    const TBool shouldCloseSocket = iHeaderConnection.Close() || !isEndpointSame;
    const TBool requiresConnect   = shouldCloseSocket;

    // Configure us to use the URL for the segment!
    iUri.Replace(aSegment.iUrlBuffer);


    if (shouldCloseSocket) {
        Close();
        iSocket.SetSecure(false);
    }

    // Decide what port to use
    TUint port = 80; // Default to HTTP
    if (iUri.Port() != -1) {
        port = iUri.Port();
    }
    else {
        if (iUri.Scheme() == Brn("https")) {
            port = 443;
        }
    }

    if (requiresConnect) {
        if (port == 443) {
            iSocket.SetSecure(true);
        }

        // Connect....
        if (!Connect(iUri, port)) {
            LOG_ERROR(kMedia, "ProtocolDash::StreamSegment - Connection failure.\n");
            return ProtocolStreamResult::EProtocolStreamErrorUnrecoverable;
        }
    }

    // Send off the request...
    try {
        LOG(kMedia, "ProtocolDash::StreamSegment - Send request\n");
        iWriterRequest.WriteMethod(Http::kMethodGet, iUri.PathAndQuery(), Http::eHttp11);
        Http::WriteHeaderHostAndPort(iWriterRequest, iUri.Host(), port);

        Http::WriteHeaderUserAgent(iWriterRequest, iEnv);

        if (aSegment.iRangeStart != -1) {
            if (aSegment.iRangeEnd != -1) {
                Http::WriteHeaderRange(iWriterRequest, aSegment.iRangeStart, aSegment.iRangeEnd);
            }
            else {
                Http::WriteHeaderRangeFirstOnly(iWriterRequest, aSegment.iRangeStart);
            }
        }

        iWriterRequest.WriteFlush();
    }
    catch(WriterError&) {
        LOG_ERROR(kMedia, "ProtocolDash::StreamSegment - Failed to write segment request\n");
        return ProtocolStreamResult::EProtocolStreamErrorUnrecoverable;
    }


    // Wait for & read the result....
    try {
        LOG(kMedia, "ProtocolDash::StreamSegment - Read response\n");
        iReaderResponse.Read();
    }
    catch (AssertionFailed&) {
        throw;
    }
    catch (Exception& ex) {
        LOG_ERROR(kMedia, "ProtocolDash::StreamSegment - Failed to read response(%s)\n", ex.Message());
        return ProtocolStreamResult::EProtocolStreamErrorUnrecoverable;
    }

    const TUint responseCode = iReaderResponse.Status().Code();
    LOG(kMedia, "ProtocolDash::StreamSegment - Read response code: %u\n", responseCode);
    if (responseCode != HttpStatus::kPartialContent.Code() && responseCode != HttpStatus::kOk.Code()) {
        return EProtocolStreamErrorUnrecoverable;
    }


    iDechunker.SetChunked(iHeaderTransferEncoding.IsChunked());

    ContentProcessor* contentProcessor = iProtocolManager->GetAudioProcessor();
    return contentProcessor->Stream(*this, iHeaderContentLength.ContentLength());
}


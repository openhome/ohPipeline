#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/UnixTimestamp.h>

// Ex ---

EXCEPTION(SegmentStreamError);
EXCEPTION(SegmentStreamExpired);
EXCEPTION(SegmentStreamUnsupported);

// ---

namespace OpenHome
{
namespace Media
{

// NOTE#1: Only works with the formats: PT[00[.00]H][00[.00]M][00[.00]S]
//
// NOTE#2: The handling of fractional values becomes less and less accurate the longer the fractional part
//         of a specific component. For what we are currently using this for in DASH processing we can
//         accept this degree of loss (we mainly deal with whole numbers or max 2 decimal places of fractional
//         components e.g 0.5, 0.25). However, this _may_ come back to bite us in the future!
class ISO8601Duration
{
public:
    ISO8601Duration();

public:
    TUint64 TotalSeconds() const;
    TBool TryParse(const Brx& aDurationStr);

private:
    void SplitNumberIntoParts(const Brx& aNumberStr, Brn& aWholePart, Brn& aFractionalPart);
    TUint64 ConvertPartsToSeconds(const Brx& aWholePart, const Brx& aFractionalPart, TUint aComponentSeconds);

private:
    TUint64 iSeconds;
};


struct SegmentTemplateParams
{
    const Brx& iRepresentationId;
    TUint      iBandwidth;
    TUint      iTime;
    TUint      iNumber;
    TUint      iSubNumber;
};

class SegmentTemplate
{
public:
    static const Brn kTemplateParameterTime;
    static const Brn kTemplateParameterNumber;
    static const Brn kTemplateParameterSubNumber;
    static const Brn kTemplateParameterRepresentationId;
    static const Brn kTemplateParameterRepresentationBandwidth;

    static const Brn kAttributeInitialization;
    static const Brn kAttributeMedia;
    static const Brn kAttributeTimescale;
    static const Brn kAttributeDuration;
    static const Brn kAttributeStartNumber;

public:
    static TBool TryFormatTemplateUrl(Bwx& aUrlBuf, const Brx& aTemplateUrl, const SegmentTemplateParams& aTemplateParams);

public:
    SegmentTemplate(const Brx& aSegmentXml);

public:
    const Brx& Initialization() const;
    const Brx& Media() const;
    TUint Timescale() const;
    TUint Duration() const;
    TUint StartNumber() const;

private:
    Brn iInitialization;
    Brn iMedia;
    TUint iTimescale;
    TUint iDuration;
    TUint iStartNumber;
};


class IBaseUrlVisitor
{
public:
    virtual ~IBaseUrlVisitor() {}
    virtual void VisitBaseUrl(const Brx& aLevel, TUint aIndex, TUint aSelectionPriority, TUint aWeight, const Brx& aServiceLocation, const Brx& aUrl, const Brx& aElementXml) = 0;
};

class IBaseUrlSelector : public IBaseUrlVisitor
{
public:
    virtual ~IBaseUrlSelector() { }
    virtual const Brx& SelectedBaseUrl() const = 0;
};

class BaseUrlCollection
{
public:
    static const Brn kTagBaseUrl;
    static const Brn kAttributeDVBWeight;
    static const Brn kAttributeServiceLocation;
    static const Brn kAttributeDVBSelectionPriority;

    static const TUint kDefaultWeight;
    static const TUint kDefaultSelectionPriority;

public:
    static TBool TryVisit(const Brx& aXml, const Brx& aLevel, IBaseUrlVisitor& aVisitor);
};

struct ContentProtection
{
    static const Brn kTagContentProtection;

    static const Brn kAttributeValue;
    static const Brn kAttributeSchemeIdUri;
    static const Brn kAttributeCencDefaultKID;

    static const Brn kProtectionTypeMPEG4;

    TBool IsMPEG4Protection() const;

    TBool TrySet(const Brx& aXml);

    Brn iSchemeIdUri;
    Brn iValue;
    Brn iDefaultKID;    // cenc only

    Brn iPropertiesSchemeIdUri; // GUID Scheme ID URI for the properties container
    Brn iPropertiesXML;         // ContentProtection tag containing the further properties
};



class MPDRepresentation;
class MPDAdaptationSet;
class MPDPeriod;
class MPDDocument;

struct MPDSegment
{
    MPDSegment(Bwx& aUrlBuffer) : iUrlBuffer(aUrlBuffer), iRangeStart(-1), iRangeEnd(-1) { }
    Bwx& iUrlBuffer;
    TInt iRangeStart;
    TInt iRangeEnd;
};


class MPDRepresentation
{
public:
    static const Brn kAttributeId;
    static const Brn kAttributeBandwidth;
    static const Brn kAttributeQualityRanking;

    static const TUint kDefaultBandwidth;
    static const TUint kDefaultQualityRanking;

public:
    const Brx& Id() const;
    const Brx& Xml() const;
    const Brx& ElementXml() const;
    TUint Bandwidth() const;
    TUint QualityRanking() const;

    TBool TrySet(const Brx& aXml);

private:
    Brn iXml;
    Brn iId;
    Brn iElementXml; // TODO: Is this actually required, or could we provide a method to fetch this OR rely on the caller to get the value out from the returned thing (Function or MACRO???)
    TUint iBandwidth;
    TUint iQualityRanking;
};


class IRepresentationVisitor
{
public:
    virtual ~IRepresentationVisitor() {}
    virtual void VisitRepresentation(const Brx& aId, TUint aBandwidth, TUint aQualityRanking, const Brx& aRepresentationXml) = 0;
};

class MPDAdaptationSet
{
public:
    static const Brn kAttributeMimeType;
    static const Brn kAttributeContentType;
    static const Brn kAttributeSelectionPriority;

    static const TUint kDefaultSelectionPriority;

public:
    TBool IsAudio() const;
    const Brx& Xml() const;
    const Brx& ElementXml() const;
    const MPDRepresentation& Representation() const;
    TUint SelectionPriority() const;

    TBool TrySet(const Brx& aXml);

    TBool TrySelectRepresentation(const Brx& aRepresentationId);

    void Visit(IRepresentationVisitor& aVisitor);

private:
    MPDRepresentation iRepresentation;
    Brn iXml;
    Brn iElementXml;
    TBool iIsAudio;
    TUint iSelectionPriority;
};


class IAdaptationSetVisitor
{
public:
    virtual ~IAdaptationSetVisitor() { }
    virtual void VisitAdaptationSet(TUint aIndex, TUint aSelectionPriority, TBool aIsAudio, const Brx& aXml) = 0;
};

class MPDPeriod
{
public:
    const Brx& Xml() const;
    const Brx& ElementXml() const;
    const MPDAdaptationSet& AdaptationSet() const;

    TBool TrySet(const Brx& aXml);

    TBool TrySelectAdaptationSet(TUint aIndex); // Annoyingly, adaptation sets don't require an ID and so we must rely on using indexes...

    void Visit(IAdaptationSetVisitor& aVisitor);

private:
    MPDAdaptationSet iAdaptationSet;
    Brn iXml;
    Brn iElementXml;
};

class MPDDocument
{
public:
    static const Brn kAttributeAvailabilityStartTime;

public:
    const Brx& Xml() const;
    const Brx& ElementXml() const;
    const MPDPeriod& Period() const;
    TUint64 MinimumUpdatePeriod() const;

    TBool IsStatic() const;

    TBool HasExpired() const;
    void SetExpired();

    TBool IsContentProtected() const;
    const ContentProtection& ContentProtectionDetails() const;

    void GetBaseUrl(Bwx& aUrlBuffer);

    TBool TrySet(const Brx& aXml);

    void Visit(IBaseUrlVisitor& aVisitor);

private:
    void TryDetectContentProtection();

private:
    MPDPeriod iPeriod;
    ContentProtection iContentProtection;
    Brn iXml;
    Brn iElementXml;
    TBool iIsStatic;
    TBool iExpired;
    ISO8601Duration iMinimumUpdatePeriod;

    Uri iBaseUrl;
    Bws<Uri::kMaxUriBytes> iUrlBuf; // Needed when we're appending so we can reuse iBaseUrl.AbsoluteUri() as this is cleared during the start of Replace(...)
};


class MPDSegmentStream
{
private:
    enum class ESegmentType : TByte
    {
        Base,
        List,
        Template,
        Unknown
    };

public:
    MPDSegmentStream(IUnixTimestamp& aTimestamp);

public:
    TBool TryGetNextSegment(MPDSegment& aSegement);

    /*
    // Seeking support??
    // ---
    TBool TrySeekByTime(TUint aCurrentTimestamp);
    TBool TrySeekByBytes(TUint64 aBytes);
    // ----
    */

public: // FIXME: Maybe should be internal to the MPDDocument??
    TBool TrySet(MPDDocument& aDocument); // FIXME: Do we need the const here? Perhaps it could be constructed with the document and then have some sort of generational counter to ensure we're still valid??

private:
    TBool TryGetInitialisationSegment(MPDSegment& aSegment);
    TBool TryGetMediaSegment(MPDSegment& aSegment);

    TBool TrySetInitialSegmentNumber();

private:
    IUnixTimestamp& iTimestamp;
    MPDDocument* iCurrentDocument; // NOT OWNED
    Brn iSegmentXml;
    ESegmentType iSegmentType;
    TBool iNeedsInitialisationSegment;
    TUint iSegmentNumber;
};

class IDashDRMProvider
{
public:
    virtual ~IDashDRMProvider() { }
    virtual TBool TryRecognise(const ContentProtection& aContentProtection) = 0;
};



} // namespace Media
} // namespace OpenHome

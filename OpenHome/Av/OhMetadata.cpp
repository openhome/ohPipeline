#include <OpenHome/Av/OhMetadata.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Av/Scd/ScdMsg.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Av/Debug.h>

namespace OpenHome {
namespace Scd {

class Oh2DidlTagMapping
{
public:
    Oh2DidlTagMapping(const TChar* aOhKey, const TChar* aDidlTag)
        : iOhKey(aOhKey)
        , iDidlTag(aDidlTag)
        , iRole(OpenHome::Brx::Empty())
    {}
    Oh2DidlTagMapping(const TChar* aOhKey, const Brx& aDidlTag)
        : iOhKey(aOhKey)
        , iDidlTag(aDidlTag)
        , iRole(OpenHome::Brx::Empty())
    {}
    Oh2DidlTagMapping(const TChar* aOhKey, const TChar* aDidlTag, const TChar* aRole)
        : iOhKey(aOhKey)
        , iDidlTag(aDidlTag)
        , iRole(aRole)
    {}
    Oh2DidlTagMapping(const TChar* aOhKey, const Brx& aDidlTag, const TChar* aRole)
        : iOhKey(aOhKey)
        , iDidlTag(aDidlTag)
        , iRole(aRole)
    {}
public:
    Brn iOhKey;
    Brn iDidlTag;
    Brn iRole;
};

} // namespace Scd
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Scd;
using namespace OpenHome::Av;

// DIDLLite
const Brn DIDLLite::kProtocolHttpGet("http-get:*:*:*");

const Brn DIDLLite::kTagTitle("dc:title");
const Brn DIDLLite::kTagGenre("upnp:genre");
const Brn DIDLLite::kTagClass("upnp:class");
const Brn DIDLLite::kTagArtist("upnp:artist");
const Brn DIDLLite::kTagAlbumTitle("upnp:album");
const Brn DIDLLite::kTagArtwork("upnp:albumArtURI");
const Brn DIDLLite::kTagDescription("dc:description" );
const Brn DIDLLite::kTagOriginalTrackNumber("upnp:originalTrackNumber");

const Brn DIDLLite::kItemTypeTrack("object.item.audioItem.musicTrack");
const Brn DIDLLite::kItemTypeAudioItem("object.item.audioItem");

const Brn DIDLLite::kNameSpaceLinn("https://linn.co.uk");

// WriterDIDLXml
const Brn WriterDIDLXml::kNsDc("dc=\"http://purl.org/dc/elements/1.1/\"");
const Brn WriterDIDLXml::kNsUpnp("upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\"");
const Brn WriterDIDLXml::kNsOh("oh=\"http://www.openhome.org\"");

WriterDIDLXml::WriterDIDLXml(const Brx& aItemId, IWriter& aWriter)
    : WriterDIDLXml(aItemId, Brx::Empty(), aWriter)
{ }

WriterDIDLXml::WriterDIDLXml(const Brx& aItemId, const Brx& aParentId, IWriter& aWriter)
    : iWriter(aWriter)
    , iEndWritten(false)
{
    // Preamble.... We include the 3 most common namespaces to avoid us having to inline them on every tag call
    TryWrite("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    TryWrite("<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\"");
    TryWrite(" xmlns:");
    TryWrite(kNsUpnp);
    TryWrite(" xmlns:");
    TryWrite(kNsDc);
    TryWrite(" xmlns:");
    TryWrite(kNsOh);
    TryWrite(">");
    TryWrite("<item");

    TryWriteAttribute("id", aItemId);
    TryWriteAttribute("parentID", (aParentId.Bytes() == 0 ? static_cast<const Brx&>(Brn("-1"))
                                                          : aParentId));
    TryWriteAttribute("restricted", Brn("1"));
    TryWrite(">");
}


void WriterDIDLXml::TryWriteAttribute(const TChar* aDidlAttr, const Brx& aValue)
{
    Brn attr(aDidlAttr);
    TryWriteAttribute(attr, aValue);
}

void WriterDIDLXml::TryWriteAttribute(const Brx& aDidlAttr, const Brx& aValue)
{
    if (aValue.Bytes() == 0) {
        return;
    }

    TryWrite(" ");
    TryWrite(aDidlAttr);
    TryWrite("=\"");
    TryWrite(aValue);
    TryWrite("\"");
}

void WriterDIDLXml::TryWriteAttribute(const TChar* aDidlAttr, TUint aValue)
{
    Brn attr(aDidlAttr);
    TryWriteAttribute(attr, aValue);
}

void WriterDIDLXml::TryWriteAttribute(const Brx& aDidlAttr, TUint aValue)
{
    TryWrite(" ");
    TryWrite(aDidlAttr);
    TryWrite("=\"");
    WriterAscii wa(iWriter);
    wa.WriteUint(aValue);
    TryWrite("\"");
}

void WriterDIDLXml::TryWriteTag(const Brx& aDidlTag, const Brx& aValue)
{
    TryWriteTagWithAttribute(aDidlTag, Brx::Empty(), Brx::Empty(), Brx::Empty(), aValue);
}

void WriterDIDLXml::TryWriteTag(const Brx& aDidlTag, const Brx& aNs, const Brx& aValue)
{
    TryWriteTagWithAttribute(aDidlTag, aNs, Brx::Empty(), Brx::Empty(), aValue);
}

void WriterDIDLXml::TryWriteTagWithAttribute(const Brx& aDidlTag, const Brx& aAttribute, const Brx& aAttributeValue, const Brx& aValue)
{
    TryWriteTagWithAttribute(aDidlTag, Brx::Empty(), aAttribute, aAttributeValue, aValue);
}

void WriterDIDLXml::TryWriteTagWithAttribute(const Brx& aDidlTag, const Brx& aNs, const Brx& aAttribute, const Brx& aAttributeValue, const Brx& aValue)
{
    // Don't bother trying to write out any values that are totally empty!
    if (aValue.Bytes() == 0) {
        return;
    }

    TryWrite("<");
    TryWrite(aDidlTag);

    if (aNs.Bytes() > 0) {
        TryWrite(" xmlns:");
        TryWrite(aNs);
    }

    if (aAttribute.Bytes() > 0 && aAttributeValue.Bytes() > 0) {
        TryWriteAttribute(aAttribute, aAttributeValue);
    }

    TryWrite(">");

    TryWriteEscaped(aValue);

    TryWrite("</");
    TryWrite(aDidlTag);
    TryWrite(">");
}

void WriterDIDLXml::TryWrite(const TChar* aStr)
{
    Brn val(aStr);
    TryWrite(val);
}

void WriterDIDLXml::TryWrite(const Brx& aBuf)
{
    iWriter.Write(aBuf);
}


void WriterDIDLXml::TryWriteEscaped(const Brx& aValue)
{
    Converter::ToXmlEscaped(iWriter, aValue);
}

void WriterDIDLXml::TryWriteEnd()
{
    ASSERT(!iEndWritten);
    iEndWritten = true;

    TryWrite("</item>");
    TryWrite("</DIDL-Lite>");
}

void WriterDIDLXml::FormatDuration(TUint aDuration, EDurationResolution aResolution, Bwx& aTempBuf)
{
    if (aDuration == 0) {
        return;
    }

    // H+:MM:SS[.F0/F1]
    // Fraction of seconds is fixed (value is in milliseconds, so F0 is always
    // 3 bytes, and F1 always has value 1000, i.e., is 4 bytes).
    // Everything else apart from hours is fixed. Assume no track will ever be
    // >99 hours, so hours requires 2 bytes.
    // Therefore, need enough bytes for string of form: 12:34:56.789/1000
    ASSERT(aTempBuf.MaxBytes() > 17);


    // H+:MM:SS[.F0/F1]
    static const TUint msPerSecond = 1000;
    static const TUint msPerMinute = msPerSecond*60;
    static const TUint msPerHour = msPerMinute*60;

    TUint timeRemaining = aDuration;

    // This method assumes the provided time is in milliseconds
    if (aResolution == EDurationResolution::Seconds) {
        timeRemaining *= msPerSecond;
    }

    const TUint hours = timeRemaining / msPerHour;
    timeRemaining -= hours * msPerHour;

    const TUint minutes = timeRemaining / msPerMinute;
    timeRemaining -= minutes * msPerMinute;

    const TUint seconds = timeRemaining / msPerSecond;
    timeRemaining -= seconds * msPerSecond;

    const TUint milliseconds = timeRemaining;

    ASSERT(hours <= 99);
    if (hours < 10) {
        aTempBuf.Append('0');
    }
    Ascii::AppendDec(aTempBuf, hours);
    aTempBuf.Append(':');

    ASSERT(minutes <= 59);
    if (minutes < 10) {
        aTempBuf.Append('0');
    }
    Ascii::AppendDec(aTempBuf, minutes);
    aTempBuf.Append(':');

    ASSERT(seconds <= 60);
    if (seconds < 10) {
        aTempBuf.Append('0');
    }
    Ascii::AppendDec(aTempBuf, seconds);

    if (milliseconds > 0) {
        aTempBuf.Append('.');
        Ascii::AppendDec(aTempBuf, milliseconds);
        aTempBuf.Append('/');
        Ascii::AppendDec(aTempBuf, msPerSecond);
    }
}

// WriterDIDLLite
WriterDIDLLite::WriterDIDLLite(const Brx& aItemId, const Brx& aItemType, IWriter& aWriter)
    : WriterDIDLLite(aItemId, aItemType, Brx::Empty(), aWriter)
{ }

WriterDIDLLite::WriterDIDLLite(const Brx& aItemId, const Brx& aItemType, const Brx& aParentId, IWriter& aWriter)
    : iWriter(aItemId, aParentId, aWriter)
    , iTitleWritten(false)
    , iGenreWritten(false)
    , iAlbumWritten(false)
    , iArtistWritten(false)
    , iTrackNumberWritten(false)
    , iDescriptionWritten(false)
    , iStreamingDetailsWritten(false)
{
    iWriter.TryWriteTag(DIDLLite::kTagClass, aItemType);
}

void WriterDIDLLite::WriteTitle(const Brx& aTitle)
{
    ASSERT(!iTitleWritten);
    iTitleWritten = true;

    iWriter.TryWriteTag(DIDLLite::kTagTitle, aTitle);
}

void WriterDIDLLite::WriteAlbum(const Brx& aAlbum)
{
    ASSERT(!iAlbumWritten);
    iAlbumWritten = true;

    iWriter.TryWriteTag(DIDLLite::kTagAlbumTitle, aAlbum);
}

void WriterDIDLLite::WriteArtist(const Brx& aArtist)
{
    ASSERT(!iArtistWritten);
    iArtistWritten = true;

    iWriter.TryWriteTag(DIDLLite::kTagArtist, aArtist);
}

void WriterDIDLLite::WriteTrackNumber(const Brx& aTrackNumber)
{
    ASSERT(!iTrackNumberWritten);
    iTrackNumberWritten = true;

    iWriter.TryWriteTag(DIDLLite::kTagOriginalTrackNumber, aTrackNumber);
}

void WriterDIDLLite::WriteGenre(const Brx& aGenre)
{
    ASSERT(!iGenreWritten);
    iGenreWritten = true;

    iWriter.TryWriteTag(DIDLLite::kTagGenre, aGenre);
}


void WriterDIDLLite::WriteStreamingDetails(const Brx& aProtocol, StreamingDetails& aDetails, const Brx& aUri)
{
    ASSERT(!iStreamingDetailsWritten);
    iStreamingDetailsWritten = true;

    iWriter.TryWrite("<res");

    if (aProtocol.Bytes() > 0) {
        iWriter.TryWriteAttribute("protocolInfo", aProtocol);
    }

    if (aDetails.duration > 0) {
        Bws<32> formatted;
        WriterDIDLXml::FormatDuration(aDetails.duration, aDetails.durationResolution, formatted);
        iWriter.TryWriteAttribute("duration", formatted);
    }

    if (aDetails.bitDepth > 0) {
        iWriter.TryWriteAttribute("bitsPerSample", aDetails.bitDepth);
    }

    if (aDetails.sampleRate > 0) {
        iWriter.TryWriteAttribute("sampleFrequency", aDetails.sampleRate);
    }

    if (aDetails.numberOfChannels != 0) {
        iWriter.TryWriteAttribute("nrAudioChannels", aDetails.numberOfChannels);
    }

    // DIDL-Lite bitrate attribute actually refers to a byte rate!
    if (aDetails.byteRate) {
        iWriter.TryWriteAttribute("bitrate", aDetails.byteRate);
    }

    if (aDetails.bitDepth > 0 && aDetails.numberOfChannels > 0 && aDetails.sampleRate > 0 && aDetails.duration > 0) {
        const TUint byteDepth = aDetails.bitDepth /8;
        const TUint bytesPerSec = byteDepth * aDetails.sampleRate * aDetails.numberOfChannels;
        const TUint bytesPerMs = bytesPerSec / 1000;
        const TUint totalBytes = aDetails.duration * bytesPerMs;

        iWriter.TryWriteAttribute("size", totalBytes);
    }

    iWriter.TryWrite(">");

    if (aUri.Bytes() > 0) {
        iWriter.TryWriteEscaped(aUri);
    }

    iWriter.TryWrite("</res>");
}

void WriterDIDLLite::WriteDescription(const Brx& aDescription)
{
    ASSERT(!iDescriptionWritten);
    iDescriptionWritten = true;

    iWriter.TryWriteTag(DIDLLite::kTagDescription, aDescription);
}


void WriterDIDLLite::WriteEnd()
{
    // NOTE: Will throw if WriteEnd() is called multiple times on writer
    iWriter.TryWriteEnd();
}

void WriterDIDLLite::WriteArtwork(const Brx& aArtwork)
{
    iWriter.TryWriteTag(DIDLLite::kTagArtwork, aArtwork);
}

void WriterDIDLLite::WriteCustomMetadata(const TChar* aId, const Brx& aNamespace, const Brx& aValue)
{
    iWriter.TryWrite("<desc");
    iWriter.TryWriteAttribute("id", Brn(aId));
    iWriter.TryWriteAttribute("nameSpace", aNamespace);
    iWriter.TryWrite(">");
    iWriter.TryWrite(aValue);
    iWriter.TryWrite("</desc>");
}


// WriterDIDLLiteDefault

const Brn WriterDIDLLiteDefault::kDefaultItemId("0");
const Brn WriterDIDLLiteDefault::kDefaultParentId("0");

void WriterDIDLLiteDefault::Write(const Brx& aTitle, Bwx& aBuffer)
{
    WriterBuffer buf(aBuffer);
    WriterDIDLLite writer(kDefaultItemId, DIDLLite::kItemTypeTrack, kDefaultParentId, buf);
    writer.WriteTitle(aTitle);
    writer.WriteEnd();
}


// OhMetadata
Media::Track* OhMetadata::ToTrack(const OpenHomeMetadataBuf& aMetadata,
                                  Media::TrackFactory& aTrackFactory)
{ // static
    OhMetadata self(aMetadata);
    try {
        self.Parse();
    }
    catch (BufferOverflow&) {
        self.iMetaDataDidl.Replace(Brx::Empty());
    }
    return aTrackFactory.CreateTrack(self.iUri, self.iMetaDataDidl);
}

void OhMetadata::ToDidlLite(const OpenHomeMetadataBuf& aMetadata, Bwx& aDidl)
{ // static
    OhMetadata self(aMetadata);
    try {
        self.Parse();
        aDidl.Replace(self.iMetaDataDidl);
    }
    catch (BufferOverflow&) {
        aDidl.Replace(Brx::Empty());
    }
}

void OhMetadata::ToUriDidlLite(const OpenHomeMetadataBuf& aMetadata, Bwx& aUri, Bwx& aDidl)
{ // static
    OhMetadata self(aMetadata);
    try {
        self.Parse();
        aUri.Replace(self.iUri);
        aDidl.Replace(self.iMetaDataDidl);
    }
    catch (BufferOverflow&) {
        aUri.Replace(Brx::Empty());
        aDidl.Replace(Brx::Empty());
    }
}

OhMetadata::OhMetadata(const OpenHomeMetadataBuf& aMetadata)
    : iMetadata(aMetadata)
{
}

void OhMetadata::Parse()
{
    static const Oh2DidlTagMapping kOh2Didl[] = {
        { "artist", DIDLLite::kTagArtist },
        { "albumArtist", DIDLLite::kTagArtist, "AlbumArtist" },
        { "composer", DIDLLite::kTagArtist, "composer" },
        { "conductor", DIDLLite::kTagArtist,"conductor" },
        { "narrator", DIDLLite::kTagArtist,  "narrator" },
        { "performer", DIDLLite::kTagArtist, "performer" },
        { "genre", DIDLLite::kTagGenre },
        { "albumGenre", DIDLLite::kTagGenre},
        { "author", "dc:author"},
        { "title", DIDLLite::kTagTitle},
        { "year", "dc:date"},
        { "albumTitle", DIDLLite::kTagAlbumTitle },
        { "albumArtwork", DIDLLite::kTagArtwork },
        { "provider", "oh:provider" },
        { "artwork", "oh:artwork"},
        { "track", DIDLLite::kTagOriginalTrackNumber },
        { "tracks", "oh:originalTrackCount" },
        { "disc", "oh:originalDiscNumber" },
        { "discs", "oh:originalDiscCount" },
        { "work", "oh:work" },
        { "movement", "oh:movement" },
        { "show", "oh:show" },
        { "episode", "oh:episodeNumber" },
        { "episodes", "oh:episodeCount" },
        { "published", "oh:published" },
        { "website", "oh:website" },
        { "location", "oh:location" },
        { "details", "oh:details" },
        { "extensions", "oh:extensions" },
        { "publisher", "dc:publisher" },
        { "description", DIDLLite::kTagDescription },
        { "rating", "upnp:rating" }
    };
    static const TUint kNumOh2DidlMappings = sizeof kOh2Didl / sizeof kOh2Didl[0];

    iUri.Replace(Brx::Empty());
    iMetaDataDidl.Replace(Brx::Empty());

    Brn val;
    if (TryGetValue("uri", val)) {
        iUri.Replace(val);
    }

    Brn itemId;
    Brn parentId;
    TryGetValue("id", itemId);          // Assuming present
    TryGetValue("parentId", parentId);  // Optionally parent
    WriterBuffer w(iMetaDataDidl);
    WriterDIDLXml writer(itemId, parentId, w);

    for (auto kvp : iMetadata) {
        for (TUint i = 0; i < kNumOh2DidlMappings; i++) {
            const auto& mapping = kOh2Didl[i];
            if (kvp.first == mapping.iOhKey) {
                writer.TryWriteTagWithAttribute(mapping.iDidlTag, Brn("role"), mapping.iRole, kvp.second);
                break;
            }
        }
    }  

    writer.TryWrite("<res");
    if (TryGetValue("duration", val)) {
        try {
            TUint duration = Ascii::Uint(val);
            Bws<32> formatted;
            WriterDIDLXml::FormatDuration(duration, EDurationResolution::Seconds, formatted);
            writer.TryWriteAttribute("duration", formatted);
        }
        catch (AsciiError&) {
            LOG_ERROR(kScd, "OhMetadata - AsciiError parsing duration of %.*s\n", PBUF(val));
        }
    }
    if (TryGetValue("bitRate", val)) {
        try {
            TUint bitRate = Ascii::Uint(val);
            bitRate /= 8; // DIDL-Lite bitrate attribute actually refers to a byte rate!
            Bws<Ascii::kMaxUintStringBytes> brBuf;
            (void)Ascii::AppendDec(brBuf, bitRate);
            writer.TryWriteAttribute("bitrate", brBuf);
        }
        catch (AsciiError&) {
            LOG_ERROR(kScd, "OhMetadata - AsciiError parsing bitRate of %.*s\n", PBUF(val));
        }
    }

    if (TryGetValue("bitDepth", val)) {
        writer.TryWriteAttribute("bitsPerSample", val);
    }
    if (TryGetValue("sampleRate", val)) {
        writer.TryWriteAttribute("sampleFrequency", val);
    }
    if (TryGetValue("channels", val)) {
        writer.TryWriteAttribute("nrAudioChannels", val);
    }
    if (TryGetValue("mimeType", val)) {
        writer.TryWriteAttribute("protocolInfo", val);
    }
    writer.TryWrite(">");

    if (iUri.Bytes() > 0) {
        WriterBuffer bufWriter(iMetaDataDidl);
        Converter::ToXmlEscaped(bufWriter, iUri);
    }

    writer.TryWrite("</res>");
    if (TryGetValue("type", val)) {
        writer.TryWriteTag(Brn("upnp:class"), WriterDIDLXml::kNsUpnp, val);
    }

    writer.TryWriteEnd();
}

TBool OhMetadata::TryGetValue(const TChar* aKey, Brn& aValue) const
{
    Brn key(aKey);
    return TryGetValue(key, aValue);
}

TBool OhMetadata::TryGetValue(const Brx& aKey, Brn& aValue) const
{
    for (auto kvp : iMetadata) {
        if (kvp.first == aKey) {
            aValue.Set(kvp.second);
            return true;
        }
    }
    return false;
}

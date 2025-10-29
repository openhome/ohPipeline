#include <OpenHome/DidlLite.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Net/Private/XmlParser.h>
#include <OpenHome/Media/Debug.h>

#include <utility>

using namespace OpenHome;

// DIDLLite
const Brn DIDLLite::kProtocolHttpGet("http-get:*:*:*");

const Brn DIDLLite::kTagTitle("dc:title");
const Brn DIDLLite::kTagGenre("upnp:genre");
const Brn DIDLLite::kTagClass("upnp:class");
const Brn DIDLLite::kTagArtist("upnp:artist");
const Brn DIDLLite::kTagResource("res");
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
    if (aDuration >= 0x7FFFFFFF) {
        LOG(kMedia, "DIDL-Lite duration suspiciously long - omit.");
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

    ASSERT_VA(hours <= 99, "WriterDIDLXml::FormatDuration - hours=%u\n", hours);
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


// DIDLLiteTruncator

void DIDLLiteTruncator::CheckTruncate(const Brx& aSrc, Bwx& aDest)
{
    if (aSrc.Bytes() <= aDest.MaxBytes()) {
        aDest.Replace(aSrc);
        return;
    }
    LOG(kMedia, "DIDL-Lite too long - truncating.");
    Brn item;
    Brn didl;

    if (!Net::XmlParserBasic::TryFind("DIDL-Lite", aSrc, didl) || !Net::XmlParserBasic::TryFind("item", didl, item)) {
        LOG(kMedia, "DIDL-Lite invalid - cannot truncate.");
        return;
    }

    Brn id;
    Brn parentId;
    Brn itemValue;

    Net::XmlParserBasic::TryFind("id", item, id);
    Net::XmlParserBasic::TryFind("parentID", item, parentId);

    try {
        aDest.SetBytes(0);
        WriterBuffer writerBuf(aDest);
        WriterDIDLXml writerDidl(id, parentId, writerBuf);

        const std::pair<const TChar*, Brn> singleTags[4] = {
            {"title",  DIDLLite::kTagTitle},
            {"artist", DIDLLite::kTagArtist},
            {"album",  DIDLLite::kTagAlbumTitle},
            {"res",    DIDLLite::kTagResource},      // Required for CPs to display as a track, not a source
        };

        const std::pair<const TChar*, Brn> multiTags[1] = {
            {"albumArtURI", DIDLLite::kTagArtwork},
        };

        for(TUint i = 0; i < (sizeof(singleTags) / sizeof(singleTags[0])); i += 1) {
            if (Net::XmlParserBasic::TryFind(singleTags[i].first, item, itemValue)) {
                writerDidl.TryWriteTag(singleTags[i].second, itemValue);
            }
        }

        // In cases where we may have multiple entries with the same tag we want to select the last copy
        // as this is the 'best' value to use. Eg AlbumArtwork. Last = highest quality, so keep this
        for(TUint i = 0; i < (sizeof(multiTags) / sizeof(multiTags[0])); i += 1) {
            Brn remaining(item);
            itemValue.Set(Brx::Empty());
            while (Net::XmlParserBasic::TryFind(multiTags[i].first, remaining, remaining, itemValue)) {
                // ...
            }
            if (itemValue.Bytes() > 0) {
                writerDidl.TryWriteTag(multiTags[i].second, itemValue);
            }
        }


        writerDidl.TryWriteEnd();
    }
    catch (WriterError&) {
        LOG(kMedia, "DIDL-Lite truncation failed - removing.");
        aDest.SetBytes(0);
    }
}

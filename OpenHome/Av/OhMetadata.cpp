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
namespace Av {

class WriterDIDLXml
{
public:
    static const Brn kNsDc;
    static const Brn kNsUpnp;
    static const Brn kNsOh;

public:
    WriterDIDLXml(const Brx& aItemId, Bwx& aBuffer);
    WriterDIDLXml(const Brx& aItemId, const Brx& aParentId, Bwx& aBuffer);

public:
    void TryWriteAttribute(const TChar* aDidlAttr, const Brx& aValue);
    void TryWriteAttribute(const Brx& aDidlAttr, const Brx& aValue);
    void TryWriteTag(const Brx& aDidlTag, const Brx& aNs, const Brx& aValue);
    void TryWriteTagWithAttribute(const Brx& aDidlTag, const Brx& aNs, const Brx& aAttribute, const Brx& aAttributeValue, const Brx& aValue);
    void TryWrite(const TChar* aStr);
    void TryWrite(const Brx& aBuf);
    void TryWriteEnd();

private:
    Bwx& iBuffer;
    TBool iEndWritten;
};

} // namespace Av
namespace Scd {

class Oh2DidlTagMapping
{
public:
    Oh2DidlTagMapping(const TChar* aOhKey, const TChar* aDidlTag, const Brx& aNs)
        : iOhKey(aOhKey)
        , iDidlTag(aDidlTag)
        , iNs(aNs)
        , iRole(OpenHome::Brx::Empty())
    {}
    Oh2DidlTagMapping(const TChar* aOhKey, const TChar* aDidlTag, const Brx& aNs, const TChar* aRole)
        : iOhKey(aOhKey)
        , iDidlTag(aDidlTag)
        , iNs(aNs)
        , iRole(aRole)
    {}
public:
    Brn iOhKey;
    Brn iDidlTag;
    Brn iNs;
    Brn iRole;
};

} // namespace Scd
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Scd;
using namespace OpenHome::Av;

// WriterDIDLXml
const Brn WriterDIDLXml::kNsDc("dc=\"http://purl.org/dc/elements/1.1/\"");
const Brn WriterDIDLXml::kNsUpnp("upnp=\"urn:schemas-upnp-org:metadata-1-0/upnp/\"");
const Brn WriterDIDLXml::kNsOh("oh=\"http://www.openhome.org\"");

WriterDIDLXml::WriterDIDLXml(const Brx& aItemId, Bwx& aBuffer)
    : WriterDIDLXml(aItemId, Brx::Empty(), aBuffer)
{ }

WriterDIDLXml::WriterDIDLXml(const Brx& aItemId, const Brx& aParentId, Bwx& aBuffer)
    : iBuffer(aBuffer)
    , iEndWritten(false)
{
    iBuffer.SetBytes(0);

    // Preamble....
    TryWrite("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
    TryWrite("<DIDL-Lite xmlns=\"urn:schemas-upnp-org:metadata-1-0/DIDL-Lite/\">");
    TryWrite("<item");

    // Every item *MUST* have an ID.
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
    TryWrite(" ");
    TryWrite(aDidlAttr);
    TryWrite("=\"");
    TryWrite(aValue);
    TryWrite("\"");
}

void WriterDIDLXml::TryWriteTag(const Brx& aDidlTag, const Brx& aNs, const Brx& aValue)
{
    TryWriteTagWithAttribute(aDidlTag, aNs, Brx::Empty(), Brx::Empty(), aValue);
}

void WriterDIDLXml::TryWriteTagWithAttribute(const Brx& aDidlTag, const Brx& aNs, const Brx& aAttribute, const Brx& aAttributeValue, const Brx& aValue)
{
    TryWrite("<");
    TryWrite(aDidlTag);
    TryWrite(" xmlns:");
    TryWrite(aNs);

    if (aAttribute.Bytes() > 0 && aAttributeValue.Bytes() > 0) {
        TryWriteAttribute(aAttribute, aAttributeValue);
    }

    TryWrite(">");

    WriterBuffer writer(iBuffer);
    Converter::ToXmlEscaped(writer, aValue);

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
    iBuffer.AppendThrow(aBuf);
}

void WriterDIDLXml::TryWriteEnd()
{
    ASSERT(!iEndWritten);
    iEndWritten = true;

    TryWrite("</item>");
    TryWrite("</DIDL-Lite>");
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
        { "artist", "upnp:artist", WriterDIDLXml::kNsUpnp },
        { "albumArtist", "upnp:artist", WriterDIDLXml::kNsUpnp, "AlbumArtist" },
        { "composer", "upnp:artist", WriterDIDLXml::kNsUpnp, "composer" },
        { "conductor", "upnp:artist", WriterDIDLXml::kNsUpnp, "conductor" },
        { "narrator", "upnp:artist", WriterDIDLXml::kNsUpnp, "narrator" },
        { "performer", "upnp:artist", WriterDIDLXml::kNsUpnp, "performer" },
        { "genre", "upnp:genre", WriterDIDLXml::kNsUpnp },
        { "albumGenre", "upnp:genre", WriterDIDLXml::kNsUpnp },
        { "author", "dc:author", WriterDIDLXml::kNsDc },
        { "title", "dc:title", WriterDIDLXml::kNsDc },
        { "year", "dc:date", WriterDIDLXml::kNsDc },
        { "albumTitle", "upnp:album", WriterDIDLXml::kNsUpnp },
        { "albumArtwork", "upnp:albumArtURI", WriterDIDLXml::kNsUpnp },
        { "provider", "oh:provider", WriterDIDLXml::kNsOh },
        { "artwork", "oh:artwork", WriterDIDLXml::kNsOh },
        { "track", "upnp:originalTrackNumber", WriterDIDLXml::kNsUpnp },
        { "tracks", "oh:originalTrackCount", WriterDIDLXml::kNsOh },
        { "disc", "oh:originalDiscNumber", WriterDIDLXml::kNsOh },
        { "discs", "oh:originalDiscCount", WriterDIDLXml::kNsOh },
        { "work", "oh:work", WriterDIDLXml::kNsOh },
        { "movement", "oh:movement", WriterDIDLXml::kNsOh },
        { "show", "oh:show", WriterDIDLXml::kNsOh },
        { "episode", "oh:episodeNumber", WriterDIDLXml::kNsOh },
        { "episodes", "oh:episodeCount", WriterDIDLXml::kNsOh },
        { "published", "oh:published", WriterDIDLXml::kNsOh },
        { "website", "oh:website", WriterDIDLXml::kNsOh },
        { "location", "oh:location", WriterDIDLXml::kNsOh },
        { "details", "oh:details", WriterDIDLXml::kNsOh },
        { "extensions", "oh:extensions", WriterDIDLXml::kNsOh },
        { "publisher", "dc:publisher", WriterDIDLXml::kNsDc },
        { "description", "dc:description", WriterDIDLXml::kNsDc },
        { "rating", "upnp:rating", WriterDIDLXml::kNsUpnp }
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
    WriterDIDLXml writer(itemId, parentId, iMetaDataDidl);

    for (auto kvp : iMetadata) {
        for (TUint i = 0; i < kNumOh2DidlMappings; i++) {
            const auto& mapping = kOh2Didl[i];
            if (kvp.first == mapping.iOhKey) {
                writer.TryWriteTagWithAttribute(mapping.iDidlTag, mapping.iNs, Brn("role"), mapping.iRole, kvp.second);
                break;
            }
        }
    }  

    writer.TryWrite("<res");
    if (TryGetValue("duration", val)) {
        try {
            TUint duration = Ascii::Uint(val);
            const TUint secs = duration % 60;
            duration /= 60;
            const TUint mins = duration % 60;
            const TUint hours = duration / 60;
            Bws<32> formatted;
            formatted.AppendPrintf("%u:%02u:%02u.000", hours, mins, secs);

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
        WriterBuffer writer(iMetaDataDidl);
        Converter::ToXmlEscaped(writer, iUri);
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

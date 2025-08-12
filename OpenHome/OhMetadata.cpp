#include <OpenHome/OhMetadata.h>
#include <OpenHome/DidlLite.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Av/Scd/ScdMsg.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Net/Private/XmlParser.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Av/Debug.h>

namespace OpenHome {

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

} // namespace OpenHome

using namespace OpenHome;


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

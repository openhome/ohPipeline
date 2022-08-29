#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Media/Pipeline/Msg.h>

#include <string>
#include <vector>
#include <utility>

namespace OpenHome {
namespace Av {

class DIDLLite
{
public:
    static const Brn kProtocolHttpGet;

    static const Brn kTagTitle;
    static const Brn kTagArtist;
    static const Brn kTagArtwork;
    static const Brn kTagResource;
    static const Brn kTagAlbumTitle;
    static const Brn kTagDescription;
    static const Brn kTagOriginalTrackNumber;

    static const Brn kItemTypeTrack;
};

// NOTE: It is not expected that this class be used directly.
//       Instead it should be accessed via 'WriterDIDLLite' or 'OhMetadata'
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
    void TryWriteTag(const Brx& aDidlTag, const Brx& aValue);
    void TryWriteTag(const Brx& aDidlTag, const Brx& aNs, const Brx& aValue);
    void TryWriteTagWithAttribute(const Brx& aDidlTag, const Brx& aAttribute, const Brx& aAttributeValue, const Brx& aValue);
    void TryWriteTagWithAttribute(const Brx& aDidlTag, const Brx& aNs, const Brx& aAttribute, const Brx& aAttributeValue, const Brx& aValue);
    void TryWrite(const TChar* aStr);
    void TryWrite(const Brx& aBuf);
    void TryWriteEscaped(const Brx& aValue);
    void TryWriteEnd();

    static void FormatDuration(TUint duration, Bwx& aTempBuf);

private:
    Bwx& iBuffer;
    TBool iEndWritten;
};

class WriterDIDLLite
{
public:
    WriterDIDLLite(const Brx& aItemId,
                   const Brx& aItemType,
                   Bwx& aBuffer);
    WriterDIDLLite(const Brx& aItemId,
                   const Brx& aItemType,
                   const Brx& aParentId,
                   Bwx& aBuffer);

public:
    // The following methods should only be called once
    void WriteTitle(const Brx& aTitle);
    void WriteTrackNumber(const Brx& aTrackNumber);
    void WriteAlbum(const Brx& aAlbum);
    void WriteDescription(const Brx& aDescription);
    void WriteArtist(const Brx& aArtist); // TODO: This could be expanded to allow multiple calls accepting 'Roles'
    void WriteStreamingDetails(const Brx& aProtocol, TUint aDuration, const Brx& aUri);
    void WriteEnd();

    // The following methods can be called multiple times
    void WriteArtwork(const Brx& aUri);

private:
    WriterDIDLXml iWriter;
    TBool iTitleWritten;
    TBool iAlbumWritten;
    TBool iArtistWritten;
    TBool iTrackNumberWritten;
    TBool iDescriptionWritten;
    TBool iStreamingDetailsWritten;
};


typedef std::vector<std::pair<std::string, std::string>> OpenHomeMetadata;
typedef std::vector<std::pair<Brn, Brn>> OpenHomeMetadataBuf;

class OhMetadata : private INonCopyable
{
public:
    static Media::Track* ToTrack(const OpenHomeMetadataBuf& aMetadata,
                                 Media::TrackFactory& aTrackFactory);
    static void ToDidlLite(const OpenHomeMetadataBuf& aMetadata, Bwx& aDidl);
    static void ToUriDidlLite(const OpenHomeMetadataBuf& aMetadata, Bwx& aUri, Bwx& aDidl);
private:
    OhMetadata(const OpenHomeMetadataBuf& aMetadata);
    void Parse();
    TBool TryGetValue(const TChar* aKey, Brn& aValue) const;
    TBool TryGetValue(const Brx& aKey, Brn& aValue) const;
private:
    const OpenHomeMetadataBuf& iMetadata;
    Media::BwsTrackUri iUri;
    Media::BwsTrackMetaData iMetaDataDidl;
};

} // namespace Av
} // namespace OpenHome

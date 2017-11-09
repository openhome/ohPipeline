#pragma once

#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Net/Private/CpiStack.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <Generated/CpAvOpenhomeOrgRadio1.h>
#include <Generated/CpAvOpenhomeOrgPlaylist1.h>
#include <OpenHome/DebugManager.h>
#include <OpenHome/Av/Playlist/TrackDatabase.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Media/Pipeline/Msg.h>
        
EXCEPTION(ITunesResponseInvalid);
EXCEPTION(ITunesRequestInvalid);

namespace OpenHome {
    class Environment;
    class JsonParser;
namespace Configuration {
    class IConfigInitialiser;
    class ConfigChoice;
}
namespace Av {
    class PodcastEpisode
    {
    public:
        PodcastEpisode(const Brx& aXmlItem);
        ~PodcastEpisode();
        const Brx& Title();
        const Brx& Url();
        const Brx& ReleaseDate();
        TUint Duration();
    private:
        void Parse(const Brx& aXmlItem);
    private:
        Bwh iTitle;
        Bwh iUrl;
        Bwh iReleaseDate;
        TUint iDuration;
    };

    class ITunes;
    class ITunesMetadata : private OpenHome::INonCopyable
    {
        static const TUint kXmlResponseChunks = 4 * 1024;

        static const OpenHome::Brn kNsDc;
        static const OpenHome::Brn kNsUpnp;
        static const OpenHome::Brn kNsOh;
    public:
        static const OpenHome::Brn kMediaTypePodcast;
    public:
        ITunesMetadata(ITunes& aITunes, OpenHome::Media::TrackFactory& aTrackFactory);
        OpenHome::Media::Track* TrackFromJson(const OpenHome::Brx& aMetadata);
        static Brn FirstIdFromJson(const OpenHome::Brx& aJsonResponse);
        PodcastEpisode* GetLatestEpisodeFromFeed(const Brx& aFeedUrl);
    private:
        void ParseITunesMetadata(const OpenHome::Brx& aMetadata);
        TBool TryGetResponse(WriterBwh& aWriter, const Brx& aFeedUrl, TUint aBlocksToRead);
        TBool TryConnect(Uri& aUri);
        void WriteRequestHeaders(const Brx& aMethod, Uri& aFeedUri, TUint aContentLength = 0);
        void TryAddAttribute(OpenHome::JsonParser& aParser,
                             const TChar* aITunesKey, const TChar* aDidlAttr);
        void TryAddAttribute(const TChar* aValue, const TChar* aDidlAttr);
        void TryAddTagsFromArray(OpenHome::JsonParser& aParser,
                                 const OpenHome::Brx& aITunesKey, const OpenHome::Brx& aDidlTag,
                                 const OpenHome::Brx& aNs, const OpenHome::Brx& aRole);
        void TryAddTag(OpenHome::JsonParser& aParser, const OpenHome::Brx& aITunesKey,
                       const OpenHome::Brx& aDidlTag, const OpenHome::Brx& aNs);
        void TryAddTag(const OpenHome::Brx& aDidlTag, const OpenHome::Brx& aNs,
                       const OpenHome::Brx& aRole, const OpenHome::Brx& aValue);
        void TryAppend(const TChar* aStr);
        void TryAppend(const OpenHome::Brx& aBuf);
    private:
        ITunes& iTunes;
        OpenHome::Media::TrackFactory& iTrackFactory;
        OpenHome::Media::BwsTrackUri iTrackUri;
        OpenHome::Media::BwsTrackMetaData iMetaDataDidl;
        WriterBwh iXmlResponse;
    };

class ITunes
{
    friend class ITunesMetadata;
    
    static const TUint kReadBufferBytes = 8 * 1024;
    static const TUint kWriteBufferBytes = 1024;
    static const TUint kConnectTimeoutMs = 10000; // FIXME - should read this + ProtocolNetwork's equivalent from a single client-changable location
    static const Brn kHost;
    static const TUint kPort = 80;
    static const TUint kMaxStatusBytes = 512;
    static const TUint kMaxPathAndQueryBytes = 512;
public:
    ITunes(Environment& aEnv);
    ~ITunes();

    TBool TryGetId(WriterBwh& aWriter, const Brx& aQuery);
    TBool TryGetPodcastById(WriterBwh& aWriter, const Brx& aId);
    void Interrupt(TBool aInterrupt);
    static Brn GetFirstValue(const Brx& aXml, const Brx& aTag);
    static Brn GetFirstAttribute(const Brx& aXml, const Brx& aAttribute);
private:
    TBool TryConnect(TUint aPort);
    TBool TryGetResponse(WriterBwh& aWriter, Bwx& aPathAndQuery, TUint aLimit);
    void WriteRequestHeaders(const Brx& aMethod, const Brx& aPathAndQuery, TUint aPort, TUint aContentLength = 0);
    static Brn ReadInt(ReaderUntil& aReader, const Brx& aTag);
    static Brn ReadString(ReaderUntil& aReader, const Brx& aTag);
private:
    Mutex iLock;
    Environment& iEnv;
    SocketTcpClient iSocket;
    Srs<1024> iReaderBuf;
    ReaderUntilS<kReadBufferBytes> iReaderUntil;
    Sws<kWriteBufferBytes> iWriterBuf;
    WriterHttpRequest iWriterRequest;
    ReaderHttpResponse iReaderResponse;
    HttpHeaderContentLength iHeaderContentLength;
};

class PodcastPins
    : public IDebugTestHandler
{
    static const TUint kJsonResponseChunks = 4 * 1024;
public:
    PodcastPins(Net::DvDeviceStandard& aDevice, Media::TrackFactory& aTrackFactory, Net::CpStack& aCpStack);
    ~PodcastPins();

    TBool LoadPodcastLatest(const Brx& aQuery); // iTunes id or search string (single episode - radio single)
    TBool LoadPodcastList(const Brx& aQuery); // iTunes id or search string (episode list - playlist)

    // iTunes podcast attribute options: titleTerm, languageTerm, authorTerm, genreIndex, artistTerm, ratingIndex, keywordsTerm, descriptionTerm (default is all)

public:  // IDebugTestHandler
    TBool Test(const OpenHome::Brx& aType, const OpenHome::Brx& aInput, OpenHome::IWriterAscii& aWriter);
private:
    TBool LoadById(const Brx& aId, TBool aLatestOnly);
    TBool LoadByQuery(const Brx& aQuery, TBool aLatestOnly);
    TBool IsValidId(const Brx& aRequest);
private:
    Mutex iLock;
    ITunes* iITunes;
    WriterBwh iJsonResponse;
    Media::TrackFactory& iTrackFactory;
    Net::CpProxyAvOpenhomeOrgRadio1* iCpRadio;
    Net::CpProxyAvOpenhomeOrgPlaylist1* iCpPlaylist;
    Net::CpStack& iCpStack;
};

};  // namespace Av
};  // namespace OpenHome



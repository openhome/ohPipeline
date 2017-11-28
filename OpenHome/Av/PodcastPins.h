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
    class Parser;
namespace Configuration {
    class IConfigInitialiser;
    class ConfigChoice;
}
namespace Av {
    class PodcastInfo
    {
    public:
        PodcastInfo(const Brx& aJsonObj, const Brx& aId);
        ~PodcastInfo();
        const Brx& Name();
        const Brx& FeedUrl();
        const Brx& Artist();
        const Brx& ArtworkUrl();
        const Brx& Id();
    private:
        void Parse(const Brx& aJsonObj);
    private:
        Bwh iName;
        Bwh iFeedUrl;
        Bwh iArtist;
        Bwh iArtworkUrl;
        Bwh iId;
    };

    class PodcastEpisode
    {
    public:
        PodcastEpisode(const Brx& aXmlItem);
        ~PodcastEpisode();
        const Brx& Title();
        const Brx& Url();
        const Brx& ReleaseDate();
        TUint Duration();
        static Brn GetNextXmlValueByTag(OpenHome::Parser& aParser, const Brx& aTag);
    private:
        void Parse(const Brx& aXmlItem);
        static Brn GetFirstXmlAttribute(const Brx& aXml, const Brx& aAttribute);
    private:
        Bwh iTitle;
        Bwh iUrl;
        Bwh iReleaseDate;
        TUint iDuration;
    };

    class ITunes;
    class ITunesMetadata : private OpenHome::INonCopyable
    {
        static const OpenHome::Brn kNsDc;
        static const OpenHome::Brn kNsUpnp;
        static const OpenHome::Brn kNsOh;
    public:
        static const OpenHome::Brn kMediaTypePodcast;
    public:
        ITunesMetadata(OpenHome::Media::TrackFactory& aTrackFactory);
        Media::Track* GetNextEpisode(PodcastInfo& aPodcast, const Brx& aXmlItem);
        static Brn FirstIdFromJson(const OpenHome::Brx& aJsonResponse);
    private:
        void ParseITunesMetadata(PodcastInfo& aPodcast, const OpenHome::Brx& aMetadata);

        void TryAddAttribute(OpenHome::JsonParser& aParser,
                             const TChar* aITunesKey, const TChar* aDidlAttr);
        void TryAddAttribute(const TChar* aValue, const TChar* aDidlAttr);

        void TryAddTag(OpenHome::JsonParser& aParser, const OpenHome::Brx& aITunesKey,
                       const OpenHome::Brx& aDidlTag, const OpenHome::Brx& aNs);
        void TryAddTag(const OpenHome::Brx& aDidlTag, const OpenHome::Brx& aNs,
                       const OpenHome::Brx& aRole, const OpenHome::Brx& aValue);
        void TryAppend(const TChar* aStr);
        void TryAppend(const OpenHome::Brx& aBuf);
    private:
        OpenHome::Media::TrackFactory& iTrackFactory;
        OpenHome::Media::BwsTrackUri iTrackUri;
        OpenHome::Media::BwsTrackMetaData iMetaDataDidl;
    };

class ITunes
{
    static const TUint kReadBufferBytes = 8 * 1024;
    static const TUint kSingleEpisodesBlockSize = 2; // 1 block is kReadBufferBytes
    static const TUint kMultipleEpisodesBlockSize = 50; // 1 block is kReadBufferBytes
    static const TUint kWriteBufferBytes = 1024;
    static const TUint kConnectTimeoutMs = 10000; // FIXME - should read this + ProtocolNetwork's equivalent from a single client-changable location
    static const Brn kHost;
    static const TUint kPort = 80;
    static const TUint kMaxStatusBytes = 512;
    static const TUint kMaxPathAndQueryBytes = 512;
public:
    ITunes(Environment& aEnv);
    ~ITunes();

    TBool TryGetPodcastId(WriterBwh& aWriter, const Brx& aQuery);
    TBool TryGetPodcastById(WriterBwh& aWriter, const Brx& aId);
    TBool TryGetPodcastEpisodeInfo(WriterBwh& aWriter, const Brx& aXmlFeedUrl, TBool aLatestOnly);
    void Interrupt(TBool aInterrupt);
private:
    TBool TryConnect(const Brx& aHost, TUint aPort);
    TBool TryGetJsonResponse(WriterBwh& aWriter, Bwx& aPathAndQuery, TUint aLimit);
    TBool TryGetXmlResponse(WriterBwh& aWriter, const Brx& aFeedUrl, TUint aBlocksToRead);
    void WriteRequestHeaders(const Brx& aMethod, const Brx& aHost, const Brx& aPathAndQuery, TUint aPort, TUint aContentLength = 0);
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
    static const TUint kJsonResponseChunks = 8 * 1024;
    static const TUint kXmlResponseChunks = 8 * 1024;
public:
    PodcastPins(Net::DvDeviceStandard& aDevice, Media::TrackFactory& aTrackFactory, Net::CpStack& aCpStack);
    ~PodcastPins();

    TBool LoadPodcastLatest(const Brx& aQuery); // iTunes id or search string (single episode - radio single)
    TBool LoadPodcastList(const Brx& aQuery); // iTunes id or search string (episode list - playlist)

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
    WriterBwh iXmlResponse;
    Media::TrackFactory& iTrackFactory;
    Net::CpProxyAvOpenhomeOrgRadio1* iCpRadio;
    Net::CpProxyAvOpenhomeOrgPlaylist1* iCpPlaylist;
    Net::CpStack& iCpStack;
};

};  // namespace Av
};  // namespace OpenHome



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
#include <OpenHome/Av/Playlist/TrackDatabase.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Av/Pins/Pins.h>
#include <OpenHome/Av/Pins/PodcastPins.h>
        
EXCEPTION(TuneInResponseInvalid);
EXCEPTION(TuneInRequestInvalid);

namespace OpenHome {
    class Environment;
    class JsonParser;
    class Parser;
    class Timer;
namespace Configuration {
    class IConfigInitialiser;
    class ConfigChoice;
}
namespace Net {
    class CpProxyAvOpenhomeOrgRadio1;
    class CpProxyAvOpenhomeOrgPlaylist1;
}

namespace Av {
    class PodcastInfoTuneIn
    {
    public:
        PodcastInfoTuneIn(const Brx& aJsonObj, const Brx& aId);
        ~PodcastInfoTuneIn();
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

    class PodcastEpisodeTuneIn
    {
    public:
        PodcastEpisodeTuneIn(const Brx& aXmlItem);
        ~PodcastEpisodeTuneIn();
        const Brx& Title();
        const Brx& Url();
        const Brx& PublishedDate();
        TUint Duration();
        static Brn GetNextXmlValueByTag(OpenHome::Parser& aParser, const Brx& aTag);
    private:
        void Parse(const Brx& aXmlItem);
        static Brn GetFirstXmlAttribute(const Brx& aXml, const Brx& aAttribute);
    private:
        Bwh iTitle;
        Bwh iUrl;
        Bwh iPublishedDate;
        TUint iDuration;
    };

    class TuneIn;
    class TuneInMetadata : private OpenHome::INonCopyable
    {
        static const OpenHome::Brn kNsDc;
        static const OpenHome::Brn kNsUpnp;
        static const OpenHome::Brn kNsOh;
    public:
        static const OpenHome::Brn kMediaTypePodcast;
    public:
        TuneInMetadata(OpenHome::Media::TrackFactory& aTrackFactory);
        Media::Track* GetNextEpisodeTrack(PodcastInfoTuneIn& aPodcast, const Brx& aXmlItem);
        const Brx& GetNextEpisodePublishedDate(const Brx& aXmlItem);
        static Brn FirstIdFromJson(const OpenHome::Brx& aJsonResponse);
    private:
        void ParseTuneInMetadata(PodcastInfoTuneIn& aPodcast, const OpenHome::Brx& aMetadata);

        void TryAddAttribute(OpenHome::JsonParser& aParser,
                             const TChar* aTuneInKey, const TChar* aDidlAttr);
        void TryAddAttribute(const TChar* aValue, const TChar* aDidlAttr);

        void TryAddTag(OpenHome::JsonParser& aParser, const OpenHome::Brx& aTuneInKey,
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

class TuneIn
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
    TuneIn(Environment& aEnv);
    ~TuneIn();

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

class ListenedDatePooledTuneIn;

class PodcastPinsTuneIn
{
    static const TUint kJsonResponseChunks = 8 * 1024;
    static const TUint kXmlResponseChunks = 8 * 1024;
    static const OpenHome::Brn kPodcastKey;
public:
    static const TUint kMaxPodcastIdBytes = 16;
    static const TUint kMaxPodcastDateBytes = 40;
    static const TUint kMaxFormatBytes = 40; // cover json formatting
    static const TUint kMaxEntryBytes = kMaxPodcastIdBytes + kMaxPodcastDateBytes + kMaxFormatBytes;  //{ "id" : "261447018", "date" : "Fri, 24 Nov 2017 20:15:00 GMT", "pty" : 26}, 
    static const TUint kMaxEntries = 26;
    static const TUint kNewEposdeListMaxBytes = kMaxEntries*kMaxPodcastIdBytes + (kMaxEntries-1); // kMaxEntries-1 covers commas
public:
    static PodcastPinsTuneIn* GetInstance(Media::TrackFactory& aTrackFactory, Environment& aEnv, Configuration::IStoreReadWrite& aStore);
    ~PodcastPinsTuneIn();
    void AddNewPodcastEpisodesObserver(IPodcastPinsObserver& aObserver); // event describing podcast IDs with new episodes available (compared to last listened stored data)
    TBool CheckForNewEpisode(const Brx& aQuery); // poll using TuneIn id or search string (single episode)
    TBool LoadPodcastLatest(const Brx& aQuery, IPodcastTransportHandler& aHandler); // TuneIn id or search string (single episode - radio single)
    TBool LoadPodcastList(const Brx& aQuery, IPodcastTransportHandler& aHandler, TBool aShuffle); // TuneIn id or search string (episode list - playlist)
private:
    PodcastPinsTuneIn(Media::TrackFactory& aTrackFactory, Environment& aEnv, Configuration::IStoreReadWrite& aStore);

    void SetLastLoadedPodcastAsListened(); // save date of last podcast ID for new episode notification [option to allow this to be done outside of this class: currently done internally on cp->SyncPlay]
    void StartPollingForNewEpisodes(); // check existing mappings (latest selected podcasts) for new episodes (currently started in constructor)
    void StopPollingForNewEpisodes();
private:
    TBool LoadById(const Brx& aId, IPodcastTransportHandler& aHandler);
    TBool LoadByQuery(const Brx& aQuery, IPodcastTransportHandler& aHandler, TBool aShuffle);
    TBool IsValidId(const Brx& aRequest);
    TBool CheckForNewEpisodeById(const Brx& aId);
    const Brx& GetLastListenedEpisodeDateLocked(const Brx& aId); // pull last stored date for given podcast ID
    void SetLastListenedEpisodeDateLocked(const Brx& aId, const Brx& aDate); // set last stored date for given podcast ID
    void TimerCallback();
    void StartPollingForNewEpisodesLocked();
private:
    static PodcastPinsTuneIn* iInstance;
    Mutex iLock;
    TuneIn* iTuneIn;
    WriterBwh iJsonResponse;
    WriterBwh iXmlResponse;
    Media::TrackFactory& iTrackFactory;

    std::list<ListenedDatePooledTuneIn*> iMappings;
    OpenHome::Configuration::IStoreReadWrite& iStore;
    OpenHome::Bwh iListenedDates;
    OpenHome::Bws<kMaxPodcastIdBytes> iLastSelectedId;
    OpenHome::Bws<kMaxPodcastDateBytes> iLastSelectedDate;
    OpenHome::Timer* iTimer;
    std::vector<IPodcastPinsObserver*> iEpisodeObservers;
    OpenHome::Bws<kNewEposdeListMaxBytes> iNewEpisodeList;
};

class PodcastPinsLatestEpisodeTuneIn
    : public IPinInvoker
    , public IPodcastTransportHandler
{
public:
    PodcastPinsLatestEpisodeTuneIn(Net::DvDeviceStandard& aDevice, Media::TrackFactory& aTrackFactory, Net::CpStack& aCpStack, Configuration::IStoreReadWrite& aStore);
    ~PodcastPinsLatestEpisodeTuneIn();
private:  // from IPodcastTransportHandler
    void Init(TBool aShuffle) override;
    virtual void Load(Media::Track& aTrack) override;
    virtual void Play() override;
    virtual TBool SingleShot() override;
public: // from IPinInvoker
    void Invoke(const IPin& aPin) override;
    const TChar* Mode() const override;
private:
    PodcastPinsTuneIn* iPodcastPins;
    Net::CpProxyAvOpenhomeOrgRadio1* iCpRadio;
};

class PodcastPinsEpisodeListTuneIn
    : public IPinInvoker
    , public IPodcastTransportHandler
{
public:
    PodcastPinsEpisodeListTuneIn(Net::DvDeviceStandard& aDevice, Media::TrackFactory& aTrackFactory, Net::CpStack& aCpStack, Configuration::IStoreReadWrite& aStore);
    ~PodcastPinsEpisodeListTuneIn();
private:  // from IPodcastTransportHandler
    void Init(TBool aShuffle) override;
    virtual void Load(Media::Track& aTrack) override;
    virtual void Play() override;
    virtual TBool SingleShot() override;
public: // from IPinInvoker
    void Invoke(const IPin& aPin) override;
    const TChar* Mode() const override;
    
private:
    PodcastPinsTuneIn* iPodcastPins;
    Net::CpProxyAvOpenhomeOrgPlaylist1* iCpPlaylist;
    TUint iLastId;
};

class ListenedDatePooledTuneIn
{
public:
    ListenedDatePooledTuneIn();
    void Set(const OpenHome::Brx& aId, const OpenHome::Brx& aDate, TUint aPriority);
    const OpenHome::Brx& Id() const;
    const OpenHome::Brx& Date() const;
    const TUint Priority() const;
    void DecPriority();
    static TBool Compare(const ListenedDatePooledTuneIn* aFirst, const ListenedDatePooledTuneIn* aSecond);
private:
    OpenHome::Bws<PodcastPinsTuneIn::kMaxPodcastIdBytes> iId;
    OpenHome::Bws<PodcastPinsTuneIn::kMaxPodcastDateBytes> iDate;
    TUint iPriority;
};

};  // namespace Av
};  // namespace OpenHome



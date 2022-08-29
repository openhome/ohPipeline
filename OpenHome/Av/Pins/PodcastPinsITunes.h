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
        
EXCEPTION(ITunesResponseInvalid);
EXCEPTION(ITunesRequestInvalid);

namespace OpenHome {
    class Environment;
    class IThreadPool;
    class IThreadPoolHandle;
    class JsonParser;
    class Parser;
    class Timer;
namespace Configuration {
    class IConfigInitialiser;
    class ConfigChoice;
}
namespace Net {
    class CpProxyAvOpenhomeOrgRadio2;
    class CpProxyAvOpenhomeOrgPlaylist1;
}

namespace Av {
    class PodcastInfoITunes
    {
    public:
        PodcastInfoITunes(const Brx& aJsonObj, const Brx& aId);
        ~PodcastInfoITunes();
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

    class PodcastEpisodeITunes
    {
    public:
        PodcastEpisodeITunes(const Brx& aXmlItem);
        ~PodcastEpisodeITunes();
        const Brx& Title();
        const Brx& Url();
        const Brx& PublishedDate();
        TUint Duration();
    private:
        void Parse(const Brx& aXmlItem);
    private:
        Bwh iTitle;
        Bwh iUrl;
        Bwh iPublishedDate;
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
        Media::Track* GetNextEpisodeTrack(PodcastInfoITunes& aPodcast, const Brx& aXmlItem);
        const Brx& GetNextEpisodePublishedDate(const Brx& aXmlItem);
        static Brn FirstIdFromJson(const OpenHome::Brx& aJsonResponse);
    private:
        void ParseITunesMetadata(PodcastInfoITunes& aPodcast, const OpenHome::Brx& aMetadata);
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

    TBool TryGetPodcastId(IWriter& aWriter, const Brx& aQuery);
    TBool TryGetPodcastById(IWriter& aWriter, const Brx& aId);
    TBool TryGetPodcastEpisodeInfo(IWriter& aWriter, const Brx& aXmlFeedUrl, TBool aLatestOnly);
    void Interrupt(TBool aInterrupt);
private:
    TBool TryConnect(const Brx& aHost, TUint aPort);
    TBool TryGetJsonResponse(IWriter& aWriter, Bwx& aPathAndQuery, TUint aLimit);
    TBool TryGetXmlResponse(IWriter& aWriter, const Brx& aFeedUrl, TUint aBlocksToRead);
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

class PodcastPinsITunes
{
    static const TUint kJsonResponseChunks = 8 * 1024;
    static const TUint kXmlResponseChunks = 8 * 1024;
    static const OpenHome::Brn kPodcastKey;

public:
    static const TUint kMaxFormatBytes = 40; // cover json formatting
    static const TUint kMaxEntryBytes = PodcastPins::kMaxPodcastIdBytes + PodcastPins::kMaxPodcastDateBytes + kMaxFormatBytes;  //{ "id" : "261447018", "date" : "Fri, 24 Nov 2017 20:15:00 GMT", "pty" : 26}, 
    static const TUint kMaxEntries = 26;
    static const TUint kNewEposdeListMaxBytes = kMaxEntries*(PodcastPins::kMaxPodcastIdBytes) + (kMaxEntries-1); // kMaxEntries-1 covers commas
public:
    static PodcastPinsITunes* GetInstance(Media::TrackFactory& aTrackFactory, Environment& aEnv, Configuration::IStoreReadWrite& aStore);
    ~PodcastPinsITunes();
    void AddNewPodcastEpisodesObserver(IPodcastPinsObserver& aObserver); // event describing podcast IDs with new episodes available (compared to last listened stored data)
    TBool CheckForNewEpisode(const Brx& aQuery); // poll using iTunes id or search string (single episode)
    TBool LoadPodcastLatest(const Brx& aQuery, IPodcastTransportHandler& aHandler); // iTunes id or search string (single episode - radio single)
    TBool LoadPodcastList(const Brx& aQuery, IPodcastTransportHandler& aHandler, TBool aShuffle); // iTunes id or search string (episode list - playlist)
    void Cancel(TBool aCancelState);
private:
    PodcastPinsITunes(Media::TrackFactory& aTrackFactory, Environment& aEnv, Configuration::IStoreReadWrite& aStore);

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
    static PodcastPinsITunes* iInstance;
    Mutex iLock;
    ITunes* iITunes;
    TBool iStarted;
    WriterBwh iJsonResponse;
    WriterBwh iXmlResponse;
    Media::TrackFactory& iTrackFactory;

    std::list<ListenedDatePooled*> iMappings;
    OpenHome::Configuration::IStoreReadWrite& iStore;
    OpenHome::Bwh iListenedDates;
    OpenHome::Bws<PodcastPins::kMaxPodcastIdBytes> iLastSelectedId;
    OpenHome::Bws<PodcastPins::kMaxPodcastDateBytes> iLastSelectedDate;
    OpenHome::Timer* iTimer;
    std::vector<IPodcastPinsObserver*> iEpisodeObservers;
    OpenHome::Bws<kNewEposdeListMaxBytes> iNewEpisodeList;
};

class PodcastPinsLatestEpisodeITunes
    : public IPinInvoker
    , public IPodcastTransportHandler
{
    const TUint kMinSupportedVersion = 1;
    const TUint kMaxSupportedVersion = 1;

public:
    PodcastPinsLatestEpisodeITunes(Net::DvDeviceStandard& aDevice, Media::TrackFactory& aTrackFactory, Net::CpStack& aCpStack, Configuration::IStoreReadWrite& aStore, IThreadPool& aThreadPool);
    ~PodcastPinsLatestEpisodeITunes();
private:  // from IPodcastTransportHandler
    void Init(TBool aShuffle) override;
    virtual void Load(Media::Track& aTrack) override;
    virtual void Play() override;
    virtual TBool SingleShot() override;
private: // from IPinInvoker
    void BeginInvoke(const IPin& aPin, Functor aCompleted) override;
    void Cancel() override;
    const TChar* Mode() const override;
    TBool SupportsVersion(TUint version) const override;
private:
    void Invoke();
private:
    PodcastPinsITunes* iPodcastPins;
    Net::CpProxyAvOpenhomeOrgRadio2* iCpRadio;
    IThreadPoolHandle* iThreadPoolHandle;
    Bws<128> iToken;
    Functor iCompleted;
    PinIdProvider iPinIdProvider;
    Pin iPin;
};

class PodcastPinsEpisodeListITunes
    : public IPinInvoker
    , public IPodcastTransportHandler
{
    const TUint kMinSupportedVersion = 1;
    const TUint kMaxSupportedVersion = 1;

public:
    PodcastPinsEpisodeListITunes(Net::DvDeviceStandard& aDevice, Media::TrackFactory& aTrackFactory, Net::CpStack& aCpStack, Configuration::IStoreReadWrite& aStore, IThreadPool& aThreadPool);
    ~PodcastPinsEpisodeListITunes();
private:  // from IPodcastTransportHandler
    void Init(TBool aShuffle) override;
    virtual void Load(Media::Track& aTrack) override;
    virtual void Play() override;
    virtual TBool SingleShot() override;
private: // from IPinInvoker
    void BeginInvoke(const IPin& aPin, Functor aCompleted) override;
    void Cancel() override;
    const TChar* Mode() const override;
    TBool SupportsVersion(TUint version) const override;
private:
    void Invoke();
private:
    PodcastPinsITunes* iPodcastPins;
    Net::CpProxyAvOpenhomeOrgPlaylist1* iCpPlaylist;
    TUint iLastId;
    IThreadPoolHandle* iThreadPoolHandle;
    Bws<128> iToken;
    Functor iCompleted;
    PinIdProvider iPinIdProvider;
    Pin iPin;
};

};  // namespace Av
};  // namespace OpenHome



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
    class IThreadPool;
    class IThreadPoolHandle;
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
        PodcastInfoTuneIn(const Brx& aFeedUrl);
        ~PodcastInfoTuneIn();
        const Brx& FeedUrl();
        const Brx& Id();
    private:
        void Parse(const Brx& aFeedUrl);
    private:
        Bwh iFeedUrl;
        Bwh iId;
    };

    class PodcastEpisodeTuneIn
    {
    public:
        PodcastEpisodeTuneIn(const Brx& aXmlItem);
        ~PodcastEpisodeTuneIn();
        const Brx& Title();
        const Brx& Url();
        const Brx& ArtworkUrl();
        const Brx& PublishedDate();
        TUint Duration();
    private:
        void Parse(const Brx& aXmlItem);
    private:
        Bwh iTitle;
        Bwh iUrl;
        Bwh iArtworkUrl;
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
        Media::Track* GetNextEpisodeTrack(const OpenHome::Brx& aPodcastId, const Brx& aXmlItem);
        const Brx& GetNextEpisodePublishedDate(const Brx& aXmlItem);
    private:
        void ParseTuneInMetadata(const Brx& aPodcastId, const OpenHome::Brx& aMetadata);
        void TryAddAttribute(const TChar* aValue, const TChar* aDidlAttr);
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
    static const TUint kPort = 80;
    static const TUint kMaxStatusBytes = 512;
    static const TUint kMaxPathAndQueryBytes = 512;
public:
    TuneIn(Environment& aEnv);
    ~TuneIn();

    TBool TryGetPodcastById(IWriter& aWriter, const Brx& aId);
    TBool TryGetPodcastEpisodeInfoById(IWriter& aWriter, const Brx& aId);
    TBool TryGetPodcastFromPath(IWriter& aWriter, const Brx& aPath);
    const Brx& GetPathFromId(const Brx& aId);
    static void SetPathFromId(Bwx& aPath, const Brx& aId);
    void Interrupt(TBool aInterrupt);
private:
    TBool TryConnect(const Brx& aHost, TUint aPort);
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
    Bwh iPath;
};

class PodcastPinsTuneIn
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
    static PodcastPinsTuneIn* GetInstance(Media::TrackFactory& aTrackFactory, Environment& aEnv, Configuration::IStoreReadWrite& aStore, const OpenHome::Brx& aPartnerId);
    static const Brx& GetPartnerId();
    ~PodcastPinsTuneIn();
    void AddNewPodcastEpisodesObserver(IPodcastPinsObserver& aObserver); // event describing podcast IDs with new episodes available (compared to last listened stored data)
    TBool CheckForNewEpisode(const Brx& aId); // poll using TuneIn id (single episode)
    TBool LoadPodcastLatestById(const Brx& aId, IPodcastTransportHandler& aHandler); // TuneIn id (single episode - radio single)
    TBool LoadPodcastLatestByPath(const Brx& aPath, IPodcastTransportHandler& aHandler); // TuneIn path (single episode - radio single)
    TBool LoadPodcastListById(const Brx& aId, IPodcastTransportHandler& aHandler, TBool aShuffle); // TuneIn id (episode list - playlist)
    TBool LoadPodcastListByPath(const Brx& aPath, IPodcastTransportHandler& aHandler, TBool aShuffle); // TuneIn path (episode list - playlist)
    void Cancel();
private:
    PodcastPinsTuneIn(Media::TrackFactory& aTrackFactory, Environment& aEnv, Configuration::IStoreReadWrite& aStore);

    void SetLastLoadedPodcastAsListened(); // save date of last podcast ID for new episode notification [option to allow this to be done outside of this class: currently done internally on cp->SyncPlay]
    void StartPollingForNewEpisodes(); // check existing mappings (latest selected podcasts) for new episodes (currently started in constructor)
    void StopPollingForNewEpisodes();
private:
    TBool LoadByPath(const Brx& aPath, IPodcastTransportHandler& aHandler, TBool aShuffle);
    TBool CheckForNewEpisodeById(const Brx& aId);
    const Brx& GetLastListenedEpisodeDateLocked(const Brx& aId); // pull last stored date for given podcast ID
    void SetLastListenedEpisodeDateLocked(const Brx& aId, const Brx& aDate); // set last stored date for given podcast ID
    void TimerCallback();
    void StartPollingForNewEpisodesLocked();
private:
    static PodcastPinsTuneIn* iInstance;
    static Brh iPartnerId;
    Mutex iLock;
    TuneIn* iTuneIn;
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

class PodcastPinsLatestEpisodeTuneIn
    : public IPodcastTransportHandler
{
public:
    PodcastPinsLatestEpisodeTuneIn(Net::DvDeviceStandard& aDevice, Media::TrackFactory& aTrackFactory, Net::CpStack& aCpStack, Configuration::IStoreReadWrite& aStore, const OpenHome::Brx& aPartnerId);
    ~PodcastPinsLatestEpisodeTuneIn();
    void LoadPodcast(const IPin& aPin); // not required to be a separate pin invoker as it is created as part of TuneInPins
    void Cancel();
private:  // from IPodcastTransportHandler
    void Init(TBool aShuffle) override;
    virtual void Load(Media::Track& aTrack) override;
    virtual void Play() override;
    virtual TBool SingleShot() override;
private:
    PodcastPinsTuneIn* iPodcastPins;
    Net::CpProxyAvOpenhomeOrgRadio1* iCpRadio;
};

class PodcastPinsEpisodeListTuneIn
    : public IPinInvoker
    , public IPodcastTransportHandler
{
public:
    PodcastPinsEpisodeListTuneIn(Net::DvDeviceStandard& aDevice, Media::TrackFactory& aTrackFactory, Net::CpStack& aCpStack, Configuration::IStoreReadWrite& aStore, IThreadPool& aThreadPool);
    ~PodcastPinsEpisodeListTuneIn();
private:  // from IPodcastTransportHandler
    void Init(TBool aShuffle) override;
    virtual void Load(Media::Track& aTrack) override;
    virtual void Play() override;
    virtual TBool SingleShot() override;
private: // from IPinInvoker
    void BeginInvoke(const IPin& aPin, Functor aCompleted) override;
    void Cancel() override;
    const TChar* Mode() const override;
private:
    void Invoke();
private:
    PodcastPinsTuneIn* iPodcastPins;
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



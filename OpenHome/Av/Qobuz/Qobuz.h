#pragma once

#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Types.h>
#include <OpenHome/SocketSsl.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <Generated/CpAvOpenhomeOrgPlaylist1.h>
#include <OpenHome/Av/Qobuz/QobuzMetadata.h>
#include <OpenHome/Media/PipelineObserver.h>

#include <atomic>
#include <vector>

namespace OpenHome {
    class Environment;
    class Timer;
    class IUnixTimestamp;
namespace Configuration {
    class IConfigInitialiser;
    class ConfigChoice;
}
namespace Av {

    class AutoConnectionQobuz;


class QobuzTrack;

class IQobuzTrackObserver
{
public:
    virtual void TrackStarted(QobuzTrack& aTrack) = 0;
    virtual void TrackStopped(QobuzTrack& aTrack) = 0;
    virtual ~IQobuzTrackObserver() {}
};

class QobuzTrack : private Media::IPipelineObserver
{
public:
    QobuzTrack(IUnixTimestamp& aUnixTimestamp, Media::IPipelineObservable& aPipelineObservable,
               IQobuzTrackObserver& aObserver, TUint aTrackId, const Brx& aUrl, TUint aFormatId);
    ~QobuzTrack();
    void ProtocolStarted(TUint aStreamId);
    void ProtocolCompleted(TBool aStopped);
    void UpdateUrl(const Brx& aUrlEncoded);
    TUint Id() const;
    const Brx& Url() const;
    TUint FormatId() const;
    TUint StartTime() const; // unix time
    TUint PlayedSeconds() const;
private: // from IPipelineObserver
    void NotifyPipelineState(Media::EPipelineState aState) override;
    void NotifyMode(const Brx& aMode, const Media::ModeInfo& aInfo,
                    const Media::ModeTransportControls& aTransportControls) override;
    void NotifyTrack(Media::Track& aTrack, TBool aStartOfStream) override;
    void NotifyMetaText(const Brx& aText) override;
    void NotifyTime(TUint aSeconds) override;
    void NotifyStreamInfo(const Media::DecodedStreamInfo& aStreamInfo) override;
private:
    mutable Mutex iLock;
    IUnixTimestamp& iUnixTimestamp;
    Media::IPipelineObservable& iPipelineObservable;
    IQobuzTrackObserver& iObserver;
    TUint iTrackId; // from Qobuz
    Bwh iUrl;
    std::atomic<TUint> iStartTime;
    std::atomic<TUint> iPlayedSeconds;
    TUint iStreamId; // from pipeline
    TUint iFormatId;
    TBool iCurrentStream;
    TBool iStarted;
};

class Qobuz : public ICredentialConsumer, private IQobuzTrackObserver
{
    friend class TestQobuz;
    friend class QobuzPins;
    friend class AutoConnectionQobuz;
    static const TUint kReadBufferBytes = 4 * 1024;
    static const TUint kWriteBufferBytes = 1024;
    static const TUint kConnectTimeoutMs = 10000; // FIXME - should read this + ProtocolNetwork's equivalent from a single client-changable location
    static const Brn kHost;
    static const TUint kPort = 443;
    static const TUint kGranularityUsername = 128;
    static const TUint kGranularityPassword = 128;
    static const Brn kId;
    static const Brn kVersionAndFormat;
    static const TUint kSecsBetweenNtpAndUnixEpoch = 2208988800; // secs between 1900 and 1970
    static const TUint kMaxStatusBytes = 512;
    static const TUint kMaxPathAndQueryBytes = 512;
    static const TUint kSocketKeepAliveMs = 5000; // close socket after 5s inactivity
    static const Brn kTagFileUrl;
public:
    static const Brn kConfigKeySoundQuality;
    enum class Connection
    {
        KeepAlive,
        Close
    };
public:
    Qobuz(Environment& aEnv, SslContext& aSsl, const Brx& aAppId, const Brx& aAppSecret, const Brx& aDeviceId,
           ICredentialsState& aCredentialsState, Configuration::IConfigInitialiser& aConfigInitialiser,
           IUnixTimestamp& aUnixTimestamp, IThreadPool& aThreadPool,
           Media::IPipelineObservable& aPipelineObservable);
    ~Qobuz();
    TBool TryLogin();
    QobuzTrack* StreamableTrack(const Brx& aTrackId);
    TBool TryUpdateStreamUrl(QobuzTrack& aTrack);
    TBool TryGetId(IWriter& aWriter, const Brx& aQuery, QobuzMetadata::EIdType aType, Connection aConnection = Connection::KeepAlive);
    TBool TryGetIds(IWriter& aWriter, const Brx& aGenre, QobuzMetadata::EIdType aType, TUint aLimitPerResponse, Connection aConnection = Connection::KeepAlive);
    TBool TryGetIdsByRequest(IWriter& aWriter, const Brx& aRequestUrl, TUint aLimitPerResponse, TUint aOffset, Connection aConnection = Connection::KeepAlive);
    TBool TryGetGenreList(IWriter& aWriter, Connection aConnection = Connection::KeepAlive);
    TBool TryGetTracksById(IWriter& aWriter, const Brx& aId, QobuzMetadata::EIdType aType, TUint aLimit, TUint aOffset, Connection aConnection = Connection::KeepAlive);
    void Interrupt(TBool aInterrupt);
private: // from ICredentialConsumer
    const Brx& Id() const override;
    void CredentialsChanged(const Brx& aUsername, const Brx& aPassword) override;
    void UpdateStatus() override;
    void Login(Bwx& aToken) override;
    void ReLogin(const Brx& aCurrentToken, Bwx& aNewToken) override;
private: // from IQobuzTrackObserver
    void TrackStarted(QobuzTrack& aTrack) override;
    void TrackStopped(QobuzTrack& aTrack) override;
private:
    TBool TryConnect();
    TBool TryLoginLocked();
    TBool TryGetFileUrlLocked(const Brx& aTrackId);
    void NotifyStreamStarted(QobuzTrack& aTrack);
    void NotifyStreamStopped(QobuzTrack& aTrack);
    TUint WriteRequestReadResponse(const Brx& aMethod, const Brx& aHost, const Brx& aPathAndQuery, Connection aConnection = Connection::Close);
    TBool TryGetResponseLocked(IWriter& aWriter, const Brx& aHost, TUint aLimit, TUint aOffset, Connection aConnection);
    void QualityChanged(Configuration::KeyValuePair<TUint>& aKvp);
    static void AppendMd5(Bwx& aBuffer, const Brx& aToHash);
    void SocketInactive();
    void CloseConnection();
    void ReportStreamEvents();
    void UpdatePurchasedTracks();
    void ScheduleUpdatePurchasedTracks();
    TBool IsTrackPurchased(TUint aId) const;
private:
    Environment& iEnv;
    Mutex iLock;
    Mutex iLockConfig;
    ICredentialsState& iCredentialsState;
    IUnixTimestamp& iUnixTimestamp;
    Media::IPipelineObservable& iPipelineObservable;
    SocketSsl iSocket;
    Timer* iTimerSocketActivity;
    Srs<1024> iReaderBuf;
    ReaderUntilS<1024> iReaderUntil;
    Sws<2048> iWriteBuffer;
    WriterHttpRequest iWriterRequest;
    ReaderHttpResponse iReaderResponse;
    ReaderHttpEntity iReaderEntity;
    HttpHeaderContentLength iHeaderContentLength;
    HttpHeaderTransferEncoding iHeaderTransferEncoding;
    const Bws<32> iAppId;
    const Bws<32> iAppSecret;
    const Brx& iDeviceId;
    WriterBwh iUsername;
    WriterBwh iPassword;
    TUint iSoundQuality;
    Bws<128> iAuthToken;
    TUint iUserId;
    TUint iCredentialId;
    Bws<512> iPathAndQuery; // slightly too large for the stack; requires that all network operations are serialised
    WriterBwh iResponseBody;
    Configuration::ConfigChoice* iConfigQuality;
    TUint iSubscriberIdQuality;
    Bwh iUri;
    Uri iRequest;
    TBool iConnected;
    Mutex iLockStreamEvents;
    std::list<QobuzTrack*> iPendingStarts;
    std::list<QobuzTrack*> iPendingStops;
    WriterBwh iStreamEventBuf;
    IThreadPoolHandle* iSchedulerStreamEvents;
    IThreadPoolHandle* iSchedulerPurchasedTracks;
    Timer* iTimerPurchasedTracks;
    mutable Mutex iLockPurchasedTracks;
    std::vector<TUint> iPurchasedTracks;
};

class AutoConnectionQobuz
{
public:
    AutoConnectionQobuz(Qobuz& aQobuz, IReader& aReader);
    ~AutoConnectionQobuz();
private:
    Qobuz& iQobuz;
    IReader& iReader;
};

};  // namespace Av
};  // namespace OpenHome

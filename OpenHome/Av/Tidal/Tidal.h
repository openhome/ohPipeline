#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/SocketSsl.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <Generated/CpAvOpenhomeOrgPlaylist1.h>
#include <OpenHome/Av/Tidal/TidalMetadata.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/OAuth.h>
        
#include <vector>
#include <deque>

namespace OpenHome {
    class Environment;
    class Timer;
namespace Configuration {
    class IConfigInitialiser;
    class ConfigChoice;
}
namespace Av {

class Tidal : public IOAuthAuthenticator
            , public IOAuthTokenPoller
{
    friend class TestTidal;
    friend class TidalPins;
    static const TUint kReadBufferBytes = 4 * 1024;
    static const TUint kWriteBufferBytes = 1024;
    static const TUint kConnectTimeoutMs = 10000; // FIXME - should read this + ProtocolNetwork's equivalent from a single client-changable location

public:
    static const Brn kId;

    static const TUint kMaximumNumberOfShortLivedTokens = 10; //Family account of 4 + one for each of the 6 device pins
    static const TUint kMaximumNumberOfLongLivedTokens = 1; // Currently only need one for Gateway use
    static const TUint kMaximumNumberOfTokens = kMaximumNumberOfShortLivedTokens + kMaximumNumberOfLongLivedTokens;

    static const TUint kMaximumNumberOfPollingJobs = 5;

private:
    static const Brn kHost;
    static const Brn kAuthenticationHost;

    static const TUint kPort = 443;

    static const TUint kMaxStatusBytes = 512;
    static const TUint kMaxPathAndQueryBytes = 512;
    static const TUint kSocketKeepAliveMs = 5000; // close socket after 5s inactivity

    static const Brn kConfigKeyEnabled;
    static const Brn kConfigKeySoundQuality;

public:

    enum class Connection : TByte
    {
        KeepAlive,
        Close
    };

    struct ConfigurationValues
    {
        const Brx& clientId;                            // Used for OAuth authentication, directly by the DS
        const Brx& clientSecret;
        const std::vector<OAuthAppDetails> appDetails;  // All other supported CPs
        const TUint maxSoundQualityOption;              // Used to set the max sound quality. See Tidal.cpp for details
    };


    struct AuthenticationConfig
    {
        TBool fallbackIfTokenNotPresent;
        const Brx& oauthTokenId;
    };

private:
    // Differentiates between which host the socket
    // is currently connected to
    enum class SocketHost : TByte
    {
        None,
        API,
        Auth,
    };

    class UserInfo;

public:
    Tidal(Environment& aEnv, SslContext& aSsl, const ConfigurationValues&, Configuration::IConfigInitialiser& aConfigInitialiser, IThreadPool& aThreadPool);
    ~Tidal();
    TBool TryGetStreamUrl(const Brx& aTrackId, const Brx& aTokenId, Bwx& aStreamUrl);
    TBool TryGetIdsByRequest(IWriter& aWriter, const Brx& aRequestUrl,TUint aLimitPerResponse, TUint aOffset, const AuthenticationConfig& aAuthConfig, Connection aConnection = Connection::KeepAlive);
    TBool TryGetTracksById(IWriter& aWriter, const Brx& aId, TidalMetadata::EIdType aType, TUint aLimit, TUint aOffset, const AuthenticationConfig& aAuthConfig, Connection aConnection = Connection::KeepAlive);
    void Interrupt(TBool aInterrupt);
    void SetTokenProvider(ITokenProvider* aProvider);

public: // IOAuthAuthenticator
     TBool TryGetAccessToken(const Brx& aTokenId,
                             const Brx& aTokenSource,
                             const Brx& aRefreshToken,
                             AccessTokenResponse& aResponse) override;

     TBool TryGetUsernameFromToken(const Brx& aTokenId,
                                   const Brx& aTokenSource,
                                   const Brx& aAccessToken,
                                   IWriter& aUsername) override;

     void OnTokenRemoved(const Brx& aTokenId,
                         const Brx& aTokenSource,
                         const Brx& aAccessToken) override;

public: // IOAuthTokenPoller
     TUint MaxPollingJobs() const override;
     void SetPollResultListener(IOAuthTokenPollResultListener* aListener) override;
     TBool StartLimitedInputFlow(LimitedInputFlowDetails& aDetails) override;
     TBool RequestPollForToken(OAuthPollRequest& aRequest) override;

private:
    TBool TryConnect(SocketHost aHost, TUint aPort);
    TBool TryGetResponseLocked(IWriter& aWriter, const Brx& aHost, Bwx& aPathAndQuery, TUint aLimit, TUint aOffset, const UserInfo& aAuthConfig, Connection aConnection);
    const UserInfo* SelectSuitableToken(const AuthenticationConfig& aAuthConfig) const;
    void WriteRequestHeaders(const Brx& aMethod,
                             const Brx& aHost,
                             const Brx& aPathAndQuery,
                             TUint aPort,
                             Connection aConnection = Connection::Close,
                             TUint aContentLength = 0,
                             const Brx& aAccessToken = Brx::Empty());
    void QualityChanged(Configuration::KeyValuePair<TUint>& aKvp);
    void SocketInactive();
    void DoPollForToken();
    TBool DoTryGetAccessToken(const Brx& aTokenId, const Brx& aTokenSource, const Brx& aRefreshToken, AccessTokenResponse& aResponse);
    TBool DoInheritToken(const Brx& aAccessTokenIn, AccessTokenResponse& aResponse);
private:
    Mutex iLock;
    Mutex iLockConfig;
    SocketSsl iSocket;
    Timer* iTimerSocketActivity;
    Srs<1024> iReaderBuf;
    ReaderUntilS<kReadBufferBytes> iReaderUntil;
    Sws<kWriteBufferBytes> iWriterBuf;
    WriterHttpRequest iWriterRequest;
    ReaderHttpResponse iReaderResponse;
    ReaderHttpEntity iReaderEntity;
    HttpHeaderContentLength iHeaderContentLength;
    HttpHeaderTransferEncoding iHeaderTransferEncoding;
    const Bws<128> iClientId;
    const Bws<128> iClientSecret;
    std::map<Brn, OAuthAppDetails, BufferCmp> iAppDetails;
    TUint iSoundQuality;
    TUint iMaxSoundQuality;
    Bws<1024> iStreamUrl;
    Configuration::ConfigChoice* iConfigEnable;
    Configuration::ConfigChoice* iConfigQuality;
    TUint iSubscriberIdQuality;
    Bwh iUri;
    Uri iRequest;
    Bws<4096> iReqBody; // local variable but too big for the stack
    Bws<4096> iResponseBuffer;
    ITokenProvider* iTokenProvider;
    SocketHost iConnectedHost;
    std::vector<UserInfo> iUserInfos;
    IOAuthTokenPollResultListener* iPollResultListener; // ownership not taken. Reference taken after constructer called
    IThreadPoolHandle* iPollHandle;
    Mutex iPollRequestLock;
    std::deque<OAuthPollRequest> iPollRequests;
};

};  // namespace Av
};  // namespace OpenHome



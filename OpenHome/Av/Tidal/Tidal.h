#pragma once

#include <OpenHome/Av/Credentials.h>
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
        
#include <vector>

namespace OpenHome {
    class Environment;
    class Timer;
namespace Configuration {
    class IConfigInitialiser;
    class ConfigChoice;
}
namespace Av {

class Tidal : public ICredentialConsumer,
              public IOAuthAuthenticator
{
    friend class TestTidal;
    friend class TidalPins;
    static const TUint kReadBufferBytes = 4 * 1024;
    static const TUint kWriteBufferBytes = 1024;
    static const TUint kConnectTimeoutMs = 10000; // FIXME - should read this + ProtocolNetwork's equivalent from a single client-changable location

public:
    static const Brn kId;

    static const TUint kMaximumNumberOfStoredTokens = 10; //Family account of 4 + one for each of the 6 device pins

private:
    static const Brn kHost;
    static const Brn kAuthenticationHost;

    static const TUint kPort = 443;
    static const TUint kGranularityUsername = 128;
    static const TUint kGranularityPassword = 128;


    static const TUint kMaxStatusBytes = 512;
    static const TUint kMaxPathAndQueryBytes = 512;
    static const TUint kSocketKeepAliveMs = 5000; // close socket after 5s inactivity

    static const Brn kConfigKeySoundQuality;

public:

    enum class Connection : TByte
    {
        KeepAlive,
        Close
    };

    struct ConfigurationValues
    {
        const Brx& partnerId;   //Used with Username/Password authentication
        const Brx& clientId;    //Used for OAuth authentication
        const Brx& clientSecret;

        TBool SupportsOAuth() const { return clientId.Bytes() > 0 && clientSecret.Bytes() > 0; }
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
    Tidal(Environment& aEnv, SslContext& aSsl, const ConfigurationValues&, ICredentialsState& aCredentialsState, Configuration::IConfigInitialiser& aConfigInitialiser);
    ~Tidal();
    TBool TryLogin(Bwx& aSessionId);
    TBool TryReLogin(const Brx& aCurrentToken, Bwx& aNewToken);
    TBool TryGetStreamUrl(const Brx& aTrackId, const Brx& aTokenId, Bwx& aStreamUrl);
    TBool TryLogout(const Brx& aSessionId);
    TBool TryGetId(IWriter& aWriter, const Brx& aQuery, TidalMetadata::EIdType aType, const AuthenticationConfig& aAuthConfig, Connection aConnection = Connection::KeepAlive);
    TBool TryGetIds(IWriter& aWriter, const Brx& aMood, TidalMetadata::EIdType aType, TUint aLimitPerResponse, const AuthenticationConfig& aAuthConfig, Connection aConnection = Connection::KeepAlive);
    TBool TryGetIdsByRequest(IWriter& aWriter, const Brx& aRequestUrl,TUint aLimitPerResponse, TUint aOffset, const AuthenticationConfig& aAuthConfig, Connection aConnection = Connection::KeepAlive);
    TBool TryGetTracksById(IWriter& aWriter, const Brx& aId, TidalMetadata::EIdType aType, TUint aLimit, TUint aOffset, const AuthenticationConfig& aAuthConfig, Connection aConnection = Connection::KeepAlive);
    void Interrupt(TBool aInterrupt);
    void SetTokenProvider(ITokenProvider* aProvider);

public: // IOAuthAuthenticator
     TBool TryGetAccessToken(const Brx& aTokenId,
                             const Brx& aRefreshToken,
                             AccessTokenResponse& aResponse) override;

     TBool TryGetUsernameFromToken(const Brx& aTokenId,
                                   const Brx& aAccessToken,
                                   IWriter& aUsername) override;

     void OnTokenRemoved(const Brx& aTokenId,
                         const Brx& aAccessToken) override;

private: // from ICredentialConsumer
    const Brx& Id() const override;
    void CredentialsChanged(const Brx& aUsername, const Brx& aPassword) override;
    void UpdateStatus() override;
    void Login(Bwx& aToken) override;
    void ReLogin(const Brx& aCurrentToken, Bwx& aNewToken) override;

private:
    TBool TryConnect(SocketHost aHost, TUint aPort);
    TBool TryLoginLocked();
    TBool TryLoginLocked(Bwx& aSessionId);
    TBool TryLogoutSession(const TokenType aTokenType, const Brx& aToken);
    TBool TryLogoutLocked(const Brx& aSessionId);
    TBool TryGetSubscriptionLocked();
    TBool TryGetResponse(IWriter& aWriter, const Brx& aHost, Bwx& aPathAndQuery, TUint aLimit, TUint aOffset, const AuthenticationConfig& aAuthConfig, Connection aConnection);
    void WriteRequestHeaders(const Brx& aMethod,
                             const Brx& aHost,
                             const Brx& aPathAndQuery,
                             TUint aPort,
                             Connection aConnection = Connection::Close,
                             TUint aContentLength = 0,
                             const Brx& aAccessToken = Brx::Empty());
    void QualityChanged(Configuration::KeyValuePair<TUint>& aKvp);
    void SocketInactive();
private:
    Mutex iLock;
    Mutex iLockConfig;
    ICredentialsState& iCredentialsState;
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
    const Bws<32> iToken;
    const Bws<128> iClientId;
    const Bws<128> iClientSecret;
    WriterBwh iUsername;
    WriterBwh iPassword;
    TUint iSoundQuality;
    TUint iMaxSoundQuality;
    Bws<16> iUserId;
    Bws<64> iSessionId;
    Bws<8> iCountryCode;
    Bws<1024> iStreamUrl;
    Configuration::ConfigChoice* iConfigQuality;
    TUint iSubscriberIdQuality;
    Bwh iUri;
    Uri iRequest;
    Bws<4096> iReqBody; // local variable but too big for the stack
    Bws<4096> iResponseBuffer;
    ITokenProvider* iTokenProvider;
    SocketHost iConnectedHost;
    std::vector<UserInfo> iUserInfos;
};

};  // namespace Av
};  // namespace OpenHome



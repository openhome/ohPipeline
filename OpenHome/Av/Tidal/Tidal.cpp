#include <OpenHome/Av/Tidal/Tidal.h>
#include <OpenHome/Av/Tidal/TidalMetadata.h>
#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Types.h>
#include <OpenHome/SocketSsl.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Timer.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Av/Debug.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Av/Utils/FormUrl.h>
#include <OpenHome/Json.h>
#include <OpenHome/Net/Core/CpDeviceDv.h>
#include <OpenHome/Private/Converter.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Configuration;

static const TChar* kSoundQualities[3] = {"LOW", "HIGH", "LOSSLESS"};
static const TUint kNumSoundQualities = sizeof(kSoundQualities) / sizeof(kSoundQualities[0]);


const Brn Tidal::kHost("api.tidalhifi.com");
const Brn Tidal::kAuthenticationHost("auth.tidal.com");

const Brn Tidal::kId("tidalhifi.com");

const Brn Tidal::kConfigKeySoundQuality("tidalhifi.com.SoundQuality");


// UserInfo
/* Associated information connected to an OAuthToken.
 * Populated when an AccessToken is generated to avoid later requests
 * to obtain this information */
class Tidal::UserInfo
{
    public:
        UserInfo()
            : iPopulated(false)
            , iUsername(64)
            , iTokenId(32)
        { }

        const TBool Populated() const { return iPopulated; };
        const TUint UserId() const { return iUserId; };
        const Brx& CountryCode() const { return iCountryCode; };
        const Brx& Username() const { return iUsername.Buffer(); }
        const Brx& TokenId() const { return iTokenId.Buffer(); }

        void Populate(const Brx& aTokenId,
                      TUint aUserId,
                      const Brx& aUsername,
                      const Brx& aCountryCode)
        {
            iPopulated = true;
            iUserId  = aUserId;

            iTokenId.Reset();
            iTokenId.Write(aTokenId);

            iUsername.Reset();
            iUsername.Write(aTokenId);

            iCountryCode.Replace(aCountryCode);
        }

        void Empty()
        {
            iPopulated = false;
            iUserId = 0;

            iTokenId.Reset();
            iUsername.Reset();

            iCountryCode.Replace("");
        }

    private:
        TBool iPopulated;
        TUint iUserId;
        Bws<4> iCountryCode;
        WriterBwh iUsername;
        WriterBwh iTokenId;
};



// Tidal

Tidal::Tidal(Environment& aEnv, SslContext& aSsl, const ConfigurationValues& aTidalConfig, ICredentialsState& aCredentialsState, Configuration::IConfigInitialiser& aConfigInitialiser)
    : iLock("TDL1")
    , iLockConfig("TDL2")
    , iCredentialsState(aCredentialsState)
    , iSocket(aEnv, aSsl, kReadBufferBytes)
    , iReaderBuf(iSocket)
    , iReaderUntil(iReaderBuf)
    , iWriterBuf(iSocket)
    , iWriterRequest(iSocket)
    , iReaderResponse(aEnv, iReaderUntil)
    , iReaderEntity(iReaderUntil)
    , iToken(aTidalConfig.partnerId)
    , iClientId(aTidalConfig.clientId)
    , iClientSecret(aTidalConfig.clientSecret)
    , iUsername(kGranularityUsername)
    , iPassword(kGranularityPassword)
    , iUri(1024)
    , iTokenProvider(nullptr)
    , iConnectedHost(SocketHost::None)
    , iUserInfos(kMaximumNumberOfStoredTokens)
{
    iTimerSocketActivity = new Timer(aEnv, MakeFunctor(*this, &Tidal::SocketInactive), "Tidal");

    iReaderResponse.AddHeader(iHeaderContentLength);
    iReaderResponse.AddHeader(iHeaderTransferEncoding);

    const int arr[] = {0, 1, 2};
    std::vector<TUint> qualities(arr, arr + sizeof(arr)/sizeof(arr[0]));
    iConfigQuality = new ConfigChoice(aConfigInitialiser, kConfigKeySoundQuality, qualities, 2);
    iMaxSoundQuality = kNumSoundQualities - 1;
    iSubscriberIdQuality = iConfigQuality->Subscribe(MakeFunctorConfigChoice(*this, &Tidal::QualityChanged));
}

Tidal::~Tidal()
{
    delete iTimerSocketActivity;
    iConfigQuality->Unsubscribe(iSubscriberIdQuality);
    delete iConfigQuality;
}


void Tidal::SetTokenProvider(ITokenProvider* aProvider)
{
    iTokenProvider = aProvider;
}


TBool Tidal::TryLogin(Bwx& aSessionId)
{
    iTimerSocketActivity->Cancel(); // socket automatically closed by call below
    AutoMutex _(iLock);
    return TryLoginLocked(aSessionId);
}

TBool Tidal::TryReLogin(const Brx& aCurrentToken, Bwx& aNewToken)
{
    iTimerSocketActivity->Cancel(); // socket automatically closed by call below
    AutoMutex _(iLock);
    if (iSessionId.Bytes() == 0 || aCurrentToken == iSessionId) {
        (void)TryLogoutLocked(aCurrentToken);
        if (TryLoginLocked()) {
            aNewToken.Replace(iSessionId);
            return true;
        }
        return false;
    }
    aNewToken.Replace(iSessionId);
    return true;
}

TBool Tidal::TryGetStreamUrl(const Brx& aTrackId,
                             const Brx& aTokenId,
                             Bwx& aStreamUrl)
{
    iTimerSocketActivity->Cancel(); // socket automatically closed by call below
    AutoMutex _(iLock);
    TBool success = false;

    const TBool isUsingOAuth = aTokenId.Bytes() > 0;

    if (!TryConnect(SocketHost::API, kPort))
    {
        LOG_ERROR(kPipeline, "Tidal::TryGetStreamUrl() - connection failure.\n");
        return false;
    }

    ServiceToken accessToken;
    if (isUsingOAuth)
    {
        if (!iTokenProvider->TryGetToken(aTokenId, accessToken))
        {
            LOG_ERROR(kPipeline, "Tidal::TryGetStreamUrl() - token '%.*s' not available.\n", PBUF(aTokenId));
            return false;
        }
    }

    AutoSocketSsl __(iSocket);

    Bws<256> pathAndQuery("/v1/tracks/");

    pathAndQuery.Append(aTrackId);
    pathAndQuery.Append("/playbackinfopostpaywall?");

    pathAndQuery.Append("playbackmode=STREAM");     // Options: STREAM, OFFLINE
    pathAndQuery.Append("&assetpresentation=FULL"); // Options: FULL, PREVIEW

    if (!isUsingOAuth)
    {
        pathAndQuery.Append("&sessionId=");
        pathAndQuery.Append(iSessionId);
    }

    pathAndQuery.Append("&audioquality=");
    iLockConfig.Wait();
    pathAndQuery.Append(Brn(kSoundQualities[iSoundQuality]));
    iLockConfig.Signal();

    LOG_TRACE(kPipeline, "~ Tidal::TryGetStreamUrl() - Resource: %.*s\n", PBUF(pathAndQuery));

    Brn url;
    try
    {
        if (isUsingOAuth)
        {
            WriteRequestHeaders(Http::kMethodGet, kHost, pathAndQuery, kPort, Connection::Close, 0, accessToken.token);
        }
        else
        {
            WriteRequestHeaders(Http::kMethodGet, kHost, pathAndQuery, kPort);
        }

        iReaderResponse.Read();
        const TUint code = iReaderResponse.Status().Code();

        iResponseBuffer.Replace(Brx::Empty());
        WriterBuffer writer(iResponseBuffer);

        iReaderEntity.ReadAll(writer,
                              iHeaderContentLength,
                              iHeaderTransferEncoding,
                              ReaderHttpEntity::Mode::Client);

        if (code != 200) 
        {
            LOG_ERROR(kPipeline, 
                      "Http error - %d - in response to Tidal GetStreamUrl.  Some/all of response is:\n%.*s\n", 
                      code,
                      PBUF(iResponseBuffer));
            THROW(ReaderError);
        }

        JsonParser playbackInfoParser;
        playbackInfoParser.ParseAndUnescape(iResponseBuffer);

        LOG_TRACE(kPipeline,
                  "Tidal::TryGetStreamUrl - Requested TrackId: %.*s, received: %.*s (Quality: %.*s)\n",
                  PBUF(aTrackId),
                  PBUF(playbackInfoParser.String("trackId")),
                  PBUF(playbackInfoParser.String("audioQuality")));

        Brn manifestType = playbackInfoParser.String("manifestMimeType");

        /* 4 types of manifest:
         * - EMU: Link that points to the actual manifest (Not Implemented)
         * - BTS: Old streaming API wrapped up in the new payload
         * - 2x MPEG LiveStreaming (Assuming for video content. Not implemented) */
        if (manifestType == Brn("application/vnd.tidal.bts"))
        {
            LOG_TRACE(kPipeline, "Tidal::TryGetStreamUrl - Manifest type is 'Basic (BTS)'\n");

            Brn manifest = playbackInfoParser.String("manifest");
            Bwn manifestW(manifest.Ptr(), manifest.Bytes(), manifest.Bytes());  // We can reuse the underlying buffer provided by iResponseBuffer
            Converter::FromBase64(manifestW);

            JsonParser manifestParser;
            manifestParser.ParseAndUnescape(manifestW);

            LOG_TRACE(kPipeline,
                      "Tidal::TryGetStreamUrl - Parsed manifest. Audio mimeType: %.*s, encryption: %.*s\n",
                      PBUF(manifestParser.String("mimeType")),
                      PBUF(manifestParser.String("encryptionType")));

            auto urlParser = JsonParserArray::Create(manifestParser.String("urls"));
            aStreamUrl.Replace(urlParser.NextString());

            LOG(kMedia, "Tidal::TryGetStreamUrl aStreamUrl: %.*s\n", PBUF(aStreamUrl));
            success = true;
        }
        else
        {
            LOG_ERROR(kPipeline, "Unknown manifest type (%.*s) in Tidal::TryGetStreamUrl\n", PBUF(manifestType));
        }
    }
    catch (HttpError&) {
        LOG_ERROR(kPipeline, "HttpError in Tidal::TryGetStreamUrl\n");
    }
    catch (ReaderError&) {
        LOG_ERROR(kPipeline, "ReaderError in Tidal::TryGetStreamUrl\n");
    }
    catch (WriterError&) {
        LOG_ERROR(kPipeline, "WriterError in Tidal::TryGetStreamUrl\n");
    }
    return success;
}

TBool Tidal::TryLogout(const Brx& aSessionId)
{
    iTimerSocketActivity->Cancel(); // socket automatically closed by call below
    AutoMutex _(iLock);
    return TryLogoutLocked(aSessionId);
}

TBool Tidal::TryGetId(IWriter& aWriter,
                      const Brx& aQuery,
                      TidalMetadata::EIdType aType,
                      const AuthenticationConfig& aAuthConfig,
                      Connection aConnection)
{
    Bws<kMaxPathAndQueryBytes> pathAndQuery("/v1/");

    pathAndQuery.Append("search/?query=");
    Uri::Escape(pathAndQuery, aQuery);
    pathAndQuery.Append("&types=");
    pathAndQuery.Append(TidalMetadata::IdTypeToString(aType));

    return TryGetResponse(aWriter, kHost, pathAndQuery, 1, 0, aAuthConfig, aConnection);
}

TBool Tidal::TryGetIds(IWriter& aWriter,
                       const Brx& aMood,
                       TidalMetadata::EIdType aType,
                       TUint aLimitPerResponse,
                       const AuthenticationConfig& aAuthConfig,
                       Connection aConnection)
{
    Bws<kMaxPathAndQueryBytes> pathAndQuery("/v1/");

    if (aType == TidalMetadata::eMood) {
        // will return the most recently updated playlist for the given mood
        pathAndQuery.Append(TidalMetadata::IdTypeToString(aType));
        pathAndQuery.Append("/");
        pathAndQuery.Append(aMood);
        pathAndQuery.Append(Brn("/playlists?&order=DATE&orderDirection=DESC"));
    }
    else if (aType == TidalMetadata::eSavedPlaylist) {
        // will return the latest saved playlist
        pathAndQuery.Append(TidalMetadata::kIdTypeUserSpecific);
        pathAndQuery.Append("/");
        pathAndQuery.Append(iUserId);
        pathAndQuery.Append(Brn("/playlists?&order=DATE&orderDirection=DESC"));
    }
    else if (aType == TidalMetadata::eSmartExclusive) {
        // will return the latest exclusive playlist
        pathAndQuery.Append(TidalMetadata::IdTypeToString(aType));
        pathAndQuery.Append(Brn("/playlists?&order=DATE&orderDirection=DESC"));
    }
    else if (aType == TidalMetadata::eFavorites) {
        pathAndQuery.Append(TidalMetadata::kIdTypeUserSpecific);
        pathAndQuery.Append("/");
        pathAndQuery.Append(iUserId);
        pathAndQuery.Append("/");
        pathAndQuery.Append(TidalMetadata::IdTypeToString(aType));
        pathAndQuery.Append(Brn("/albums?order=NAME&orderDirection=ASC"));
    }

    return TryGetResponse(aWriter, kHost, pathAndQuery, aLimitPerResponse, 0, aAuthConfig, aConnection);
}

TBool Tidal::TryGetTracksById(IWriter& aWriter,
                              const Brx& aId,
                              TidalMetadata::EIdType aType,
                              TUint aLimit,
                              TUint aOffset,
                              const AuthenticationConfig& aAuthConfig,
                              Connection aConnection)
{
    Bws<kMaxPathAndQueryBytes> pathAndQuery("/v1/");
    if (aType == TidalMetadata::eMood || aType == TidalMetadata::eSmartExclusive || aType == TidalMetadata::eSavedPlaylist) {
        pathAndQuery.Append(TidalMetadata::IdTypeToString(TidalMetadata::ePlaylist));
    }
    else {
        if (aId == TidalMetadata::kIdTypeUserSpecific) {
            pathAndQuery.Append(aId);
            pathAndQuery.Append("/");
            pathAndQuery.Append(iUserId);
            pathAndQuery.Append("/");
        }
        pathAndQuery.Append(TidalMetadata::IdTypeToString(aType));
    }
    if ((aId != TidalMetadata::kIdTypeSmart && aId != TidalMetadata::kIdTypeUserSpecific) || aType == TidalMetadata::eSmartExclusive) {
        pathAndQuery.Append("/");
        pathAndQuery.Append(aId);
    }
    switch (aType) {
        case TidalMetadata::eArtist: pathAndQuery.Append(Brn("/toptracks?")); break;
        case TidalMetadata::eGenre:
        case TidalMetadata::eSmartNew:
        case TidalMetadata::eSmartRecommended:
        case TidalMetadata::eSmartTop20:
        case TidalMetadata::eSmartRising:
        case TidalMetadata::eSmartDiscovery:
        case TidalMetadata::eAlbum: pathAndQuery.Append(Brn("/tracks?")); break;
        case TidalMetadata::eFavorites: pathAndQuery.Append(Brn("/tracks?order=NAME&orderDirection=ASC")); break;
        case TidalMetadata::eMood:
        case TidalMetadata::eSmartExclusive:
        case TidalMetadata::eSavedPlaylist:
        case TidalMetadata::ePlaylist: pathAndQuery.Append(Brn("/items?order=INDEX&orderDirection=ASC")); break;
        case TidalMetadata::eTrack: pathAndQuery.Append(Brn("?")); break;
        case TidalMetadata::eNone: break;
    }

    return TryGetResponse(aWriter, kHost, pathAndQuery, aLimit, aOffset, aAuthConfig, aConnection);
}

TBool Tidal::TryGetIdsByRequest(IWriter& aWriter,
                                const Brx& aRequestUrl,
                                TUint aLimitPerResponse,
                                TUint aOffset,
                                const AuthenticationConfig& aAuthConfig,
                                Connection aConnection)
{
    iUri.SetBytes(0);
    Uri::Unescape(iUri, aRequestUrl);
    iRequest.Replace(iUri);
    iUri.Replace(iRequest.PathAndQuery());
    return TryGetResponse(aWriter, iRequest.Host(), iUri, aLimitPerResponse, aOffset, aAuthConfig, aConnection);
}

TBool Tidal::TryGetResponse(IWriter& aWriter,
                            const Brx& aHost,
                            Bwx& aPathAndQuery,
                            TUint aLimit,
                            TUint aOffset,
                            const AuthenticationConfig& aAuthConfig,
                            Connection aConnection)
{
    // First check all our tokens are correct and valid!
    TBool hasToken = false;
    WriterBwh tokenIdWriter(128);
    tokenIdWriter.Reset();

    if (aAuthConfig.oauthTokenId.Bytes() > 0)
    {
        Log::Print("Tidal: TGR -> Has OAuth token Id\n");
        hasToken = iTokenProvider != nullptr && iTokenProvider->HasToken(aAuthConfig.oauthTokenId);
        if (hasToken)
        {
            Log::Print("Tidal: TGR -> OAuth Token was found.\n");
            tokenIdWriter.Write(aAuthConfig.oauthTokenId);
        }
    }
    else if (aAuthConfig.oauthTokenId.Bytes() == 0 && aAuthConfig.fallbackIfTokenNotPresent)
    {
        hasToken = iTokenProvider != nullptr && iTokenProvider->TryGetFirstValidTokenId(tokenIdWriter);
    }

    if (!hasToken && !aAuthConfig.fallbackIfTokenNotPresent)
    {
        Log::Print("Tidal: TGR -> Token not found and not falling back.\n");
        return false;
    }


    iTimerSocketActivity->Cancel();
    AutoMutex _(iLock);
    TBool success = false;
    if (!TryConnect(SocketHost::API, kPort)) {
        LOG_ERROR(kMedia, "Tidal::TryGetResponse - connection failure\n");
        return false;
    }

    //TODO: If we're appending the ? then limit should be appended without prefix '&'
    if (!Ascii::Contains(aPathAndQuery, '?')) {
        aPathAndQuery.Append("?");
    }

    aPathAndQuery.Append("&limit=");
    Ascii::AppendDec(aPathAndQuery, aLimit);
    aPathAndQuery.Append("&offset=");
    Ascii::AppendDec(aPathAndQuery, aOffset);

    if (!Ascii::Contains(aPathAndQuery, Brn("countryCode")))
    {
        if (hasToken)
        {
            Log::Print("Write Tidal request: Using OAuth and has token of id: %.s\n", PBUF(tokenIdWriter.Buffer()));

            TBool success = false;
            for(auto& v : iUserInfos)
            {
                if (v.TokenId() == tokenIdWriter.Buffer())
                {
                    aPathAndQuery.Append("&countryCode=");
                    aPathAndQuery.Append(v.CountryCode());
                    success = true;
                    break;
                }
            }

            if (!success)
            {
                //TODO: Handle error here...
                Log::Print("No country code found...\n");
            }
        }
        else
        {
            aPathAndQuery.Append("&countryCode=");
            aPathAndQuery.Append(iCountryCode);
        }
    }

    if (!hasToken && !Ascii::Contains(aPathAndQuery, Brn("sessionId"))) {
        aPathAndQuery.Append("&sessionId=");
        aPathAndQuery.Append(iSessionId);
    }

    try {
        Log::Print("Write Tidal request: https://%.*s%.*s\n", PBUF(aHost), PBUF(aPathAndQuery));

        if (hasToken)
        {
            ServiceToken accessToken;
            if (!iTokenProvider->TryGetToken(tokenIdWriter.Buffer(), accessToken)) {
                THROW(OAuthTokenIdNotFound);
            }

            WriteRequestHeaders(Http::kMethodGet, aHost, aPathAndQuery, kPort, aConnection, 0, accessToken.token);
        }
        else
        {
            WriteRequestHeaders(Http::kMethodGet, aHost, aPathAndQuery, kPort, aConnection);
        }

        iReaderResponse.Read();
        const TUint code = iReaderResponse.Status().Code();
        if (code != 200) {
            LOG_ERROR(kPipeline, "Http error - %d - in response to Tidal TryGetResponse.  Some/all of response is:\n", code);
            Brn buf = iReaderUntil.Read(kReadBufferBytes);
            LOG_ERROR(kPipeline, "%.*s\n", PBUF(buf));
            THROW(ReaderError);
        }  
        
        iReaderEntity.ReadAll(aWriter,
                              iHeaderContentLength,
                              iHeaderTransferEncoding,
                              ReaderHttpEntity::Mode::Client);

        success = true;
    }
    catch (Exception& ex) {
        LOG_ERROR(kPipeline, "%s in Tidal::TryGetResponse\n", ex.Message());
    }
    if (aConnection == Connection::Close) {
        iSocket.Close();
    }
    else { // KeepAlive
        iTimerSocketActivity->FireIn(kSocketKeepAliveMs);
    }
    return success;
}

void Tidal::Interrupt(TBool aInterrupt)
{
    iSocket.Interrupt(aInterrupt);
}

const Brx& Tidal::Id() const
{
    return kId;
}

void Tidal::CredentialsChanged(const Brx& aUsername, const Brx& aPassword)
{
    AutoMutex _(iLockConfig);
    iUsername.Reset();
    iUsername.Write(aUsername);
    iPassword.Reset();
    iPassword.Write(aPassword);
}

void Tidal::UpdateStatus()
{
    AutoMutex _(iLock);
    (void)TryLogoutLocked(iSessionId);
    iLockConfig.Wait();
    const TBool noCredentials = (iUsername.Buffer().Bytes() == 0 && iPassword.Buffer().Bytes() == 0);
    iLockConfig.Signal();
    if (noCredentials) {
        iCredentialsState.SetState(kId, Brx::Empty(), Brx::Empty());
    }
    else {
        (void)TryLoginLocked();
    }
}

void Tidal::Login(Bwx& aToken)
{
    AutoMutex _(iLock);
    if (iSessionId.Bytes() > 0) {
        aToken.Replace(iSessionId);
        return;
    }
    if (!TryLoginLocked(aToken)) {
        THROW(CredentialsLoginFailed);
    }
}

void Tidal::ReLogin(const Brx& aCurrentToken, Bwx& aNewToken)
{
    if (!TryReLogin(aCurrentToken, aNewToken)) {
        THROW(CredentialsLoginFailed);
    }
}

TBool Tidal::TryConnect(SocketHost aHost, 
                        TUint aPort)
{
    const TBool isConnected = iSocket.IsConnected();
    const TBool isMatchingHost = aHost == iConnectedHost;

    if (isConnected && isMatchingHost)
    {        
        return true;
    }

    if (isConnected)
    {
        iSocket.Close();
    }

    
    Endpoint ep;
    Brn host;

    try
    {
        switch(aHost)
        {
            case SocketHost::API:
            {
                host = kHost;
                break;
            }
            case SocketHost::Auth:
            {
                host = kAuthenticationHost;
                break;
            }
            default:
            {
                ASSERTS();
            }
        }

        ep.SetAddress(host);
        ep.SetPort(aPort);

        iSocket.Connect(ep, host, kConnectTimeoutMs);
    }
    catch (NetworkTimeout&)
    {
        iSocket.Close();
        return false;
    }
    catch (NetworkError&)
    {
        iSocket.Close();
        return false;
    }

    iConnectedHost = aHost;
    return true;
}

TBool Tidal::TryLoginLocked(Bwx& aSessionId)
{
    if (!TryLoginLocked()) {
        aSessionId.SetBytes(0);
        return false;
    }
    aSessionId.Replace(iSessionId);
    return true;
}

TBool Tidal::TryLoginLocked()
{
    TBool updatedStatus = false;
    Bws<80> error;
    iSessionId.SetBytes(0);
    TBool success = false;
    if (!TryConnect(SocketHost::API, kPort)) {
        LOG_ERROR(Debug::kPipeline, "Tidal::TryLogin - connection failure\n");
        iCredentialsState.SetState(kId, Brn("Login Error (Connection Failed): Please Try Again."), Brx::Empty());
        return false;
    }
    {
        AutoSocketSsl _(iSocket);
        iReqBody.Replace(Brn("username="));
        WriterBuffer writer(iReqBody);
        iLockConfig.Wait();
        FormUrl::Encode(writer, iUsername.Buffer());
        iReqBody.Append(Brn("&password="));
        FormUrl::Encode(writer, iPassword.Buffer());
        iLockConfig.Signal();

        Bws<128> pathAndQuery("/v1/login/username?token=");
        pathAndQuery.Append(iToken);
        try {
            WriteRequestHeaders(Http::kMethodPost, kHost, pathAndQuery, kPort, Connection::Close, iReqBody.Bytes());
            iWriterBuf.Write(iReqBody);
            iWriterBuf.WriteFlush();

            iReaderResponse.Read();

            const TUint code = iReaderResponse.Status().Code();

            iResponseBuffer.Replace(Brx::Empty());
            WriterBuffer writerResponse(iResponseBuffer);

            iReaderEntity.ReadAll(writerResponse,
                                  iHeaderContentLength,
                                  iHeaderTransferEncoding,
                                  ReaderHttpEntity::Mode::Client);

            if (code != 200)
            {
                if (iResponseBuffer.Bytes() > 0)
                {
                    iCredentialsState.SetState(kId, iResponseBuffer, Brx::Empty());
                }
                else
                {
                    error.AppendPrintf("Login Error (Response Code %d): Please Try Again.", code);
                    iCredentialsState.SetState(kId, error, Brx::Empty());
                    LOG_ERROR(kPipeline, "HTTP error - %d - in Tidal::TryLogin\n", code);
                }

                updatedStatus = true;
                LOG(kPipeline, "Http error - %d - in response to Tidal login.  Some/all of response is:\n%.*s\n", code, PBUF(iResponseBuffer));
                THROW(ReaderError);
            }

            JsonParser p;
            p.ParseAndUnescape(iResponseBuffer);

            iUserId.Replace(p.String("userId"));
            iSessionId.Replace(p.String("sessionId"));
            iCountryCode.Replace(p.String("countryCode"));

            iCredentialsState.SetState(kId, Brx::Empty(), iCountryCode);
            updatedStatus = true;
            success = true;
        }
        catch (HttpError&) {
            error.Append("Login Error (Http Failure): Please Try Again.");
            LOG_ERROR(kPipeline, "HttpError in Tidal::TryLogin\n");
        }
        catch (ReaderError&) {
            if (error.Bytes() == 0) {
                error.Append("Login Error (Read Failure): Please Try Again.");
            }
            LOG_ERROR(kPipeline, "ReaderError in Tidal::TryLogin\n");
        }
        catch (WriterError&) {
            error.Append("Login Error (Write Failure): Please Try Again.");
            LOG_ERROR(kPipeline, "WriterError in Tidal::TryLogin\n");
        }
    }

    if (success) {
        success = TryGetSubscriptionLocked();
    }
    else if (!updatedStatus) {
        iCredentialsState.SetState(kId, error, Brx::Empty());
    }
    return success;
}

TBool Tidal::TryLogoutLocked(const Brx& aSessionId)
{
    if (aSessionId.Bytes() == 0)
    {
        return true;
    }

    TBool success = TryLogoutSession(TokenType::UsernamePassword, aSessionId);
    if (success)
    {
        iSessionId.SetBytes(0);
    }

    return success;
}

TBool Tidal::TryGetSubscriptionLocked()
{
    TBool updateStatus = false;
    Bws<kMaxStatusBytes> error;
    TBool success = false;
    if (!TryConnect(SocketHost::API, kPort)) {
        LOG_ERROR(kMedia, "Tidal::TryGetSubscriptionLocked - connection failure\n");
        iCredentialsState.SetState(kId, Brn("Subscription Error (Connection Failed): Please Try Again."), Brx::Empty());
        return false;
    }
    AutoSocketSsl _(iSocket);

    Bws<128> pathAndQuery("/v1/users/");
    pathAndQuery.Append(iUserId);
    pathAndQuery.Append("/subscription?sessionId=");
    pathAndQuery.Append(iSessionId);

    try {
        WriteRequestHeaders(Http::kMethodGet, kHost, pathAndQuery, kPort);

        iReaderResponse.Read();
        const TUint code = iReaderResponse.Status().Code();

        iResponseBuffer.Replace(Brx::Empty());
        WriterBuffer writer(iResponseBuffer);

        iReaderEntity.ReadAll(writer,
                              iHeaderContentLength,
                              iHeaderTransferEncoding,
                              ReaderHttpEntity::Mode::Client);
        if (code != 200)
        {
            if (iResponseBuffer.Bytes() > 0)
            {
                error.Replace(iResponseBuffer.Ptr(), kMaxStatusBytes);
            }
            else
            {
                error.AppendPrintf("Subscription Error (Response Code %d): Please Try Again.", code);
            }

            updateStatus = true;
            LOG_ERROR(kPipeline, "Http error - %d - in response to Tidal subscription.  Some/all of response is:\n%.*s\n", code, PBUF(iResponseBuffer));
            THROW(ReaderError);
        }

        JsonParser p;
        p.ParseAndUnescape(iResponseBuffer);

        Brn quality = p.String("highestSoundQuality");

        for (TUint i=0; i<kNumSoundQualities; i++)
         {
            if (Brn(kSoundQualities[i]) == quality)
            {
                iMaxSoundQuality = i;
                break;
            }
        }
        
        iSoundQuality = std::min(iSoundQuality, iMaxSoundQuality);
        updateStatus = false;
        success = true;
    }
    catch (HttpError&) {
        error.Append("Subscription Error (Http Failure): Please Try Again.");
        LOG_ERROR(kPipeline, "HttpError in Tidal::TryGetSubscriptionLocked\n");
    }
    catch (ReaderError&) {
        error.Append("Subscription Error (Read Failure): Please Try Again.");
        LOG_ERROR(kPipeline, "ReaderError in Tidal::TryGetSubscriptionLocked\n");
    }
    catch (WriterError&) {
        error.Append("Subscription Error (Write Failure): Please Try Again.");
        LOG_ERROR(kPipeline, "WriterError in Tidal::TryGetSubscriptionLocked\n");
    }
    if (updateStatus) {
        iCredentialsState.SetState(kId, error, Brx::Empty());
    }
    return success;
}

void Tidal::WriteRequestHeaders(const Brx& aMethod,
                                const Brx& aHost,
                                const Brx& aPathAndQuery,
                                TUint aPort,
                                Connection aConnection,
                                TUint aContentLength,
                                const Brx& aAccessToken)
{
    iWriterRequest.WriteMethod(aMethod, aPathAndQuery, Http::eHttp11);
    Http::WriteHeaderHostAndPort(iWriterRequest, aHost, aPort);

    if (aContentLength > 0) 
    {
        Http::WriteHeaderContentLength(iWriterRequest, aContentLength);
    }

    Http::WriteHeaderContentType(iWriterRequest, Brn("application/x-www-form-urlencoded"));

    if (aConnection == Connection::Close)
    {
        Http::WriteHeaderConnectionClose(iWriterRequest);
    }

    if (aAccessToken != Brx::Empty())
    {
        OAuth::WriteAccessTokenHeader(iWriterRequest, aAccessToken);
    }

    iWriterRequest.WriteFlush();
}


TBool Tidal::TryGetAccessToken(const Brx& aTokenId,
                               const Brx& aRefreshToken,
                               AccessTokenResponse& aResponse)
{
    //TODO: All other methods tend to lock round the credentials.
    //      now that credentials are seperate, we might need to introduce
    //      a second mutex to lock around the socket, preventing multiple
    //      threads from accessing this at once.
    AutoMutex m(iLock);

    const Brn kTidalScope("r_usr+w_usr+w_sub");

    iTimerSocketActivity->Cancel(); //Socket automatically closed by call below

    if (!TryConnect(SocketHost::Auth, kPort))
    {
        LOG_ERROR(kOAuth, "Tidal::TryGetAccessToken() - connection failure.\n");
        return false;
    }

    // Write request
    iReqBody.Replace(Brx::Empty());
    WriterBuffer writer(iReqBody);

    const Brn path("/v1/oauth2/token");

    OAuth::ConstructRefreshTokenRequestBody(writer,
                                            aRefreshToken,
                                            iClientId,
                                            iClientSecret,
                                            kTidalScope);

    AutoSocketSsl _(iSocket);

    try
    {
        WriteRequestHeaders(Http::kMethodPost, kAuthenticationHost, path, 443, Connection::Close, iReqBody.Bytes());

        iWriterBuf.Write(iReqBody);
        iWriterBuf.WriteFlush();

        iReaderResponse.Read();

        const TUint code = iReaderResponse.Status()
                                          .Code();

        iResponseBuffer.Replace(Brx::Empty());
        WriterBuffer writer2(iResponseBuffer);

        iReaderEntity.ReadAll(writer2,
                              iHeaderContentLength,
                              iHeaderTransferEncoding,
                              ReaderHttpEntity::Mode::Client);

        JsonParser parser;
        parser.ParseAndUnescape(iResponseBuffer);


        if (code != 200)
        {
            const Brx& error = parser.String(OAuth::kErrorResponseFieldError);
            const Brx& errorDesc = parser.StringOptional(OAuth::kErrorResponseFieldErrorDescription);
            const TBool hasDesc = errorDesc != Brx::Empty();

            const Brn noDescMsg("< No description present >");

            LOG_ERROR(kOAuth,
                      "Tidal::TryGetAccessToken() ~ Failed to refresh access token.\n- HttpCode: %u\n- Error: %.*s\n- Message: %.*s\n",
                      code,
                      PBUF(error),
                      PBUF(hasDesc ? errorDesc : noDescMsg));

            return false;
        }

        const Brx& accessToken = parser.String(OAuth::kTokenResponseFieldAccessToken);
        const TUint expiry = (TUint)parser.Num(OAuth::kTokenResponseFieldTokenExpiry);

        // Make sure to populate response value
        aResponse.accessToken.Set(accessToken);
        aResponse.tokenExpiry = expiry;

        // User information is also contained within our response
        // which is needed for future API requests.
        JsonParser parserUser;
        parserUser.Parse(parser.String("user"));

        const TUint userId = parserUser.Num("userId");
        const Brx& countryCode = parserUser.String("countryCode");
        const Brx& username = parserUser.String("username");

        // Store our user info internally for future API calls...
        for(auto& v : iUserInfos)
        {
            if (!v.Populated())
            {
                v.Populate(aTokenId, userId, username, countryCode);
                break;
            }
        }

        //FIX ME: Need to handle JSON exceptions that might be thrown by this...
        return true;
    }
    catch (HttpError&)
    {
        LOG_ERROR(kOAuth, "HttpError in Tidal::TryGetAccessToken\n");
    }
    catch (ReaderError&) 
    {
        LOG_ERROR(kOAuth, "ReaderError in Tidal::TryGetAccessToken\n");
    }
    catch (WriterError&)
    {
        LOG_ERROR(kOAuth, "WriterError in Tidal::TryGetAccessToken\n");
    }

    return false;
}


TBool Tidal::TryGetUsernameFromToken(const Brx& aTokenId,
                                     const Brx& /*aAccessToken*/,
                                     IWriter& aUsername)
{
    TBool success = false;
    AutoMutex m(iLock);
    Brn tokenComp(aTokenId);

    for(auto& v : iUserInfos)
    {
        if (v.TokenId() == aTokenId)
        {
            aUsername.Write(v.TokenId());
            success = true;
            break;
        }
    }

    return success;
}


void Tidal::OnTokenRemoved(const Brx& aTokenId,
                           const Brx& aAccessToken)
{
    LOG(kOAuth, "Tidal::OnTokenRemoved() - %.*s\n", PBUF(aTokenId));

    // TODO: All other methods lock around the credentials to prevent multi-threaded
    //       access. We could create a socket lock as well as credential locking
    AutoMutex m(iLock);

    for(auto& v : iUserInfos)
    {
        if (v.TokenId() == aTokenId)
        {
            v.Empty();
            break;
        }
    }

    (void)TryLogoutSession(TokenType::OAuth, aAccessToken);
}



TBool Tidal::TryLogoutSession(const TokenType aTokenType, const Brx& aToken)
{
    if (aToken.Bytes() == 0)
    {
        return true;
    }


    if (!TryConnect(SocketHost::API, kPort))
    {
        LOG_ERROR(kOAuth, "Tidal: connection failure\n");
        return true;
    }

    AutoSocketSsl _(iSocket);
    TBool success = false;
    Bws<64> pathAndQuery("/v1/logout");

    if (aTokenType == TokenType::UsernamePassword)
    {
        pathAndQuery.Append("?sessionId=");
        pathAndQuery.Append(aToken);
    }

    try
    {
        WriteRequestHeaders(Http::kMethodPost,
                            kHost,
                            pathAndQuery,
                            kPort,
                            Connection::Close,
                            0,
                            aTokenType == TokenType::OAuth ? aToken : Brx::Empty());

        iReaderResponse.Read();
        const TUint code = iReaderResponse.Status().Code();
        if (code < 200 || code >= 300)
        {
            LOG_ERROR(kOAuth, "Http error - %d - in response to Tidal logout.  Some/all of response is:\n", code);
            Brn buf = iReaderUntil.Read(kReadBufferBytes);
            LOG_ERROR(kOAuth, "%.*s\n", PBUF(buf));
            THROW(ReaderError);
        }

        success = true;
    }
    catch (WriterError&)
    {
        LOG_ERROR(kOAuth, "WriterError from Tidal logout\n");
    }
    catch (ReaderError&)
    {
        LOG_ERROR(kOAuth, "ReaderError from Tidal logout\n");
    }
    catch (HttpError&)
    {
        LOG_ERROR(kOAuth, "HttpError from Tidal logout\n");
    }

    return success;
}




void Tidal::QualityChanged(Configuration::KeyValuePair<TUint>& aKvp)
{
    iLockConfig.Wait();
    iSoundQuality = std::min(aKvp.Value(), iMaxSoundQuality);
    iLockConfig.Signal();
}

void Tidal::SocketInactive()
{
    AutoMutex _(iLock);
    iSocket.Close();
}

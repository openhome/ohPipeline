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
#include <OpenHome/ThreadPool.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Configuration;

static const TChar* kSoundQualities[4] = {"LOW", "HIGH", "LOSSLESS", "HI_RES"};

// Staging = XXXX.stage.tidal.com
const Brn Tidal::kHost("api.tidal.com");
const Brn Tidal::kAuthenticationHost("auth.tidal.com");

const Brn Tidal::kId("tidalhifi.com");

const Brn Tidal::kConfigKeyEnabled("tidalhifi.com.Enabled");
const Brn Tidal::kConfigKeySoundQuality("tidalhifi.com.SoundQuality");

const Brn kTidalTokenScope("r_usr+w_usr");

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
            iUsername.Write(aUsername);

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

Tidal::Tidal(Environment& aEnv,
             SslContext& aSsl,
             const ConfigurationValues& aTidalConfig,
             Configuration::IConfigInitialiser& aConfigInitialiser,
             IThreadPool& aThreadPool)
    : iLock("TDL1")
    , iLockConfig("TDL2")
    , iSocket(aEnv, aSsl, kReadBufferBytes)
    , iReaderBuf(iSocket)
    , iReaderUntil(iReaderBuf)
    , iWriterBuf(iSocket)
    , iWriterRequest(iSocket)
    , iReaderResponse(aEnv, iReaderUntil)
    , iReaderEntity(iReaderUntil)
    , iClientId(aTidalConfig.clientId)
    , iClientSecret(aTidalConfig.clientSecret)
    , iUri(1024)
    , iTokenProvider(nullptr)
    , iConnectedHost(SocketHost::None)
    , iUserInfos(kMaximumNumberOfTokens + 1) // Need an extra slot so that incoming tokens can be verified first, before stored internally
    , iPollResultListener(nullptr)
    , iPollRequestLock("TDL3")
    , iPollRequests(0)
{
    for(const auto& v : aTidalConfig.appDetails)
    {
        iAppDetails.insert(std::pair<Brn, OAuthAppDetails>(Brn(v.AppId()), OAuthAppDetails(v.AppId(), v.ClientId(), v.ClientSecret())));
    }

    iTimerSocketActivity = new Timer(aEnv, MakeFunctor(*this, &Tidal::SocketInactive), "Tidal");

    iReaderResponse.AddHeader(iHeaderContentLength);
    iReaderResponse.AddHeader(iHeaderTransferEncoding);

    // Enabled Config value. Previous this was provided to us by the Credentials service but TIDAL is no longer present there.
    std::vector<TUint> choices;
    choices.push_back(ENABLED_NO);
    choices.push_back(ENABLED_YES);
    iConfigEnable = new ConfigChoice(aConfigInitialiser, kConfigKeyEnabled, choices, ENABLED_YES);

    iMaxSoundQuality = std::min(3u, aTidalConfig.maxSoundQualityOption);
    Log::Print("TIDAL: MaxSoundQuality limited to: %u\n", iMaxSoundQuality);

    std::vector<TUint> qualities;
    qualities.reserve(4);
    for(TUint i = 0; i <= iMaxSoundQuality; ++i) {
        qualities.emplace_back(i);
    }

    const TUint defaultOption = qualities.back();
    iConfigQuality = new ConfigChoice(aConfigInitialiser, kConfigKeySoundQuality, qualities, defaultOption);
    iSubscriberIdQuality = iConfigQuality->Subscribe(MakeFunctorConfigChoice(*this, &Tidal::QualityChanged));

    iPollHandle = aThreadPool.CreateHandle(MakeFunctor(*this, &Tidal::DoPollForToken), "Tidal-POLL", ThreadPoolPriority::Low);
}

Tidal::~Tidal()
{
    delete iTimerSocketActivity;
    iConfigQuality->Unsubscribe(iSubscriberIdQuality);

    delete iConfigEnable;
    delete iConfigQuality;

    iPollHandle->Cancel();
    iPollHandle->Destroy();
}


void Tidal::SetTokenProvider(ITokenProvider* aProvider)
{
    iTokenProvider = aProvider;
}


TBool Tidal::TryGetStreamUrl(const Brx& aTrackId,
                             const Brx& aTokenId,
                             Bwx& aStreamUrl)
{
    iTimerSocketActivity->Cancel(); // socket automatically closed by call below


    if (aTokenId.Bytes() == 0) {
        LOG_ERROR(kPipeline, "Tidal::TryGetStreamUrl() - no token ID given.");
        return false;
    }

    AutoMutex _(iLock);
    ServiceToken accessToken;

    if (!iTokenProvider->TryGetToken(aTokenId, accessToken)) {
       LOG_ERROR(kPipeline, "Tidal::TryGetStreamUrl() - token '%.*s' not available.\n", PBUF(aTokenId));
       return false;
    }

    TBool success = false;

    if (!TryConnect(SocketHost::API, kPort))
    {
        LOG_ERROR(kPipeline, "Tidal::TryGetStreamUrl() - connection failure.\n");
        return false;
    }

    AutoSocketSsl __(iSocket);

    Bws<256> pathAndQuery("/v1/tracks/");

    pathAndQuery.Append(aTrackId);
    pathAndQuery.Append("/playbackinfopostpaywall?");

    pathAndQuery.Append("playbackmode=STREAM");     // Options: STREAM, OFFLINE
    pathAndQuery.Append("&assetpresentation=FULL"); // Options: FULL, PREVIEW

    pathAndQuery.Append("&audioquality=");
    iLockConfig.Wait();
    pathAndQuery.Append(Brn(kSoundQualities[iSoundQuality]));
    iLockConfig.Signal();

    LOG_TRACE(kPipeline, "~ Tidal::TryGetStreamUrl() - Resource: %.*s\n", PBUF(pathAndQuery));

    Brn url;
    try
    {
        WriteRequestHeaders(Http::kMethodGet, kHost, pathAndQuery, kPort, Connection::Close, 0, accessToken.token);

        iReaderResponse.Read();
        const TUint code = iReaderResponse.Status().Code();

        iResponseBuffer.Replace(Brx::Empty());
        WriterBuffer writer(iResponseBuffer);

        iReaderEntity.ReadAll(writer,
                              iHeaderContentLength,
                              iHeaderTransferEncoding,
                              ReaderHttpEntity::Mode::Client);

        JsonParser responseParser;

        if (code != 200) 
        {
            LOG_ERROR(kPipeline, 
                      "Http error - %d - in response to Tidal GetStreamUrl.  Some/all of response is:\n%.*s\n", 
                      code,
                      PBUF(iResponseBuffer));

            // Attetmpt to parse some of the response to give slightly better error messages in logs
            try
            {
                responseParser.ParseAndUnescape(iResponseBuffer);

                if (responseParser.HasKey("subStatus"))
                {
                    TInt tidalErrCode = responseParser.Num("subStatus");

                    switch(tidalErrCode)
                    {
                        case 4005: {
                            LOG_ERROR(kPipeline, "Tidal::GetStreamUrl - TIDAL error 4005. 'For some reaon' the asset can't be played.\n");
                            break;
                        }
                        case 4006: {
                            LOG_ERROR(kPipeline, "Tidal::GetStreamUrl - TIDAL error 4006. User is streaming on more than one device. \n");
                            break;
                        }
                        case 4007: {
                            LOG_ERROR(kPipeline, "Tidal::GetStreamUrl - TIDAL error 4007. Track isn't available in user's region. \n");
                            break;
                        }
                        default:
                            break; // No additional info for the other errors
                    }
                }
            }
            catch (...) {
                // Failed to try and parse anything. Already logged out enough information
                // so can ignore this and move on
            }


            THROW(ReaderError);
        }


        responseParser.ParseAndUnescape(iResponseBuffer);

        LOG_TRACE(kPipeline,
                  "Tidal::TryGetStreamUrl - Requested TrackId: %.*s, received: %.*s (Quality: %.*s)\n",
                  PBUF(aTrackId),
                  PBUF(responseParser.String("trackId")),
                  PBUF(responseParser.String("audioQuality")));

        Brn manifestType = responseParser.String("manifestMimeType");

        /* 4 types of manifest:
         * - EMU: Link that points to the actual manifest (Not Implemented)
         * - BTS: Old streaming API wrapped up in the new payload
         * - 2x MPEG LiveStreaming (Assuming for video content. Not implemented) */
        if (manifestType == Brn("application/vnd.tidal.bts"))
        {
            LOG_TRACE(kPipeline, "Tidal::TryGetStreamUrl - Manifest type is 'Basic (BTS)'\n");

            Brn manifest = responseParser.String("manifest");
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
    catch (AssertionFailed&) {
        throw;
    }
    catch (Exception& ex) {
        LOG_ERROR(kPipeline, "Tidal::TryGetStreamUrl %s\n", ex.Message());
    }

    return success;
}

const Tidal::UserInfo* Tidal::SelectSuitableToken(const AuthenticationConfig& aAuthConfig) const
{
    const TBool hasProvidedTokenId = aAuthConfig.oauthTokenId.Bytes() > 0;

    if (hasProvidedTokenId) {
        LOG_TRACE(kMedia, "Tidal::SelectSuitableTokenId -> Provided with OAuth TokenId\n");

        const TBool tokenPresent = iTokenProvider != nullptr
                                    && iTokenProvider->HasToken(aAuthConfig.oauthTokenId);

        if (tokenPresent) {
            LOG_TRACE(kMedia, "Tidal::SelectSuitableTokenId -> OAuth token with ID '%.*s' found.\n", PBUF(aAuthConfig.oauthTokenId));

            for(const auto& element : iUserInfos) {
                if (element.TokenId() == aAuthConfig.oauthTokenId) {
                    return &element;
                }
            }
        }

        return nullptr;
    }
    else {

        if (aAuthConfig.fallbackIfTokenNotPresent) {
            LOG_TRACE(kMedia, "Tidal::SelectSuitableTokenId -> No token id given. Falling back to use first valid token we can find.\n");

            Bws<128> tokenId;
            WriterBuffer w(tokenId);

            const TBool hasFallbackToken = iTokenProvider != nullptr
                                            && iTokenProvider->TryGetFirstValidTokenId(w);

            if (hasFallbackToken) {
                for(const auto& element : iUserInfos) {
                    if (element.TokenId() == tokenId) {
                        return &element;
                    }
                }
            }

            return nullptr;
        }
        else {
            LOG_TRACE(kMedia, "Tidal::SelectSuitableTokenId -> No token id given and fallback was not requested.\n");
            return nullptr;
        }
    }
}

TBool Tidal::TryGetTracksById(IWriter& aWriter,
                              const Brx& aId,
                              TidalMetadata::EIdType aType,
                              TUint aLimit,
                              TUint aOffset,
                              const AuthenticationConfig& aAuthConfig,
                              Connection aConnection)
{
    AutoMutex m(iLock);
    const UserInfo* userInfo = SelectSuitableToken(aAuthConfig);
    if (userInfo == nullptr) {
        return false;
    }

    Bws<kMaxPathAndQueryBytes> pathAndQuery("/v1/");
    if (aType == TidalMetadata::eMood || aType == TidalMetadata::eSmartExclusive || aType == TidalMetadata::eSavedPlaylist) {
        pathAndQuery.Append(TidalMetadata::IdTypeToString(TidalMetadata::ePlaylist));
    }
    else {
        if (aId == TidalMetadata::kIdTypeUserSpecific) {
            pathAndQuery.Append(aId);
            pathAndQuery.Append("/");
            Ascii::AppendDec(pathAndQuery, userInfo->UserId());
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

    return TryGetResponseLocked(aWriter, kHost, pathAndQuery, aLimit, aOffset, *userInfo, aConnection);
}

TBool Tidal::TryGetIdsByRequest(IWriter& aWriter,
                                const Brx& aRequestUrl,
                                TUint aLimitPerResponse,
                                TUint aOffset,
                                const AuthenticationConfig& aAuthConfig,
                                Connection aConnection)
{
    AutoMutex m(iLock);
    const UserInfo* userInfo = SelectSuitableToken(aAuthConfig);
    if (userInfo == nullptr) {
        return false;
    }

    iUri.SetBytes(0);
    Uri::Unescape(iUri, aRequestUrl);
    iRequest.Replace(iUri);
    iUri.Replace(iRequest.PathAndQuery());

    return TryGetResponseLocked(aWriter, iRequest.Host(), iUri, aLimitPerResponse, aOffset, *userInfo, aConnection);
}

TBool Tidal::TryGetResponseLocked(IWriter& aWriter,
                                  const Brx& aHost,
                                  Bwx& aPathAndQuery,
                                  TUint aLimit,
                                  TUint aOffset,
                                  const UserInfo& aUserInfo,
                                  Connection aConnection)
{
    iTimerSocketActivity->Cancel();

    TBool success = false;
    if (!TryConnect(SocketHost::API, kPort)) {
        LOG_ERROR(kMedia, "Tidal::TryGetResponse - connection failure\n");
        return false;
    }

    aPathAndQuery.Append(Ascii::Contains(aPathAndQuery, '?') ? "&limit="
                                                             : "?limit=");
    Ascii::AppendDec(aPathAndQuery, aLimit);
    aPathAndQuery.Append("&offset=");
    Ascii::AppendDec(aPathAndQuery, aOffset);

    if (!Ascii::Contains(aPathAndQuery, Brn("countryCode")))
    {
        aPathAndQuery.Append("&countryCode=");
        aPathAndQuery.Append(aUserInfo.CountryCode());
    }

    try {
        Log::Print("Tidal::TryGetResponse: Request for 'https://%.*s%.*s'\n", PBUF(aHost), PBUF(aPathAndQuery));

        ServiceToken accessToken;
        if (!iTokenProvider->TryGetToken(aUserInfo.TokenId(), accessToken)) {
            THROW(OAuthTokenIdNotFound);
        }

        WriteRequestHeaders(Http::kMethodGet, aHost, aPathAndQuery, kPort, aConnection, 0, accessToken.token);

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

    if (aAccessToken.Bytes() > 0)
    {
        Log::Print("Using AccessToken: %.*s\n", PBUF(aAccessToken));
        OAuth::WriteAccessTokenHeader(iWriterRequest, aAccessToken);
    }

    iWriterRequest.WriteFlush();
}


TBool Tidal::TryGetAccessToken(const Brx& aTokenId,
                               const Brx& aTokenSource,
                               const Brx& aRefreshToken,
                               AccessTokenResponse& aResponse)
{
    //TODO: All other methods tend to lock round the credentials.
    //      now that credentials are seperate, we might need to introduce
    //      a second mutex to lock around the socket, preventing multiple
    //      threads from accessing this at once.
    AutoMutex m(iLock);

    /* TIDAL token fetching takes a 2 stage process.
     * First we refresh the token to grant us an up-to-date token.
     * After that, we then 'inherit' that token to be granted a
     * longer-living token for the DS.
     *
     * However, if the token has been added internally, using the
     * limited input flow, then we can't inherit this as the id/secret
     * used MUST be different. */
    if (DoTryGetAccessToken(aTokenId, aTokenSource, aRefreshToken, aResponse))
    {
        return aTokenSource == OAuth::kTokenSourceInternal ? true
                                                           : DoInheritToken(aResponse.AccessToken(), aResponse);
    }
    else
    {
        LOG_TRACE(kOAuth, "Tidal::TryGetAccessToken - Initial token fetch failed. Not attempting an inherit.\n");
        return false;
    }
}


TBool Tidal::TryGetUsernameFromToken(const Brx& aTokenId,
                                     const Brx& /*aTokenSource*/,
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
            aUsername.Write(v.Username());
            success = true;
            break;
        }
    }

    return success;
}


void Tidal::OnTokenRemoved(const Brx& aTokenId,
                           const Brx& /*aTokenSource*/,
                           const Brx& /*aAccessToken*/)
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

    // Don't try and logout the session. Doing so would invalidate all other tokens
    // on systems stopping playback...
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

TUint Tidal::MaxPollingJobs() const
{
    return Tidal::kMaximumNumberOfPollingJobs;
}

TBool Tidal::StartLimitedInputFlow(LimitedInputFlowDetails& aDetails)
{
    AutoMutex m(iLock);

    if (!TryConnect(SocketHost::Auth, kPort))
    {
        LOG_ERROR(kOAuth, "Tidal::StartLimitedInputFlow - Failed to connect socket.\n");
        return false;
    }

    AutoSocketSsl s(iSocket);

    iReqBody.SetBytes(0);
    WriterBuffer bodyWriter(iReqBody);
    Brn path("/v1/oauth2/device_authorization");

    OAuth::WriteRequestToStartLimitedInputFlowBody(bodyWriter,
                                                   iClientId,
                                                   kTidalTokenScope);

    try
    {
        WriteRequestHeaders(Http::kMethodPost,
                            kAuthenticationHost,
                            path,
                            kPort,
                            Connection::Close,
                            iReqBody.Bytes());

        iWriterBuf.Write(iReqBody);
        iWriterBuf.WriteFlush();

        iReaderResponse.Read();

        iResponseBuffer.SetBytes(0);
        WriterBuffer responseWriter(iResponseBuffer);

        iReaderEntity.ReadAll(responseWriter,
                              iHeaderContentLength,
                              iHeaderTransferEncoding,
                              ReaderHttpEntity::Mode::Client);

        const TUint status = iReaderResponse.Status().Code();

        if (status != 200)
        {
            LOG_ERROR(kOAuth, "Tidal::StartLimitedInputFlow - Failed to start flow. Code: %d. Response:\n%.*s\n", status, PBUF(iResponseBuffer));
            return false;
        }
        else
        {
            JsonParser p;
            p.ParseAndUnescape(iResponseBuffer);

            aDetails.Set(p.String("verificationUriComplete"),
                         p.String("userCode"),
                         p.String("deviceCode"),
                         static_cast<TUint>(p.Num("interval")));

            LOG_TRACE(kOAuth,
                      "Tidal::StartLimitedInputFlow - Fetched new device code: %.*s, user code: %.*s, with polling interval: %ud\n",
                      PBUF(aDetails.DeviceCode()),
                      PBUF(aDetails.AuthCode()),
                      aDetails.SuggestedPollingInterval());

            return true;
        }
    }
    catch (AssertionFailed&) {
        throw;
    }
    catch (Exception& ex) {
        LOG_ERROR(kOAuth, "Tidal::StartLimitedInputFlow  %s\n", ex.Message());
        return false;
    }
}

void Tidal::SetPollResultListener(IOAuthTokenPollResultListener* aListener)
{
    AutoMutex m(iPollRequestLock);

    ASSERT_VA(iPollResultListener == nullptr,
              "%s\n",
              "Tidal::SetPollResultListener - Listener already set.");

    iPollResultListener = aListener;
}

TBool Tidal::RequestPollForToken(OAuthPollRequest& aRequest)
{
    AutoMutex m(iPollRequestLock);

    if (iPollRequests.size() == kMaximumNumberOfPollingJobs)
    {
        LOG_WARNING(kOAuth, "Tidal::RequestPollForToken - Not enough space in queue to add new polling job.\n");
        return false;
    }
    else
    {
        const TBool hasPendingJobs = iPollRequests.size() == 0;

        iPollRequests.push_back(aRequest);
        LOG_TRACE(kOAuth, "Tidal::RequestPollForToken - Polling request added for job: %.*s\n", PBUF(aRequest.JobId()));

        if (hasPendingJobs)
        {
            LOG_TRACE(kOAuth, "Tidal::RequestPollForToken - Queue was previously empty, scheduling the polling task.\n");
            return iPollHandle->TrySchedule();
        }
        else
        {
            return true;
        }
    }
}


void Tidal::DoPollForToken()
{
    ASSERT_VA(iPollResultListener != nullptr,
              "%s\n",
              "Tidal::DoPollForToken - Attempting to poll for a token when there is nothing to listen for results.");

    OAuthPollRequest request;
    {
        AutoMutex m(iPollRequestLock);

        if (iPollRequests.size() == 0)
        {
            LOG_TRACE(kOAuth, "Tidal::DoPollForToken - Poll task has no work left to complete!\n");
            return;
        }
        else
        {
            request = iPollRequests.front();
            iPollRequests.pop_front();
        }

    }

    OAuthPollResult result(request.JobId());

    LOG_TRACE(kOAuth, "Tidal::DoPollForToken - Polling token for Job: %.*s\n", PBUF(request.JobId()));

    /* NOTE: As the reporting of polling results could call back into
     *       this class, we need to release control of the socket and lock
     *       so prevent deadlocks and subsequent network calls tramping on
     *       response buffers! */
    {
        AutoMutex m(iLock);

        if (!TryConnect(SocketHost::Auth, kPort))
        {
            LOG_ERROR(kOAuth, "Tidal::DoPollForToken - Failed to connect socket. Reporting error.\n");

            result.Set(OAuth::PollResult::Failed);
            iPollResultListener->OnPollCompleted(result);

            iPollHandle->TrySchedule();
        }

        AutoSocketSsl s(iSocket);
        Brn path("/v1/oauth2/token");

        iReqBody.SetBytes(0);
        WriterBuffer bodyWriter(iReqBody);

        OAuth::WriteTokenPollRequestBody(bodyWriter,
                                         iClientId,
                                         iClientSecret,
                                         kTidalTokenScope,
                                         request.DeviceCode());


        try
        {
            WriteRequestHeaders(Http::kMethodPost,
                                kAuthenticationHost,
                                path,
                                kPort,
                                Connection::Close,
                                iReqBody.Bytes());

            iWriterBuf.Write(iReqBody);
            iWriterBuf.WriteFlush();

            iReaderResponse.Read();

            iResponseBuffer.SetBytes(0);
            WriterBuffer responseWriter(iResponseBuffer);

            iReaderEntity.ReadAll(responseWriter,
                                  iHeaderContentLength,
                                  iHeaderTransferEncoding,
                                  ReaderHttpEntity::Mode::Client);

            const TUint status = iReaderResponse.Status().Code();

            JsonParser p;
            p.ParseAndUnescape(iResponseBuffer);

            // Success - polling complete...
            if (status == 200)
            {
                LOG_INFO(kOAuth, "Tidal::DoPollForToken - Polling successful for job: %.*s\n", PBUF(result.JobId()));

                result.Set(OAuth::PollResult::Success, p.String(OAuth::kTokenResponseFieldRefreshToken));
            }
            else if (status > 399 && status < 500)
            {
                const Brx& error = p.String(OAuth::kErrorResponseFieldError);
                const Brx& errorDesc = p.String(OAuth::kErrorResponseFieldErrorDescription);

                if (error == OAuth::kPollingStateTryAgain)
                {
                    LOG_TRACE(kOAuth, "Tidal::DoPollForToken - User not yet completed login for job: %.*s\n", PBUF(result.JobId()));
                    result.Set(OAuth::PollResult::Poll);
                }
                else if (error == OAuth::kPollingStateSlowDown)
                {
                    LOG_TRACE(kOAuth, "Tidal::DoPollForToken - We're polling to quickly on job: %.*s\n", PBUF(result.JobId()));
                    result.Set(OAuth::PollResult::SlowDown);
                }
                else
                {
                    LOG_ERROR(kOAuth,
                              "Tidal::DoPollForToken - Polling failed on job: %.*s with the following error: %.*s. Message: %.*s\n",
                              PBUF(result.JobId()),
                              PBUF(error),
                              PBUF(errorDesc));

                    result.Set(OAuth::PollResult::Failed);
                }
            }
        }
        catch (AssertionFailed&) {
            throw;
        }
        catch (Exception& ex) {
            LOG_ERROR(kOAuth, "Tidal::DoPollForToken %s\n", ex.Message());
            result.Set(OAuth::PollResult::Failed);
        }
    }

    //NOTE: This should be called outside the lock to allow
    //      other threads to continue and prevent deadlocking
    iPollResultListener->OnPollCompleted(result);
    iPollHandle->TrySchedule();
}

TBool Tidal::DoTryGetAccessToken(const Brx& aTokenId,
                                 const Brx& aTokenSource,
                                 const Brx& aRefreshToken,
                                 AccessTokenResponse& aResponse)
{
    iTimerSocketActivity->Cancel(); //Socket automatically closed by call below

    if (!TryConnect(SocketHost::Auth, kPort))
    {
        LOG_ERROR(kOAuth, "Tidal::DoTryGetAccessToken() - connection failure.\n");
        return false;
    }

    // Write request
    iReqBody.Replace(Brx::Empty());
    WriterBuffer writer(iReqBody);

    const Brn path("/v1/oauth2/token");


    if (aTokenSource == OAuth::kTokenSourceInternal)
    {
        OAuth::ConstructRefreshTokenRequestBody(writer,
                                                aRefreshToken,
                                                iClientId,
                                                iClientSecret,
                                                kTidalTokenScope);
    }
    else
    {
        const auto& details = iAppDetails.find(Brn(aTokenSource));

        if (details == iAppDetails.end())
        {
            LOG_ERROR(kOAuth, "Tidal::DoTryGetAccessToken() - Given token source (%.*s) not known.\n", PBUF(aTokenSource));
            return false;
        }

        OAuth::ConstructRefreshTokenRequestBody(writer,
                                                aRefreshToken,
                                                details->second.ClientId(),
                                                details->second.ClientSecret(),
                                                kTidalTokenScope);
    }



    AutoSocketSsl _(iSocket);

    try
    {
        WriteRequestHeaders(Http::kMethodPost, kAuthenticationHost, path, kPort, Connection::Close, iReqBody.Bytes());

        iWriterBuf.Write(iReqBody);
        iWriterBuf.WriteFlush();

        iReaderResponse.Read();

        iResponseBuffer.SetBytes(0);
        WriterBuffer responseWriter(iResponseBuffer);

        iReaderEntity.ReadAll(responseWriter,
                              iHeaderContentLength,
                              iHeaderTransferEncoding,
                              ReaderHttpEntity::Mode::Client);

        const TUint code = iReaderResponse.Status()
                                          .Code();

        JsonParser parser;
        parser.ParseAndUnescape(iResponseBuffer);

        if (code != 200)
        {
            const Brx& error = parser.String(OAuth::kErrorResponseFieldError);
            const Brx& errorDesc = parser.StringOptional(OAuth::kErrorResponseFieldErrorDescription);
            const TBool hasDesc = errorDesc.Bytes() > 0;

            const Brn noDescMsg("< No description present >");

            LOG_ERROR(kOAuth,
                      "Tidal::DoTryGetAccessToken() ~ Failed to refresh access token.\n- HttpCode: %u\n- Error: %.*s\n- Message: %.*s\n",
                      code,
                      PBUF(error),
                      PBUF(hasDesc ? errorDesc : noDescMsg));

            return false;
        }

        const Brx& accessToken = parser.String(OAuth::kTokenResponseFieldAccessToken);
        const Brx& refreshToken = parser.StringOptional(OAuth::kTokenResponseFieldRefreshToken);
        const TUint expiry = (TUint)parser.Num(OAuth::kTokenResponseFieldTokenExpiry);

        // Make sure to populate response value
        aResponse.Set(accessToken,
                      refreshToken,
                      expiry);
;

        // User information is also contained within our response
        // which is needed for future API requests.
        JsonParser parserUser;
        parserUser.Parse(parser.String("user"));

        const TUint userId = parserUser.Num("userId");
        const Brx& countryCode = parserUser.String("countryCode");
        const Brx& username = parserUser.String("username");

        // Store our user info internally for future API calls...
        TBool didPopulate = false;
        for(auto& v : iUserInfos)
        {
            const TBool isPopulated = v.Populated();
            const TBool isMatch = isPopulated && v.TokenId() == aTokenId;

            if (isMatch || !isPopulated)
            {
                LOG_TRACE(kOAuth, "Tidal::DoTryGetAccessToken - Storing user details for token: %.*s (Replace Existing: %d, Populate New: %d)\n", PBUF(aTokenId), isMatch, !isPopulated);
                v.Populate(aTokenId, userId, username, countryCode);
                didPopulate = true;
                break;
            }
        }

        if (!didPopulate)
        {
            LOG_ERROR(kOAuth, "Tidal::DoTryGetAccessToken - Unable to store user information related for tokenId: %.s\n", PBUF(aTokenId));
        }

        return didPopulate;
    }
    catch (AssertionFailed&) {
        throw;
    }
    catch (Exception& ex) {
        LOG_ERROR(kOAuth, "Tidal::DoTryGetAccessToken %s\n", ex.Message());
    }

    return false;
}

TBool Tidal::DoInheritToken(const Brx& aAccessTokenIn,
                            AccessTokenResponse& aResponse)
{
    iTimerSocketActivity->Cancel(); //Socket automatically closed by call below

    if (!TryConnect(SocketHost::Auth, kPort))
    {
        LOG_ERROR(kOAuth, "Tidal::DoInheritToken() - connection failure.\n");
        return false;
    }

    // Write request
    iReqBody.Replace(Brx::Empty());
    WriterBuffer writer(iReqBody);

    const Brn path("/v1/oauth2/token");

    // Construt the inherit body...
    writer.Write(Brn("access_token="));
    writer.Write(aAccessTokenIn);

    writer.Write('&');

    writer.Write(OAuth::kParameterClientId);
    writer.Write('=');
    writer.Write(iClientId);

    if (iClientSecret.Bytes() > 0)
    {
        writer.Write('&');

        writer.Write(OAuth::kParameterClientSecret);
        writer.Write('=');
        writer.Write(iClientSecret);
    }

    writer.Write('&');

    writer.Write(OAuth::kParameterGrantType);
    writer.Write(Brn("=switch_client"));

    writer.Write('&');

    writer.Write(OAuth::kParameterScope);
    writer.Write('=');
    writer.Write(kTidalTokenScope);


    AutoSocketSsl _(iSocket);

    try
    {
        WriteRequestHeaders(Http::kMethodPost, kAuthenticationHost, path, kPort, Connection::Close, iReqBody.Bytes());

        iWriterBuf.Write(iReqBody);
        iWriterBuf.WriteFlush();

        iReaderResponse.Read();

        iResponseBuffer.SetBytes(0);
        WriterBuffer responseWriter(iResponseBuffer);

        iReaderEntity.ReadAll(responseWriter,
                              iHeaderContentLength,
                              iHeaderTransferEncoding,
                              ReaderHttpEntity::Mode::Client);

        const TUint code = iReaderResponse.Status()
                                          .Code();

        JsonParser parser;
        parser.ParseAndUnescape(iResponseBuffer);

        if (code != 200)
        {
            const Brx& error = parser.String(OAuth::kErrorResponseFieldError);
            const Brx& errorDesc = parser.StringOptional(OAuth::kErrorResponseFieldErrorDescription);
            const TBool hasDesc = errorDesc.Bytes() > 0;

            const Brn noDescMsg("< No description present >");

            LOG_ERROR(kOAuth,
                      "Tidal::DoInheritToken() ~ Failed to refresh access token.\n- HttpCode: %u\n- Error: %.*s\n- Message: %.*s\n",
                      code,
                      PBUF(error),
                      PBUF(hasDesc ? errorDesc : noDescMsg));

            return false;
        }

        const Brx& accessToken = parser.String(OAuth::kTokenResponseFieldAccessToken);
        const Brx& refreshToken = parser.StringOptional(OAuth::kTokenResponseFieldRefreshToken);
        const TUint expiry = (TUint)parser.Num(OAuth::kTokenResponseFieldTokenExpiry);

        // Make sure to populate response value
        aResponse.Set(accessToken,
                      refreshToken,
                      expiry);

        LOG_TRACE(kOAuth,
                  "Tidal::DoInheritToken() - Token successfully inherited. Expires in %d\n",
                  expiry);

        return true;
    }
    catch (AssertionFailed&) {
        throw;
    }
    catch (Exception& ex) {
        LOG_ERROR(kOAuth, "Tidal::DoInheritToken %s\n", ex.Message());
    }

    return false;
}

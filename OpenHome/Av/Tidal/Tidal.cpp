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
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Av/Utils/FormUrl.h>
#include <OpenHome/Json.h>
#include <OpenHome/Net/Core/CpDeviceDv.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Configuration;


static const TChar* kSoundQualities[3] = {"LOW", "HIGH", "LOSSLESS"};
static const TUint kNumSoundQualities = sizeof(kSoundQualities) / sizeof(kSoundQualities[0]);

const Brn Tidal::kHost("api.tidalhifi.com");
const Brn Tidal::kId("tidalhifi.com");
const Brn Tidal::kConfigKeySoundQuality("tidalhifi.com.SoundQuality");

Tidal::Tidal(Environment& aEnv, SslContext& aSsl, const Brx& aToken, ICredentialsState& aCredentialsState, Configuration::IConfigInitialiser& aConfigInitialiser)
    : iLock("TDL1")
    , iLockConfig("TDL2")
    , iCredentialsState(aCredentialsState)
    , iSocket(aEnv, aSsl, kReadBufferBytes)
    , iReaderBuf(iSocket)
    , iReaderUntil(iReaderBuf)
    , iWriterBuf(iSocket)
    , iWriterRequest(iSocket)
    , iReaderResponse(aEnv, iReaderUntil)
    , iToken(aToken)
    , iUsername(kGranularityUsername)
    , iPassword(kGranularityPassword)
    , iUri(1024)
{
    iTimerSocketActivity = new Timer(aEnv, MakeFunctor(*this, &Tidal::SocketInactive), "Tidal");
    iReaderResponse.AddHeader(iHeaderContentLength);
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

TBool Tidal::TryGetStreamUrl(const Brx& aTrackId, Bwx& aStreamUrl)
{
    iTimerSocketActivity->Cancel(); // socket automatically closed by call below
    AutoMutex _(iLock);
    TBool success = false;
    if (!TryConnect(kPort)) {
        LOG_ERROR(kMedia, "Tidal::TryGetStreamUrl - connection failure\n");
        return false;
    }
    AutoSocketSsl __(iSocket);
    Bws<128> pathAndQuery("/v1/tracks/");
    pathAndQuery.Append(aTrackId);
    pathAndQuery.Append("/streamurl?sessionId=");
    pathAndQuery.Append(iSessionId);
    pathAndQuery.Append("&countryCode=");
    pathAndQuery.Append(iCountryCode);
    pathAndQuery.Append("&soundQuality=");
    iLockConfig.Wait();
    pathAndQuery.Append(Brn(kSoundQualities[iSoundQuality]));
    iLockConfig.Signal();
    Brn url;
    try {
        WriteRequestHeaders(Http::kMethodGet, kHost, pathAndQuery, kPort);

        iReaderResponse.Read();
        const TUint code = iReaderResponse.Status().Code();
        if (code != 200) {
            LOG_ERROR(kPipeline, "Http error - %d - in response to Tidal GetStreamUrl.  Some/all of response is:\n", code);
            Brn buf = iReaderUntil.Read(kReadBufferBytes);
            LOG_ERROR(kPipeline, "%.*s\n", PBUF(buf));
            THROW(ReaderError);
        }

        aStreamUrl.Replace(ReadString(iReaderUntil, Brn("url")));
        LOG(kMedia, "Tidal::TryGetStreamUrl aStreamUrl: %.*s\n", PBUF(aStreamUrl));
        success = true;
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

TBool Tidal::TryGetId(IWriter& aWriter, const Brx& aQuery, TidalMetadata::EIdType aType, Connection aConnection)
{
    Bws<kMaxPathAndQueryBytes> pathAndQuery("/v1/");

    pathAndQuery.Append("search/?query=");
    Uri::Escape(pathAndQuery, aQuery);
    pathAndQuery.Append("&types=");
    pathAndQuery.Append(TidalMetadata::IdTypeToString(aType));

    return TryGetResponse(aWriter, kHost, pathAndQuery, 1, 0, aConnection);
}

TBool Tidal::TryGetIds(IWriter& aWriter, const Brx& aMood, TidalMetadata::EIdType aType, TUint aLimitPerResponse, Connection aConnection)
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

    return TryGetResponse(aWriter, kHost, pathAndQuery, aLimitPerResponse, 0, aConnection);
}

TBool Tidal::TryGetTracksById(IWriter& aWriter, const Brx& aId, TidalMetadata::EIdType aType, TUint aLimit, TUint aOffset, Connection aConnection)
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

    return TryGetResponse(aWriter, kHost, pathAndQuery, aLimit, aOffset, aConnection);
}

TBool Tidal::TryGetIdsByRequest(IWriter& aWriter, const Brx& aRequestUrl, TUint aLimitPerResponse, TUint aOffset, Connection aConnection)
{
    iUri.SetBytes(0);
    Uri::Unescape(iUri, aRequestUrl);
    iRequest.Replace(iUri);
    iUri.Replace(iRequest.PathAndQuery());
    return TryGetResponse(aWriter, iRequest.Host(), iUri, aLimitPerResponse, aOffset, aConnection);
}

TBool Tidal::TryGetResponse(IWriter& aWriter, const Brx& aHost, Bwx& aPathAndQuery, TUint aLimit, TUint aOffset, Connection aConnection)
{
    iTimerSocketActivity->Cancel();
    AutoMutex _(iLock);
    TBool success = false;
    if (!TryConnect(kPort)) {
        LOG_ERROR(kMedia, "Tidal::TryGetResponse - connection failure\n");
        return false;
    }
    if (!Ascii::Contains(aPathAndQuery, '?')) {
        aPathAndQuery.Append("?");
    }
    aPathAndQuery.Append("&limit=");
    Ascii::AppendDec(aPathAndQuery, aLimit);
    aPathAndQuery.Append("&offset=");
    Ascii::AppendDec(aPathAndQuery, aOffset);
    if (!Ascii::Contains(aPathAndQuery, Brn("sessionId"))) {
        aPathAndQuery.Append("&sessionId=");
        aPathAndQuery.Append(iSessionId);
    }
    if (!Ascii::Contains(aPathAndQuery, Brn("countryCode"))) {
        aPathAndQuery.Append("&countryCode=");
        aPathAndQuery.Append(iCountryCode);
    }
    
    try {
        Log::Print("Write Tidal request: http://%.*s%.*s\n", PBUF(aHost), PBUF(aPathAndQuery));
        WriteRequestHeaders(Http::kMethodGet, aHost, aPathAndQuery, kPort, aConnection);

        iReaderResponse.Read();
        const TUint code = iReaderResponse.Status().Code();
        if (code != 200) {
            LOG_ERROR(kPipeline, "Http error - %d - in response to Tidal TryGetResponse.  Some/all of response is:\n", code);
            Brn buf = iReaderUntil.Read(kReadBufferBytes);
            LOG_ERROR(kPipeline, "%.*s\n", PBUF(buf));
            THROW(ReaderError);
        }  
        
        TUint count = iHeaderContentLength.ContentLength();
        //Log::Print("Read tidal response (%d): ", count);
        while(count > 0) {
            Brn buf = iReaderUntil.Read(kReadBufferBytes);
            //Log::Print(buf);
            aWriter.Write(buf);
            count -= buf.Bytes();
        }   
        //Log::Print("\n");     

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

TBool Tidal::TryConnect(TUint aPort)
{
    if (iSocket.IsConnected()) {
        return true;
    }
    Endpoint ep;
    try {
        ep.SetAddress(kHost);
        ep.SetPort(aPort);
        iSocket.Connect(ep, kConnectTimeoutMs);
    }
    catch (NetworkTimeout&) {
        iSocket.Close();
        return false;
    }
    catch (NetworkError&) {
        iSocket.Close();
        return false;
    }
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
    if (!TryConnect(kPort)) {
        LOG_ERROR(kPipeline, "Tidal::TryLogin - connection failure\n");
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
            if (code != 200) {
                Bws<kMaxStatusBytes> status;
                const TUint len = std::min(status.MaxBytes(), iHeaderContentLength.ContentLength());
                if (len > 0) {
                    status.Replace(iReaderUntil.Read(len));
                    iCredentialsState.SetState(kId, status, Brx::Empty());
                }
                else {
                    error.AppendPrintf("Login Error (Response Code %d): Please Try Again.", code);
                    iCredentialsState.SetState(kId, error, Brx::Empty());
                    LOG_ERROR(kPipeline, "HTTP error - %d - in Tidal::TryLogin\n", code);
                }
                updatedStatus = true;
                LOG(kPipeline, "Http error - %d - in response to Tidal login.  Some/all of response is:\n%.*s\n", code, PBUF(status));
                THROW(ReaderError);
            }

            iUserId.Replace(ReadInt(iReaderUntil, Brn("userId")));
            iSessionId.Replace(ReadString(iReaderUntil, Brn("sessionId")));
            iCountryCode.Replace(ReadString(iReaderUntil, Brn("countryCode")));
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
    if (aSessionId.Bytes() == 0) {
        return true;
    }
    TBool success = false;
    if (!TryConnect(kPort)) {
        LOG_ERROR(kPipeline, "Tidal: connection failure\n");
        return false;
    }
    AutoSocketSsl _(iSocket);
    Bws<128> pathAndQuery("/v1/logout?sessionId=");
    pathAndQuery.Append(aSessionId);
    try {
        WriteRequestHeaders(Http::kMethodPost, kHost, pathAndQuery, kPort);

        iReaderResponse.Read();
        const TUint code = iReaderResponse.Status().Code();
        if (code < 200 || code >= 300) {
            LOG_ERROR(kPipeline, "Http error - %d - in response to Tidal logout.  Some/all of response is:\n", code);
            Brn buf = iReaderUntil.Read(kReadBufferBytes);
            LOG_ERROR(kPipeline, "%.*s\n", PBUF(buf));
            THROW(ReaderError);
        }
        success = true;
        iSessionId.SetBytes(0);
    }
    catch (WriterError&) {
        LOG_ERROR(kPipeline, "WriterError from Tidal logout\n");
    }
    catch (ReaderError&) {
        LOG_ERROR(kPipeline, "ReaderError from Tidal logout\n");
    }
    catch (HttpError&) {
        LOG_ERROR(kPipeline, "HttpError from Tidal logout\n");
    }
    return success;
}

TBool Tidal::TryGetSubscriptionLocked()
{
    TBool updateStatus = false;
    Bws<kMaxStatusBytes> error;
    TBool success = false;
    if (!TryConnect(kPort)) {
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
        if (code != 200) {
            Bws<kMaxStatusBytes> status;
            const TUint len = std::min(status.MaxBytes(), iHeaderContentLength.ContentLength());
            if (len > 0) {
                error.Replace(iReaderUntil.Read(len));
            }
            else {
                error.AppendPrintf("Subscription Error (Response Code %d): Please Try Again.", code);
            }
            updateStatus = true;
            LOG_ERROR(kPipeline, "Http error - %d - in response to Tidal subscription.  Some/all of response is:\n%.*s\n", code, PBUF(status));
            THROW(ReaderError);
        }
        Brn quality = ReadString(iReaderUntil, Brn("highestSoundQuality"));
        for (TUint i=0; i<kNumSoundQualities; i++) {
            if (Brn(kSoundQualities[i]) == quality) {
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

void Tidal::WriteRequestHeaders(const Brx& aMethod, const Brx& aHost, const Brx& aPathAndQuery, TUint aPort, Connection aConnection, TUint aContentLength)
{
    iWriterRequest.WriteMethod(aMethod, aPathAndQuery, Http::eHttp11);
    Http::WriteHeaderHostAndPort(iWriterRequest, aHost, aPort);
    if (aContentLength > 0) {
        Http::WriteHeaderContentLength(iWriterRequest, aContentLength);
    }
    Http::WriteHeaderContentType(iWriterRequest, Brn("application/x-www-form-urlencoded"));
    if (aConnection == Connection::Close) {
        Http::WriteHeaderConnectionClose(iWriterRequest);
    }
    iWriterRequest.WriteFlush();
}


Brn Tidal::ReadInt(ReaderUntil& aReader, const Brx& aTag)
{ // static
    (void)aReader.ReadUntil('\"');
    for (;;) {
        Brn buf = aReader.ReadUntil('\"');
        if (buf == aTag) {
            break;
        }
    }

    (void)aReader.ReadUntil(':');
    Brn buf = aReader.ReadUntil(','); // FIXME - assumes aTag isn't the last element in this container
    return buf;
}

Brn Tidal::ReadString(ReaderUntil& aReader, const Brx& aTag)
{ // static
    (void)aReader.ReadUntil('\"');
    for (;;) {
        Brn buf = aReader.ReadUntil('\"');
        if (buf == aTag) {
            break;
        }
    }
    (void)aReader.ReadUntil('\"');
    Brn buf = aReader.ReadUntil('\"');
    return buf;
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

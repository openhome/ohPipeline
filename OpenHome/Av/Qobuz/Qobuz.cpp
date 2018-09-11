#include <OpenHome/Av/Qobuz/Qobuz.h>
#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/UnixTimestamp.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Private/md5.h>
#include <OpenHome/Json.h>

#include <algorithm>
#include <vector>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Configuration;


const Brn Qobuz::kHost("www.qobuz.com");
const Brn Qobuz::kId("qobuz.com");
const Brn Qobuz::kVersionAndFormat("/api.json/0.2/");
const Brn Qobuz::kConfigKeySoundQuality("qobuz.com.SoundQuality");

static const TUint kQualityValues[] ={ 5, 6, 7, 27 };

Qobuz::Qobuz(Environment& aEnv, const Brx& aAppId, const Brx& aAppSecret,
             ICredentialsState& aCredentialsState, IConfigInitialiser& aConfigInitialiser,
             IUnixTimestamp& aUnixTimestamp)
    : iEnv(aEnv)
    , iLock("QBZ1")
    , iLockConfig("QBZ2")
    , iCredentialsState(aCredentialsState)
    , iUnixTimestamp(aUnixTimestamp)
    , iReaderBuf(iSocket)
    , iReaderUntil1(iReaderBuf)
    , iWriterBuf(iSocket)
    , iWriterRequest(iWriterBuf)
    , iReaderResponse(aEnv, iReaderUntil1)
    , iReaderEntity(iReaderUntil1)
    , iReaderUntil2(iReaderEntity)
    , iAppId(aAppId)
    , iAppSecret(aAppSecret)
    , iUsername(kGranularityUsername)
    , iPassword(kGranularityPassword)
    , iUri(1024)
{
    iReaderResponse.AddHeader(iHeaderContentLength);
    iReaderResponse.AddHeader(iHeaderTransferEncoding);

    const int arr[] = {0, 1, 2, 3};
    /* 'arr' above describes the highest possible quality of a Qobuz stream
         5:  320kbps AAC
         6:  FLAC 16-bit, 44.1kHz
         7:  FLAC 24-bit, up to 96kHz
        27:  FLAC 24-bit, up to 192kHz
    */
    std::vector<TUint> qualities(arr, arr + sizeof(arr)/sizeof(arr[0]));
    iConfigQuality = new ConfigChoice(aConfigInitialiser, kConfigKeySoundQuality, qualities, 3);
    iSubscriberIdQuality = iConfigQuality->Subscribe(MakeFunctorConfigChoice(*this, &Qobuz::QualityChanged));
}

Qobuz::~Qobuz()
{
    iConfigQuality->Unsubscribe(iSubscriberIdQuality);
    delete iConfigQuality;
}

TBool Qobuz::TryLogin()
{
    AutoMutex _(iLock);
    return TryLoginLocked();
}

TBool Qobuz::TryGetStreamUrl(const Brx& aTrackId, Bwx& aStreamUrl)
{
    AutoMutex _(iLock);
    TBool success = false;
    if (!TryConnect()) {
        LOG_ERROR(kPipeline, "Qobuz::TryGetStreamUrl - connection failure\n");
        return false;
    }
    AutoSocketReader __(iSocket, iReaderUntil2);

    // see https://github.com/Qobuz/api-documentation#request-signature for rules on creating request_sig value
    TUint timestamp;
    try {
        timestamp = iUnixTimestamp.Now();
    }
    catch (UnixTimestampUnavailable&) {
        LOG_ERROR(kPipeline, "Qobuz::TryGetStreamUrl - failure to determine network time\n");
        return false;
    }
    Bws<Ascii::kMaxUintStringBytes> audioFormatBuf;
    iLockConfig.Wait();
    Ascii::AppendDec(audioFormatBuf, iSoundQuality);
    iLockConfig.Signal();
    Bws<128> sig("trackgetFileUrlformat_id");
    sig.Append(audioFormatBuf);
    sig.Append("intentstreamtrack_id");
    sig.Append(aTrackId);
    Ascii::AppendDec(sig, timestamp);
    sig.Append(iAppSecret);

    iPathAndQuery.Replace(kVersionAndFormat);
    iPathAndQuery.Append("track/getFileUrl?app_id=");
    iPathAndQuery.Append(iAppId);
    iPathAndQuery.Append("&user_auth_token=");
    iPathAndQuery.Append(iAuthToken);
    iPathAndQuery.Append("&request_ts=");
    Ascii::AppendDec(iPathAndQuery, timestamp);
    iPathAndQuery.Append("&request_sig=");
    AppendMd5(iPathAndQuery, sig);
    iPathAndQuery.Append("&track_id=");
    iPathAndQuery.Append(aTrackId);
    iPathAndQuery.Append("&format_id=");
    iPathAndQuery.Append(audioFormatBuf);
    iPathAndQuery.Append("&intent=stream");

    try {
        const TUint code = WriteRequestReadResponse(Http::kMethodGet, kHost, iPathAndQuery);
        if (code != 200) {
            LOG_ERROR(kPipeline, "Http error - %d - in response to Qobuz::TryGetStreamUrl.\n", code);
            LOG_ERROR(kPipeline, "...path/query is %.*s\n", PBUF(iPathAndQuery));
            LOG_ERROR(kPipeline, "Some/all of response is:\n");
            Brn buf = iReaderEntity.Read(kReadBufferBytes);
            LOG_ERROR(kPipeline, "%.*s\n", PBUF(buf));
            THROW(ReaderError);
        }

        static const Brn kTagUrl("url");
        Brn val;
        do {
            val.Set(ReadString());
        } while (val != kTagUrl);
        aStreamUrl.Replace(ReadString());
        Json::Unescape(aStreamUrl);
        success = true;
    }
    catch (HttpError&) {
        LOG_ERROR(kPipeline, "HttpError in Qobuz::TryGetStreamUrl\n");
    }
    catch (ReaderError&) {
        LOG_ERROR(kPipeline, "ReaderError in Qobuz::TryGetStreamUrl\n");
    }
    catch (WriterError&) {
        LOG_ERROR(kPipeline, "WriterError in Qobuz::TryGetStreamUrl\n");
    }
    return success;
}

TBool Qobuz::TryGetId(IWriter& aWriter, const Brx& aQuery, QobuzMetadata::EIdType aType)
{
    iPathAndQuery.Replace(kVersionAndFormat);

    iPathAndQuery.Append(QobuzMetadata::IdTypeToString(aType));
    iPathAndQuery.Append("/search?query=");
    Uri::Escape(iPathAndQuery, aQuery);

    return TryGetResponse(aWriter, kHost, 1, 0); // return top hit
}

TBool Qobuz::TryGetIds(IWriter& aWriter, const Brx& aGenre, QobuzMetadata::EIdType aType, TUint aLimitPerResponse)
{
    iPathAndQuery.Replace(kVersionAndFormat);

    iPathAndQuery.Append(QobuzMetadata::IdTypeToString(aType));
    switch (aType) {
        case QobuzMetadata::eSmartNew:          iPathAndQuery.Append("/getFeatured?&type=new-releases"); break;
        case QobuzMetadata::eSmartRecommended:  iPathAndQuery.Append("/getFeatured?&type=editor-picks"); break;
        case QobuzMetadata::eSmartMostStreamed: iPathAndQuery.Append("/getFeatured?&type=most-streamed"); break;
        case QobuzMetadata::eSmartBestSellers:  iPathAndQuery.Append("/getFeatured?&type=best-sellers"); break;
        case QobuzMetadata::eSmartAwardWinning: iPathAndQuery.Append("/getFeatured?&type=press-awards"); break;
        case QobuzMetadata::eSmartMostFeatured: iPathAndQuery.Append("/getFeatured?&type=most-featured"); break;
        default: break;
    }
    if (aGenre.Bytes() > 0 && aGenre != QobuzMetadata::kGenreNone) {
        if (Ascii::Contains(aGenre, ',')) {
            iPathAndQuery.Append("&genre_ids=");
        }
        else {
            iPathAndQuery.Append("&genre_id=");
        }
        iPathAndQuery.Append(aGenre);
    }

    return TryGetResponse(aWriter, kHost, aLimitPerResponse, 0);
}

TBool Qobuz::TryGetTracksById(IWriter& aWriter, const Brx& aId, QobuzMetadata::EIdType aType, TUint aLimit, TUint aOffset)
{
    iPathAndQuery.Replace(kVersionAndFormat);

    iPathAndQuery.Append(QobuzMetadata::IdTypeToString(aType));
    if (aType == QobuzMetadata::eFavorites) {
        iPathAndQuery.Append("/getTracks?&source=favorites");
    }
    else if (aType == QobuzMetadata::ePurchased) {
        iPathAndQuery.Append("/getTracks?&source=purchases");
    }
    else if (aType == QobuzMetadata::ePurchasedTracks) {
        // should not be required but collection endpoint not working correctly (only returns albums, no tracks)
        iPathAndQuery.Append("/getUserPurchases?");
    }
    else if (aType == QobuzMetadata::eSavedPlaylist) {
        iPathAndQuery.Append("/getTracks?&source=playlists");
    }
    else if (aType == QobuzMetadata::eCollection) {
        iPathAndQuery.Append("/getTracks?"); // includes purchased, playlisted, and favorited tracks for authenticated user
    }
    else {
        iPathAndQuery.Append("/get?");
        iPathAndQuery.Append(QobuzMetadata::IdTypeToString(aType));
        iPathAndQuery.Append("_id=");
        iPathAndQuery.Append(aId);
        if (aType == QobuzMetadata::eArtist || aType == QobuzMetadata::ePlaylist) {
            iPathAndQuery.Append("&extra=tracks");
        }
    }

    return TryGetResponse(aWriter, kHost, aLimit, aOffset);
}

TBool Qobuz::TryGetGenreList(IWriter& aWriter)
{
    iPathAndQuery.Replace(kVersionAndFormat);
    iPathAndQuery.Append("genre/list?");

    return TryGetResponse(aWriter, kHost, 50, 0);
}

TBool Qobuz::TryGetIdsByRequest(IWriter& aWriter, const Brx& aRequestUrl, TUint aLimitPerResponse, TUint aOffset)
{
    iUri.SetBytes(0);
    Uri::Unescape(iUri, aRequestUrl);
    iRequest.Replace(iUri);
    iPathAndQuery.Replace(iRequest.PathAndQuery());
    return TryGetResponse(aWriter, iRequest.Host(), aLimitPerResponse, aOffset);
}

TBool Qobuz::TryGetResponse(IWriter& aWriter, const Brx& aHost, TUint aLimit, TUint aOffset)
{
    AutoMutex _(iLock);
    TBool success = false;
    if (!TryConnect()) {
        LOG_ERROR(kMedia, "Qobuz::TryGetResponse - connection failure\n");
        return false;
    }
    AutoSocketReader __(iSocket, iReaderUntil2);
    if (!Ascii::Contains(iPathAndQuery, '?')) {
        iPathAndQuery.Append("?");
    }
    iPathAndQuery.Append("&limit=");
    Ascii::AppendDec(iPathAndQuery, aLimit);
    iPathAndQuery.Append("&offset=");
    Ascii::AppendDec(iPathAndQuery, aOffset);
    if (!Ascii::Contains(iPathAndQuery, Brn("app_id"))) {
        iPathAndQuery.Append("&app_id=");
        iPathAndQuery.Append(iAppId);
    }
    if (!Ascii::Contains(iPathAndQuery, Brn("user_auth_token"))) {
        iPathAndQuery.Append("&user_auth_token=");
        iPathAndQuery.Append(iAuthToken);
    }
    try {
        Log::Print("Write Qobuz request: http://%.*s%.*s\n", PBUF(aHost), PBUF(iPathAndQuery));
        const TUint code = WriteRequestReadResponse(Http::kMethodGet, aHost, iPathAndQuery);
        if (code != 200) {
            LOG_ERROR(kPipeline, "Http error - %d - in response to Qobuz::TryGetResponse.\n", code);
            LOG_ERROR(kPipeline, "...path/query is %.*s\n", PBUF(iPathAndQuery));
            LOG_ERROR(kPipeline, "Some/all of response is:\n");
            Brn buf = iReaderEntity.Read(kReadBufferBytes);
            LOG_ERROR(kPipeline, "%.*s\n", PBUF(buf));
            THROW(ReaderError);
        }
        iReaderEntity.ReadAll(aWriter);
        success = true;
    }
    catch (HttpError&) {
        LOG_ERROR(kPipeline, "HttpError in Qobuz::TryGetResponse\n");
    }
    catch (ReaderError&) {
        LOG_ERROR(kPipeline, "ReaderError in Qobuz::TryGetResponse\n");
    }
    catch (WriterError&) {
        LOG_ERROR(kPipeline, "WriterError in Qobuz::TryGetResponse\n");
    }
    return success;
}

void Qobuz::Interrupt(TBool aInterrupt)
{
    iSocket.Interrupt(aInterrupt);
}

const Brx& Qobuz::Id() const
{
    return kId;
}

void Qobuz::CredentialsChanged(const Brx& aUsername, const Brx& aPassword)
{
    AutoMutex _(iLockConfig);
    iUsername.Reset();
    iUsername.Write(aUsername);
    iPassword.Reset();
    iPassword.Write(aPassword);
}

void Qobuz::UpdateStatus()
{
    AutoMutex _(iLock);
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

void Qobuz::Login(Bwx& aToken)
{
    AutoMutex _(iLock);
    if (iAuthToken.Bytes() == 0 && !TryLoginLocked()) {
        THROW(CredentialsLoginFailed);
    }
    aToken.Replace(iAuthToken);
}

void Qobuz::ReLogin(const Brx& aCurrentToken, Bwx& aNewToken)
{
    AutoMutex _(iLock);
    if (aCurrentToken == iAuthToken) {
        if (!TryLoginLocked()) {
            THROW(CredentialsLoginFailed);
        }
    }
    aNewToken.Replace(iAuthToken);
}

TBool Qobuz::TryConnect()
{
    Endpoint ep;
    try {
        iSocket.Open(iEnv);
        ep.SetAddress(kHost);
        ep.SetPort(kPort);
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

TBool Qobuz::TryLoginLocked()
{
    TBool updatedStatus = false;
    Bws<50> error;
    TBool success = false;

    if (!TryConnect()) {
        LOG_ERROR(kMedia, "Qobuz::TryLogin - connection failure\n");
        iCredentialsState.SetState(kId, Brn("Login Error (Connection Failed): Please Try Again."), Brx::Empty());
        return false;
    }
    AutoSocketReader _(iSocket, iReaderUntil2);

    iPathAndQuery.Replace(kVersionAndFormat);
    iPathAndQuery.Append("user/login?app_id=");
    iPathAndQuery.Append(iAppId);
    iPathAndQuery.Append("&username=");
    iLockConfig.Wait();
    iPathAndQuery.Append(iUsername.Buffer());
    iPathAndQuery.Append("&password=");
    AppendMd5(iPathAndQuery, iPassword.Buffer());
    iLockConfig.Signal();

    try {
        const TUint code = WriteRequestReadResponse(Http::kMethodGet, kHost, iPathAndQuery);
        if (code != 200) {
            Bws<kMaxStatusBytes> status;
            TUint len = std::min(status.MaxBytes(), iHeaderContentLength.ContentLength());
            if (len > 0) {
                status.Replace(iReaderEntity.Read(len));
                iCredentialsState.SetState(kId, status, Brx::Empty());
            }
            else {
                status.AppendPrintf("Login Error (Response Code %d): ", code);
                Brn buf = iReaderEntity.Read(kReadBufferBytes);
                len = std::min(status.MaxBytes() - status.Bytes(), buf.Bytes());
                buf.Set(buf.Ptr(), len);
                status.Append(buf);
                iCredentialsState.SetState(kId, status, Brx::Empty());
            }
            updatedStatus = true;
            LOG_ERROR(kPipeline, "Http error - %d - in response to Qobuz login.  Some/all of response is:\n%.*s\n", code, PBUF(status));
            THROW(ReaderError);
        }

        static const Brn kUserAuthToken("user_auth_token");
        Brn val;
        do {
            val.Set(ReadString());
        } while (val != kUserAuthToken);
        iAuthToken.Replace(ReadString());
        iCredentialsState.SetState(kId, Brx::Empty(), iAppId);
        updatedStatus = true;
        success = true;
    }
    catch (HttpError&) {
        error.Append("Login Error (Http Failure): Please Try Again.");
        LOG_ERROR(kPipeline, "HttpError in Qobuz::TryLoginLocked\n");
    }
    catch (ReaderError&) {
        error.Append("Login Error (Read Failure): Please Try Again.");
        LOG_ERROR(kPipeline, "ReaderError in Qobuz::TryLoginLocked\n");
    }
    catch (WriterError&) {
        error.Append("Login Error (Write Failure): Please Try Again.");
        LOG_ERROR(kPipeline, "WriterError in Qobuz::TryLoginLocked\n");
    }

    if (!updatedStatus) {
        iCredentialsState.SetState(kId, error, Brx::Empty());
    }
    return success;
}

TUint Qobuz::WriteRequestReadResponse(const Brx& aMethod, const Brx& aHost, const Brx& aPathAndQuery)
{
    iWriterRequest.WriteMethod(aMethod, aPathAndQuery, Http::eHttp11);
    Http::WriteHeaderHostAndPort(iWriterRequest, aHost, kPort);
    Http::WriteHeaderConnectionClose(iWriterRequest);
    iWriterRequest.WriteFlush();
    iReaderResponse.Read();
    const TUint code = iReaderResponse.Status().Code();
    iReaderEntity.Set(iHeaderContentLength, iHeaderTransferEncoding, ReaderHttpEntity::Mode::Client);
    return code;
}

Brn Qobuz::ReadString()
{
    (void)iReaderUntil2.ReadUntil('\"');
    return iReaderUntil2.ReadUntil('\"');
}

void Qobuz::QualityChanged(Configuration::KeyValuePair<TUint>& aKvp)
{
    iLockConfig.Wait();
    iSoundQuality = kQualityValues[aKvp.Value()];
    iLockConfig.Signal();
}

void Qobuz::AppendMd5(Bwx& aBuffer, const Brx& aToHash)
{ // static
    md5_state_t state;
    md5_byte_t digest[16];
    md5_init(&state);
    md5_append(&state, (const md5_byte_t*)aToHash.Ptr(), aToHash.Bytes());
    md5_finish(&state, digest);
    for (TUint i=0; i<sizeof(digest); i++) {
        Ascii::AppendHex(aBuffer, digest[i]);
    }
}

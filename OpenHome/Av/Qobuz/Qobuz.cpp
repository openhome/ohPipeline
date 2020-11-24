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
#include <OpenHome/Private/Timer.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/UnixTimestamp.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Private/md5.h>
#include <OpenHome/Json.h>
#include <OpenHome/Av/Utils/FormUrl.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Media/PipelineObserver.h>
#include <OpenHome/Media/Pipeline/Msg.h>

#include <algorithm>
#include <vector>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Configuration;


// QobuzTrack

QobuzTrack::QobuzTrack(IUnixTimestamp& aUnixTimestamp, Media::IPipelineObservable& aPipelineObservable,
                       IQobuzTrackObserver& aObserver, TUint aTrackId, const Brx& aUrl, TUint aFormatId)
    : iLock("QTrk")
    , iUnixTimestamp(aUnixTimestamp)
    , iPipelineObservable(aPipelineObservable)
    , iObserver(aObserver)
    , iTrackId(aTrackId)
    , iStartTime(0)
    , iPlayedSeconds(0)
    , iStreamId(Media::IPipelineIdProvider::kStreamIdInvalid)
    , iFormatId(aFormatId)
    , iCurrentStream(false)
    , iStarted(false)
{
    Log::Print("++ QobuzTrack: iTrackId=%u\n", iTrackId);
    UpdateUrl(aUrl);
    iPipelineObservable.AddObserver(*this);
}

QobuzTrack::~QobuzTrack()
{
    Log::Print("++ ~QobuzTrack: iTrackId=%u, iStreamId=%u\n", iTrackId, iStreamId);
    iPipelineObservable.RemoveObserver(*this);
}

void QobuzTrack::ProtocolStarted(TUint aStreamId)
{
    AutoMutex _(iLock);
    iStreamId = aStreamId;
    Log::Print("++ QobuzTrack::ProtocolStarted: iTrackId=%u, iStreamId=%u\n", iTrackId, iStreamId);
}

void QobuzTrack::ProtocolCompleted(TBool aStopped)
{
    TBool reportStopped = false;
    {
        AutoMutex _(iLock);
        if (!iStarted) {
            // prevent NotifyStreamInfo setting iCurrentStream
            // Note that this would falsely report a track as complete if the pipeline
            // buffered the entire track before starting to play it.
            iStreamId = Media::IPipelineIdProvider::kStreamIdInvalid;
            reportStopped = !aStopped; // no point reporting anything if we hadn't started playing then the pipeline is cleared
        }
    }
    if (reportStopped) {
        iObserver.TrackStopped(*this);
    }
    else if (!iStarted) {
        delete this;
    }
}

void QobuzTrack::UpdateUrl(const Brx& aUrlEncoded)
{
    AutoMutex _(iLock);
    if (iUrl.MaxBytes() < aUrlEncoded.Bytes()) {
        iUrl.Grow(aUrlEncoded.Bytes());
    }
    iUrl.Replace(aUrlEncoded);
    Json::Unescape(iUrl);
}

TUint QobuzTrack::Id() const
{
    return iTrackId;
}

const Brx& QobuzTrack::Url() const
{
    AutoMutex _(iLock);
    return iUrl;
}

TUint QobuzTrack::FormatId() const
{
    return iFormatId;
}

TUint QobuzTrack::StartTime() const
{
    return iStartTime;
}

TUint QobuzTrack::PlayedSeconds() const
{
    return iPlayedSeconds;
}

void QobuzTrack::NotifyPipelineState(Media::EPipelineState /*aState*/)
{
}

void QobuzTrack::NotifyMode(const Brx& /*aMode*/, const Media::ModeInfo& /*aInfo*/,
                            const Media::ModeTransportControls& /*aTransportControls*/)
{
}

void QobuzTrack::NotifyTrack(Media::Track& /*aTrack*/, TBool aStartOfStream)
{
    TBool stopped = false;
    {
        AutoMutex _(iLock);
        if (iCurrentStream && aStartOfStream) {
            iCurrentStream = false;
            stopped = true;
        }
        if (stopped) {
            iObserver.TrackStopped(*this);
        }
    }
}

void QobuzTrack::NotifyMetaText(const Brx& /*aText*/)
{
}

void QobuzTrack::NotifyTime(TUint aSeconds)
{
    {
        AutoMutex _(iLock);
        if (iCurrentStream && aSeconds > iPlayedSeconds) {
            iPlayedSeconds = aSeconds;
        }
    }
    if (aSeconds > 0 && !iStarted) {
        iStarted = true;
        try {
            iStartTime = iUnixTimestamp.Now();
        }
        catch (UnixTimestampUnavailable&) {}
        iObserver.TrackStarted(*this);
    }
}

void QobuzTrack::NotifyStreamInfo(const Media::DecodedStreamInfo& aStreamInfo)
{
    TBool stopped = false;
    {
        AutoMutex _(iLock);
        Log::Print("++ QobuzTrack::NotifyStreamInfo: iTrackId=%u, iStreamId=%u, stream=%u\n", iTrackId, iStreamId, aStreamInfo.StreamId());
        if (aStreamInfo.StreamId() == iStreamId) {
            iCurrentStream = true;
        }
        else if (iCurrentStream) {
            iCurrentStream = false;
            stopped = true;
        }
    }
    if (stopped) {
        iObserver.TrackStopped(*this);
    }
}


// Qobuz

const Brn Qobuz::kHost("www.qobuz.com");
const Brn Qobuz::kId("qobuz.com");
const Brn Qobuz::kVersionAndFormat("/api.json/0.2/");
const Brn Qobuz::kConfigKeySoundQuality("qobuz.com.SoundQuality");
const Brn Qobuz::kTagFileUrl("url");

static const TUint kQualityValues[] ={ 5, 6, 7, 27 };

Qobuz::Qobuz(Environment& aEnv, const Brx& aAppId, const Brx& aAppSecret, const Brx& aDeviceId,
             ICredentialsState& aCredentialsState, IConfigInitialiser& aConfigInitialiser,
             IUnixTimestamp& aUnixTimestamp, IThreadPool& aThreadPool,
             Media::IPipelineObservable& aPipelineObservable)
    : iEnv(aEnv)
    , iLock("QBZ1")
    , iLockConfig("QBZ2")
    , iCredentialsState(aCredentialsState)
    , iUnixTimestamp(aUnixTimestamp)
    , iPipelineObservable(aPipelineObservable)
    , iReaderBuf(iSocket)
    , iReaderUntil(iReaderBuf)
    , iWriteBuffer(iSocket)
    , iWriterRequest(iWriteBuffer)
    , iReaderResponse(aEnv, iReaderUntil)
    , iReaderEntity(iReaderUntil)
    , iAppId(aAppId)
    , iAppSecret(aAppSecret)
    , iDeviceId(aDeviceId)
    , iUsername(kGranularityUsername)
    , iPassword(kGranularityPassword)
    , iResponseBody(2048)
    , iUri(1024)
    , iConnected(false)
    , iLockStreamEvents("QBZ3")
    , iStreamEventBuf(2048)
    , iLockPurchasedTracks("QBZ4")
{
    iTimerSocketActivity = new Timer(aEnv, MakeFunctor(*this, &Qobuz::SocketInactive), "Qobuz-Socket");
    iTimerPurchasedTracks = new Timer(aEnv, MakeFunctor(*this, &Qobuz::ScheduleUpdatePurchasedTracks), "Qobuz-Purchased");
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
    iSchedulerStreamEvents = aThreadPool.CreateHandle(MakeFunctor(*this, &Qobuz::ReportStreamEvents), "QobuzStreamEvents", ThreadPoolPriority::Low);
    iSchedulerPurchasedTracks = aThreadPool.CreateHandle(MakeFunctor(*this, &Qobuz::UpdatePurchasedTracks), "QobuzPurchased", ThreadPoolPriority::Low);
}

Qobuz::~Qobuz()
{
    delete iTimerPurchasedTracks;
    iSchedulerStreamEvents->Destroy();
    iSchedulerPurchasedTracks->Destroy();
    delete iTimerSocketActivity;
    iConfigQuality->Unsubscribe(iSubscriberIdQuality);
    delete iConfigQuality;
}

TBool Qobuz::TryLogin()
{
    iTimerSocketActivity->Cancel(); // socket automatically closed by call below
    AutoMutex _(iLock);
    return TryLoginLocked();
}

QobuzTrack* Qobuz::StreamableTrack(const Brx& aTrackId)
{
    iTimerSocketActivity->Cancel();
    AutoMutex _(iLock);
    if (!TryGetFileUrlLocked(aTrackId)) {
        return nullptr;
    }
    JsonParser parser;
    parser.Parse(iResponseBody.Buffer());
    const TUint trackId = (TUint)parser.Num("track_id");
    const Brn url = parser.String(kTagFileUrl);
    const TUint formatId = (TUint)(TUint)parser.Num("format_id");
    return new QobuzTrack(iUnixTimestamp, iPipelineObservable, *this, trackId, url, formatId);
}

TBool Qobuz::TryUpdateStreamUrl(QobuzTrack& aTrack)
{
    iTimerSocketActivity->Cancel();
    AutoMutex _(iLock);
    Bws<Ascii::kMaxUintStringBytes> trackId;
    Ascii::AppendDec(trackId, aTrack.Id());
    if (!TryGetFileUrlLocked(trackId)) {
        return false;
    }
    JsonParser parser;
    parser.Parse(iResponseBody.Buffer());
    aTrack.UpdateUrl(parser.String(kTagFileUrl));
    return true;
}

TBool Qobuz::TryGetFileUrlLocked(const Brx& aTrackId)
{
    TBool success = false;
    if (!TryConnect()) {
        LOG_ERROR(kPipeline, "Qobuz::TryGetStreamUrl - connection failure\n");
        return false;
    }
    AutoConnectionQobuz __(*this, iReaderEntity);

    // see https://github.com/Qobuz/api-documentation#request-signature for rules on creating request_sig value
    TUint timestamp;
    try {
        timestamp = iUnixTimestamp.Now();
    }
    catch (UnixTimestampUnavailable&) {
        LOG_ERROR(kPipeline, "Qobuz::TryGetFileUrlLocked - failure to determine network time\n");
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

        iResponseBody.Reset();
        iReaderEntity.ReadAll(iResponseBody);
        success = true;
    }
    catch (Exception& ex) {
        LOG_ERROR(kPipeline, "%s in Qobuz::TryGetFileUrlLocked\n", ex.Message());
    }
    return success;
}

TBool Qobuz::TryGetId(IWriter& aWriter, const Brx& aQuery, QobuzMetadata::EIdType aType, Connection aConnection)
{
    iTimerSocketActivity->Cancel();
    AutoMutex _(iLock);
    
    iPathAndQuery.Replace(kVersionAndFormat);

    iPathAndQuery.Append(QobuzMetadata::IdTypeToString(aType));
    iPathAndQuery.Append("/search?query=");
    Uri::Escape(iPathAndQuery, aQuery);

    return TryGetResponseLocked(aWriter, kHost, 1, 0, aConnection); // return top hit
}

TBool Qobuz::TryGetIds(IWriter& aWriter, const Brx& aGenre, QobuzMetadata::EIdType aType, TUint aLimitPerResponse, Connection aConnection)
{
    iTimerSocketActivity->Cancel();
    AutoMutex _(iLock);

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

    return TryGetResponseLocked(aWriter, kHost, aLimitPerResponse, 0, aConnection);
}

TBool Qobuz::TryGetTracksById(IWriter& aWriter, const Brx& aId, QobuzMetadata::EIdType aType, TUint aLimit, TUint aOffset, Connection aConnection)
{
    iTimerSocketActivity->Cancel();
    AutoMutex _(iLock);

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

    return TryGetResponseLocked(aWriter, kHost, aLimit, aOffset, aConnection);
}

TBool Qobuz::TryGetGenreList(IWriter& aWriter, Connection aConnection)
{
    iTimerSocketActivity->Cancel();
    AutoMutex _(iLock);

    iPathAndQuery.Replace(kVersionAndFormat);
    iPathAndQuery.Append("genre/list?");

    return TryGetResponseLocked(aWriter, kHost, 50, 0, aConnection);
}

TBool Qobuz::TryGetIdsByRequest(IWriter& aWriter, const Brx& aRequestUrl, TUint aLimitPerResponse, TUint aOffset, Connection aConnection)
{
    iTimerSocketActivity->Cancel();
    AutoMutex _(iLock);

    iUri.SetBytes(0);
    Uri::Unescape(iUri, aRequestUrl);
    iRequest.Replace(iUri);
    iPathAndQuery.Replace(iRequest.PathAndQuery());
    return TryGetResponseLocked(aWriter, iRequest.Host(), aLimitPerResponse, aOffset, aConnection);
}

TBool Qobuz::TryGetResponseLocked(IWriter& aWriter, const Brx& aHost, TUint aLimit, TUint aOffset, Connection aConnection)
{
    TBool success = false;
    if (!TryConnect()) {
        LOG_ERROR(kMedia, "Qobuz::TryGetResponseLocked - connection failure\n");
        return false;
    }
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
        const TUint code = WriteRequestReadResponse(Http::kMethodGet, aHost, iPathAndQuery, aConnection);
        if (code != 200) {
            LOG_ERROR(kPipeline, "Http error - %d - in response to Qobuz::TryGetResponseLocked.\n", code);
            LOG_ERROR(kPipeline, "...path/query is %.*s\n", PBUF(iPathAndQuery));
            LOG_ERROR(kPipeline, "Some/all of response is:\n");
            Brn buf = iReaderEntity.Read(kReadBufferBytes);
            LOG_ERROR(kPipeline, "%.*s\n", PBUF(buf));
            THROW(ReaderError);
        }
        iReaderEntity.ReadAll(aWriter);
        success = true;
    }
    catch (Exception& ex) {
        LOG_ERROR(kPipeline, "%s in Qobuz::TryGetResponseLocked\n", ex.Message());
    }
    if (aConnection == Connection::Close) {
        CloseConnection();
    }
    else { // KeepAlive
        iTimerSocketActivity->FireIn(kSocketKeepAliveMs);
    }
    return success;
}

void Qobuz::CloseConnection()
{
    iConnected = false;
    iSocket.Close();
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

void Qobuz::TrackStarted(QobuzTrack& aTrack)
{
    iLockStreamEvents.Wait();
    iPendingStarts.push_back(&aTrack);
    iLockStreamEvents.Signal();
    (void)iSchedulerStreamEvents->TrySchedule();
}

void Qobuz::TrackStopped(QobuzTrack& aTrack)
{
    iLockStreamEvents.Wait();
    iPendingStops.push_back(&aTrack);
    iLockStreamEvents.Signal();
    (void)iSchedulerStreamEvents->TrySchedule();
}

TBool Qobuz::TryConnect()
{
    if (iConnected) {
        return true;
    }
    Endpoint ep;
    try {
        iSocket.Open(iEnv);
        ep.SetAddress(kHost);
        ep.SetPort(kPort);
        iSocket.Connect(ep, kConnectTimeoutMs);
        //iSocket.LogVerbose(true);
        iConnected = true;
    }
    catch (NetworkTimeout&) {
        CloseConnection();
        return false;
    }
    catch (NetworkError&) {
        CloseConnection();
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
    AutoConnectionQobuz _(*this, iReaderEntity);

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
        const TUint code = WriteRequestReadResponse(Http::kMethodGet, kHost, iPathAndQuery, Connection::Close);
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
        iResponseBody.Reset();
        iReaderEntity.ReadAll(iResponseBody);
		const Brx& resp = iResponseBody.Buffer();
        try {
			JsonParser parser;
			parser.Parse(resp);
			iAuthToken.Replace(parser.String(kUserAuthToken));
			iUserId = 0;
			iCredentialId = 0;
            JsonParser parserUser;
            parserUser.Parse(parser.String("user"));
            iUserId = parserUser.Num("id");
            JsonParser parserCred;
            parserCred.Parse(parserUser.String("credential"));
            iCredentialId = parserCred.Num("id");
        }
        catch (Exception& ex) {
            LOG_ERROR(kPipeline, "Exception - %s - parsing credentialId during Qobuz login.  Login response is:\n%.*s\n", ex.Message(), PBUF(resp));
        }

        iCredentialsState.SetState(kId, Brx::Empty(), iAppId);
        updatedStatus = true;
        success = true;

        ScheduleUpdatePurchasedTracks();
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

void Qobuz::NotifyStreamStarted(QobuzTrack& aTrack)
{
    AutoMutex _(iLock);

    if (!TryConnect()) {
        LOG_ERROR(kMedia, "Qobuz::NotifyStreamStarted - connection failure\n");
        return;
    }
    AutoConnectionQobuz __(*this, iReaderEntity);

    iStreamEventBuf.Reset();
    iStreamEventBuf.Write(Brn("events="));
    WriterFormUrl writerFormUrl(iStreamEventBuf);
    WriterJsonArray writerArray(writerFormUrl);
    auto writerObject = writerArray.CreateObject();
    writerObject.WriteBool("online", true);
    writerObject.WriteBool("sample", false);
    writerObject.WriteString("intent", "streaming");
    writerObject.WriteString("device_id", iDeviceId);
    const auto trackId = aTrack.Id();
    writerObject.WriteUint("track_id", trackId);
    writerObject.WriteBool("purchase", IsTrackPurchased(trackId));
    writerObject.WriteUint("date", aTrack.StartTime());
    writerObject.WriteUint("duration", 0);
    writerObject.WriteUint("credential_id", iCredentialId);
    writerObject.WriteUint("user_id", iUserId);
    writerObject.WriteBool("local", false);
    writerObject.WriteInt("format_id", aTrack.FormatId());
    writerObject.WriteEnd();
    writerArray.WriteEnd();

    iPathAndQuery.Replace(kVersionAndFormat);
    iPathAndQuery.Append("track/reportStreamingStart?app_id=");
    iPathAndQuery.Append(iAppId);
    iWriterRequest.WriteMethod(Http::kMethodPost, iPathAndQuery, Http::eHttp11);
    Http::WriteHeaderHostAndPort(iWriterRequest, kHost, kPort);
    Http::WriteHeaderContentLength(iWriterRequest, iStreamEventBuf.Buffer().Bytes());
    Http::WriteHeaderContentType(iWriterRequest, Brn("application/x-www-form-urlencoded"));
    Http::WriteHeaderConnectionClose(iWriterRequest);
    iWriterRequest.WriteFlush();
    iWriteBuffer.Write(iStreamEventBuf.Buffer());
    iWriteBuffer.WriteFlush();

    iReaderResponse.Read();
    const TUint code = iReaderResponse.Status().Code();
    iReaderEntity.Set(iHeaderContentLength, iHeaderTransferEncoding, ReaderHttpEntity::Mode::Client);
    iResponseBody.Reset();
    iReaderEntity.ReadAll(iResponseBody);
    if (code < 200 || code > 299) {
        LOG_ERROR(kPipeline, "Http error - %d - in response to Qobuz track/reportStreamingStart.\n%.*s\n", code, PBUF(iResponseBody.Buffer()));
    }
}

void Qobuz::NotifyStreamStopped(QobuzTrack& aTrack)
{
    AutoMutex _(iLock);

    if (aTrack.PlayedSeconds() == 0) {
        // Qobuz don't cope well with being informed that we didn't play anything
        return;
    }

    if (!TryConnect()) {
        LOG_ERROR(kMedia, "Qobuz::NotifyStreamStarted - connection failure\n");
        return;
    }
    AutoConnectionQobuz __(*this, iReaderEntity);

    iStreamEventBuf.Reset();
    iStreamEventBuf.Write(Brn("events="));
    WriterFormUrl writerFormUrl(iStreamEventBuf);
    WriterJsonArray writerArray(writerFormUrl);
    auto writerObject = writerArray.CreateObject();
    writerObject.WriteInt("user_id", iUserId);
    writerObject.WriteUint("date", aTrack.StartTime());
    writerObject.WriteInt("duration", aTrack.PlayedSeconds());
    writerObject.WriteBool("online", true);
    writerObject.WriteBool("sample", false);
    writerObject.WriteString("device_id", iDeviceId);
    const auto trackId = aTrack.Id();
    writerObject.WriteInt("track_id", trackId);
    writerObject.WriteBool("purchase", IsTrackPurchased(trackId));
    writerObject.WriteBool("local", false);
    writerObject.WriteInt("credential_id", iCredentialId);
    writerObject.WriteInt("format_id", aTrack.FormatId());
    writerObject.WriteEnd();
    writerArray.WriteEnd();

    iPathAndQuery.Replace(kVersionAndFormat);
    iPathAndQuery.Append("track/reportStreamingEnd?app_id=");
    iPathAndQuery.Append(iAppId);
    iWriterRequest.WriteMethod(Http::kMethodPost, iPathAndQuery, Http::eHttp11);
    Http::WriteHeaderHostAndPort(iWriterRequest, kHost, kPort);
    Http::WriteHeaderContentLength(iWriterRequest, iStreamEventBuf.Buffer().Bytes());
    Http::WriteHeaderContentType(iWriterRequest, Brn("application/x-www-form-urlencoded"));
    Http::WriteHeaderConnectionClose(iWriterRequest);
    iWriterRequest.WriteFlush();
    iWriteBuffer.Write(iStreamEventBuf.Buffer());
    iWriteBuffer.WriteFlush();

    iReaderResponse.Read();
    const TUint code = iReaderResponse.Status().Code();
    iReaderEntity.Set(iHeaderContentLength, iHeaderTransferEncoding, ReaderHttpEntity::Mode::Client);
    iResponseBody.Reset();
    iReaderEntity.ReadAll(iResponseBody);
    if (code < 200 || code > 299) {
        LOG_ERROR(kPipeline, "Http error - %d - in response to Qobuz track/reportStreamingEnd.\n%.*s\n", code, PBUF(iResponseBody.Buffer()));
    }
}

TUint Qobuz::WriteRequestReadResponse(const Brx& aMethod, const Brx& aHost, const Brx& aPathAndQuery, Connection aConnection)
{
    iWriterRequest.WriteMethod(aMethod, aPathAndQuery, Http::eHttp11);
    Http::WriteHeaderHostAndPort(iWriterRequest, aHost, kPort);
    if (aConnection == Connection::Close) {
        Http::WriteHeaderConnectionClose(iWriterRequest);
    }
    iWriterRequest.WriteFlush();
    iReaderResponse.Read();
    const TUint code = iReaderResponse.Status().Code();
    iReaderEntity.Set(iHeaderContentLength, iHeaderTransferEncoding, ReaderHttpEntity::Mode::Client);
    return code;
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

void Qobuz::SocketInactive()
{
    AutoMutex _(iLock);
    CloseConnection();
}

void Qobuz::ReportStreamEvents()
{
    iLockStreamEvents.Wait();
    while (iPendingStarts.size() > 0) {
        auto track = iPendingStarts.front();
        iLockStreamEvents.Signal();
        NotifyStreamStarted(*track);
        iLockStreamEvents.Wait();
        iPendingStarts.pop_front();
    }
    while (iPendingStops.size() > 0) {
        auto track = iPendingStops.front();
        iPendingStops.pop_front();
        iLockStreamEvents.Signal();
        NotifyStreamStopped(*track);
        delete track;
        iLockStreamEvents.Wait();
    }
    iLockStreamEvents.Signal();
}

void Qobuz::UpdatePurchasedTracks()
{
    // update this list roughly every couple of hours
    static const TUint kMinUpdateMs = 1000 * 60 * 60; // 1 hour
    static const TUint kUpdateVarianceMs= 1000 * 60 * 60 * 2; // 2 hours
    const TUint nextUpdateMs = kMinUpdateMs + iEnv.Random(kUpdateVarianceMs);
    iTimerPurchasedTracks->FireIn(nextUpdateMs);

    AutoMutex _(iLock);

    if (!TryConnect()) {
        LOG_ERROR(kMedia, "Qobuz::NotifyStreamStarted - connection failure\n");
        return;
    }
    AutoConnectionQobuz __(*this, iReaderEntity);

    iPathAndQuery.Replace(kVersionAndFormat);
    iPathAndQuery.Append("purchase/getUserPurchasesIds?app_id=");
    iPathAndQuery.Append(iAppId);
    iPathAndQuery.Append("&user_auth_token=");
    iPathAndQuery.Append(iAuthToken);

    const TUint code = WriteRequestReadResponse(Http::kMethodGet, kHost, iPathAndQuery);
    if (code != 200) {
        LOG_ERROR(kPipeline, "Http error - %d - in response to Qobuz purchase/getUserPurchasesIds.\n", code);
        THROW(ReaderError);
    }
    static const TUint kMaxResponseBytes = 1024 * 1024; // 1Mb
    if (iHeaderContentLength.Received() && iHeaderContentLength.ContentLength() > kMaxResponseBytes) {
        LOG_ERROR(kPipeline, "Warning: Qobuz purchases too large to process - %u\n", iHeaderContentLength.ContentLength());
        THROW(ReaderError);
    }
    iResponseBody.Reset();
    iReaderEntity.ReadAll(iResponseBody);
    JsonParser parserBody;
    parserBody.Parse(iResponseBody.Buffer());
    JsonParser parserTracks;
    parserTracks.Parse(parserBody.String("tracks"));
    auto parserArray = JsonParserArray::Create(parserTracks.String("items"));
    AutoMutex ___(iLockPurchasedTracks);
    iPurchasedTracks.clear();
    try {
        for (;;) {
            JsonParser parserId;
            parserId.Parse(parserArray.NextObject());
            iPurchasedTracks.push_back((TUint)parserId.Num(("id")));
        }
    }
    catch (JsonArrayEnumerationComplete&) {}
    std::sort(iPurchasedTracks.begin(), iPurchasedTracks.end());
}

void Qobuz::ScheduleUpdatePurchasedTracks()
{
    (void)iSchedulerPurchasedTracks->TrySchedule();
}

TBool Qobuz::IsTrackPurchased(TUint aId) const
{
    AutoMutex ___(iLockPurchasedTracks);
    auto it = std::lower_bound(iPurchasedTracks.begin(), iPurchasedTracks.end(), aId);
    return it != iPurchasedTracks.end();
}


// AutoConnectionQobuz

AutoConnectionQobuz::AutoConnectionQobuz(Qobuz& aQobuz, IReader& aReader)
    : iQobuz(aQobuz)
    , iReader(aReader)
{
}

AutoConnectionQobuz::~AutoConnectionQobuz()
{
    iReader.ReadFlush();
    iQobuz.CloseConnection();
}

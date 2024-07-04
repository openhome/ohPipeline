#include <OpenHome/Av/Qobuz/Qobuz.h>
#include <OpenHome/Av/Credentials.h>
#include <OpenHome/SocketSsl.h>
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
#include <OpenHome/Private/Json.h>
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
                       IQobuzTrackObserver& aObserver, TUint aTrackId, const Brx& aUrl, TUint aFormatId, TBool aIsSample)
    : iLock("QTrk")
    , iUnixTimestamp(aUnixTimestamp)
    , iPipelineObservable(aPipelineObservable)
    , iObserver(aObserver)
    , iTrackId(aTrackId)
    , iStartTime(0)
    , iPlayedSeconds(0)
    , iLastPlayedSeconds(0)
    , iStreamId(Media::IPipelineIdProvider::kStreamIdInvalid)
    , iFormatId(aFormatId)
    , iIsSample(aIsSample)
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
        if (iStarted) {
            return;
        }
        // prevent NotifyStreamInfo setting iCurrentStream
        // Note that this would falsely report a track as complete if the pipeline
        // buffered the entire track before starting to play it.
        iStreamId = Media::IPipelineIdProvider::kStreamIdInvalid;
        reportStopped = !aStopped; // no point reporting anything if we hadn't started playing then the pipeline is cleared
    }
    if (reportStopped) {
        iObserver.TrackStopped(*this, 0, true);
    }
    else {
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

TBool QobuzTrack::IsSample() const
{
    return iIsSample;
}

TUint QobuzTrack::StartTime() const
{
    return iStartTime;
}

void QobuzTrack::NotifyPipelineState(Media::EPipelineState aState)
{
    AutoMutex _(iLock);
    if (iStarted && aState == Media::EPipelinePaused) {
        iObserver.TrackStopped(*this, iPlayedSeconds, false);
        iPlayedSeconds = 0;
        iStarted = false;
    }
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
            iObserver.TrackStopped(*this, iPlayedSeconds, true);
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
        if (!iCurrentStream) {
            return;
        }
        if (aSeconds < iLastPlayedSeconds ||
            aSeconds - iLastPlayedSeconds > 2) { // >2 allows for missing a tick when device is near maxed out
            iObserver.TrackStopped(*this, iPlayedSeconds, false);
            iPlayedSeconds = 0;
            iStarted = false;
        }
        else {
            ++iPlayedSeconds;
        }
        iLastPlayedSeconds = aSeconds;
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
        iObserver.TrackStopped(*this, iPlayedSeconds, true);
    }
}


// Qobuz

const Brn Qobuz::kHost("www.qobuz.com");
const Brn Qobuz::kId("qobuz.com");
const Brn Qobuz::kVersionAndFormat("/api.json/0.2/");
const Brn Qobuz::kConfigKeySoundQuality("qobuz.com.SoundQuality");
const Brn Qobuz::kTagFileUrl("url");

static const TUint kQualityValues[] ={ 5, 6, 7, 27 };

Qobuz::Qobuz(Environment& aEnv, SslContext& aSsl, const Brx& aAppId, const Brx& aAppSecret, const Brx& aUserAgent, const Brx& aDeviceId,
             ICredentialsState& aCredentialsState, IConfigInitialiser& aConfigInitialiser,
             IUnixTimestamp& aUnixTimestamp, IThreadPool& aThreadPool,
             Media::IPipelineObservable& aPipelineObservable)
    : iEnv(aEnv)
    , iLock("QBZ1")
    , iLockConfig("QBZ2")
    , iCredentialsState(aCredentialsState)
    , iUnixTimestamp(aUnixTimestamp)
    , iPipelineObservable(aPipelineObservable)
    , iSocket(aEnv, aSsl, kReadBufferBytes)
    , iReaderBuf(iSocket)
    , iReaderUntil(iReaderBuf)
    , iWriteBuffer(iSocket)
    , iWriterRequest(iWriteBuffer)
    , iReaderResponse(aEnv, iReaderUntil)
    , iReaderEntity(iReaderUntil)
    , iAppId(aAppId)
    , iAppSecret(aAppSecret)
    , iUserAgent(aUserAgent)
    , iDeviceId(aDeviceId)
    , iUsername(kGranularityUsername)
    , iPassword(kGranularityPassword)
    , iResponseBody(2048)
    , iUri(1024)
    , iConnected(false)
    , iLockStreamEvents("QBZ3")
    , iStreamEventBuf(2048)
{
    iTimerSocketActivity = new Timer(aEnv, MakeFunctor(*this, &Qobuz::SocketInactive), "Qobuz-Socket");
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
}

Qobuz::~Qobuz()
{
    iSchedulerStreamEvents->Destroy();
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
    TBool sample = false;
    if (parser.HasKey("sample")) {
        sample = parser.Bool("sample");
    }

    LOG(kMedia, "Qobuz::StreamableTrack TrackUrl: %.*s\n", PBUF(url));
    return new QobuzTrack(iUnixTimestamp, iPipelineObservable, *this, trackId, url, formatId, sample);
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

    const Brn url = parser.String(kTagFileUrl);
    aTrack.UpdateUrl(url);

    LOG(kMedia, "Qobuz::TryUpdateStreamUrl New TrackUrl: %.*s\n", PBUF(url));
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

    Log::Print("Qobuz::TryGetResponse: Request for 'https://%.*s%.*s'\n", PBUF(aHost), PBUF(iPathAndQuery));

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
    catch (AssertionFailed&) {
        throw;
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

    // If there is no username or password, assume the user has logged out and clear our stored auth token.
    if (aUsername.Bytes() == 0 || aPassword.Bytes() == 0) {
        iAuthToken.SetBytes(0);
    }
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

    const TBool hasCredentials = (iUsername.Buffer().Bytes() > 0 && iPassword.Buffer().Bytes() > 0);
    if (iAuthToken.Bytes() == 0)
    {
        const TBool loginSuccess = hasCredentials && TryLoginLocked();
        if (!loginSuccess) {
            THROW(CredentialsLoginFailed);
        }
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
    iPendingReports.push_back(ActivityReport(ActivityReport::Type::Start, &aTrack, 0, false));
    iLockStreamEvents.Signal();
    (void)iSchedulerStreamEvents->TrySchedule();
}

void Qobuz::TrackStopped(QobuzTrack& aTrack, TUint aPlayedSeconds, TBool aComplete)
{
    iLockStreamEvents.Wait();
    iPendingReports.push_back(ActivityReport(ActivityReport::Type::Stop, &aTrack, aPlayedSeconds, aComplete));
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
        ep.SetAddress(kHost);
        ep.SetPort(kPort);
        iSocket.Connect(ep, kHost, kConnectTimeoutMs);
        iConnected = true;
        iSocket.LogVerbose(false);
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
    Uri::EscapeDataString(iPathAndQuery, iUsername.Buffer());
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

            if (!parserCred.IsNull("id")) {
                iCredentialId = parserCred.Num("id");
            }
            else {
                LOG(kPipeline, "Qobuz: Returned user has no 'CredentialId' present. Assuming no active subscription and defaulting to '%d'\n", iCredentialId);
            }
        }
        catch (Exception& ex) {
            LOG_ERROR(kPipeline, "Exception - %s - parsing credentialId during Qobuz login.  Login response is:\n%.*s\n", ex.Message(), PBUF(resp));
        }

        iCredentialsState.SetState(kId, Brx::Empty(), iAppId);
        updatedStatus = true;
        success = true;
    }
    catch (AssertionFailed&) {
        throw;
    }
    catch (Exception& ex) {
        error.Append("Login Error. Please Try Again.");
        LOG_ERROR(kPipeline, "Error in Qobuz::TryLoginLocked (%s)\n", ex.Message());
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
    writerObject.WriteBool("sample", aTrack.IsSample());
    writerObject.WriteString("intent", "streaming");
    writerObject.WriteString("device_id", iDeviceId);
    const auto trackId = aTrack.Id();
    writerObject.WriteUint("track_id", trackId);
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
    if (iUserAgent.Bytes() > 0) {
        iWriterRequest.WriteHeader(Http::kHeaderUserAgent, iUserAgent);
    }
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

void Qobuz::NotifyStreamStopped(QobuzTrack& aTrack, TUint aPlayedSeconds)
{
    AutoMutex _(iLock);

    if (aPlayedSeconds == 0) {
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
    writerObject.WriteInt("duration", aPlayedSeconds);
    writerObject.WriteBool("online", true);
    writerObject.WriteBool("sample", false);
    writerObject.WriteString("intent", "streaming");
    writerObject.WriteString("device_id", iDeviceId);
    const auto trackId = aTrack.Id();
    writerObject.WriteInt("track_id", trackId);
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
    if (iUserAgent.Bytes() > 0) {
        iWriterRequest.WriteHeader(Http::kHeaderUserAgent, iUserAgent);
    }
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
    if (iUserAgent.Bytes() > 0) {
        iWriterRequest.WriteHeader(Http::kHeaderUserAgent, iUserAgent);
    }
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
    while (iPendingReports.size() > 0) {
        auto report = iPendingReports.front();
        iPendingReports.pop_front();
        iLockStreamEvents.Signal();
        auto track = report.iTrack;
        if (report.iType == ActivityReport::Type::Start) {
            NotifyStreamStarted(*track);
        }
        else {
            NotifyStreamStopped(*track, report.iPlayedSeconds);
            if (report.iCompleted) {
                delete track;
            }
        }
        iLockStreamEvents.Wait();
    }
    iLockStreamEvents.Signal();
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

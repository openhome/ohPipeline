#include <OpenHome/Media/Protocol/ProtocolFactory.h>
#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Types.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Av/Tidal/Tidal.h>
#include <OpenHome/Media/SupplyAggregator.h>
#include <OpenHome/Av/Tidal/TidalPins.h>
#include <OpenHome/Av/ProviderOAuth.h>

#include <algorithm>

namespace OpenHome {
namespace Av {


class ProtocolTidal : public Media::ProtocolNetwork, private IReader
{
    static const TUint kMaxErrorReadBytes = 1024;
    static const TUint kTcpConnectTimeoutMs = 10 * 1000;


    static const TUint kMinSupportedTrackVersion = 1;
    static const TUint kMaxSupportedTrackVersion = 2;

public:
    ProtocolTidal(Environment& aEnv, SslContext& aSsl, Tidal::ConfigurationValues& aConfiguration, Credentials& aCredentialsManager,
                  Configuration::IConfigInitialiser& aConfigInitialiser, Net::DvDeviceStandard& aDevice,
                  Media::TrackFactory& aTrackFactory, Net::CpStack& aCpStack,
                  Optional<IPinsInvocable> aPinsInvocable, IThreadPool& aThreadPool, ProviderOAuth& aOAuthManager);
    ~ProtocolTidal();
private: // from Media::Protocol
    void Initialise(Media::MsgFactory& aMsgFactory, Media::IPipelineElementDownstream& aDownstream) override;
    void Interrupt(TBool aInterrupt) override;
    Media::ProtocolStreamResult Stream(const Brx& aUri) override;
    Media::ProtocolGetResult Get(IWriter& aWriter, const Brx& aUri, TUint64 aOffset, TUint aBytes) override;
    void Deactivated() override;
private: // from Media::IStreamHandler
    Media::EStreamPlay OkToPlay(TUint aStreamId) override;
    TUint TrySeek(TUint aStreamId, TUint64 aOffset) override;
    TUint TryStop(TUint aStreamId) override;
private: // from IReader
    Brn Read(TUint aBytes) override;
    void ReadFlush() override;
    void ReadInterrupt() override;
private:
    static TBool TryGetTrackId(const Brx& aQuery,
                               Bwx& aTrackId,
                               WriterBwh& aTokenId);
    Media::ProtocolStreamResult DoStream();
    Media::ProtocolStreamResult DoSeek(TUint64 aOffset);
    TUint WriteRequest(TUint64 aOffset);
    Media::ProtocolStreamResult ProcessContent();
    TBool ContinueStreaming(Media::ProtocolStreamResult aResult);
    TBool IsCurrentStream(TUint aStreamId) const;
private:
    Tidal* iTidal;
    ITokenProvider* iTokenProvider;
    Media::SupplyAggregator* iSupply;
    Uri iUri;
    Bws<kMaxErrorReadBytes> iErrorBuf;
    Bws<12> iTrackId;
    Bws<1024> iStreamUrl;
    Bws<64> iSessionId;
    WriterBwh iTokenId;
    WriterHttpRequest iWriterRequest;
    ReaderUntilS<2048> iReaderUntil;
    ReaderHttpResponse iReaderResponse;
    HttpHeaderContentType iHeaderContentType;
    HttpHeaderContentLength iHeaderContentLength;
    TUint64 iTotalBytes;
    TUint iStreamId;
    TBool iSeekable;
    TBool iSeek;
    TBool iStarted;
    TBool iStopped;
    TUint64 iSeekPos;
    TUint64 iOffset;
    Media::ContentProcessor* iContentProcessor;
    TUint iNextFlushId;
};

};  // namespace Av
};  // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;
using namespace OpenHome::Configuration;

Protocol* ProtocolFactory::NewTidal(Environment& aEnv, 
                                    SslContext& aSsl,
                                    const Brx& aPartnerId,
                                    const Brx& aClientId,
                                    const Brx& aClientSecret,
                                    std::vector<OAuthAppDetails>& aAppDetails,
                                    Av::IMediaPlayer& aMediaPlayer)
{ // static
    const TBool hasPartnerId = aPartnerId.Bytes() > 0;
    const TBool hasIdSecretCombo = aClientId.Bytes() > 0 && aClientSecret.Bytes() > 0;

    ASSERT(hasPartnerId || hasIdSecretCombo);

    Tidal::ConfigurationValues config =
    {
        aPartnerId,
        aClientId,
        aClientSecret,
        aAppDetails
    };

    return new ProtocolTidal(aEnv, aSsl, config, aMediaPlayer.CredentialsManager(),
                             aMediaPlayer.ConfigInitialiser(), aMediaPlayer.Device(),
                             aMediaPlayer.TrackFactory(), aMediaPlayer.CpStack(),
                             aMediaPlayer.PinsInvocable(), aMediaPlayer.ThreadPool(), aMediaPlayer.OAuthManager());
}


// ProtocolTidal
ProtocolTidal::ProtocolTidal(Environment& aEnv, SslContext& aSsl, Tidal::ConfigurationValues& aConfig, Credentials& aCredentialsManager,
                             IConfigInitialiser& aConfigInitialiser, Net::DvDeviceStandard& aDevice,
                             Media::TrackFactory& aTrackFactory, Net::CpStack& aCpStack,
                             Optional<IPinsInvocable> aPinsInvocable, IThreadPool& aThreadPool, ProviderOAuth& aOAuthManager)
    : ProtocolNetwork(aEnv)
    , iTokenProvider(nullptr)
    , iSupply(nullptr)
    , iTokenId(128)
    , iWriterRequest(iWriterBuf)
    , iReaderUntil(iReaderBuf)
    , iReaderResponse(aEnv, iReaderUntil)
    , iTotalBytes(0)
    , iSeekable(false)
{
    iReaderResponse.AddHeader(iHeaderContentType);
    iReaderResponse.AddHeader(iHeaderContentLength);

    iTidal = new Tidal(aEnv, aSsl, aConfig, aCredentialsManager, aConfigInitialiser, aThreadPool);
    aCredentialsManager.Add(iTidal);

    if (aConfig.SupportsOAuth())
    {
        aOAuthManager.AddService(Tidal::kId,
                                 Tidal::kMaximumNumberOfShortLivedTokens,
                                 Tidal::kMaximumNumberOfLongLivedTokens,
                                 *iTidal,
                                 *iTidal);

        iTokenProvider = aOAuthManager.GetTokenProvider(Tidal::kId);
        iTidal->SetTokenProvider(iTokenProvider);
    }

    if (aPinsInvocable.Ok()) {
        auto pins = new TidalPins(*iTidal, aEnv, aDevice, aTrackFactory, aCpStack, aThreadPool);
        aPinsInvocable.Unwrap().Add(pins);
    }
}

ProtocolTidal::~ProtocolTidal()
{
    delete iSupply;
}

void ProtocolTidal::Initialise(MsgFactory& aMsgFactory, IPipelineElementDownstream& aDownstream)
{
    iSupply = new SupplyAggregatorBytes(aMsgFactory, aDownstream);
}

void ProtocolTidal::Interrupt(TBool aInterrupt)
{
    iLock.Wait();
    if (iActive) {
        LOG(kMedia, "ProtocolTidal::Interrupt(%u)\n", aInterrupt);
        if (aInterrupt) {
            iStopped = true;
        }
        iTcpClient.Interrupt(aInterrupt);
        iTidal->Interrupt(aInterrupt);
    }
    iLock.Signal();
}

ProtocolStreamResult ProtocolTidal::Stream(const Brx& aUri)
{
    iTotalBytes = iSeekPos = iOffset = 0;
    iStreamId = IPipelineIdProvider::kStreamIdInvalid;
    iSeekable = iSeek = iStarted = iStopped = false;
    iContentProcessor = nullptr;
    iNextFlushId = MsgFlush::kIdInvalid;
    iTidal->Interrupt(false);
    iUri.Replace(aUri);

    if (iUri.Scheme() != Brn("tidal")) {
        return EProtocolErrorNotSupported;
    }
    LOG(kMedia, "ProtocolTidal::Stream(%.*s)\n", PBUF(aUri));
    if (!TryGetTrackId(iUri.Query(), iTrackId, iTokenId)) {
        return EProtocolStreamErrorUnrecoverable;
    }


    // Tracks that don't specify an OAuth token ID (a V2 uri) will attempt to use the
    // first token found that's available and valid.
    //
    // If no token can be found, we'll attempt to fallback to using the provided
    // username & password.
    if (iTokenId.Buffer().Bytes() == 0)
    {
        LOG_INFO(kPipeline, "ProtocolTidal::Stream - No tokenId present. Will attempt to find a suitable token.\n");

        if (iTokenProvider != nullptr && iTokenProvider->TryGetFirstValidTokenId(iTokenId))
        {
            LOG_INFO(kPipeline, "ProtocolTidal::Stream - Found valid token: %.*s. Using for playback...\n", PBUF(iTokenId.Buffer()));
        }
        else
        {
            LOG_INFO(kPipeline, "ProtocolTidal::Stream - No token found. Falling back to username & password credentials.\n");
        }
    }


    const TBool hasTokenId = iTokenId.Buffer().Bytes() > 0;

    if (hasTokenId)
    {
        if (!iTokenProvider->HasToken(iTokenId.Buffer()))
        {
            LOG_ERROR(kPipeline, "ProtocolTidal::Stream - no tokenId present with the following key: '%.*s'\n", PBUF(iTokenId.Buffer()));
            return EProtocolStreamErrorUnrecoverable;
        }
        else
        {
            if (!iTokenProvider->EnsureTokenIsValid(iTokenId.Buffer()))
            {
                LOG_ERROR(kPipeline, "ProtocolTidal::Stream - token '%.*s' is no longer valid.\n", PBUF(iTokenId.Buffer()));
                return EProtocolStreamErrorUnrecoverable;
            }
        }
    }
    else
    {
        if (iSessionId.Bytes() == 0 && !iTidal->TryLogin(iSessionId))
        {
            return EProtocolStreamErrorUnrecoverable;
        }
    }

    // Token / Credentials available, try get the streamable URI from tidal
    if (!iTidal->TryGetStreamUrl(iTrackId, iTokenId.Buffer(), iStreamUrl))
    {
        if (hasTokenId)
        {
            if (!iTokenProvider->EnsureTokenIsValid(iTokenId.Buffer())
                    || !iTidal->TryGetStreamUrl(iTrackId, iTokenId.Buffer(), iStreamUrl))
            {
                LOG_ERROR(kPipeline, "ProtocolTidal::Stream - token '%.*s' is no longer valid or has failed to obtain a stream URL.\n", PBUF(iTokenId.Buffer()));
                return EProtocolStreamErrorUnrecoverable;
            }
        }
        else
        {
            // attempt logout, login, getStreamUrl to see if that fixes things
            (void)iTidal->TryLogout(iSessionId);

            if (!iTidal->TryLogin(iSessionId)
                  || !iTidal->TryGetStreamUrl(iTrackId, iTokenId.Buffer(), iStreamUrl))
            {
                LOG_ERROR(kPipeline, "ProtocolTidal::Stream - failed to relogin or obtain a valid stream URL.\n");
                return EProtocolStreamErrorUnrecoverable;
            }
        }
    }


    iUri.Replace(iStreamUrl);

    ProtocolStreamResult res = DoStream();
    if (res == EProtocolStreamErrorUnrecoverable) {
        return res;
    }
    while (ContinueStreaming(res)) {
        if (iStopped) {
            res = EProtocolStreamStopped;
            break;
        }
        if (iSeek) {
            iLock.Wait();
            iSupply->OutputFlush(iNextFlushId);
            iNextFlushId = MsgFlush::kIdInvalid;
            iOffset = iSeekPos;
            iSeek = false;
            iLock.Signal();
            res = DoSeek(iOffset);
        }
        else {
            // FIXME - if stream is non-seekable, set ErrorUnrecoverable as soon as Connect succeeds
            /* FIXME - reconnects should use extra http headers to check that content hasn't changed
               since our first attempt at reading it.  Any change should result in ErrorUnrecoverable */
            TUint code = WriteRequest(iOffset);
            if (code != 0) {
                iTotalBytes = iHeaderContentLength.ContentLength();
                res = ProcessContent();
            }
        }
        if (res == EProtocolStreamErrorRecoverable) {
            Thread::Sleep(50);
        }
    }

    iLock.Wait();
    if ((iStopped || iSeek) && iNextFlushId != MsgFlush::kIdInvalid) {
        iSupply->OutputFlush(iNextFlushId);
    }
    // clear iStreamId to prevent TrySeek or TryStop returning a valid flush id
    iStreamId = IPipelineIdProvider::kStreamIdInvalid;
    iLock.Signal();

    return res;
}

ProtocolGetResult ProtocolTidal::Get(IWriter& /*aWriter*/, const Brx& /*aUri*/, TUint64 /*aOffset*/, TUint /*aBytes*/)
{
    return EProtocolGetErrorNotSupported;
}

void ProtocolTidal::Deactivated()
{
    if (iContentProcessor != nullptr) {
        iContentProcessor->Reset();
        iContentProcessor = nullptr;
    }
    iReaderUntil.ReadFlush();
    Close();
}

EStreamPlay ProtocolTidal::OkToPlay(TUint aStreamId)
{
    LOG(kMedia, "ProtocolTidal::OkToPlay(%u)\n", aStreamId);
    return iIdProvider->OkToPlay(aStreamId);
}

TUint ProtocolTidal::TrySeek(TUint aStreamId, TUint64 aOffset)
{
    LOG(kMedia, "ProtocolTidal::TrySeek\n");

    iLock.Wait();
    const TBool streamIsValid = IsCurrentStream(aStreamId);
    if (streamIsValid) {
        iSeek = true;
        iSeekPos = aOffset;
        if (iNextFlushId == MsgFlush::kIdInvalid) {
            /* If a valid flushId is set then We've previously promised to send a Flush but haven't
               got round to it yet.  Re-use the same id for any other requests that come in before
               our main thread gets a chance to issue a Flush */
            iNextFlushId = iFlushIdProvider->NextFlushId();
        }
    }
    iLock.Signal();
    if (!streamIsValid) {
        return MsgFlush::kIdInvalid;
    }
    iTcpClient.Interrupt(true);
    return iNextFlushId;
}

TUint ProtocolTidal::TryStop(TUint aStreamId)
{
    iLock.Wait();
    const TBool stop = IsCurrentStream(aStreamId);
    if (stop) {
        if (iNextFlushId == MsgFlush::kIdInvalid) {
            /* If a valid flushId is set then We've previously promised to send a Flush but haven't
               got round to it yet.  Re-use the same id for any other requests that come in before
               our main thread gets a chance to issue a Flush */
            iNextFlushId = iFlushIdProvider->NextFlushId();
        }
        iStopped = true;
        iTcpClient.Interrupt(true);
    }
    iLock.Signal();
    return (stop? iNextFlushId : MsgFlush::kIdInvalid);
}

Brn ProtocolTidal::Read(TUint aBytes)
{
    Brn buf = iReaderUntil.Read(aBytes);
    iOffset += buf.Bytes();
    return buf;
}

void ProtocolTidal::ReadFlush()
{
    iReaderUntil.ReadFlush();
}

void ProtocolTidal::ReadInterrupt()
{
    iReaderUntil.ReadInterrupt();
}

TBool ProtocolTidal::TryGetTrackId(const Brx& aQuery,
                                   Bwx& aTrackId,
                                   WriterBwh& aTokenId)
{
    // static
    aTokenId.Reset();
    aTrackId.Replace(Brx::Empty());

    Parser parser(aQuery);
    (void)parser.Next('?');

    TUint version = 0;
    Brn versionStr = Brx::Empty();
    TBool parseComplete = false;

    do
    {
        Brn key = parser.Next('=');
        Brn val = parser.Next('&');

        //Exit condition
        if (val.Bytes() == 0)
        {
            val.Set(parser.Remaining());
            parseComplete = true;
        }


        if (key == Brn("version"))
        {
            versionStr = val;
        }
        else if (key == Brn("trackId"))
        {
            aTrackId.Replace(val);
        }
        else if (key == Brn("token"))
        {
            aTokenId.Write(val);
        }

    } while (!parseComplete);

    // Validate version...
    try
    {
        version = Ascii::Uint(versionStr);
    } catch (AsciiError&)
    {
        LOG_ERROR(kPipeline, "TryGetTrackId failed - invalid version\n");
        return false;
    }

    const TBool tooOld = version < kMinSupportedTrackVersion;
    const TBool tooNew = version > kMaxSupportedTrackVersion;

    if (tooOld || tooNew)
    {
        LOG_ERROR(kPipeline, "TryGetTrackId failed - unsupported version: %u (Min: %u, Max: %u)\n", version, kMinSupportedTrackVersion, kMaxSupportedTrackVersion);
        return false;
    }

    // Validate TrackId...
    if (aTrackId.Bytes() == 0)
    {
        LOG_ERROR(kPipeline, "TryGetTrackId failed - no track id value\n");
        return false;
    }

    // - V2
    // Valiate tokenId...
    Log::Print("%.*s\n", PBUF(aTokenId.Buffer()));
    if (version == 2 && aTokenId.Buffer().Bytes() == 0)
    {
        LOG_ERROR(kPipeline, "TryGetTrackId failed - no token id value\n");
        return false;
    }
    else if (version == 1)
    {
        // If for whatever reason a CP tries to pass in a TokenId
        // as part of a V1 track, then we'll ignore it and set it to
        // empty so no attempt is made to use it in the future...
        aTokenId.Reset();
    }

    return true;
}

TBool ProtocolTidal::ContinueStreaming(ProtocolStreamResult aResult)
{
    AutoMutex a(iLock);
    if (aResult == EProtocolStreamErrorRecoverable) {
        return true;
    }
    return false;
}

TBool ProtocolTidal::IsCurrentStream(TUint aStreamId) const
{
    if (iStreamId != aStreamId || aStreamId == IPipelineIdProvider::kStreamIdInvalid) {
        return false;
    }
    return true;
}

ProtocolStreamResult ProtocolTidal::DoStream()
{
    TUint code = WriteRequest(0);
    iSeekable = false;
    iTotalBytes = iHeaderContentLength.ContentLength();

    if (code != HttpStatus::kPartialContent.Code() && code != HttpStatus::kOk.Code()) {
        iErrorBuf.SetBytes(0);
        const TUint bytesToRead = std::min(iTotalBytes, static_cast<TUint64>(kMaxErrorReadBytes));

        try {
            while(iErrorBuf.Bytes() < bytesToRead) {
                const TUint bytesLeft = bytesToRead - iErrorBuf.Bytes();
                iErrorBuf.Append(iReaderUntil.Read(bytesLeft));
            }
        }
        catch (ReaderError&){
            // If we do't have enough (or any) of additional error information, it's not the end of the world.
        }

        if (iErrorBuf.Bytes() > 0) {
            LOG_ERROR(kPipeline, "ProtocolTidal::DoStream server returned error %u\nSome (or all) of the response is:\n%.*s\n", code, PBUF(iErrorBuf))
        }
        else {
            LOG_ERROR(kPipeline, "ProtocolTidal::DoStream server returned error %u\n", code);
        }

        return EProtocolStreamErrorUnrecoverable;
    }
    if (code == HttpStatus::kPartialContent.Code()) {
        if (iTotalBytes > 0) {
            iSeekable = true;
        }
        LOG(kMedia, "ProtocolTidal::DoStream 'Partial Content' seekable=%d (%lld bytes)\n", iSeekable, iTotalBytes);
    }
    else { // code == HttpStatus::kOk.Code()
        LOG(kMedia, "ProtocolTidal::DoStream 'OK' non-seekable (%lld bytes)\n", iTotalBytes);
    }

    return ProcessContent();
}

TUint ProtocolTidal::WriteRequest(TUint64 aOffset)
{
    iReaderUntil.ReadFlush();
    Close();
    TUint port = (iUri.Port() == -1? 80 : (TUint)iUri.Port());
    if (!Connect(iUri, port, kTcpConnectTimeoutMs)) {
        LOG_ERROR(kPipeline, "ProtocolTidal::WriteRequest Connection failure\n");
        return 0;
    }

    try {
        LOG(kMedia, "ProtocolTidal::WriteRequest send request\n");
        iWriterRequest.WriteMethod(Http::kMethodGet, iUri.PathAndQuery(), Http::eHttp11);
        port = (iUri.Port() == -1? 80 : (TUint)iUri.Port());
        Http::WriteHeaderHostAndPort(iWriterRequest, iUri.Host(), port);
        Http::WriteHeaderConnectionClose(iWriterRequest);
        Http::WriteHeaderRangeFirstOnly(iWriterRequest, aOffset);
        iWriterRequest.WriteFlush();
    }
    catch(WriterError&) {
        LOG_ERROR(kPipeline, "ProtocolTidal::WriteRequest WriterError\n");
        return 0;
    }

    try {
        LOG(kMedia, "ProtocolTidal::WriteRequest read response\n");
        //iTcpClient.LogVerbose(true);
        iReaderResponse.Read();
        //iTcpClient.LogVerbose(false);
    }
    catch(HttpError&) {
        LOG_ERROR(kPipeline, "ProtocolTidal::WriteRequest HttpError\n");
        return 0;
    }
    catch(ReaderError&) {
        LOG_ERROR(kPipeline, "ProtocolTidal::WriteRequest ReaderError\n");
        return 0;
    }
    const TUint code = iReaderResponse.Status().Code();
    LOG(kMedia, "ProtocolTidal::WriteRequest response code %d\n", code);
    return code;
}

ProtocolStreamResult ProtocolTidal::ProcessContent()
{
    if (!iStarted) {
        iStreamId = iIdProvider->NextStreamId();
        iSupply->OutputStream(iUri.AbsoluteUri(), iTotalBytes, iOffset, iSeekable, false, Multiroom::Allowed, *this, iStreamId);
        iStarted = true;
    }
    iContentProcessor = iProtocolManager->GetAudioProcessor();
    auto res = iContentProcessor->Stream(*this, iTotalBytes);

    if (res == EProtocolStreamErrorRecoverable && !(iSeek || iStopped))
    {
        TBool validCredentials = false;
        const TBool isV2Track = iTokenId.Buffer().Bytes() > 0;

        if (isV2Track)
        {
            validCredentials = iTokenProvider->EnsureTokenIsValid(iTokenId.Buffer());
        }
        else
        {
            //Assuming V1 here...
            validCredentials = iTidal->TryReLogin(iSessionId, iSessionId);
        }

        if (validCredentials && iTidal->TryGetStreamUrl(iTrackId, iTokenId.Buffer(), iStreamUrl))
        {
            iUri.Replace(iStreamUrl);
        }
    }
    return res;
}

ProtocolStreamResult ProtocolTidal::DoSeek(TUint64 aOffset)
{
    Interrupt(false);
    const TUint code = WriteRequest(aOffset);
    if (code == 0) {
        return EProtocolStreamErrorRecoverable;
    }
    iTotalBytes = iHeaderContentLength.ContentLength();
    if (code != HttpStatus::kPartialContent.Code()) {
        return EProtocolStreamErrorUnrecoverable;
    }

    return ProcessContent();
}

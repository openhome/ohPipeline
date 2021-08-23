#include <OpenHome/Media/Protocol/ProtocolFactory.h>
#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Types.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Av/Qobuz/Qobuz.h>
#include <OpenHome/Media/SupplyAggregator.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Av/Qobuz/QobuzPins.h>

namespace OpenHome {
    class IUnixTimestamp;
namespace Av {

class ProtocolQobuz : public Media::ProtocolNetwork, private IReader
{
    static const TUint kTcpConnectTimeoutMs = 10 * 1000;
public:
    ProtocolQobuz(Environment& aEnv, SslContext& aSsl, const Brx& aAppId, const Brx& aAppSecret,
                   Credentials& aCredentialsManager, Configuration::IConfigInitialiser& aConfigInitialiser,
                   IUnixTimestamp& aUnixTimestamp, Net::DvDeviceStandard& aDevice,
                   Media::TrackFactory& aTrackFactory, Net::CpStack& aCpStack, Optional<IPinsInvocable> aPinsInvocable,
                   IThreadPool& aThreadPool, Media::IPipelineObservable& aPipelineObservable);
    ~ProtocolQobuz();
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
private: // from IProtocolReader
    Brn Read(TUint aBytes) override;
    void ReadFlush() override;
    void ReadInterrupt() override;
private:
    static TBool TryGetTrackId(const Brx& aQuery, Bwx& aTrackId);
    Media::ProtocolStreamResult DoStream();
    Media::ProtocolStreamResult DoSeek(TUint64 aOffset);
    TUint WriteRequest(TUint64 aOffset);
    Media::ProtocolStreamResult ProcessContent();
    TBool ContinueStreaming(Media::ProtocolStreamResult aResult);
    TBool IsCurrentStream(TUint aStreamId) const;
private:
    Qobuz* iQobuz;
    Media::SupplyAggregator* iSupply;
    Uri iUri;
    Bws<12> iTrackId;
    QobuzTrack* iQobuzTrack;
    Bws<64> iSessionId;
    WriterHttpRequest iWriterRequest;
    ReaderUntilS<2048> iReaderUntil;
    ReaderHttpResponse iReaderResponse;
    ReaderHttpChunked iDechunker;
    HttpHeaderContentType iHeaderContentType;
    HttpHeaderContentLength iHeaderContentLength;
    HttpHeaderTransferEncoding iHeaderTransferEncoding;
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

class AutoQobuzTrack
{
public:
    AutoQobuzTrack(QobuzTrack& aTrack, TBool& aStopped);
    ~AutoQobuzTrack();
private:
    QobuzTrack& iTrack;
    TBool& iStopped;
};

};  // namespace Av
};  // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;
using namespace OpenHome::Configuration;


Protocol* ProtocolFactory::NewQobuz(const Brx& aAppId, const Brx& aAppSecret, Av::IMediaPlayer& aMediaPlayer)
{ // static
    return new ProtocolQobuz(aMediaPlayer.Env(), aMediaPlayer.Ssl(), aAppId, aAppSecret,
                             aMediaPlayer.CredentialsManager(), aMediaPlayer.ConfigInitialiser(),
                             aMediaPlayer.UnixTimestamp(), aMediaPlayer.Device(), 
                             aMediaPlayer.TrackFactory(), aMediaPlayer.CpStack(), aMediaPlayer.PinsInvocable(), 
                             aMediaPlayer.ThreadPool(), aMediaPlayer.Pipeline());
}


// ProtocolQobuz

ProtocolQobuz::ProtocolQobuz(Environment& aEnv, SslContext& aSsl, const Brx& aAppId, const Brx& aAppSecret,
                             Credentials& aCredentialsManager, IConfigInitialiser& aConfigInitialiser,
                             IUnixTimestamp& aUnixTimestamp, Net::DvDeviceStandard& aDevice, 
                             Media::TrackFactory& aTrackFactory, Net::CpStack& aCpStack, Optional<IPinsInvocable> aPinsInvocable, 
                             IThreadPool& aThreadPool, Media::IPipelineObservable& aPipelineObservable)
    : ProtocolNetwork(aEnv)
    , iSupply(nullptr)
    , iQobuzTrack(nullptr)
    , iWriterRequest(iWriterBuf)
    , iReaderUntil(iReaderBuf)
    , iReaderResponse(aEnv, iReaderUntil)
    , iDechunker(iReaderUntil)
    , iTotalBytes(0)
    , iSeekable(false)
{
    iReaderResponse.AddHeader(iHeaderContentType);
    iReaderResponse.AddHeader(iHeaderContentLength);
    iReaderResponse.AddHeader(iHeaderTransferEncoding);

    iQobuz = new Qobuz(aEnv, aSsl, aAppId, aAppSecret, aDevice.Udn(), aCredentialsManager,
                       aConfigInitialiser, aUnixTimestamp, aThreadPool, aPipelineObservable);
    aCredentialsManager.Add(iQobuz);

    if (aPinsInvocable.Ok()) {
        auto pins = new QobuzPins(*iQobuz, aEnv, aDevice, aTrackFactory, aCpStack, aThreadPool);
        aPinsInvocable.Unwrap().Add(pins);
    }
}

ProtocolQobuz::~ProtocolQobuz()
{
    delete iSupply;
}

void ProtocolQobuz::Initialise(MsgFactory& aMsgFactory, IPipelineElementDownstream& aDownstream)
{
    iSupply = new SupplyAggregatorBytes(aMsgFactory, aDownstream);
}

void ProtocolQobuz::Interrupt(TBool aInterrupt)
{
    iLock.Wait();
    if (iActive) {
        LOG(kMedia, "ProtocolQobuz::Interrupt(%u)\n", aInterrupt);
        if (aInterrupt) {
            iStopped = true;
        }
        iTcpClient.Interrupt(aInterrupt);
        iQobuz->Interrupt(aInterrupt);
    }
    iLock.Signal();
}

ProtocolStreamResult ProtocolQobuz::Stream(const Brx& aUri)
{
    iTotalBytes = iSeekPos = iOffset = 0;
    iStreamId = IPipelineIdProvider::kStreamIdInvalid;
    iSeekable = iSeek = iStarted = iStopped = false;
    iContentProcessor = nullptr;
    iNextFlushId = MsgFlush::kIdInvalid;
    iQobuz->Interrupt(false);
    iUri.Replace(aUri);

    if (iUri.Scheme() != Brn("qobuz")) {
        return EProtocolErrorNotSupported;
    }
    LOG(kMedia, "ProtocolQobuz::Stream(%.*s)\n", PBUF(aUri));
    if (!TryGetTrackId(iUri.Query(), iTrackId)) {
        return EProtocolStreamErrorUnrecoverable;
    }

    ProtocolStreamResult res = EProtocolStreamErrorUnrecoverable;
    iQobuzTrack = iQobuz->StreamableTrack(iTrackId);
    if (iQobuzTrack == nullptr) {
        // any error might be due to our session having expired
        // attempt login, getStreamUrl to see if that fixes things
        if (iQobuz->TryLogin()) {
            iQobuzTrack = iQobuz->StreamableTrack(iTrackId);
        }
        if (iQobuzTrack == nullptr) {
            return EProtocolStreamErrorUnrecoverable;
        }
    }
    AutoQobuzTrack _(*iQobuzTrack, iStopped);
    iUri.Replace(iQobuzTrack->Url());

    res = DoStream();
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
    // clear iStreamId to prevent TrySeek or TryStop later returning a valid flush id
    iStreamId = IPipelineIdProvider::kStreamIdInvalid;
    iLock.Signal();

    return res;
}

ProtocolGetResult ProtocolQobuz::Get(IWriter& /*aWriter*/, const Brx& /*aUri*/, TUint64 /*aOffset*/, TUint /*aBytes*/)
{
    return EProtocolGetErrorNotSupported;
}

void ProtocolQobuz::Deactivated()
{
    if (iContentProcessor != nullptr) {
        iContentProcessor->Reset();
        iContentProcessor = nullptr;
    }
    iDechunker.ReadFlush();
    Close();
}

EStreamPlay ProtocolQobuz::OkToPlay(TUint aStreamId)
{
    LOG(kMedia, "ProtocolQobuz::OkToPlay(%u)\n", aStreamId);
    return iIdProvider->OkToPlay(aStreamId);
}

TUint ProtocolQobuz::TrySeek(TUint aStreamId, TUint64 aOffset)
{
    LOG(kMedia, "ProtocolQobuz::TrySeek\n");

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

TUint ProtocolQobuz::TryStop(TUint aStreamId)
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

Brn ProtocolQobuz::Read(TUint aBytes)
{
    Brn buf = iDechunker.Read(aBytes);
    iOffset += buf.Bytes();
    return buf;
}

void ProtocolQobuz::ReadFlush()
{
    iDechunker.ReadFlush();
}

void ProtocolQobuz::ReadInterrupt()
{
    iDechunker.ReadInterrupt();
}

TBool ProtocolQobuz::TryGetTrackId(const Brx& aQuery, Bwx& aTrackId)
{ // static
    Parser parser(aQuery);
    (void)parser.Next('?');
    Brn buf = parser.Next('=');
    if (buf != Brn("version")) {
        LOG_ERROR(kPipeline, "TryGetTrackId failed - no version\n");
        return false;
    }
    Brn verBuf = parser.Next('&');
    try {
        const TUint ver = Ascii::Uint(verBuf);
        if (ver != 2) {
            LOG_ERROR(kPipeline, "TryGetTrackId failed - unsupported version - %d\n", ver);
            return false;
        }
    }
    catch (AsciiError&) {
        LOG_ERROR(kPipeline, "TryGetTrackId failed - invalid version\n");
        return false;
    }
    buf.Set(parser.Next('='));
    if (buf != Brn("trackId")) {
        LOG_ERROR(kPipeline, "TryGetTrackId failed - no track id tag\n");
        return false;
    }
    aTrackId.Replace(parser.Remaining());
    if (aTrackId.Bytes() == 0) {
        LOG_ERROR(kPipeline, "TryGetTrackId failed - no track id value\n");
        return false;
    }
    return true;
}

TBool ProtocolQobuz::ContinueStreaming(ProtocolStreamResult aResult)
{
    AutoMutex a(iLock);
    if (aResult == EProtocolStreamErrorRecoverable) {
        return true;
    }
    return false;
}

TBool ProtocolQobuz::IsCurrentStream(TUint aStreamId) const
{
    if (iStreamId != aStreamId || aStreamId == IPipelineIdProvider::kStreamIdInvalid) {
        return false;
    }
    return true;
}

ProtocolStreamResult ProtocolQobuz::DoStream()
{
    TUint code = WriteRequest(0);
    iSeekable = false;
    iTotalBytes = iHeaderContentLength.ContentLength();

    if (code != HttpStatus::kPartialContent.Code() && code != HttpStatus::kOk.Code()) {
        LOG(kPipeline, "ProtocolQobuz::DoStream server returned error %u\n", code);
        return EProtocolStreamErrorUnrecoverable;
    }
    if (code == HttpStatus::kPartialContent.Code()) {
        if (iTotalBytes > 0) {
            iSeekable = true;
        }
        LOG(kMedia, "ProtocolQobuz::DoStream 'Partial Content' seekable=%d (%lld bytes)\n", iSeekable, iTotalBytes);
    }
    else { // code == HttpStatus::kOk.Code()
        LOG(kMedia, "ProtocolQobuz::DoStream 'OK' non-seekable (%lld bytes)\n", iTotalBytes);
    }
    iDechunker.SetChunked(iHeaderTransferEncoding.IsChunked());

    return ProcessContent();
}

TUint ProtocolQobuz::WriteRequest(TUint64 aOffset)
{
    iDechunker.ReadFlush();
    Close();
    TUint port = (iUri.Port() == -1? 80 : (TUint)iUri.Port());
    if (!Connect(iUri, port, kTcpConnectTimeoutMs)) {
        LOG(kPipeline, "ProtocolQobuz::WriteRequest Connection failure\n");
        return 0;
    }

    try {
        LOG(kMedia, "ProtocolQobuz::WriteRequest send request\n");
        iWriterRequest.WriteMethod(Http::kMethodGet, iUri.PathAndQuery(), Http::eHttp11);
        port = (iUri.Port() == -1? 80 : (TUint)iUri.Port());
        Http::WriteHeaderHostAndPort(iWriterRequest, iUri.Host(), port);
        Http::WriteHeaderConnectionClose(iWriterRequest);
        Http::WriteHeaderRangeFirstOnly(iWriterRequest, aOffset);
        iWriterRequest.WriteFlush();
    }
    catch(WriterError&) {
        LOG_ERROR(kPipeline, "ProtocolQobuz::WriteRequest WriterError\n");
        return 0;
    }

    try {
        LOG(kMedia, "ProtocolQobuz::WriteRequest read response\n");
        //iTcpClient.LogVerbose(true);
        iReaderResponse.Read();
        //iTcpClient.LogVerbose(false);
    }
    catch (AssertionFailed&) {
        throw;
    }
    catch (Exception& ex) {
        LOG_ERROR(kPipeline, "ProtocolQobuz::WriteRequest %s\n", ex.Message());
        return 0;
    }

    const TUint code = iReaderResponse.Status().Code();
    LOG(kMedia, "ProtocolQobuz::WriteRequest response code %d\n", code);
    return code;
}

ProtocolStreamResult ProtocolQobuz::ProcessContent()
{
    if (!iStarted) {
        iStreamId = iIdProvider->NextStreamId();
        iQobuzTrack->ProtocolStarted(iStreamId);
        iSupply->OutputStream(iUri.AbsoluteUri(), iTotalBytes, iOffset, iSeekable, false, Multiroom::Allowed, *this, iStreamId);
        iStarted = true;
    }
    iContentProcessor = iProtocolManager->GetAudioProcessor();
    auto res = iContentProcessor->Stream(*this, iTotalBytes);
    if (res == EProtocolStreamErrorRecoverable && !(iSeek || iStopped)) {
        if (iQobuz->TryUpdateStreamUrl(*iQobuzTrack)) {
            iUri.Replace(iQobuzTrack->Url());
        }
    }
    return res;
}

ProtocolStreamResult ProtocolQobuz::DoSeek(TUint64 aOffset)
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


// AutoQobuzTrack

AutoQobuzTrack::AutoQobuzTrack(QobuzTrack& aTrack, TBool& aStopped)
    : iTrack(aTrack)
    , iStopped(aStopped)
{
}

AutoQobuzTrack::~AutoQobuzTrack()
{
    iTrack.ProtocolCompleted(iStopped);
}

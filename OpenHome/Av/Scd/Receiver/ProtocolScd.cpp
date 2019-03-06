#include <OpenHome/Av/Scd/Receiver/ProtocolScd.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Av/Scd/ScdMsg.h>
#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Av/Scd/Receiver/SupplyScd.h>
#include <OpenHome/Av/OhMetadata.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Av/Debug.h>

#include <memory>

using namespace OpenHome;
using namespace OpenHome::Scd;
using namespace OpenHome::Media;

const TUint ProtocolScd::kVersionMajor = 1;
const TUint ProtocolScd::kVersionMinor = 0;

ProtocolScd::ProtocolScd(Environment& aEnv,
                         Media::TrackFactory& aTrackFactory,
                         TUint aDsdSampleBlockWords, 
                         TUint aDsdPadBytesPerChunk,
                         IScdObserver& aObserver)
    : ProtocolNetwork(aEnv)
    , iLock("PSCD")
    , iScdFactory(1, // Ready
                  1, // MetadataDidl
                  1, // MetadataOh
                  2, // Format
                  2, // FormatDsd
                  0, // AudioOut
                  1, // AudioIn
                  1, // MetatextDidl
                  1, // MetatextOh
                  1, // Halt
                  1, // Disconnect
                  0, // Seek - currently unsupported
                  0  // Skip - currently unsupported
                  )
    , iTrackFactory(aTrackFactory)
    , iDsdSampleBlockWords(aDsdSampleBlockWords)
    , iDsdPadBytesPerChunk(aDsdPadBytesPerChunk)
    , iObserver(aObserver)
{
    Debug::AddLevel(Debug::kScd);
    iObserver.NotifyScdConnectionChange(false);
}

void ProtocolScd::Initialise(Media::MsgFactory& aMsgFactory, Media::IPipelineElementDownstream& aDownstream)
{
    iSupply.reset(new SupplyScd(aMsgFactory, aDownstream, iDsdSampleBlockWords, iDsdPadBytesPerChunk));
}

void ProtocolScd::Interrupt(TBool aInterrupt)
{
    AutoMutex _(iLock);
    if (aInterrupt) {
        iStopped = true;
    }
    iTcpClient.Interrupt(aInterrupt);
}

ProtocolStreamResult ProtocolScd::Stream(const Brx& aUri)
{
    iUri.Replace(aUri);
    if (iUri.Scheme() != Brn("scd")) {
        return EProtocolErrorNotSupported;
    }
    LOG(kMedia, "ProtocolScd::Stream(%.*s)\n", PBUF(aUri));
    {
        AutoMutex _(iLock);
        iStreamId = IPipelineIdProvider::kStreamIdInvalid;
        iNextFlushId = MsgFlush::kIdInvalid;
        iStarted = iStopped = iUnrecoverableError = iExit = false;
        iHalted = true;
    }

    for (; !iExit && !iStopped && !iUnrecoverableError;) {
        try {
            for (;;) {
                Close();
                if (Connect(iUri, 0)) { // slightly dodgy - relies on implementation ignoring iUri's scheme
                    iStarted = true;
                    break;
                }
                else {
                    if (!iStarted) {
                        LOG(kMedia, "ProtocolScd - failed to connect to sender\n");
                        return EProtocolStreamErrorUnrecoverable;
                    }
                    AutoMutex _(iLock);
                    if (iStopped) {
                        THROW(ScdError);
                    }
                }
                Thread::Sleep(500); /* This code runs in a fairly high priority thread.
                                       Avoid it busy-looping, preventing action invocation
                                       threads from being scheduled and changing the active
                                       url/mode. */
            }
            //Log::Print("\n\n\n");
            iObserver.NotifyScdConnectionChange(true);
            {
                ScdMsg* ready = iScdFactory.CreateMsgReady();
                AutoScdMsg _(ready);
                ready->Externalise(iWriterBuf);
            }
            for (;;) {
                auto msg = iScdFactory.CreateMsg(iReaderBuf);
                AutoScdMsg _(msg);
                msg->Process(*this);
            }
        }
        catch (AssertionFailed&) {
            throw;
        }
        catch (Exception& ex) {
            if (!iExit && !iStopped) {
                LOG_ERROR(kMedia, "Exception - %s - in ProtocolScd::Stream\n", ex.Message());
            }
        }
    }
    iObserver.NotifyScdConnectionChange(false);
    Close();
    iSupply->Flush();
    {
        AutoMutex _(iLock);
        if (iStopped && iNextFlushId != MsgFlush::kIdInvalid) {
            iSupply->OutputFlush(iNextFlushId);
        }
        // clear iStreamId to prevent TrySeek or TryStop returning a valid flush id
        iStreamId = IPipelineIdProvider::kStreamIdInvalid;
        if (iUnrecoverableError) {
            return EProtocolStreamErrorUnrecoverable;
        }
        if (iStopped) {
            return EProtocolStreamStopped;
        }
        return EProtocolStreamSuccess;
    }
}

ProtocolGetResult ProtocolScd::Get(IWriter& /*aWriter*/, const Brx& /*aUri*/, TUint64 /*aOffset*/, TUint /*aBytes*/)
{
    return EProtocolGetErrorNotSupported;
}

TUint ProtocolScd::TryStop(TUint aStreamId)
{
    AutoMutex _(iLock);
    if (iStreamId != aStreamId || aStreamId == IPipelineIdProvider::kStreamIdInvalid) {
        return MsgFlush::kIdInvalid;
    }
    if (iNextFlushId == MsgFlush::kIdInvalid) {
        /* If a valid flushId is set then We've previously promised to send a Flush but haven't
        got round to it yet.  Re-use the same id for any other requests that come in before
        our main thread gets a chance to issue a Flush */
        iNextFlushId = iFlushIdProvider->NextFlushId();
    }
    iStopped = true;
    iTcpClient.Interrupt(true);
    return iNextFlushId;
}

void ProtocolScd::Process(ScdMsgReady& aMsg)
{
    //Log::Print("ScdMsgReady\n");
    const TUint major = aMsg.Major();
    if (major != kVersionMajor) {
        iUnrecoverableError = true;
        LOG(kScd, "ProtocolScd received ScdMsgReady with unsupported major version (%u)\n", major);
        THROW(ScdError);
    }
}

void ProtocolScd::Process(ScdMsgMetadataDidl& aMsg)
{
    //Log::Print("ScdMsgMetadataDidl\n");
    auto track = iTrackFactory.CreateTrack(aMsg.Uri(), aMsg.Metadata());
    OutputTrack(track);
}

void ProtocolScd::Process(ScdMsgMetadataOh& aMsg)
{
    //Log::Print("ScdMsgMetadataOh\n");
    auto track = Av::OhMetadata::ToTrack(aMsg.Metadata(), iTrackFactory);
    OutputTrack(track);
}

void ProtocolScd::Process(ScdMsgFormat& aMsg)
{
    //Log::Print("ScdMsgFormat\n");
    LOG_INFO(kScd, "ScdMsgFormat: %u/%u, %uch, sampleStart=%llu, samplesTotal=%llu, seekable=%u, live=%u\n",
                   aMsg.SampleRate(), aMsg.BitDepth(), aMsg.NumChannels(), aMsg.SampleStart(),
                   aMsg.SamplesTotal(), aMsg.Seekable(), aMsg.Live());
    SpeakerProfile spStereo;
    iFormatPcm.Set(aMsg.BitDepth(), aMsg.SampleRate(), aMsg.NumChannels(),
                   AudioDataEndian::Big, spStereo, aMsg.SampleStart());
    iFormatPcm.SetCodec(aMsg.CodecName(), aMsg.Lossless());
    iFormatDsd.Clear();
    const TUint bytesPerSample = aMsg.BitDepth() * aMsg.NumChannels() / 8;
    iStreamBytes = aMsg.SamplesTotal() * bytesPerSample;
    iStreamMultiroom = aMsg.BroadcastAllowed()? Multiroom::Allowed : Multiroom::Forbidden;
    iStreamLive = aMsg.Live();
    OutputStream();
}

void ProtocolScd::Process(ScdMsgFormatDsd& aMsg)
{
    //Log::Print("ScdMsgFormatDsd\n");
    LOG_INFO(kScd, "ScdMsgFormatDsd: %u, %uch, sampleStart=%llu, samplesTotal=%llu, seekable=%u\n",
                   aMsg.SampleRate(), aMsg.NumChannels(), aMsg.SampleStart(),
                   aMsg.SamplesTotal(), aMsg.Seekable());
    if (aMsg.SampleBlockBits() != 32) { // Where does this come from??
        LOG_ERROR(kScd, "ScdMsgFormatDsd: unsupported sampleBlockBits - %u - closing connection\n",
                        aMsg.SampleBlockBits());
        iUnrecoverableError = true;
        THROW(ScdError);
    }
    iFormatPcm.Clear();
    SpeakerProfile spStereo;
    iFormatDsd.Set(aMsg.SampleRate(), aMsg.NumChannels(),
                   aMsg.SampleBlockBits(), aMsg.SampleStart());
    iFormatDsd.SetCodec(aMsg.CodecName());
    iStreamBytes = aMsg.SamplesTotal() * aMsg.NumChannels() / 8; // /8 since 1 bit per subsample
    iStreamMultiroom = Multiroom::Forbidden;
    iStreamLive = false;
    OutputStream();
}

void ProtocolScd::Process(ScdMsgAudioOut& /*aMsg*/)
{
    ASSERTS();
}

void ProtocolScd::Process(ScdMsgAudioIn& aMsg)
{
    //Log::Print("ScdMsgAudioIn - samples = %u\n", aMsg.NumSamples());
    if (iHalted) {
        iHalted = false;
        LOG_INFO(kScd, "ScdMsgAudioIn - resuming after halt\n");
    }
    if (iFormatPcm) {
        iSupply->OutputData(aMsg.NumSamples(), iReaderBuf);
    }
    else { // is DSD
        iSupply->OutputDataDsd(aMsg.NumSamples(), iReaderBuf);
    }
}

void ProtocolScd::Process(ScdMsgMetatextDidl& aMsg)
{
    //Log::Print("ScdMsgMetatextDidl\n");
    iSupply->OutputMetadata(aMsg.Metatext());
}

void ProtocolScd::Process(ScdMsgMetatextOh& aMsg)
{
    //Log::Print("ScdMsgMetatextOh\n");
    Av::OhMetadata::ToDidlLite(aMsg.Metatext(), iMetadata);
    iSupply->OutputMetadata(iMetadata);
}

void ProtocolScd::Process(ScdMsgHalt& /*aMsg*/)
{
    //Log::Print("ScdMsgHalt\n");
    LOG_INFO(kScd, "ScdMsgHalt\n");
    iHalted = true;
    iSupply->OutputWait();
    iSupply->OutputHalt();
}

void ProtocolScd::Process(ScdMsgDisconnect& /*aMsg*/)
{
    //Log::Print("ScdMsgDisconnect\n");
    LOG_INFO(kScd, "ScdMsgDisconnect\n");
    iExit = true;
    THROW(ScdError); // force Stream out of its inner msg readiing loop
}

void ProtocolScd::Process(ScdMsgSeek& /*aMsg*/)
{
    ASSERTS();
}

void ProtocolScd::Process(ScdMsgSkip& /*aMsg*/)
{
    ASSERTS();
}

void ProtocolScd::OutputTrack(Track* aTrack)
{
    iSupply->OutputTrack(*aTrack, false /* Roon don't always send this at the start of streams */);
    aTrack->RemoveRef();
}

void ProtocolScd::OutputStream()
{
    if (!iFormatPcm && !iFormatDsd) {
        LOG_ERROR(kMedia, "ProtocolScd received Audio but no Format\n");
        iUnrecoverableError = true;
        THROW(ScdError);
    }

    {
        AutoMutex _(iLock);
        iStreamId = iIdProvider->NextStreamId();
    }
    if (iFormatPcm) {
        iSupply->OutputPcmStream(Brx::Empty(), iStreamBytes, false/*seekable*/,
                                 iStreamLive, iStreamMultiroom, *this, iStreamId,
                                 iFormatPcm);
    }
    else { // iFormatDsd
        iSupply->OutputDsdStream(Brx::Empty(), iStreamBytes, false/*seekable*/,
                                 *this, iStreamId, iFormatDsd);
    }
}

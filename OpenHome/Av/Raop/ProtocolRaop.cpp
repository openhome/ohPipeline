#include <OpenHome/Av/Raop/ProtocolRaop.h>
#include <OpenHome/Av/Raop/UdpServer.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Timer.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Av/Raop/Raop.h>
#include <OpenHome/Av/VolumeManager.h>
#include <OpenHome/Media/SupplyAggregator.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;


// RtpHeaderRaop

RtpHeaderRaop::RtpHeaderRaop()
    : iPadding(false)
    , iExtension(false)
    , iCsrcCount(0)
    , iMarker(false)
    , iPayloadType(0)
    , iSequenceNumber(0)
{
}

RtpHeaderRaop::RtpHeaderRaop(TBool aPadding, TBool aExtension, TUint aCsrcCount, TBool aMarker, TUint aPayloadType, TUint aSeqNumber)
    : iPadding(aPadding)
    , iExtension(aExtension)
    , iCsrcCount(aCsrcCount)
    , iMarker(aMarker)
    , iPayloadType(aPayloadType)
    , iSequenceNumber(aSeqNumber)
{
    if (iCsrcCount > 0xf) {
        LOG_ERROR(kMedia, "RtpHeaderRaop::RtpHeaderRaop iCrscCount > 0xf. Throwing InvalidRaopPacket. iCsrcCount: %u\n", iCsrcCount);
        THROW(InvalidRaopPacket);
    }
    if (iPayloadType > 0x7f) {
        LOG_ERROR(kMedia, "RtpHeaderRaop::RtpHeaderRaop iPayloadType > 0x7f. Throwing InvalidRaopPacket. iPayloadType: %u\n", iPayloadType);
        THROW(InvalidRaopPacket);
    }
    if (iSequenceNumber > 0xffff) {
        LOG_ERROR(kMedia, "RtpHeaderRaop::RtpHeaderRaop iSequenceNumber > 0xffff. Throwing InvalidRaopPacket. iSequenceNumber: %u\n", iSequenceNumber);
        THROW(InvalidRaopPacket);
    }
}

RtpHeaderRaop::RtpHeaderRaop(const Brx& aRtpHeader)
{
    Set(aRtpHeader);
}

void RtpHeaderRaop::Set(const Brx& aRtpHeader)
{
    Clear(); // Clear previous state.

    if (aRtpHeader.Bytes() != kBytes) {
        LOG_ERROR(kMedia, "RtpHeaderRaop::Set aRtpHeader.Bytes() != kBytes. Throwing InvalidRaopPacket. aRtpHeader.Bytes(): %u, kBytes: %u\n", aRtpHeader.Bytes(), kBytes);
        THROW(InvalidRaopPacket);
    }

    /*
     * Ignore RTP version. Some RAOP senders set RTP version for resent audio packets as 0 instead of 2.
     */
    // const TUint version = (aRtpHeader[0] & 0xc0) >> 6;
    // if (version != kVersion) {
    //     LOG_ERROR(kMedia, "RtpHeaderRaop::Set version > kVersion. Throwing InvalidRaopPacket. version: %u, kVersion: %u\n", version, kVersion);
    //     THROW(InvalidRaopPacket);
    // }
    iPadding = (aRtpHeader[0] & 0x20) == 0x20;
    iExtension = (aRtpHeader[0] & 0x10) == 0x10;
    iCsrcCount = aRtpHeader[0] & 0x0f;
    iMarker = (aRtpHeader[1] & 0x80) == 0x80;
    iPayloadType = aRtpHeader[1] & 0x7f;

    static const TUint offset = 2;  // Processed first 2 bytes above.
    Brn packetRemaining(aRtpHeader.Ptr()+offset, aRtpHeader.Bytes()-offset);
    ReaderBuffer readerBuffer(packetRemaining);
    ReaderBinary readerBinary(readerBuffer);

    try {
        iSequenceNumber = readerBinary.ReadUintBe(2);
    }
    catch (ReaderError&) {
        Clear();
        THROW(InvalidRaopPacket);
    }
}

void RtpHeaderRaop::Set(const RtpHeaderRaop& aRtpHeader)
{
    iPadding = aRtpHeader.iPadding;
    iExtension = aRtpHeader.iExtension;
    iCsrcCount = aRtpHeader.iCsrcCount;
    iMarker = aRtpHeader.iMarker;
    iPayloadType = aRtpHeader.iPayloadType;
    iSequenceNumber = aRtpHeader.iSequenceNumber;
}

void RtpHeaderRaop::Clear()
{
    iPadding = false;
    iExtension = false;
    iCsrcCount = false;
    iMarker = false;
    iPayloadType = false;
    iSequenceNumber = false;
}

void RtpHeaderRaop::Write(IWriter& aWriter) const
{
    WriterBinary writerBinary(aWriter);
    TUint8 byte1 = (TUint8)((kVersion << 6) | (iPadding << 5) | (iExtension << 4) | (iCsrcCount));
    TUint8 byte2 = (TUint8)((iMarker << 7) | (iPayloadType));
    writerBinary.WriteUint8(byte1);
    writerBinary.WriteUint8(byte2);
    writerBinary.WriteUint16Be(iSequenceNumber);
}

TBool RtpHeaderRaop::Padding() const
{
    return iPadding;
}

TBool RtpHeaderRaop::Extension() const
{
    return iExtension;
}

TUint RtpHeaderRaop::CsrcCount() const
{
    return iCsrcCount;
}

TBool RtpHeaderRaop::Marker() const
{
    return iMarker;
}

TUint RtpHeaderRaop::Type() const
{
    return iPayloadType;
}

TUint RtpHeaderRaop::Seq() const
{
    return iSequenceNumber;
}


// RtpPacketRaop

RtpPacketRaop::RtpPacketRaop()
{
}

RtpPacketRaop::RtpPacketRaop(const Brx& aRtpPacket)
{
    Set(aRtpPacket);
}

void RtpPacketRaop::Set(const Brx& aRtpPacket)
{
    Clear();
    if (aRtpPacket.Bytes() >= RtpHeaderRaop::kBytes) {
        iHeader.Set(Brn(aRtpPacket.Ptr(), RtpHeaderRaop::kBytes));
        iPayload.Set(aRtpPacket.Ptr()+RtpHeaderRaop::kBytes, aRtpPacket.Bytes()-RtpHeaderRaop::kBytes);
    }
    else {
        LOG_ERROR(kMedia, "RtpPacketRaop::Set aRtpPacket.Bytes() < RtpHeaderRaop::kBytes. Throwing InvalidRaopPacket. aRtpPacket.Bytes(): %u, RtpHeaderRaop::kBytes: %u\n", aRtpPacket.Bytes(), RtpHeaderRaop::kBytes);
        THROW(InvalidRaopPacket);
    }
}

void RtpPacketRaop::Set(const RtpPacketRaop& aRtpPacket)
{
    iHeader.Set(aRtpPacket.iHeader);
    iPayload.Set(aRtpPacket.iPayload);
}

void RtpPacketRaop::Clear()
{
    iHeader.Clear();
    iPayload.Set(Brx::Empty());
}

const RtpHeaderRaop& RtpPacketRaop::Header() const
{
    return iHeader;
}

const Brx& RtpPacketRaop::Payload() const
{
    return iPayload;
}


// RaopPacketAudio

RaopPacketAudio::RaopPacketAudio()
    : iTimestamp(0)
    , iSsrc(0)
{
}

RaopPacketAudio::RaopPacketAudio(const RtpPacketRaop& aRtpPacket)
    : iTimestamp(0)
    , iSsrc(0)
{
    Set(aRtpPacket);
}

void RaopPacketAudio::Set(const RtpPacketRaop& aRtpPacket)
{
    Clear();

    iPacket.Set(aRtpPacket);
    if (iPacket.Payload().Bytes() >= kAudioSpecificHeaderBytes) {
        iPayload.Set(iPacket.Payload().Ptr()+kAudioSpecificHeaderBytes, iPacket.Payload().Bytes()-kAudioSpecificHeaderBytes);
    }
    else {
        Clear();
        LOG_ERROR(kMedia, "RaopPacketAudio::Set iPacket.Payload().Bytes() < kAudioSpecificHeaderBytes. Throwing InvalidRaopPacket. iPacket.Payload().Bytes(): %u, kAudioSpecificHeaderBytes: %u\n", iPacket.Payload().Bytes(), kAudioSpecificHeaderBytes);
        THROW(InvalidRaopPacket);
    }

    /*
     * Ignore RTP type. Some RAOP senders set RTP type for resent audio packets as 0 instead of 96.
     */
    // if (iPacket.Header().Type() != kType) {
    //     Clear();
    //     LOG_ERROR(kMedia, "RaopPacketAudio::Set iPacket.Header().Type() != kType. Throwing InvalidRaopPacket. iPacket.Header().Type(): %u, kType: %u\n", iPacket.Header().Type(), kType);
    //     THROW(InvalidRaopPacket);
    // }

    const Brx& payload = iPacket.Payload();
    ReaderBuffer readerBuffer(payload);
    ReaderBinary readerBinary(readerBuffer);

    try {
        iTimestamp = readerBinary.ReadUintBe(4);
        iSsrc = readerBinary.ReadUintBe(4);
    }
    catch (ReaderError&) {
        Clear();
        THROW(InvalidRaopPacket);
    }
}

void RaopPacketAudio::Clear()
{
    iPacket.Clear();
    iPayload.Set(Brx::Empty());
    iTimestamp = 0;
    iSsrc = 0;
}

const RtpHeaderRaop& RaopPacketAudio::Header() const
{
    return iPacket.Header();
}

const Brx& RaopPacketAudio::Payload() const
{
    return iPayload;
}

TUint RaopPacketAudio::Timestamp() const
{
    return iTimestamp;
}

TUint RaopPacketAudio::Ssrc() const
{
    return iSsrc;
}


// RaopPacketSync

RaopPacketSync::RaopPacketSync(const RtpPacketRaop& aRtpPacket)
    : iPacket(aRtpPacket)
    , iPayload(iPacket.Payload().Ptr()+kSyncSpecificHeaderBytes, iPacket.Payload().Bytes()-kSyncSpecificHeaderBytes)
{
    if (iPacket.Header().Type() != kType) {
        THROW(InvalidRaopPacket);
    }

    const Brx& payload = iPacket.Payload();
    ReaderBuffer readerBuffer(payload);
    ReaderBinary readerBinary(readerBuffer);

    try {
        iRtpTimestampMinusLatency = readerBinary.ReadUintBe(4);
        iNtpTimestampSecs = readerBinary.ReadUintBe(4);
        iNtpTimestampFract = readerBinary.ReadUintBe(4);
        iRtpTimestamp = readerBinary.ReadUintBe(4);
    }
    catch (ReaderError&) {
        THROW(InvalidRaopPacket);
    }
}

const RtpHeaderRaop& RaopPacketSync::Header() const
{
    return iPacket.Header();
}

const Brx& RaopPacketSync::Payload() const
{
    return iPayload;
}

TUint RaopPacketSync::RtpTimestampMinusLatency() const
{
    return iRtpTimestampMinusLatency;
}

TUint RaopPacketSync::NtpTimestampSecs() const
{
    return iNtpTimestampSecs;
}

TUint RaopPacketSync::NtpTimestampFract() const
{
    return iNtpTimestampFract;
}

TUint RaopPacketSync::RtpTimestamp() const
{
    return iRtpTimestamp;
}


// RaopPacketResendResponse

RaopPacketResendResponse::RaopPacketResendResponse()
{
}

RaopPacketResendResponse::RaopPacketResendResponse(const RtpPacketRaop& aRtpPacket)
{
    Set(aRtpPacket);
}

void RaopPacketResendResponse::Set(const RtpPacketRaop& aRtpPacket)
{
    iPacketOuter.Set(aRtpPacket);
    iPacketInner.Set(iPacketOuter.Payload());
    iAudioPacket.Set(iPacketInner);

    if (iPacketOuter.Header().Type() != kType) {
        Clear();
        LOG_ERROR(kMedia, "RaopPacketResendResponse::Set Expected type: %u. Actual type: %u\n", kType, iPacketOuter.Header().Type());
        THROW(InvalidRaopPacket);
    }
}

void RaopPacketResendResponse::Clear()
{
    iPacketOuter.Clear();
    iPacketInner.Clear();
    iAudioPacket.Clear();
}

const RtpHeaderRaop& RaopPacketResendResponse::Header() const
{
    return iPacketOuter.Header();
}

const RaopPacketAudio& RaopPacketResendResponse::AudioPacket() const
{
    return iAudioPacket;
}


// RaopPacketResendRequest

RaopPacketResendRequest::RaopPacketResendRequest(TUint aSeqStart, TUint aCount)
    : iHeader(false, false, 0, true, kType, 1)
    , iSeqStart(aSeqStart)
    , iCount(aCount)
{
}

void RaopPacketResendRequest::Write(IWriter& aWriter) const
{
    WriterBinary writerBinary(aWriter);
    iHeader.Write(aWriter);
    writerBinary.WriteUint16Be(iSeqStart);
    writerBinary.WriteUint16Be(iCount);
}


// ProtocolRaop

ProtocolRaop::ProtocolRaop(Environment& aEnv, Media::TrackFactory& aTrackFactory, IRaopDiscovery& aDiscovery, UdpServerManager& aServerManager, TUint aAudioId, TUint aControlId, TUint aThreadPriorityAudioServer, TUint aThreadPriorityControlServer, ITimerFactory& aTimerFactory)
    : Protocol(aEnv)
    , iTrackFactory(aTrackFactory)
    , iDiscovery(aDiscovery)
    , iServerManager(aServerManager)
    , iAudioServer(iServerManager.Find(aAudioId), *this, aThreadPriorityAudioServer)
    , iControlServer(iServerManager.Find(aControlId), *this, aThreadPriorityControlServer)
    , iSupply(nullptr)
    , iStreamId(IPipelineIdProvider::kStreamIdInvalid)
    , iNextFlushId(MsgFlush::kIdInvalid)
    , iActive(false)
    , iStopped(true)
    , iLockRaop("PRAL")
    , iSem("PRAS", 0)
    , iSemDrain("PRSM", 0)
    , iResendRangeRequester(iControlServer)
    , iRepairer(aEnv, iResendRangeRequester, *this, aTimerFactory)
{
}

ProtocolRaop::~ProtocolRaop()
{
    delete iSupply;
}

void ProtocolRaop::RepairReset()
{
    // This must only be called from Stream() method to avoid deadlock (in
    // particular, to avoid Repairer being blocked by timer lock when it
    // cancels its timer, and to avoid simultaneous calls to iSupply).
    iRepairer.DropAudio();
    iSupply->Discard();
}

void ProtocolRaop::Interrupt(TBool aInterrupt)
{
    LOG(kMedia, ">ProtocolRaop::Interrupt aInterrupt: %u\n", aInterrupt);
    {
        if (aInterrupt) {
            AutoMutex a(iLockRaop);
            iStopped = true;
            DoInterrupt(aInterrupt);
            iSem.Signal();
            iSemDrain.Signal(); // Interrupt any pending drain.
        }
        else {
            DoInterrupt(aInterrupt);
        }
    }
}

void ProtocolRaop::Initialise(MsgFactory& aMsgFactory, IPipelineElementDownstream& aDownstream)
{
    iSupply = new SupplyAggregatorBytes(aMsgFactory, aDownstream);
}

ProtocolStreamResult ProtocolRaop::Stream(const Brx& aUri)
{
    LOG(kMedia, "ProtocolRaop::Stream(%.*s)\n", PBUF(aUri));

    {
        AutoMutex a(iLockRaop);
        try {
            iUri.Replace(aUri);
        }
        catch (UriError&) {
            LOG(kMedia, "ProtocolRaop::Stream unable to parse URI\n");
            return EProtocolErrorNotSupported;
        }
    }

    // RAOP doesn't actually stream from a URI, so just expect a dummy URI.
    if (iUri.Scheme() != Brn("raop")) {
        LOG(kMedia, "ProtocolRaop::Stream Scheme not recognised\n");
        return EProtocolErrorNotSupported;
    }

    Reset();
    RepairReset();

    // FIXME - clear iSem here and purge iControlServer/iAudioServer of any audio they still hold? (Will probably need to reconstruct socket to purge audio in low-level network queue too.)
    //iSem.Clear(); // Clear any stale signals. This is the only safe place to do this, as guaranteed all servers are currently closed (i.e., won't miss any message consumer calls and end up waiting forever).
    iAudioServer.Interrupt(false);
    iControlServer.Interrupt(false);
    StartServers();

    iStarted = false;

    // Output audio stream
    for (;;) {
        iSem.Wait();
        {
            // Can't hold lock while outputting to supply as may block pipeline
            // if callbacks come in, so do admin here before outputting msgs.
            TUint flushId = MsgFlush::kIdInvalid;
            TBool waiting = false;
            TBool stopped = false;
            TBool discontinuity = false;
            {
                AutoMutex a(iLockRaop);

                flushId = iNextFlushId;
                iNextFlushId = MsgFlush::kIdInvalid;

                if (iStopped) {
                    stopped = true;
                    iStopped = false;
                    iStreamId = IPipelineIdProvider::kStreamIdInvalid;
                    iActive = false;
                }
                if (iWaiting) {
                    waiting = true;
                    iWaiting = false;
                    DoInterrupt(false);
                }
                if (iDiscontinuity) {
                    discontinuity = true;
                    iDiscontinuity = false;
                    DoInterrupt(false);
                }
                if (iStarving) {
                    iStarving = false;
                    DoInterrupt(false);
                    // Pipeline has already starved, so should be no need to
                    // output a drain msg here (as should be no glitching).
                }
            }

            // Output any pending flush.
            if (flushId != MsgFlush::kIdInvalid) {
                iSupply->Flush();
                iSupply->OutputFlush(flushId);
                RepairReset();
            }

            if (stopped) {
                iSupply->Flush();
                // There may not have been a pending flush for this, as this may have been triggered by ::Interrupt(true).
                iDiscovery.Close();
                StopServers();
                LOG(kMedia, "<ProtocolRaop::Stream stopped. Returning EProtocolStreamStopped\n");
                return EProtocolStreamStopped;
            }

            if (waiting || discontinuity) {
                LOG(kMedia, "ProtocolRaop::Stream discontinuity.\n");
                OutputDiscontinuity();
                RepairReset();
                LOG(kMedia, "ProtocolRaop::Stream signalled end of discontinuity. waiting: %u, discontinuity: %u\n", waiting, discontinuity);

                // Stream may have gone stopped during OutputDiscontinuity() call above, in which case draining may have been interrupted.
                // If that is the case, ensure we exit Stream() here and do not attempt to output further audio.
                TBool stoppedDiscontinuity = false;
                {
                    AutoMutex amx(iLockRaop);
                    stoppedDiscontinuity = iStopped;
                }
                if (stoppedDiscontinuity) {
                    iSupply->Flush();
                    iDiscovery.Close();
                    StopServers();
                    LOG(kMedia, "<ProtocolRaop::Stream stopped after discontinuity. Returning EProtocolStreamStopped\n");
                    return EProtocolStreamStopped;
                }

                // Continue and resume normal operation.
            }
        }

        // Check if session still active.
        if (!iDiscovery.Active()) {
            LOG(kMedia, "ProtocolRaop::Stream() no active session\n");
            TUint flushId = MsgFlush::kIdInvalid;
            {
                AutoMutex a(iLockRaop);
                iActive = false;
                iStopped = true;
                flushId = iNextFlushId;
                iNextFlushId = MsgFlush::kIdInvalid;
            }

            // Output any pending flush ID, then wait for pipeline to drain.
            iSupply->Flush();
            if (flushId != MsgFlush::kIdInvalid) {
                iSupply->OutputFlush(flushId);
            }
            RepairReset();
            iDiscovery.Close();
            StopServers();
            LOG(kMedia, "<ProtocolRaop::Stream !iDiscovery.Active(). Returning EProtocolStreamStopped\n");
            return EProtocolStreamStopped;
        }

        // First, see if this was signalled for a resent packet.
        TBool packetSuccess = false;
        try {
            const auto& packet = iControlServer.Packet();
            ProcessPacket(packet);
            iControlServer.PacketConsumed();
            packetSuccess = true;
        }
        catch (RaopPacketUnavailable&) {
            LOG_DEBUG(kPipeline, "ProtocolRaop::Stream caught RaopPacketUnavailable from iControlServer\n");
        }

        // If no packet from control server, was maybe a packet from audio server.
        if (!packetSuccess) {
            try {
                const auto& packet = iAudioServer.Packet();
                ProcessPacket(packet);
                iAudioServer.PacketConsumed();
                packetSuccess = true;
            }
            catch (RaopPacketUnavailable&) {
                LOG_DEBUG(kPipeline, "ProtocolRaop::Stream caught RaopPacketUnavailable from iAudioServer\n");
            }
        }
    }
}

ProtocolGetResult ProtocolRaop::Get(IWriter& /*aWriter*/, const Brx& /*aUri*/, TUint64 /*aOffset*/, TUint /*aBytes*/)
{
    return EProtocolGetErrorNotSupported;
}

void ProtocolRaop::DoInterrupt(TBool aInterrupt)
{
    LOG(kMedia, ">ProtocolRaop::DoInterrupt aInterrupt: %u\n", aInterrupt);
    // Only interrupt network sockets here.
    // Do NOT call RepairReset() here, as that should only happen within
    // Stream() method.
    iAudioServer.Interrupt(aInterrupt);
    iControlServer.Interrupt(aInterrupt);
    LOG(kMedia, "<ProtocolRaop::DoInterrupt\n");


    // FIXME - should iSem.Signal() be called here instead of all the individual locations it is currently called in?
}

void ProtocolRaop::Reset()
{
    AutoMutex a(iLockRaop);

    // Parse URI to get client control/timing ports.
    // (Timing channel isn't monitored, so don't bother parsing port.)
    Parser p(iUri.AbsoluteUri());
    p.Forward(7);   // skip raop://
    const Brn ctrlPortBuf = p.Next('.');
    const TUint ctrlPort = Ascii::Uint(ctrlPortBuf);
    iAudioServer.Reset();
    iControlServer.Reset(ctrlPort);

    iSupply->Discard();
    iSessionId = 0;
    iStreamId = IPipelineIdProvider::kStreamIdInvalid;
    iLatency = iControlServer.Latency();    // Get last known (or default) latency.
    iFlushSeq = 0;
    iFlushTime = 0;
    iNextFlushId = MsgFlush::kIdInvalid;
    iActive = true;
    iWaiting = false;
    iResumePending = false;
    iStopped = false;
    iDiscontinuity = false;
    iStarving = false;
    // Clear any pending signals on drain semaphore, just to be safe, as won't be waiting on a drain when entering ::Stream() method.
    iSemDrain.Clear();
}

void ProtocolRaop::UpdateSessionId(TUint aSessionId)
{
    AutoMutex a(iLockRaop);
    if (iSessionId == 0) {
        // Initialise session ID.
        iSessionId = aSessionId;
        LOG(kMedia, "ProtocolRaop::UpdateSessionId new iSessionId: %u\n", iSessionId);
    }
}

TBool ProtocolRaop::IsValidSession(TUint aSessionId) const
{
    AutoMutex a(iLockRaop);
    if (iSessionId == aSessionId) {
        return true;
    }
    return false;
}

TBool ProtocolRaop::ShouldFlush(TUint aSeq, TUint aTimestamp) const
{
    AutoMutex a(iLockRaop);
    if (iResumePending) {   // FIXME - is this valid? Should we not just be discarding these packets anyway?
        const TBool seqInFlushRange = (aSeq <= iFlushSeq);
        const TBool timeInFlushRange = (aTimestamp <= iFlushTime);
        const TBool shouldFlush = (seqInFlushRange && timeInFlushRange);
        return shouldFlush;
    }
    return false;
}

void ProtocolRaop::OutputContainer(const Brx& aFmtp)
{
    Bws<60> container(Brn("Raop"));
    container.Append(Brn(" "));
    Ascii::AppendDec(container, aFmtp.Bytes()+1);   // account for newline char added
    container.Append(" ");
    container.Append(aFmtp);
    container.Append(Brn("\n"));
    LOG(kMedia, "ProtocolRaop::OutputContainer container %d bytes [", container.Bytes()); LOG(kMedia, container); LOG(kMedia, "]\n");
    iSupply->OutputData(container);
}

void ProtocolRaop::OutputAudio(const Brx& aAudio)
{
    /*
     * Outputting delay mid-stream is causing VariableDelay to ramp audio up/down
     * mid-stream and immediately after unpausing/seeking.
     * That makes for an unpleasant listening experience. Better to just
     * potentially allow stream to go a few ms out of sync.
     */
    TBool outputDelay = false;
    TUint latency = iControlServer.Latency();
    {
        AutoMutex a(iLockRaop);
        if (latency != iLatency) {

            TUint diffSamples = 0;
            if (latency > iLatency) {
                diffSamples = latency - iLatency;
            }
            else {
                diffSamples = iLatency - latency;
            }
            //LOG(kMedia, "ProtocolRaop::OutputAudio diffSamples: %u\n", diffSamples);

            // Some senders may toggle latency by a small amount when unpausing.
            // To avoid unnecessary ramp down/up as delay changes, only change
            // if a threshold is exceeded.
            if (diffSamples >= kMinDelayChangeSamples) {
                iLatency = latency;
                outputDelay = true;
            }
        }
    }
    if (outputDelay) {
        iSupply->OutputDelay(Delay(latency));
    }

    iAudioDecryptor.Decrypt(aAudio, iAudioDecrypted);
    iSupply->OutputData(iAudioDecrypted);
}

void ProtocolRaop::OutputDiscontinuity()
{
    LOG(kMedia, ">ProtocolRaop::OutputDiscontinuity\n");
    StopServers();

    // These are called AFTER OutputDiscontinuity().
    //iRepairer.DropAudio();  // Drop any audio buffered in repairer.
    //iSupply->Discard();

    {
        AutoMutex a(iLockRaop);
        iResumePending = true;
    }

    iSemDrain.Clear();
    iSupply->Flush();
    LOG(kMedia, "ProtocolRaop::OutputDiscontinuity before OutputDrain()\n");
    iSupply->OutputDrain(MakeFunctor(iSemDrain, &Semaphore::Signal)); // FIXME - what if doing this while waiter is flushing?
    LOG(kMedia, "ProtocolRaop::OutputDiscontinuity after OutputDrain()\n");
    try {
        iSemDrain.Wait(ISupply::kMaxDrainMs);
    }
    catch (Timeout&) {
        LOG(kPipeline, "WARNING: ProtocolRaop: timeout draining pipeline\n");
    }
    LOG(kMedia, "ProtocolRaop::OutputDiscontinuity after sem.Wait()\n");

    // Only reopen audio server if a TryStop() hasn't come in.
    AutoMutex a(iLockRaop);
    if (!iStopped) {
        StartServers();
    }

    // FIXME - if doing lots of skips, don't seem to return from this. See #4348.
    LOG(kMedia, "<ProtocolRaop::OutputDiscontinuity\n");
}

void ProtocolRaop::ProcessPacket(const RaopPacketAudio& aPacket)
{
    if (ShouldFlush(aPacket.Header().Seq(), aPacket.Timestamp())) {
        return; // Do nothing more with this packet.
    }

    iLockRaop.Wait();
    const TBool started = iStarted;
    const TBool resumePending = iResumePending;
    iLockRaop.Signal();
    if (!started || resumePending) {
        LOG(kMedia, "ProtocolRaop::ProcessPacket starting new stream started: %u, resumePending: %u\n", started, resumePending);
        UpdateSessionId(aPacket.Ssrc());
        ProcessStreamStartOrResume();
    }
    iDiscovery.KeepAlive();

    // FIXME - for airplay dropout, are these values being set appropriately and dropping the received packets?
    const TBool validSession = IsValidSession(aPacket.Ssrc());
    const TBool shouldFlush = ShouldFlush(aPacket.Header().Seq(), aPacket.Timestamp());

    //LOG(kMedia, "ProtocolRaop::ProcessPacket validSession: %u, shouldFlush: %u\n", validSession, shouldFlush);

    if (validSession && !shouldFlush) {
        try {
            IRepairable* repairable = iRepairableAllocator.Allocate(aPacket);
            try {
                    iRepairer.OutputAudio(*repairable);
            }
            catch (const RepairerBufferFull&) {
                LOG(kPipeline, "ProtocolRaop::ProcessPacket(const RaopPacketAudio&) RepairerBufferFull\n");
                // Set state so that no more audio is output until a MsgDrain followed by a MsgEncodedStream.
                AutoMutex a(iLockRaop);
                iDiscontinuity = true;
                iSem.Signal();
            }
            catch (const RepairerStreamRestarted&) {
                LOG(kPipeline, "ProtocolRaop::ProcessPacket(const RaopPacketAudio&) RepairerStreamRestarted\n");
                AutoMutex a(iLockRaop);
                iDiscontinuity = true;
                iSem.Signal();
            }
        }
        catch (const RaopAllocationFailure&) {
            LOG(kPipeline, "ProtocolRaop::ProcessPacket(const RaopPacketAudio&) RaopAllocationFailure aPacket.Header().Seq(): %u, aPacket.Payload().Bytes(): %u\n", aPacket.Header().Seq(), aPacket.Payload().Bytes());
        }
    }
}

void ProtocolRaop::ProcessPacket(const RaopPacketResendResponse& aPacket)
{
    // FIXME - this is essentially a clone of ::ProcessPacket(const RaopPacketAudio& aPacket). The only different is that an RaopPacketResendResponse is passed into iRepairableAllocator, as it needs to know that packet is a resent packet.

    if (ShouldFlush(aPacket.AudioPacket().Header().Seq(), aPacket.AudioPacket().Timestamp())) {
        return; // Do nothing more with this packet.
    }

    iLockRaop.Wait();
    const TBool started = iStarted;
    const TBool resumePending = iResumePending;
    iLockRaop.Signal();
    if (!started || resumePending) {
        LOG(kMedia, "ProtocolRaop::ProcessPacket starting new stream started: %u, resumePending: %u\n", started, resumePending);
        UpdateSessionId(aPacket.AudioPacket().Ssrc());
        ProcessStreamStartOrResume();
    }
    iDiscovery.KeepAlive();

    // FIXME - for airplay dropout, are these values being set appropriately and dropping the received packets?
    const TBool validSession = IsValidSession(aPacket.AudioPacket().Ssrc());
    const TBool shouldFlush = ShouldFlush(aPacket.AudioPacket().Header().Seq(), aPacket.AudioPacket().Timestamp());

    //LOG(kMedia, "ProtocolRaop::ProcessPacket validSession: %u, shouldFlush: %u\n", validSession, shouldFlush);

    if (validSession && !shouldFlush) {
        try {
            IRepairable* repairable = iRepairableAllocator.Allocate(aPacket);
            try {
                    iRepairer.OutputAudio(*repairable);
            }
            catch (RepairerBufferFull&) {
                LOG(kPipeline, "ProtocolRaop::ProcessPacket(const RaopPacketResendResponse&) RepairerBufferFull\n");
                // Set state so that no more audio is output until a MsgDrain followed by a MsgEncodedStream.
                AutoMutex a(iLockRaop);
                iDiscontinuity = true;
                iSem.Signal();
            }
            catch (RepairerStreamRestarted&) {
                LOG(kPipeline, "ProtocolRaop::ProcessPacket(const RaopPacketResendResponse&) RepairerStreamRestarted\n");
                AutoMutex a(iLockRaop);
                iDiscontinuity = true;
                iSem.Signal();
            }
        }
        catch (const RaopAllocationFailure&) {
            LOG(kPipeline, "ProtocolRaop::ProcessPacket(const RaopPacketResendResponse&) RaopAllocationFailure aPacket.AudioPacket().Header().Seq(): %u, aPacket.AudioPacket().Payload().Bytes(): %u\n", aPacket.AudioPacket().Header().Seq(), aPacket.AudioPacket().Payload().Bytes());
        }
    }
}

void ProtocolRaop::ProcessStreamStartOrResume()
{
    iAudioDecryptor.Init(iDiscovery.Aeskey(), iDiscovery.Aesiv());

    Track *track = nullptr;
    TUint latency = 0;
    TUint streamId = 0;
    Uri uri;
    TBool started = false;
    TBool resumePending = false;
    {
        AutoMutex a(iLockRaop);
        started = iStarted;
        iStarted = true;
        resumePending = iResumePending;
        iResumePending = false;
        iFlushSeq = 0;
        iFlushTime = 0;

        if (!started) {
            // Report blank URI.
            track = iTrackFactory.CreateTrack(Brx::Empty(), Brx::Empty());
            latency = iLatency = iControlServer.Latency();
        }
        // Always output a new stream ID on start or resume pending (to avoid CodecStreamCorrupt exceptions).
        streamId = iStreamId = iIdProvider->NextStreamId();
        uri.Replace(iUri.AbsoluteUri());
    }

    /*
     * NOTE: outputting MsgTrack then MsgEncodedStream causes accumulated time reported by pipeline to be reset to 0.
     * Not necessarily desirable when pausing or seeking.
     */
    if (!started) {
        iSupply->OutputTrack(*track, !resumePending);
        track->RemoveRef();
    }
    // Always output a new stream ID on start or resume pending (to avoid CodecStreamCorrupt exceptions).
    iSupply->OutputStream(uri.AbsoluteUri(), 0, 0, false /* seekable */, true /* live */, Multiroom::Allowed, *this, streamId);
    if (!started) {
        // Stream has been output, so now safe to output delay.
        iSupply->OutputDelay(Delay(latency));
    }
    OutputContainer(iDiscovery.Fmtp());
}

void ProtocolRaop::StartServers()
{
    iAudioServer.Open();
    iControlServer.Open();
}

void ProtocolRaop::StopServers()
{
    // This must only be called when no PacketConsumed() call is outstanding on either server.
    iControlServer.Close();
    iAudioServer.Close();
}

TUint ProtocolRaop::Delay(TUint aSamples)
{
    static const TUint kJiffiesPerSample = Jiffies::PerSample(kSampleRate);
    return kJiffiesPerSample*aSamples;
}

TUint ProtocolRaop::TryStop(TUint aStreamId)
{
    LOG(kMedia, "ProtocolRaop::TryStop aStreamId: %u\n", aStreamId);
    TBool stop = false;
    AutoMutex a(iLockRaop);
    if (!iStopped && iActive) {
        stop = (iStreamId == aStreamId && aStreamId != IPipelineIdProvider::kStreamIdInvalid);
        if (stop) {
            // Don't increment flush ID if current MsgFlush hasn't been output.
            if (iNextFlushId == MsgFlush::kIdInvalid) {
                iNextFlushId = iFlushIdProvider->NextFlushId();
            }
            iStopped = true;
            DoInterrupt(true);
            // Lock doesn't need to be held for this; code that opens server from other thread must check iStopped before opening it.
            StopServers();
            iSem.Signal();
        }
    }
    return (stop ? iNextFlushId : MsgFlush::kIdInvalid);
}

void ProtocolRaop::NotifyStarving(const Brx& aMode, TUint aStreamId, TBool aStarving)
{
    /*
     * The pipeline calls into this method.
     *
     * Calling Repairer::DropAudio() will result in Repairer calling Cancel()
     * on its retry timer.
     *
     * However, it's possible that a timer callback in RAOP discovery code will
     * have triggered a call into SourceRaop to tell the pipeline to Stop().
     *
     * That can cause a deadlock where the RAOP discovery timer callback is
     * waiting for the pipeline to stop (as Stopper is waiting on a mutex), and
     * Stopper already holds mutex for this NotifyStarving() call and
     * Repairer::DropAudio() is trying to cancel its timer but the timer
     * callback already holds the timer mutex!
     *
     * Solution for ProtocolRaop:
     * - Set a flag here (iDiscontinuity).
     * - Only interrupt network sockets.
     * - Call Repairer::DropAudio() from thread running ProtocolRaop::Stream().
     * Solution for RAOP discovery and SourceRaop:
     * - When a TEARDOWN request comes in, or timer times out, do NOT call
     *   NotifySessionEnd() (in fact, never call NotifySessionEnd(), as only
     *   ever want to stop pipeline in Net Aux mode when swithcing to
     *   different source).
     */
    LOG(kMedia, ">ProtocolRaop::NotifyStarving mode: %.*s, sid: %u, starving: %u\n", PBUF(aMode), aStreamId, aStarving);
    AutoMutex a(iLockRaop);
    if (aStarving) {
        //iDiscontinuity = true;    // FIXME - if enabling this and, say, forcing Repairer to throw a RepairerBufferFull after every 1000 packets, protocol goes into an infinite restart loop as it ends up blocked waiting for MsgDrain to reach end up pipeline before it can output anymore audio.
        iStarving = true;
        DoInterrupt(true);
        iSem.Signal();
    }
}

void ProtocolRaop::AudioPacketReceived()
{
    iSem.Signal();
}

void ProtocolRaop::ResendPacketReceived()
{
    iSem.Signal();
}

void ProtocolRaop::SendFlush(TUint aSeq, TUint aTime, FunctorGeneric<TUint> aFlushHandler)
{
    LOG(kMedia, ">ProtocolRaop::SendFlush\n");
    AutoMutex _(iLockRaop);

    if (!iActive) {
        // It's possible that the RAOP session is still active, so this is a
        // valid call from the source module, but the pipeline has been unable
        // to play the stream for some reason (network issue, change in
        // protocol) and TryStop() has been called which has caused Stream() to
        // return, so no valid flush can be sent.
        //
        // Do not notify callback, as no flush to wait for.
        LOG(kMedia, "<ProtocolRaop::SendFlush !iActive\n");
        return;
    }

    iFlushSeq = aSeq;
    iFlushTime = aTime;

    // Don't increment flush ID if current MsgFlush hasn't been output.
    if (iNextFlushId == MsgFlush::kIdInvalid) {
        iNextFlushId = iFlushIdProvider->NextFlushId();
        iWaiting = true;
    }

    aFlushHandler(iNextFlushId);

    // FIXME - clear any resend-related members here?

    DoInterrupt(true);  // FIXME - need to do an interrupt here?

    // FIXME - need to signal iSem here, or should DoInterrupt() do that?
    iSem.Signal();

    LOG(kMedia, "<ProtocolRaop::SendFlush iNextFlushId: %u\n", iNextFlushId);
}


// RaopControlServer

RaopControlServer::RaopControlServer(SocketUdpServer* aServer, IRaopResendConsumer& aResendConsumer, TUint aThreadPriority)
    : iClientPort(kInvalidServerPort)
    , iServer(aServer)
    , iResendConsumer(aResendConsumer)
    , iLatency(kDefaultLatencySamples)
    , iLock("RACL")
    , iOpen(false)
    , iExit(false)
    , iAwaitingConsumer(false)
    , iSem("RCSS", 0)
{
    iThread = new ThreadFunctor("RaopControlServer", MakeFunctor(*this, &RaopControlServer::Run), aThreadPriority, kSessionStackBytes);
    iThread->Start();
}

RaopControlServer::~RaopControlServer()
{
    {
        AutoMutex _(iLock);
        iExit = true;
    }
    iServer->Interrupt(true);
    iServer->RemoveRef();
    iSem.Signal();
    iThread->Join();
    delete iThread;
}

void RaopControlServer::Open()
{
    LOG_INFO(kMedia, "RaopControlServer::Open\n");
    AutoMutex a(iLock);
    if (!iOpen) {
        iServer->Open();
        iOpen = true;
        iSem.Clear();
        iSem.Signal();
    }
}

void RaopControlServer::Close()
{
    LOG_INFO(kMedia, "RaopControlServer::Close\n");
    AutoMutex a(iLock);
    if (iOpen) {
        iServer->Close();
        iOpen = false;

        // Clear any unread packet, which is now invalid.
        iBuf.SetBytes(0);
        iPacket.Clear();
        iAwaitingConsumer = false;
    }
}

void RaopControlServer::Interrupt(TBool aInterrupt)
{
    LOG(kMedia, "RaopControlServer::Interrupt aInterrupt: %u\n", aInterrupt);
    iServer->Interrupt(aInterrupt);
}

void RaopControlServer::Reset(TUint aClientPort)
{
    AutoMutex a(iLock);
    iClientPort = aClientPort;
    //iLatency = kDefaultLatencySamples; // Persist previous latency on assumption that next device to connect will have same (or similar) latency to last device. Attempts to avoid audio ramp down/up caused by unnecessarily changing delay.
}

const RaopPacketResendResponse& RaopControlServer::Packet()
{
    AutoMutex _(iLock);
    if (iAwaitingConsumer || !iOpen) {
        return iPacket;
    }
    else {
        THROW(RaopPacketUnavailable);
    }
}

void RaopControlServer::PacketConsumed()
{
    AutoMutex _(iLock);
    if (iAwaitingConsumer) {
        iAwaitingConsumer = false;
        iSem.Signal();
    }
}

void RaopControlServer::Run()
{
    for (;;) {
        iSem.Wait();

        TBool canRead = false;
        {
            AutoMutex _(iLock);
            if (iExit) {
                return;
            }
            if (!iOpen) {
                // Semaphore was signalled, but server now closed.
                // Silently consume this signal.
                LOG_DEBUG(kMedia, "RaopControlServer::Run !iOpen\n");
                continue;
            }
            if (!iAwaitingConsumer) {
                canRead = true;
            }
        }

        if (canRead) {
            try {
                iBuf.SetBytes(0);
                iEndpoint.Replace(iServer->Receive(iBuf));
                try {
                    // This (and other packet wrappers) may throw InvalidRaopPacket.
                    RtpPacketRaop packet(iBuf);
                    if (packet.Header().Type() == ESync) {
                        RaopPacketSync syncPacket(packet);

                        // Extension bit set on sync packet signifies stream (re-)starting.
                        // However, by it's nature, UDP is unreliable, so can't rely on this for detecting (re-)start.
                        //LOG(kMedia, "RaopControlServer::Run packet.Extension(): %u\n", packet.Header().Extension());

                        AutoMutex a(iLock);
                        const TUint latency = iLatency;
                        iLatency = syncPacket.RtpTimestamp()-syncPacket.RtpTimestampMinusLatency();

                        if (iLatency != latency) {
                            LOG(kMedia, "RaopControlServer::Run Old latency: %u; New latency: %u\n", latency, iLatency);
                        }

                        //LOG(kMedia, "RaopControlServer::Run RtpTimestampMinusLatency: %u, NtpTimestampSecs: %u, NtpTimestampFract: %u, RtpTimestamp: %u, iLatency: %u\n", syncPacket.RtpTimestampMinusLatency(), syncPacket.NtpTimestampSecs(), syncPacket.NtpTimestampFract(), syncPacket.RtpTimestamp(), iLatency);


                        // FIXME - should notify latency observer.



                        // Set to read next packet.
                        iSem.Signal();
                    }
                    else if (packet.Header().Type() == EResendResponse) {
                        // Resend response packet contains a full audio packet as payload.
                        try {
                            iPacket.Set(packet);
                        }
                        catch (InvalidRaopPacket&) {
                            LOG_ERROR(kMedia, "RaopControlServer::Run caught InvalidRaopPacket while attempting to parse resend respons. Padding: %u, extension: %u, csrcCount: %u, marker: %u, type: %u, seq: %u\n",
                                packet.Header().Padding(),
                                packet.Header().Extension(),
                                packet.Header().CsrcCount(),
                                packet.Header().Marker(),
                                packet.Header().Type(),
                                packet.Header().Seq()
                            );
                            iSem.Signal();
                            continue;
                        }

                        // Resend packet received. Do not attempt to read anything else (including latency packets) until consumer reads this packet.
                        AutoMutex _(iLock);
                        iAwaitingConsumer = true;
                        iResendConsumer.ResendPacketReceived();

                        // No iSem.Signal()here . Wait for consumer to call Packet()/PacketConsumed() or Close()/Open().
                        // Noisy logging - will make any dropouts worse.
                        LOG_DEBUG(kPipeline, "RaopControlServer::Run called iResendConsumer.ResendPacketReceived()\n");
                    }
                    else {
                        LOG(kMedia, "RaopControlServer::Run unexpected packet type: %u\n", packet.Header().Type());
                        iSem.Signal();
                    }
                }
                catch (InvalidRaopPacket&) {
                    LOG_ERROR(kMedia, "RaopControlServer::Run caught InvalidRaopPacket\n");
                    iSem.Signal();
                }
            }
            catch (UdpServerClosed&) {
                LOG_DEBUG(kMedia, "RaopControlServer::Run caught UdpServerClosed\n");
                Thread::Sleep(kSocketFailureRetryIntervalMs); // Try avoid blocking low priority threads if this goes into a retry loop.
                iSem.Signal();
            }
            catch (NetworkError&) {
                LOG_DEBUG(kMedia, "RaopControlServer::Run caught NetworkError\n");
                Thread::Sleep(kSocketFailureRetryIntervalMs); // Try avoid blocking low priority threads if this goes into a retry loop.
                iSem.Signal();
            }
        }
    }
}

TUint RaopControlServer::Latency() const
{
    AutoMutex a(iLock);
    return iLatency;
}

void RaopControlServer::RequestResend(TUint aSeqStart, TUint aCount)
{
    // If there is a high packet drop rate, enabling this logging can make the problem even worse.
    LOG_TRACE(kPipeline, "RaopControlServer::RequestResend aSeqStart: %u, aCount: %u\n", aSeqStart, aCount);

    RaopPacketResendRequest resendPacket(aSeqStart, aCount);
    Bws<RaopPacketResendRequest::kBytes> resendBuf;
    WriterBuffer writerBuffer(resendBuf);
    resendPacket.Write(writerBuffer);

    try {
        iLock.Wait();
        //iEndpoint.SetPort(iClientPort); // Send to client listening port.
        iLock.Signal();

        // FIXME - need to lock around iEndpoint (or do this on main thread).
        iServer->Send(resendBuf, iEndpoint);

        // Signal thread to try read response (it should always be trying to read from socket anyway).
        //iSem.Signal();
    }
    catch (NetworkError&) {
        // Will handle this by timing out on receive.
    }
}


// RaopResendRangeRequester

RaopResendRangeRequester::RaopResendRangeRequester(IRaopResendRequester& aResendRequester)
    : iResendRequester(aResendRequester)
{
}

void RaopResendRangeRequester::RequestResendSequences(const std::vector<const IResendRange*> aRanges)
{
    // Noisy logging - will make dropouts worse.
    LOG_TRACE(kPipeline, ">RaopResendRangeRequester::RequestResendSequences\n");
    for (auto range : aRanges) {
        const TUint start = range->Start();
        const TUint end = range->End();
        const TUint count = (end-start)+1;  // +1 to include start packet.
        LOG_TRACE(kPipeline, "\t%d->%d\n", start, end);
        iResendRequester.RequestResend(start, count);
    }
}


// ResendRange

ResendRange::ResendRange()
    : iStart(0)
    , iEnd(0)
{
}

void ResendRange::Set(TUint aStart, TUint aEnd)
{
    iStart = aStart;
    iEnd = aEnd;
}

TUint ResendRange::Start() const
{
    return iStart;
}

TUint ResendRange::End() const
{
    return iEnd;
}


// RaopAudioServer

RaopAudioServer::RaopAudioServer(SocketUdpServer* aServer, IRaopAudioConsumer& aConsumer, TUint aThreadPriority)
    : iServer(aServer)
    , iConsumer(aConsumer)
    , iOpen(false)
    , iQuit(false)
    , iAwaitingConsumer(false)
    , iSem("RASS", 0)
    , iLock("RASL")
{
    iThread = new ThreadFunctor("RaopAudioServer", MakeFunctor(*this, &RaopAudioServer::Run), aThreadPriority);
    iThread->Start();
}

RaopAudioServer::~RaopAudioServer()
{
    {
        AutoMutex _(iLock);
        iQuit = true;
    }
    iServer->Interrupt(true);
    iServer->RemoveRef();
    iSem.Signal();
    iThread->Join();
    delete iThread;
}

void RaopAudioServer::Open()
{
    LOG_INFO(kMedia, "RaopAudioServer::Open\n");
    AutoMutex a(iLock);
    if (!iOpen) {
        iServer->Open();
        iOpen = true;
        iSem.Clear();
        iSem.Signal();
    }
}

void RaopAudioServer::Close()
{
    LOG_INFO(kMedia, "RaopAudioServer::Close\n");
    AutoMutex a(iLock);
    if (iOpen) {
        iServer->Close();
        iOpen = false;

        // Clear any unread packet, which is now invalid.
        iBuf.SetBytes(0);
        iPacket.Clear();
        iAwaitingConsumer = false;
    }
}

void RaopAudioServer::Interrupt(TBool aInterrupt)
{
    LOG(kMedia, "RaopAudioServer::Interrupt aInterrupt: %u\n", aInterrupt);
    iServer->Interrupt(aInterrupt);
}

void RaopAudioServer::Reset()
{
}

const RaopPacketAudio& RaopAudioServer::Packet() const
{
    AutoMutex _(iLock);
    if (iAwaitingConsumer || !iOpen) {
        return iPacket;
    }
    else {
        THROW(RaopPacketUnavailable);
    }
}

void RaopAudioServer::PacketConsumed()
{
    AutoMutex _(iLock);
    if (iAwaitingConsumer) {
        iAwaitingConsumer = false;
        iSem.Signal();
    }
}

void RaopAudioServer::Run()
{
    for (;;) {
        iSem.Wait();

        TBool canRead = false;
        {
            AutoMutex _(iLock);
            if (iQuit) {
                return;
            }
            if (!iOpen) {
                // Semaphore was signalled, but server now closed.
                // Silently consume this signal.
                LOG_DEBUG(kMedia, "RaopAudioServer::Run !iOpen\n");
                continue;
            }
            if (!iAwaitingConsumer) {
                canRead = true;
            }
        }

        if (canRead) {
            try {
                iBuf.SetBytes(0);
                (void)iServer->Receive(iBuf); // Never send any data to audio server, so don't care about Endpoint returned.
                try {
                    iPacket.Set(iBuf);
                }
                catch (InvalidRaopPacket&) {
                    iSem.Signal();
                    continue;
                }

                AutoMutex _(iLock);
                iAwaitingConsumer = true;
                iConsumer.AudioPacketReceived();
            }
            catch (UdpServerClosed&) {
                LOG_DEBUG(kMedia, "RaopAudioServer::Run UdpServerClosed\n");
                Thread::Sleep(kSocketFailureRetryIntervalMs); // Try avoid blocking low priority threads if this goes into a retry loop.
                // Read failed, so resignal semaphore.
                iSem.Signal();
            }
            catch (NetworkError&) {
                LOG_DEBUG(kMedia, "RaopAudioServer::Run NetworkError\n");
                Thread::Sleep(kSocketFailureRetryIntervalMs); // Try avoid blocking low priority threads if this goes into a retry loop.
                // Read failed, so resignal semaphore.
                iSem.Signal();
            }
        }
    }
}


// RaopAudioDecryptor

void RaopAudioDecryptor::Init(const Brx& aAesKey, const Brx& aAesInitVector)
{
    iKey.Replace(aAesKey);
    iInitVector.Replace(aAesInitVector);
}

void RaopAudioDecryptor::Decrypt(const Brx& aEncryptedIn, Bwx& aAudioOut) const
{
    //LOG(kMedia, ">RaopAudioDecryptor::Decrypt aEncryptedIn.Bytes(): %u\n", aEncryptedIn.Bytes());
    ASSERT(iKey.Bytes() > 0);
    ASSERT(iInitVector.Bytes() > 0);
    ASSERT(aAudioOut.MaxBytes() >= kPacketSizeBytes+aEncryptedIn.Bytes());

    aAudioOut.SetBytes(0);
    WriterBuffer writerBuffer(aAudioOut);
    WriterBinary writerBinary(writerBuffer);
    writerBinary.WriteUint32Be(aEncryptedIn.Bytes());    // Write out payload size.

    unsigned char* inBuf = const_cast<unsigned char*>(aEncryptedIn.Ptr());
    unsigned char* outBuf = const_cast<unsigned char*>(aAudioOut.Ptr()+aAudioOut.Bytes());
    unsigned char initVector[16];
    memcpy(initVector, iInitVector.Ptr(), sizeof(initVector));  // Use same initVector at start of each decryption block.

    AES_cbc_encrypt(inBuf, outBuf, aEncryptedIn.Bytes(), (AES_KEY*)iKey.Ptr(), initVector, AES_DECRYPT);
    const TUint audioRemaining = aEncryptedIn.Bytes() % 16;
    const TUint audioWritten = aEncryptedIn.Bytes()-audioRemaining;
    if (audioRemaining > 0) {
        // Copy remaining audio to outBuf if <16 bytes.
        memcpy(outBuf+audioWritten, inBuf+audioWritten, audioRemaining);
    }
    aAudioOut.SetBytes(kPacketSizeBytes+aEncryptedIn.Bytes());
}

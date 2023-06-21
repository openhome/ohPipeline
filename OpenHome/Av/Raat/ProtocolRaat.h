#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Av/Raat/Output.h>
#include <OpenHome/Av/Raat/Transport.h>
#include <OpenHome/Media/SupplyAggregator.h>

namespace OpenHome {

    class Environment;

    namespace Media {
        class TrackFactory;
    }

namespace Av {

class RaatSupplyDsd;

class ProtocolRaat : public Media::Protocol, private IRaatWriter
{
    static const TUint kMaxStreamUrlSize = 1024;
public:
    ProtocolRaat(Environment& aEnv, IRaatReader& aRaatReader, Media::TrackFactory& aTrackFactory);
    ~ProtocolRaat();
private: // from Media::Protocol
    void Initialise(Media::MsgFactory& aMsgFactory, Media::IPipelineElementDownstream& aDownstream) override;
    void Interrupt(TBool aInterrupt) override;
    Media::ProtocolStreamResult Stream(const Brx& aUri) override;
    Media::ProtocolGetResult Get(IWriter& aWriter, const Brx& aUri, TUint64 aOffset, TUint aBytes) override;
private: // from Media::IStreamHandler
    TUint TryStop(TUint aStreamId) override;
private: // from IRaatWriter
    void WriteMetadata(const Brx& aTitle, const Brx& aSubtitle, TUint aPosSeconds, TUint aDurationSeconds) override;
    void WriteDelay(TUint aJiffies) override;
    void WriteData(const Brx& aData) override;
private:
    void OutputStream(TUint64 aSampleStart, TUint64 aDurationBytes);
private:
    Mutex iLock;
    IRaatReader& iRaatReader;
    Media::TrackFactory& iTrackFactory;
    Media::SupplyAggregator* iSupplyPcm;
    RaatSupplyDsd* iSupplyDsd;
    Media::ISupply* iSupply;
    RaatUri iRaatUri;
    TUint iStreamId;
    TUint iMaxBytesPerAudioChunk;
    TUint iNextFlushId;
    TBool iStopped;
    TBool iPcmStream;
    Bws<RaatTransport::kMaxBytesMetadataTitle> iMetadataTitle;
    Bws<RaatTransport::kMaxBytesMetadataSubtitle> iMetadataSubtitle;
    Bws<Media::kTrackMetaDataMaxBytes> iDidlLite; // local scope but too big for the stack
    TUint iLastTrackPosSeconds;
};

class RaatSupplyDsd : public Media::ISupply
{
public:
    RaatSupplyDsd(Media::MsgFactory& aMsgFactory, Media::IPipelineElementDownstream& aDownStreamElement);
    virtual ~RaatSupplyDsd();
    void Flush();
public: // from ISupply
    void OutputTrack(Media::Track& aTrack, TBool aStartOfStream = true) override;
    void OutputDrain(Functor aCallback) override;
    void OutputDelay(TUint aJiffies) override;
    void OutputStream(const Brx& aUri, TUint64 aTotalBytes, TUint64 aStartPos, TBool aSeekable, TBool aLive, Media::Multiroom aMultiroom, Media::IStreamHandler& aStreamHandler, TUint aStreamId, TUint aSeekPosMs = 0) override;
    void OutputPcmStream(const Brx& aUri, TUint64 aTotalBytes, TBool aSeekable, TBool aLive, Media::Multiroom aMultiroom, Media::IStreamHandler& aStreamHandler, TUint aStreamId, const Media::PcmStreamInfo& aPcmStream) override;
    void OutputPcmStream(const Brx& aUri, TUint64 aTotalBytes, TBool aSeekable, TBool aLive, Media::Multiroom aMultiroom, Media::IStreamHandler& aStreamHandler, TUint aStreamId, const Media::PcmStreamInfo& aPcmStream, Media::RampType aRamp) override;
    void OutputDsdStream(const Brx& aUri, TUint64 aTotalBytes, TBool aSeekable, Media::IStreamHandler& aStreamHandler, TUint aStreamId, const Media::DsdStreamInfo& aDsdStream) override;
    void OutputSegment(const Brx& aId) override;
    void OutputData(const Brx& aData) override;
    void OutputMetadata(const Brx& aMetadata) override;
    void OutputHalt(TUint aHaltId = Media::MsgHalt::kIdNone) override;
    void OutputFlush(TUint aFlushId) override;
    void OutputWait() override;
private:
    void Output(Media::Msg* aMsg);
private:
    Media::MsgFactory& iMsgFactory;
    Media::IPipelineElementDownstream& iDownStreamElement;
    static const TUint kMaxDsdDataBytes = Media::AudioData::kMaxBytes - (Media::AudioData::kMaxBytes % 6);
    Bws<kMaxDsdDataBytes> iDsdDataBuf;
    Bws<4> iDsdPartialBlock;
};

} // namespace Av
} // namespace OpenHome

#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Codec/DsdFiller.h>
#include <OpenHome/Av/Raat/Output.h>
#include <OpenHome/Av/Raat/Transport.h>
#include <OpenHome/Media/SupplyAggregator.h>

namespace OpenHome {

    class Environment;

    namespace Media {
        class TrackFactory;
    }

namespace Av {

class ProtocolRaat
    : public Media::Protocol
    , private Media::DsdFiller
    , private IRaatWriter
{
private:
    static const TUint kDsdBlockBytes = 4;
    static const TUint kDsdChunksPerBlock = 1;
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
private: // from DsdFiller
    void WriteChunkDsd(const TByte*& aSrc, TByte*& aDest) override;
    void OutputDsd(const Brx& aData) override;
private: // from IRaatWriter
    void Write(const Brx& aData) override;
private:
    void OutputStream(TUint64 aSampleStart);
private:
    Mutex iLock;
    IRaatReader& iRaatReader;
    Media::TrackFactory& iTrackFactory;
    Media::SupplyAggregator* iSupply;
    RaatUri iRaatUri;
    TUint iStreamId;
    TUint iNextFlushId;
    TBool iStopped;
    TBool iPcmStream;
};

} // namespace Av
} // namespace OpenHome

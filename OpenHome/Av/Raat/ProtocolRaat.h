#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Av/Raat/Output.h>
#include <OpenHome/Media/SupplyAggregator.h>

namespace OpenHome {

    class Environment;

    namespace Media {
        class TrackFactory;
    }

namespace Av {

class ProtocolRaat : public Media::Protocol, private IRaatWriter
{
    static const TUint kMaxStreamUrlSize = 1024;
public:
    ProtocolRaat(Environment& aEnv, IRaatReader& aRaatReader, Media::TrackFactory& aTrackFactory);
private: // from Media::Protocol
    void Initialise(Media::MsgFactory& aMsgFactory, Media::IPipelineElementDownstream& aDownstream) override;
    void Interrupt(TBool aInterrupt) override;
    Media::ProtocolStreamResult Stream(const Brx& aUri) override;
    Media::ProtocolGetResult Get(IWriter& aWriter, const Brx& aUri, TUint64 aOffset, TUint aBytes) override;
private: // from Media::IStreamHandler
    TUint TryStop(TUint aStreamId) override;
private: // from IRaatWriter
    void WriteMetadata(const Brx& aMetadata) override;
    void WriteDelay(TUint aJiffies) override;
    void WriteData(const Brx& aData) override;
private:
    Mutex iLock;
    IRaatReader& iRaatReader;
    Media::TrackFactory& iTrackFactory;
    Media::SupplyAggregator* iSupply;
    RaatUri iRaatUri;
    TUint iStreamId;
    TUint iMaxBytesPerAudioChunk;
    TUint iNextFlushId;
    TBool iStopped;
};

} // namespace Av
} // namespace OpenHome

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

EXCEPTION(ProtocolRaatInterrupt);

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
    static const TUint kDefaultDelayMs = 150;
    static const TUint kDefaultDelayJiffies = kDefaultDelayMs * Media::Jiffies::kPerMs;
public:
    static const Brn kUri;
private:
    enum class EStreamState {
        eStopped,
        eIdle,
        eStreaming
    };
public:
    ProtocolRaat(Environment& aEnv, IRaatReader& aRaatReader, Media::TrackFactory& aTrackFactory);
    ~ProtocolRaat();
public:
    TBool IsStreaming();
    void NotifySetup();
    void NotifyStart();
    TUint FlushAsync();
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
    void OutputStream(const RaatStreamFormat& aStreamFormat);
    void OutputDrain();
    void DoInterrupt();
private:
    Environment& iEnv;
    IRaatReader& iRaatReader;
    Media::TrackFactory& iTrackFactory;
    Media::SupplyAggregator* iSupply;
    std::atomic<EStreamState> iState;
    std::atomic<TBool> iInterrupt;
    Semaphore iSemStateChange;
    Mutex iLock;

    TUint iNextFlushId;
    TBool iPcmStream;
    TBool iSetup;
};

} // namespace Av
} // namespace OpenHome

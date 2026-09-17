#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Protocol/Protocol.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/SupplyAggregator.h>
#include <OpenHome/Av/QobuzConnect/AudioStream.h>

EXCEPTION(ProtocolQobuzConnectInterrupt)

namespace OpenHome {

    class Environment;

    namespace Media {
        class TrackFactory;
    }

namespace Av {

/**
 * Pipeline-facing Protocol implementation for Qobuz Connect - mirrors ProtocolRaat
 * (OpenHome/Av/Raat/ProtocolRaat.h), pulling audio from IQobuzConnectAudioReader (backed by
 * QobuzConnectAudioStream, which bridges the SDK's push-based delegate callbacks) instead of
 * IRaatReader. Qobuz Connect is PCM-only (no DSD), so this is simpler than ProtocolRaat - no
 * DsdFiller involvement.
 */
class ProtocolQobuzConnect
    : public Media::Protocol
    , private IQobuzConnectAudioWriter
{
private:
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
    ProtocolQobuzConnect(Environment& aEnv, IQobuzConnectAudioReader& aReader, Media::TrackFactory& aTrackFactory);
    ~ProtocolQobuzConnect();
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
private: // from IQobuzConnectAudioWriter
    void Write(const Brx& aData) override;
private:
    void OutputStream(const QobuzConnectStreamFormat& aStreamFormat);
    void OutputDrain();
    void DoInterrupt();
private:
    Environment& iEnv;
    IQobuzConnectAudioReader& iReader;
    Media::TrackFactory& iTrackFactory;
    Media::SupplyAggregator* iSupply;
    std::atomic<EStreamState> iState;
    std::atomic<TBool> iInterrupt;
    Semaphore iSemStateChange;
    Semaphore iSemDrain;
    Mutex iLock;

    TUint iNextFlushId;
    TBool iSetup;
};

} // namespace Av
} // namespace OpenHome

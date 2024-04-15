#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Media/Pipeline/Msg.h>

#include <list>

namespace OpenHome {
namespace Media {

class IAsyncTrackBoundary
{
public:
    virtual ~IAsyncTrackBoundary() {}
    virtual const Brx& Mode() const = 0;
    virtual TUint OffsetMs() const = 0;
    virtual TUint DurationMs() const = 0;
};

class IAsyncTrackPosition
{
public:
    virtual ~IAsyncTrackPosition() {}
    virtual const Brx& Mode() const = 0;
    virtual TUint PositionMs() const = 0;
};

/* IAsyncTrackClient
 * Clients should implement this interface in order to register themselves with 
 * IAsyncTrackObserver. Metadata can then be written asynchronously at IAsyncTrackObserver's
 * descretion
 */
class IAsyncTrackClient
{
public:
    virtual ~IAsyncTrackClient() {}
    virtual const Brx& Mode() const = 0;
    virtual void WriteMetadata(const Brx& aTrackUri, const DecodedStreamInfo& aStreamInfo, IWriter& aWriter) = 0;
    virtual const IAsyncTrackBoundary& GetTrackBoundary() = 0;
};

class IAsyncTrackObserver
{
public:
    virtual ~IAsyncTrackObserver() {}
    virtual void AddClient(IAsyncTrackClient& aClient) = 0;
    /*
     * Call when new metadata is available
     */
    virtual void TrackMetadataChanged(const Brx& aMode) = 0;
    /*
     * Call when the track offset or duration has changed (e.g., following a seek)
     */
    virtual void TrackBoundaryChanged(const Brx& aMode) = 0;
    /*
     * Call to update the current playback position, so that action can be taken if loss of sync is detected
     */
    virtual void TrackPositionChanged(const IAsyncTrackPosition& aPosition) = 0;
};

class AsyncMetadataRequests
{
public:
    AsyncMetadataRequests() {}
public:
    TBool Exists(const Brx& aMode);
    void Add(const Brx& aMode);
    void Remove(const Brx& aMode);
    void Trim(const Brx& aMode);
    void Clear();
private:
    std::list<std::unique_ptr<Brx>> iRequests;
};

/* AsyncTrackObserver
 * Concrete pipeline element implementation of IAsyncTrackObserver
 */
class AsyncTrackObserver
    : public PipelineElement
    , public IPipelineElementUpstream
    , public IAsyncTrackObserver
    , private INonCopyable
{
private:
    static const TUint kSupportedMsgTypes;
    static const TUint kPositionDeltaThresholdMs = 2000;
public:
    AsyncTrackObserver(
        IPipelineElementUpstream& aUpstreamElement,
        MsgFactory& aMsgFactory,
        TrackFactory& aTrackFactory);

    ~AsyncTrackObserver();
public: // from IPipelineElementUpstream
    Msg* Pull() override;
public: // from IAsyncTrackObserver
    void AddClient(IAsyncTrackClient& aClient) override;
    void TrackMetadataChanged(const Brx& aMode) override;
    void TrackBoundaryChanged(const Brx& aMode) override;
    void TrackPositionChanged(const IAsyncTrackPosition& aPosition) override;
private: // PipelineElement
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgTrack* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
private:
    void UpdateDecodedStreamLocked(const IAsyncTrackBoundary& aBoundary);
private:
    IPipelineElementUpstream& iUpstreamElement;
    MsgFactory& iMsgFactory;
    TrackFactory& iTrackFactory;
    IAsyncTrackClient* iClient;
    MsgDecodedStream* iDecodedStream;
    TBool iDecodedStreamPending;
    TBool iPipelineTrackSeen;
    TUint iLastKnownPositionMs;
    mutable Mutex iLock;

    AsyncMetadataRequests iRequests;
    std::vector<IAsyncTrackClient*> iClients;
    BwsTrackUri iTrackUri;
};

} // namespace Media
} // namespace OpenHome

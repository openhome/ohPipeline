#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Media/Pipeline/Msg.h>

namespace OpenHome {
namespace Media {


/* IAsyncMetadata
 * Interface to provide the minimum information required by IAsyncTrackObserver
 * Clients should implement and extend as necessary
 */
class IAsyncMetadata
{
public:
    virtual TUint DurationMs() const = 0;

    virtual ~IAsyncMetadata() {}
};

/* IAsyncMetadataAllocated
 * Reference-counted object to wrap IAsyncMetadata
 */
class IAsyncMetadataAllocated
{
public:
    virtual const IAsyncMetadata& Metadata() const = 0;
    virtual void AddReference() = 0;
    virtual void RemoveReference() = 0;
    virtual ~IAsyncMetadataAllocated() {}
};

/* IAsyncTrackClient
 * Clients should implement this interface in order to register themselves with 
 * IAsyncTrackObserver. Metadata can then be written asynchronously at IAsyncTrackObserver's
 * descretion
 */
class IAsyncTrackClient
{
public:
    virtual const Brx& Mode() const = 0;
    virtual TBool ForceDecodedStream() const = 0;
    virtual void WriteMetadata(const Brx& aTrackUri, const IAsyncMetadata& aMetadata, const DecodedStreamInfo& aStreamInfo, IWriter& aWriter) = 0;

    virtual ~IAsyncTrackClient() {}
};

class IAsyncTrackObserver
{
public:
    virtual void AddClient(IAsyncTrackClient& aClient) = 0;
    /*
     * Call when new metadata is available
     */
    virtual void MetadataChanged(const Brx& aMode, IAsyncMetadataAllocated* aMetadata) = 0;
    /*
     * Call when the track offset has actively changed (e.g., due to a seek)
     */
    virtual void TrackOffsetChanged(const Brx& aMode, TUint aOffsetMs) = 0;
    /*
     * Call to update the current playback position, so that action can be taken if loss of sync is detected
     */
    virtual void TrackPositionChanged(const Brx& aMode, TUint aPositionMs) = 0;

    virtual ~IAsyncTrackObserver() {}
};

/**
 * Helper class to store start offset expressed in milliseconds or samples.
 * Each call to either of the Set() methods overwrites any value set (be it in
 * milliseconds or samples) in a previous call.
 */
class AsyncStartOffset
{
public:
    AsyncStartOffset();
    void SetMs(TUint aOffsetMs);
    TUint64 OffsetSample(TUint aSampleRate) const;
    TUint OffsetMs() const;
    TUint AbsoluteDifference(TUint aOffsetMs) const;
private:
    TUint iOffsetMs;
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
    static const TUint kTrackOffsetChangeThresholdMs = 2000;
public:
    AsyncTrackObserver(
        IPipelineElementUpstream&   aUpstreamElement,
        MsgFactory&                 aMsgFactory,
        TrackFactory&               aTrackFactory);

    ~AsyncTrackObserver();
public: // from IPipelineElementUpstream
    Msg* Pull() override;
public: // from IAsyncTrackObserver
    void AddClient(IAsyncTrackClient& aClient) override;
    void MetadataChanged(const Brx& aMode, IAsyncMetadataAllocated* aMetadata) override;
    void TrackOffsetChanged(const Brx& aMode, TUint aOffsetMs) override;
    void TrackPositionChanged(const Brx& aMode, TUint aPositionMs) override;
private: // PipelineElement
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgTrack* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
private:
    void ClearDecodedStream();
    void UpdateDecodedStream(MsgDecodedStream& aMsg);
    TUint64 TrackLengthJiffiesLocked() const;
    MsgDecodedStream* CreateMsgDecodedStreamLocked() const;
private:
    IPipelineElementUpstream& iUpstreamElement;
    MsgFactory& iMsgFactory;
    TrackFactory& iTrackFactory;
    IAsyncTrackClient* iClient;
    IAsyncMetadataAllocated* iMetadata;
    MsgDecodedStream* iDecodedStream;
    TBool iInterceptMode;
    TBool iMsgDecodedStreamPending;
    TBool iGeneratedTrackPending;
    TBool iPipelineTrackSeen;
    TUint iTrackDurationMs;
    mutable Mutex iLock;

    std::vector<IAsyncTrackClient*> iClients;
    BwsTrackUri iTrackUri;
    AsyncStartOffset iStartOffset;
};

} // namespace Media
} // namespace OpenHome

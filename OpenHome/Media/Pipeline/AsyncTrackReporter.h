#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Media/Pipeline/Msg.h>

namespace OpenHome {
namespace Media {


/* IAsyncMetadata
 * Interface to provide the minimum information required by IAsyncTrackReporter
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
 * IAsyncTrackReporter. Metadata can then be written asynchronously at IAsyncTrackReporter's
 * descretion
 */
class IAsyncTrackClient
{
public:
    virtual const Brx& Mode() const = 0;
    virtual void WriteMetadata(const Brx& aTrackUri, const IAsyncMetadata& aMetadata, const DecodedStreamInfo& aStreamInfo, IWriter& aWriter) = 0;

    virtual ~IAsyncTrackClient() {}
};

class IAsyncTrackReporter
{
public:
    virtual void AddClient(IAsyncTrackClient& aClient) = 0;
    /*
     * Call when new metadata is available
     */
    virtual void MetadataChanged(IAsyncMetadataAllocated* aMetadata) = 0;
    /*
     * Call when the track offset has actively changed (e.g., due to a seek)
     */
    virtual void TrackOffsetChanged(TUint aOffsetMs) = 0;
    /*
     * Call to update the current playback position, so that action can be taken if loss of sync is detected
     */
    virtual void TrackPositionChanged(TUint aPositionMs) = 0;

    virtual ~IAsyncTrackReporter() {}
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

/* AsyncTrackReporter
 * Concrete pipeline element implementation of IAsyncTrackReporter
 */
class AsyncTrackReporter
    : public PipelineElement
    , public IPipelineElementUpstream
    , public IAsyncTrackReporter
    , private INonCopyable
{
private:
    static const TUint kSupportedMsgTypes;
    static const TUint kTrackOffsetChangeThresholdMs = 2000;
public:
    AsyncTrackReporter(
        IPipelineElementUpstream&   aUpstreamElement,
        MsgFactory&                 aMsgFactory,
        TrackFactory&               aTrackFactory);

    ~AsyncTrackReporter();
public: // from IPipelineElementUpstream
    Msg* Pull() override;
public: // from IAsyncTrackReporter
    void AddClient(IAsyncTrackClient& aClient) override;
    void MetadataChanged(IAsyncMetadataAllocated* aMetadata) override;
    void TrackOffsetChanged(TUint aOffsetMs) override;
    void TrackPositionChanged(TUint aPositionMs) override;
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

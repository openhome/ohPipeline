#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Pipeline/AudioReservoir.h>
#include <OpenHome/Media/Pipeline/Reporter.h>

namespace OpenHome {
namespace Media {

class IAirplayReporter
{
public:
    virtual TUint64 Samples() const = 0;
    virtual void Flush(TUint aFlushId) = 0; // Do not increment subsample count until aFlushId passes.
    virtual ~IAirplayReporter() {}
};

class IAirplayMetadata
{
public:
    virtual const Brx& Track() const = 0;
    virtual const Brx& Artist() const = 0;
    virtual const Brx& Album() const = 0;
    virtual const Brx& Genre() const = 0;
    virtual TUint DurationMs() const = 0;
    virtual ~IAirplayMetadata() {}
};

class IAirplayMetadataAllocated
{
public:
    virtual const IAirplayMetadata& Metadata() const = 0;
    virtual void AddReference() = 0;
    virtual void RemoveReference() = 0;
    virtual ~IAirplayMetadataAllocated() {}
};

class IAirplayTrackObserver
{
public:
    virtual void MetadataChanged(Media::IAirplayMetadataAllocated* aMetadata) = 0;
    /*
     * Should be called when track offset has actively changed (e.g., due to a
     * seek).
     */
    virtual void TrackOffsetChanged(TUint aOffsetMs) = 0;
    /*
     * Should be called to update current playback pos, so that action can be
     * taken if loss of sync detected.
     */
    virtual void TrackPosition(TUint aPositionMs) = 0;
    virtual ~IAirplayTrackObserver() {}
};

class AirplayDidlLiteWriter : private INonCopyable
{
private:
    static const TUint kMaxDurationBytes = 17;
public:
    AirplayDidlLiteWriter(const Brx& aUri, const IAirplayMetadata& aMetadata);
    void Write(IWriter& aWriter, TUint aBitDepth, TUint aChannels, TUint aSampleRate) const;
private:
    void SetDurationString(Bwx& aBuf) const;
    void WriteRes(IWriter& aWriter, TUint aBitDepth, TUint aChannels, TUint aSampleRate) const;
    void WriteOptionalAttributes(IWriter& aWriter, TUint aBitDepth, TUint aChannels, TUint aSampleRate) const;
protected:
    const BwsTrackUri iUri;
    const IAirplayMetadata& iMetadata;
};

/**
 * Helper class to store start offset expressed in milliseconds or samples.
 * Each call to either of the Set() methods overwrites any value set (be it in
 * milliseconds or samples) in a previous call.
 */
class AirplayStartOffset
{
public:
    AirplayStartOffset();
    void SetMs(TUint aOffsetMs);
    TUint64 OffsetSample(TUint aSampleRate) const;
    TUint OffsetMs() const;
    TUint AbsoluteDifference(TUint aOffsetMs) const;
private:
    TUint iOffsetMs;
};

/*
 * Element to report number of samples seen since last MsgMode.
 */
class AirplayReporter : public PipelineElement, public IPipelineElementUpstream, public IAirplayReporter, public IAirplayTrackObserver, private INonCopyable
{
private:
    static const TUint kSupportedMsgTypes;
    static const Brn kInterceptMode;

    static const TUint kTrackOffsetChangeThresholdMs = 2000;
public:
    AirplayReporter(IPipelineElementUpstream& aUpstreamElement, MsgFactory& aMsgFactory, TrackFactory& aTrackFactory);
    ~AirplayReporter();
public: // from IPipelineElementUpstream
    Msg* Pull() override;
public: // from IAirplayReporter
    TUint64 Samples() const override;
    void Flush(TUint aFlushId) override;
public: // from IAirplayTrackObserver
    void MetadataChanged(Media::IAirplayMetadataAllocated* aMetadata) override;
    void TrackOffsetChanged(TUint aOffsetMs) override;
    void TrackPosition(TUint aPositionMs) override;
private: // PipelineElement
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgTrack* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgFlush* aMsg) override;
private:
    void ClearDecodedStream();
    void UpdateDecodedStream(MsgDecodedStream& aMsg);
    TUint64 TrackLengthJiffiesLocked() const;
    MsgDecodedStream* CreateMsgDecodedStreamLocked() const;
private:
    IPipelineElementUpstream& iUpstreamElement;
    MsgFactory& iMsgFactory;
    TrackFactory& iTrackFactory;
    AirplayStartOffset iStartOffset;
    TUint iTrackDurationMs;
    BwsTrackUri iTrackUri;
    IAirplayMetadataAllocated* iMetadata;
    TBool iMsgDecodedStreamPending;
    MsgDecodedStream* iDecodedStream;
    TUint64 iSamples;
    TBool iInterceptMode;
    TBool iGeneratedTrackPending;
    TBool iPipelineTrackSeen;
    TUint iPendingFlushId;
    mutable Mutex iLock;
};

} // namespace Media
} // namespace OpenHome

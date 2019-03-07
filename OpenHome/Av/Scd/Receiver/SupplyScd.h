#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Media/Pipeline/Msg.h>

namespace OpenHome {
    class IReader;
namespace Scd {

class SupplyScd : public Media::ISupply, private INonCopyable
{
    static const TUint kAggregateAudioJiffies = 5 * Media::Jiffies::kPerMs;
    static const TUint kDsdPlayableBytesPerChunk = 4; // DSD Specific
    static const TByte kDsdPadding = 0;
public:
    SupplyScd(Media::MsgFactory& aMsgFactory,
              Media::IPipelineElementDownstream& aDownStreamElement,
              TUint aDsdSampleBlockWords,
              TUint aDsdPadBytesPerChunk);
    virtual ~SupplyScd();
    void OutputData(TUint aNumSamples, IReader& aReader);
    void OutputDataDsd(TUint aNumSamples, IReader& aReader);
    void Flush();
    void Discard();
public: // from Media::ISupply
    void OutputTrack(Media::Track& aTrack, TBool aStartOfStream = true) override;
    void OutputDrain(Functor aCallback) override;
    void OutputDelay(TUint aJiffies) override;
    void OutputStream(const Brx& aUri, TUint64 aTotalBytes, TUint64 aStartPos, TBool aSeekable, TBool aLive, Media::Multiroom aMultiroom, Media::IStreamHandler& aStreamHandler, TUint aStreamId) override;
    void OutputPcmStream(const Brx& aUri, TUint64 aTotalBytes, TBool aSeekable, TBool aLive, Media::Multiroom aMultiroom, Media::IStreamHandler& aStreamHandler, TUint aStreamId, const Media::PcmStreamInfo& aPcmStream) override;
    void OutputDsdStream(const Brx& aUri, TUint64 aTotalBytes, TBool aSeekable, Media::IStreamHandler& aStreamHandler, TUint aStreamId, const Media::DsdStreamInfo& aDsdStream) override;
    void OutputData(const Brx& aData) override;
    void OutputMetadata(const Brx& aMetadata) override;
    void OutputHalt(TUint aHaltId = Media::MsgHalt::kIdNone) override;
    void OutputFlush(TUint aFlushId) override;
    void OutputWait() override;
private:
    inline void WriteBlockDsd(const TByte*& aPtr);
protected:
    void Output(Media::Msg* aMsg);
    inline void OutputEncodedAudio();
private:
    Media::MsgFactory& iMsgFactory;
    Media::IPipelineElementDownstream& iDownStreamElement;
    Media::MsgAudioEncoded* iAudioEncoded;
    TUint iBitsPerSample;
    TUint iSamplesCapacity;
    TUint iBytesPerAudioMsg;
    const TUint iDsdSampleBlockWords;    // DSD specific
    const TUint iDsdPadBytesPerChunk;   // DSD specific
    Bwh iPaddingBuffer; // DSD specific 
    TBool iIsDsd;   // DSD specific 
    Bws<Media::AudioData::kMaxBytes> iAudioBuf;
};

};  // namespace Scd
};  // namespace OpenHome

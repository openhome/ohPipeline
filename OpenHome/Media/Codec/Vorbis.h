#include <OpenHome/Media/Codec/CodecController.h>
#include <OpenHome/Media/MimeTypeList.h>

#include <memory>

namespace OpenHome {
namespace Media {
namespace Codec {

class CodecVorbis : public CodecBase, public IWriter
{
private:
    static const TUint kHeaderBytesReq = 14; // granule pos is byte 6:13 inclusive
    static const TUint kSearchChunkSize = 1024;
    static const TUint kIcyMetadataBytes = 255 * 16;
    static const TUint kBitDepth = 16;  // Bit depth always 16 for Vorbis.
    static const TInt kInvalidBitstream;
public:
    static const Brn kCodecVorbis;
public:
    CodecVorbis(IMimeTypeList& aMimeTypeList);
    ~CodecVorbis();
protected: // from CodecBase
    TBool Recognise(const EncodedStreamInfo& aStreamInfo) override;
    void StreamInitialise() override;
    void Process() override;
    TBool TrySeek(TUint aStreamId, TUint64 aSample) override;
    void StreamCompleted() override;
protected:
    virtual void ParseOgg();
    virtual TUint64 GetSamplesTotal();
    TBool TrySeekBytes(TUint aStreamId, TUint64 aSample, TUint64 aBytePos);
private: // from IWriter
    void Write(TByte aValue) override;
    void Write(const Brx& aBuffer) override;
    void WriteFlush() override;
public:
    virtual void ReadCallback(Bwx& aBuf);
    TInt SeekCallback(TInt64 aOffset, TInt aWhence);
    TInt CloseCallback();
    TInt TellCallback();
private:
    TBool FindSync();
    TUint64 GetTotalSamples();
    void BigEndian(TInt16* aDst, TInt16* aSrc, TUint aSamples);
    void FlushOutput();
    TBool StreamInfoChanged(TUint aChannels, TUint aSampleRate) const;
    void OutputMetaData();
private:
    class Pimpl;
    std::unique_ptr<Pimpl> iPimpl;

    Bws<DecodedAudio::kMaxBytes> iInBuf;
    Bws<DecodedAudio::kMaxBytes> iOutBuf;
    Bws<2*kSearchChunkSize> iSeekBuf;   // can store 2 read chunks, to check for sync word across read boundaries

    TUint iSampleRate;
    TUint iBytesPerSec;
    TUint iBitrateAverage;
    TUint iChannels;
    TUint iBytesPerSample;
    TUint64 iSamplesTotal;
    TUint64 iTotalSamplesOutput;
    TUint64 iTrackLengthJiffies;
    TUint64 iTrackOffset;
    TInt iBitstream;
    Bws<kIcyMetadataBytes> iIcyMetadata;
    Bws<kIcyMetadataBytes> iNewIcyMetadata;

    TBool iStreamEnded;
    TBool iNewStreamStarted;
};

} //namespace Codec
} //namespace Media
} //namespace OpenHome

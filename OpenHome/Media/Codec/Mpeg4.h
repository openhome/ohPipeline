#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Media/Codec/Container.h>
#include <OpenHome/Media/Codec/CodecController.h>
#include <OpenHome/Private/Uri.h>

#include <memory>
#include <vector>

EXCEPTION(Mpeg4BoxUnrecognised);

EXCEPTION(MediaMpeg4FileInvalid);
EXCEPTION(MediaMpeg4OutOfRange);

namespace OpenHome {
namespace Media {
    class IMimeTypeList;
namespace Codec {


class IMpeg4InfoReadable
{
public:
    virtual const Brx& Codec() const = 0;
    virtual TUint SampleRate() const = 0;
    virtual TUint Timescale() const = 0;
    virtual TUint Channels() const = 0;
    virtual TUint BitDepth() const = 0;
    virtual TUint64 Duration() const = 0;
    virtual TUint StreamDescriptorBytes() const = 0;
    virtual ~IMpeg4InfoReadable() {}
};

class IMpeg4InfoWritable
{
public:
    static const TUint kCodecBytes = 4;
public:
    virtual void SetCodec(const Brx& aCodec) = 0;
    virtual void SetSampleRate(TUint aSampleRate) = 0;
    virtual void SetTimescale(TUint aTimescale) = 0;
    virtual void SetChannels(TUint aChannels) = 0;
    virtual void SetBitDepth(TUint aBitDepth) = 0;
    virtual void SetDuration(TUint64 aDuration) = 0;
    virtual void SetStreamDescriptorBytes(TUint aBytes) = 0;
    virtual ~IMpeg4InfoWritable() {}
};

class Mpeg4Info : public IMpeg4InfoReadable, public IMpeg4InfoWritable
{
public:
    Mpeg4Info();
    Mpeg4Info(const Brx& aCodec, TUint aSampleRate, TUint aTimescale, TUint aChannels, TUint aBitDepth, TUint64 aDuration, TUint aStreamDescriptorBytes);
    TBool Initialised() const;
    void Clear();
public: // from IMpeg4InfoReadable
    const Brx& Codec() const override;
    TUint SampleRate() const override;
    TUint Timescale() const override;
    TUint Channels() const override;
    TUint BitDepth() const override;
    TUint64 Duration() const override;
    TUint StreamDescriptorBytes() const override;
public: // from IMpeg4InfoWritable
    void SetCodec(const Brx& aCodec) override;
    void SetSampleRate(TUint aSampleRate) override;
    void SetTimescale(TUint aTimescale) override;
    void SetChannels(TUint aChannels) override;
    void SetBitDepth(TUint aBitDepth) override;
    void SetDuration(TUint64 aDuration) override;
    void SetStreamDescriptorBytes(TUint aBytes) override;
private:
    Bws<kCodecBytes> iCodec;    // FIXME - not really good enough for recognition; MP3 can also be contained in mp4a
    TUint iSampleRate;
    TUint iTimescale;
    TUint iChannels;
    TUint iBitDepth;
    TUint64 iDuration;
    TUint iStreamDescBytes;
};

class Mpeg4InfoReader : public INonCopyable
{
public:
    Mpeg4InfoReader(IReader& aReader);
    void Read(IMpeg4InfoWritable& aInfo);
private:
    IReader& iReader;
};

class Mpeg4InfoWriter : public INonCopyable
{
public:
    static const TUint kMaxBytes = Mpeg4Info::kCodecBytes +
                                   4 +    // sampler rate
                                   4 +    // timescale
                                   4 +    // channels
                                   4 +    // bitdepth
                                   8 +    // duration
                                   4;     // stream descriptor length
public:
    Mpeg4InfoWriter(const IMpeg4InfoReadable& aInfo);
    void Write(IWriter& aWriter) const;
private:
    const IMpeg4InfoReadable& iInfo;
};

/**
* Class responsible for parsing an entire Mpeg4Box.
* Must not report true for Complete() until entire box has been processed.
*/
class IMpeg4BoxProcessor
{
public:
    virtual Msg* Process() = 0; // May return nullptr upon completion.
    virtual TBool Complete() const = 0;
    virtual ~IMpeg4BoxProcessor() {}
};

class IMpeg4BoxRecognisable : public IMpeg4BoxProcessor
{
public:
    virtual void Reset() = 0;
    virtual TBool Recognise(const Brx& aBoxId) const = 0;
    virtual void Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes) = 0;
    virtual ~IMpeg4BoxRecognisable() {}
};

class IMpeg4BoxProcessorFactory
{
public:
    virtual IMpeg4BoxProcessor& GetMpeg4BoxProcessor(const Brx& aBoxId, TUint aBytes, IMsgAudioEncodedCache& aCache) = 0; // THROWS Mpeg4BoxUnrecognised
};

class Mpeg4BoxProcessorFactory : public IMpeg4BoxProcessorFactory
{
public:
    void Add(IMpeg4BoxRecognisable* aProcessor);
    void Reset();
public: // from IMpeg4BoxProcessorFactory
    IMpeg4BoxProcessor& GetMpeg4BoxProcessor(const Brx& aBoxId, TUint aBytes, IMsgAudioEncodedCache& aCache) override;
private:
    std::vector<std::unique_ptr<IMpeg4BoxRecognisable>> iProcessors;
};

class Mpeg4BoxHeaderReader
{
public:
    static const TUint kSizeBytes = 4;
    static const TUint kNameBytes = 4;
    static const TUint kHeaderBytes = kSizeBytes + kNameBytes;
public:
    void Reset(IMsgAudioEncodedCache& aCache);
    Msg* ReadHeader();  // May return nullptr upon completion.
    TUint Bytes() const;
    TUint PayloadBytes() const;
    const Brx& Id() const;
private:
    IMsgAudioEncodedCache* iCache;
    Bws<kHeaderBytes> iHeader;
    Brn iId;
    TUint iBytes;
    TBool iHeaderReadPending;
};

class IMpeg4MetadataNotifiable
{
public:
    virtual void MetadataRetrieved() = 0;
    virtual ~IMpeg4MetadataNotifiable() {}
};

class IMpeg4MetadataProvider
{
public:
    virtual void ResetProvider() = 0;
    virtual MsgAudioEncoded* GetMetadata() = 0;
    virtual TBool Complete() = 0;
    virtual ~IMpeg4MetadataProvider() {}
};

// FIXME - pass box offset in via Set() call instead?
class IBoxOffsetProvider
{
public:
    virtual TUint64 BoxOffset() const = 0;
    virtual ~IBoxOffsetProvider() {};
};

class Mpeg4BoxSwitcherRoot : public IBoxOffsetProvider
{
public:
    static const TChar* kNoTargetId;
public:
    Mpeg4BoxSwitcherRoot(IMpeg4BoxProcessorFactory& aProcessorFactory);
    void Reset();
    void Set(IMsgAudioEncodedCache& aCache, const TChar* aTargetId);
    Msg* Process();
private: // from IBoxOffsetProvider
    TUint64 BoxOffset() const;
private:
    enum EState
    {
        eNone,
        eHeader,
        eBox,
        eComplete,
    };
private:
    IMpeg4BoxProcessorFactory& iProcessorFactory;
    IMsgAudioEncodedCache* iCache;
    Brn iTargetId;
    Mpeg4BoxHeaderReader iHeaderReader;
    IMpeg4BoxProcessor* iProcessor;
    EState iState;
    TUint64 iOffset;
};

class Mpeg4BoxSwitcher : public IMpeg4BoxRecognisable
{
public:
    Mpeg4BoxSwitcher(IMpeg4BoxProcessorFactory& aProcessorFactory, const Brx& aBoxId);
public: // from IMpeg4BoxRecognisable
    Msg* Process() override;
    TBool Complete() const override;
    void Reset() override;
    TBool Recognise(const Brx& aBoxId) const override;
    void Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes) override;
private:
    enum EState
    {
        eNone,
        eHeader,
        eBox,
        eComplete,
    };
private:
    IMpeg4BoxProcessorFactory& iProcessorFactory;
    const Bws<Mpeg4BoxHeaderReader::kNameBytes> iId;
    IMsgAudioEncodedCache* iCache;
    IMpeg4BoxProcessor* iProcessor;
    Mpeg4BoxHeaderReader iHeaderReader;
    EState iState;
    TUint iBytes;
    TUint iOffset;
};

class Mpeg4BoxMoov : public Mpeg4BoxSwitcher
{
public:
    Mpeg4BoxMoov(IMpeg4BoxProcessorFactory& aProcessorFactory, IMpeg4MetadataNotifiable& aMetadataNotifiable);
public: // from Mpeg4BoxSwitcher
    Msg* Process() override;
    void Reset() override;
    TBool Recognise(const Brx& aBoxId) const override;
private:
    IMpeg4MetadataNotifiable& iMetadataNotifiable;
    TBool iMetadataRetrieved;
};

class Mpeg4ContainerInfo;

class Mpeg4BoxMoof : public Mpeg4BoxSwitcher
{
public:
    Mpeg4BoxMoof(IMpeg4BoxProcessorFactory& aProcessorFactory, Mpeg4ContainerInfo&);
public: // from Mpeg4BoxSwitcher
    void Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes) override;
    void Reset() override;
private:
    Mpeg4ContainerInfo& iContainerInfo;
};

class SeekTable;
class Mpeg4ProtectionDetails;

class Mpeg4BoxStts : public IMpeg4BoxRecognisable
{
private:
    static const TUint kVersion = 0;
public:
    Mpeg4BoxStts(SeekTable& aSeekTable);
public: // from IMpeg4BoxRecognisable
    Msg* Process() override;
    TBool Complete() const override;
    void Reset() override;
    TBool Recognise(const Brx& aBoxId) const override;
    void Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes) override;
private:
    enum EState
    {
        eNone,
        eVersion,
        eEntries,
        eSampleCount,
        eSampleDelta,
        eComplete,
    };
private:
    SeekTable& iSeekTable;
    IMsgAudioEncodedCache* iCache;
    EState iState;
    TUint iBytes;
    TUint iOffset;
    Bws<4> iBuf;
    TUint iEntries;
    TUint iEntryCount;
    TUint iSampleCount;
};

class Mpeg4BoxStsc : public IMpeg4BoxRecognisable
{
private:
    static const TUint kVersion = 0;
public:
    Mpeg4BoxStsc(SeekTable& aSeekTable);
public: // from IMpeg4BoxRecognisable
    Msg* Process() override;
    TBool Complete() const override;
    void Reset() override;
    TBool Recognise(const Brx& aBoxId) const override;
    void Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes) override;
private:
    enum EState
    {
        eNone,
        eVersion,
        eEntries,
        eFirstChunk,
        eSamplesPerChunk,
        eSampleDescriptionIndex,
        eComplete,
    };
private:
    SeekTable& iSeekTable;
    IMsgAudioEncodedCache* iCache;
    EState iState;
    TUint iBytes;
    TUint iOffset;
    TUint iEntries;
    TUint iEntryCount;
    TUint iFirstChunk;
    TUint iSamplesPerChunk;
    TUint iSampleDescriptionIndex;
    Bws<4> iBuf;
};

class Mpeg4BoxStco : public IMpeg4BoxRecognisable
{
private:
    static const TUint kVersion = 0;
public:
    Mpeg4BoxStco(SeekTable& aSeekTable);
public: // from IMpeg4BoxRecognisable
    Msg* Process() override;
    TBool Complete() const override;
    void Reset() override;
    TBool Recognise(const Brx& aBoxId) const override;
    void Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes) override;
private:
    enum EState
    {
        eNone,
        eVersion,
        eEntries,
        eChunkOffset,
        eComplete,
    };
private:
    SeekTable& iSeekTable;
    IMsgAudioEncodedCache* iCache;
    EState iState;
    TUint iBytes;
    TUint iOffset;
    Bws<4> iBuf;
    TUint iEntries;
    TUint iEntryCount;
};

class Mpeg4BoxCo64 : public IMpeg4BoxRecognisable
{
private:
    static const TUint kVersion = 0;
public:
    Mpeg4BoxCo64(SeekTable& aSeekTable);
public: // from IMpeg4BoxRecognisable
    Msg* Process() override;
    TBool Complete() const override;
    void Reset() override;
    TBool Recognise(const Brx& aBoxId) const override;
    void Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes) override;
private:
    enum EState
    {
        eNone,
        eVersion,
        eEntries,
        eChunkOffset,
        eComplete,
    };
private:
    SeekTable& iSeekTable;
    IMsgAudioEncodedCache* iCache;
    EState iState;
    TUint iBytes;
    TUint iOffset;
    Bws<4> iBuf32;
    Bws<8> iBuf64;
    TUint iEntries;
    TUint iEntryCount;
};

class SampleSizeTable;

class Mpeg4BoxStsz : public IMpeg4BoxRecognisable
{
private:
    static const TUint kVersion = 0;
public:
    Mpeg4BoxStsz(SampleSizeTable& aSampleSizeTable);
public: // from IMpeg4BoxRecognisable
    Msg* Process() override;
    TBool Complete() const override;
    void Reset() override;
    TBool Recognise(const Brx& aBoxId) const override;
    void Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes) override;
private:
    enum EState
    {
        eNone,
        eVersion,
        eSampleSize,
        eEntryCount,
        eEntry,
        eComplete,
    };
private:
    SampleSizeTable& iSampleSizeTable;
    IMsgAudioEncodedCache* iCache;
    EState iState;
    TUint iBytes;
    TUint iOffset;
    Bws<4> iBuf;
    TUint iSampleSize;
};

class Mpeg4BoxTfhd : public IMpeg4BoxRecognisable
{
private:
    static const TUint kFlagBaseDataOffsetPresent;
    static const TUint kFlagSampleDescriptionIndexPresent;
    static const TUint kFlagDefaultSampleDurationPresent;
    static const TUint kFlagDefaultSampleSizePresent;
    static const TUint kFlagDefaultSampleFlagsPresent;

public:
    Mpeg4BoxTfhd(SampleSizeTable&, Mpeg4ContainerInfo&);
public: // from IMpeg4BoxRecognisable
    Msg* Process() override;
    TBool Complete() const override;
    void Reset() override;
    TBool Recognise(const Brx& aBoxId) const override;
    void Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes) override;
private:
    enum EState
    {
        eNone,
        eFlags,
        eBaseDataOffset,
        eDefaultSampleSize,
        eComplete,
    };
private:
    SampleSizeTable& iSampleSizeTable;
    Mpeg4ContainerInfo& iContainerInfo;
    IMsgAudioEncodedCache* iCache;
    EState iState;
    TUint iBytes;
    TUint iOffset;
    TByte iFlags;
    Bws<4> iBuf;
    Bws<8> iBuf64;
};

class Mpeg4BoxTrun : public IMpeg4BoxRecognisable
{
private:
    static const TUint kVersion = 0;

    static const TUint kFlagDataOffsetPresent;
    static const TUint kFlagFirstSampleFlagsPresent;
    static const TUint kFlagSampleDurationPresent;
    static const TUint kFlagSampleSizePresent;
public:
    Mpeg4BoxTrun(SampleSizeTable&, Mpeg4ContainerInfo&);
public: // from IMpeg4BoxRecognisable
    Msg* Process() override;
    TBool Complete() const override;
    void Reset() override;
    TBool Recognise(const Brx& aBoxId) const override;
    void Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes) override;
private:
    enum EState
    {
        eNone,
        eVersionAndFlags,
        eSampleCount,
        eDataOffset,
        eEntries,
        eComplete,
    };
private:
    SampleSizeTable& iSampleSizeTable;
    Mpeg4ContainerInfo& iContainerInfo;
    IMsgAudioEncodedCache* iCache;
    EState iState;
    TUint iBytes;
    TUint iOffset;
    TByte iFlags;
    TUint iEntryBytes;
    TUint iSampleCount;
    TUint iSamplesRead;
    Bws<4> iBuf;
    Bws<16> iEntryBuf;
};

class IMpeg4DurationSettable
{
public:
    virtual void SetTimescale(TUint aTimescale) = 0;
    virtual void SetDuration(TUint64 aDuration) = 0;
    virtual ~IMpeg4DurationSettable() {}
};

class Mpeg4BoxMdhd : public IMpeg4BoxRecognisable
{
private:
    static const TUint kVersion32 = 0;
    static const TUint kVersion64 = 1;
public:
    Mpeg4BoxMdhd(IMpeg4DurationSettable& aDurationSettable);
public: // from IMpeg4BoxRecognisable
    Msg* Process() override;
    TBool Complete() const override;
    void Reset() override;
    TBool Recognise(const Brx& aBoxId) const override;
    void Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes) override;
private:
    enum EState
    {
        eNone,
        eVersion,
        eTimescale,
        eDuration,
        eComplete,
    };
private:
    IMpeg4DurationSettable& iDurationSettable;
    IMsgAudioEncodedCache* iCache;
    EState iState;
    TUint iBytes;
    TUint iOffset;
    Bws<4> iBuf32;
    Bws<8> iBuf64;
    TUint iVersion;
};

class Mpeg4BoxTkhd : public IMpeg4BoxRecognisable
{
private:
    static const TUint kVersion32 = 0;
    static const TUint kVersion64 = 1;
public:
    Mpeg4BoxTkhd(IMpeg4DurationSettable& aDurationSettable);
public: // from IMpeg4BoxRecognisable
    Msg* Process() override;
    TBool Complete() const override;
    void Reset() override;
    TBool Recognise(const Brx& aBoxId) const override;
    void Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes) override;
private:
    enum EState
    {
        eNone,
        eFlagsAndVersion,
        eDuration,
        eComplete,
    };
private:
    IMpeg4DurationSettable& iDurationSettable;
    IMsgAudioEncodedCache* iCache;
    EState iState;
    TUint iBytes;
    TUint iOffset;
    Bws<4> iBuf32;
    Bws<8> iBuf64;
    TUint iVersion;
};

class Mpeg4BoxMehd : public IMpeg4BoxRecognisable
{
private:
    static const TUint kVersion32 = 0;
    static const TUint kVersion64 = 1;
public:
    Mpeg4BoxMehd(IMpeg4DurationSettable& aDurationSettable);
public: // from IMpeg4BoxRecognisable
    Msg* Process() override;
    TBool Complete() const override;
    void Reset() override;
    TBool Recognise(const Brx& aBoxId) const override;
    void Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes) override;
private:
    enum EState
    {
        eNone,
        eVersion,
        eFragmentDuration,
        eComplete,
    };
private:
    IMpeg4DurationSettable& iDurationSettable;
    IMsgAudioEncodedCache* iCache;
    EState iState;
    TUint iBytes;
    TUint iOffset;
    Bws<4> iBuf;
    TUint iVersion;
};

class IStreamInfoSettable
{
public:
    static const TUint kCodecBytes = 4;
public:
    virtual void SetChannels(TUint aChannels) = 0;
    virtual void SetBitDepth(TUint aBitDepth) = 0;
    virtual void SetSampleRate(TUint aSampleRate) = 0;
    virtual void SetCodec(const Brx& aCodec) = 0;
    virtual ~IStreamInfoSettable() {}
};

class ICodecInfoSettable
{
public:
    virtual void SetCodecInfo(MsgAudioEncoded* aMsg) = 0;
    virtual ~ICodecInfoSettable() {}
};

class Mpeg4BoxCodecBase : public IMpeg4BoxRecognisable
{
protected:
    Mpeg4BoxCodecBase(const Brx& aCodecId, IStreamInfoSettable& aStreamInfoSettable);
public: // from IMpeg4BoxRecognisable
    Msg* Process() override;
    TBool Complete() const override;
    void Reset() override;
    TBool Recognise(const Brx& aBoxId) const override;
    void Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes) override;
private:
    enum EState
    {
        eNone,
        eChannels,
        eBitDepth,
        eSampleRate,
        eHeader,
        eBox,
        eComplete,
    };
protected:
    Mpeg4BoxProcessorFactory iProcessorFactory;
private:
    const Bws<IStreamInfoSettable::kCodecBytes> iId;
    IStreamInfoSettable& iStreamInfoSettable;
    IMsgAudioEncodedCache* iCache;
    IMpeg4BoxProcessor* iProcessor;
    Mpeg4BoxHeaderReader iHeaderReader;
    EState iState;
    TUint iBytes;
    TUint iOffset;
    Bws<2> iBuf;
};

class Mpeg4BoxCodecMp4a : public Mpeg4BoxCodecBase
{
public:
    Mpeg4BoxCodecMp4a(IStreamInfoSettable& aStreamInfoSettable, ICodecInfoSettable& aCodecInfoSettable);
};

class Mpeg4BoxCodecAlac : public Mpeg4BoxCodecBase
{
public:
    Mpeg4BoxCodecAlac(IStreamInfoSettable& aStreamInfoSettable, ICodecInfoSettable& aCodecInfoSettable);
};

class Mpeg4BoxCodecFlac : public Mpeg4BoxCodecBase
{
public:
    Mpeg4BoxCodecFlac(IStreamInfoSettable& aStreamInfoSettable, ICodecInfoSettable& aCodecInfoSettable);
};

class Mpeg4BoxCodecMp4aProtected : public Mpeg4BoxCodecBase
{
public:
    Mpeg4BoxCodecMp4aProtected(IStreamInfoSettable& aStreamInfoSettable, Mpeg4ProtectionDetails& aProtectionDetails);
};

class Mpeg4BoxSchm : public IMpeg4BoxRecognisable
{
public:
    Mpeg4BoxSchm();
public: // from IMpeg4BoxRecognisable
    Msg* Process() override;
    TBool Complete() const override;
    void Reset() override;
    TBool Recognise(const Brx& aBoxId) const override;
    void Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes) override;
private:
    enum EState
    {
        eNone,
        eFlags,
        eSchemeType,
        eComplete,
    };
private:
    IMsgAudioEncodedCache* iCache;
    EState iState;
    TUint iBytes;
    TUint iOffset;
    Bws<4> iBuf;
};


class Mpeg4BoxTenc : public IMpeg4BoxRecognisable
{
public:
    Mpeg4BoxTenc(Mpeg4ProtectionDetails& aProtectionDetails);
public: // from IMpeg4BoxRecognisable
    Msg* Process() override;
    TBool Complete() const override;
    void Reset() override;
    TBool Recognise(const Brx& aBoxId) const override;
    void Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes) override;
private:
    enum EState
    {
        eNone,
        eFlagsAndVersion,
        eIsProtected,
        eDefaultPerSampleIVSize,
        eDefaultKID,
        eComplete,
    };
private:
    Mpeg4ProtectionDetails& iProtectionDetails;
    IMsgAudioEncodedCache* iCache;
    EState iState;
    TUint iBytes;
    TUint iOffset;
    Bws<4> iBuf;
    Bws<1> iBuf8;
    Bws<16> iKIDBuf;
};

class Mpeg4BoxSenc : public IMpeg4BoxRecognisable
{
public:
    Mpeg4BoxSenc(Mpeg4ProtectionDetails& aProtectionDetails);
public: // from IMpeg4BoxRecognisable
    Msg* Process() override;
    TBool Complete() const override;
    void Reset() override;
    TBool Recognise(const Brx& aBoxId) const override;
    void Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes) override;
private:
    enum EState
    {
        eNone,
        eFlagsAndVersion,
        eSampleCount,
        eSampleIV,
        eComplete,
    };
private:
    Mpeg4ProtectionDetails& iProtectionDetails;
    IMsgAudioEncodedCache* iCache;
    EState iState;
    TUint iBytes;
    TUint iOffset;
    TUint iSampleCount;
    Bws<4> iBuf;
    Bws<8> iBuf64;
};

class Mpeg4BoxEsds : public IMpeg4BoxRecognisable
{
private:
    static const TUint kVersion = 0;
public:
    Mpeg4BoxEsds(ICodecInfoSettable& aCodecInfoSettable);
public: // from IMpeg4BoxRecognisable
    Msg* Process() override;
    TBool Complete() const override;
    void Reset() override;
    TBool Recognise(const Brx& aBoxId) const override;
    void Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes) override;
private:
    enum EState
    {
        eNone,
        eVersion,
        eCodecInfo,
        eComplete,
    };
private:
    ICodecInfoSettable& iCodecInfoSettable;
    IMsgAudioEncodedCache* iCache;
    MsgAudioEncodedRecogniser iAudioEncodedRecogniser;
    EState iState;
    TUint iBytes;
    TUint iOffset;
    Bws<1> iBuf;
};

class Mpeg4BoxAlac : public IMpeg4BoxRecognisable
{
private:
    static const TUint kVersion = 0;
public:
    Mpeg4BoxAlac(ICodecInfoSettable& aCodecInfoSettable);
public: // from IMpeg4BoxRecognisable
    Msg* Process() override;
    TBool Complete() const override;
    void Reset() override;
    TBool Recognise(const Brx& aBoxId) const override;
    void Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes) override;
private:
    enum EState
    {
        eNone,
        eCodecInfo,
        eComplete,
    };
private:
    ICodecInfoSettable& iCodecInfoSettable;
    IMsgAudioEncodedCache* iCache;
    MsgAudioEncodedRecogniser iAudioEncodedRecogniser;
    EState iState;
    TUint iBytes;
    TUint iOffset;
    Bws<1> iBuf;
};


class Mpeg4BoxDfla : public IMpeg4BoxRecognisable
{
private:
    static const TUint kVersion = 0;
public:
    Mpeg4BoxDfla(ICodecInfoSettable& aCodecInfoSettable);
public: // from IMpeg4BoxRecognisable
    Msg* Process() override;
    TBool Complete() const override;
    void Reset() override;
    TBool Recognise(const Brx& aBoxId) const override;
    void Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes) override;
private:
    enum EState
    {
        eNone,
        eVersion,
        eCodecInfo,
        eComplete,
    };
private:
    ICodecInfoSettable& iCodecInfoSettable;
    IMsgAudioEncodedCache* iCache;
    MsgAudioEncodedRecogniser iAudioEncodedRecogniser;
    EState iState;
    TUint iBytes;
    TUint iOffset;
    Bws<4> iBuf;
};

class Mpeg4BoxStsd : public IMpeg4BoxRecognisable
{
private:
    static const TUint kVersion = 0;
public:
    Mpeg4BoxStsd(IStreamInfoSettable& aStreamInfoSettable,
                 ICodecInfoSettable& aCodecInfoSettable,
                 Mpeg4ProtectionDetails& aProtectionDetails);
public: // from IMpeg4BoxRecognisable
    Msg* Process() override;
    TBool Complete() const override;
    void Reset() override;
    TBool Recognise(const Brx& aBoxId) const override;
    void Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes) override;
private:
    enum EState
    {
        eNone,
        eVersion,
        eEntries,
        eHeader,
        eBox,
        eComplete,
    };
private:
    Mpeg4BoxProcessorFactory iProcessorFactory;
    IMsgAudioEncodedCache* iCache;
    IMpeg4BoxProcessor* iProcessor;
    Mpeg4BoxHeaderReader iHeaderReader;
    EState iState;
    TUint iBytes;
    TUint iOffset;
    Bws<4> iBuf;
};

class Mpeg4Duration : public IMpeg4DurationSettable
{
public:
    Mpeg4Duration();
    void Reset();
    TUint Timescale() const;
    TUint64 Duration() const;
public: // from IMpeg4DurationSettable
    void SetTimescale(TUint aTimescale) override;
    void SetDuration(TUint64 aDuration) override;
private:
    TUint iTimescale;
    TUint64 iDuration;
};

class Mpeg4StreamInfo : public IStreamInfoSettable
{
public:
    Mpeg4StreamInfo();
    void Reset();
    TUint Channels() const;
    TUint BitDepth() const;
    TUint SampleRate() const;
    const Brx& Codec() const;
public: // from IStreamInfoSettable
    void SetChannels(TUint aChannels) override;
    void SetBitDepth(TUint aBitDepth) override;
    void SetSampleRate(TUint aSampleRate) override;
    void SetCodec(const Brx& aCodec) override;
private:
    TUint iChannels;
    TUint iBitDepth;
    TUint iSampleRate;
    Bws<IStreamInfoSettable::kCodecBytes> iCodec;
};

class Mpeg4CodecInfo : public ICodecInfoSettable
{
public:
    Mpeg4CodecInfo();
    ~Mpeg4CodecInfo();
    void Reset();
    MsgAudioEncoded* CodecInfo();
public: // from ICodecInfoSettable
    void SetCodecInfo(MsgAudioEncoded* aMsg) override;
private:
    MsgAudioEncoded* iAudioEncoded;
};

class IMpeg4MetadataChecker
{
public:
    virtual TBool MetadataAvailable() const = 0;
    virtual ~IMpeg4MetadataChecker() {}
};

class IMpeg4ChunkSeekObserver
{
public:
    virtual void ChunkSeek(TUint aChunk) = 0;
    virtual ~IMpeg4ChunkSeekObserver() {}
};

class IMpeg4ChunkSeekObservable
{
public:
    virtual void RegisterChunkSeekObserver(IMpeg4ChunkSeekObserver& aChunkSeekObserver) = 0;
    virtual ~IMpeg4ChunkSeekObservable() {}
};

class Mpeg4OutOfBandReader;

class IMpegDRMProvider
{
public:
    virtual ~IMpegDRMProvider() {};
    virtual TBool Decrypt(const Brx& aKID,
                          const Brx& aEncryptedData,
                          const Brx& aIV,
                          Bwx& aDecryptBuffer) = 0;
};

class Mpeg4BoxMdat : public IMpeg4BoxRecognisable, public IMpeg4ChunkSeekObserver, private INonCopyable
{
public:
    Mpeg4BoxMdat(Optional<IMpegDRMProvider> iDRMProvider,
                 MsgFactory& aMsgFactory,
                 Mpeg4BoxSwitcherRoot& aBoxSwitcher,
                 IMpeg4MetadataChecker& aMetadataChecker,
                 IMpeg4MetadataProvider& aMetadataProvider,
                 IMpeg4ChunkSeekObservable& aChunkSeeker,
                 IBoxOffsetProvider& aOffsetProvider,
                 SeekTable& aSeekTable,
                 SampleSizeTable& aSampleSizeTable,
                 Mpeg4ProtectionDetails& aProtectionDetails,
                 Mpeg4ContainerInfo& aContainerInfo,
                 Mpeg4OutOfBandReader& aOutOfBandReader);
    ~Mpeg4BoxMdat();
public: // from IMpeg4BoxRecognisable
    Msg* Process() override;
    TBool Complete() const override;
    void Reset() override;
    TBool Recognise(const Brx& aBoxId) const override;
    void Set(IMsgAudioEncodedCache& aCache, TUint aBoxBytes) override;
private: // from IMpeg4ChunkSeekObserver
    void ChunkSeek(TUint aChunk) override;
private:
    TUint BytesUntilChunk() const;
    TUint ChunkBytes() const;
    TUint BytesToRead() const;
private:
    enum EState
    {
        eNone,
        eRetrieveMetadata,
        eTransmitMetadata,
        eChunkReadSetup,
        eChunk,
        eProtectedChunk,
        eComplete,
    };
private:
    Optional<IMpegDRMProvider> iDRMProvider;
    MsgFactory& iMsgFactory;
    Mpeg4BoxSwitcherRoot& iBoxSwitcher;
    IMpeg4MetadataChecker& iMetadataChecker;
    IMpeg4MetadataProvider& iMetadataProvider;
    IBoxOffsetProvider& iOffsetProvider;
    SeekTable& iSeekTable;
    SampleSizeTable& iSampleSizeTable;
    Mpeg4ProtectionDetails& iProtectionDetails;
    Mpeg4ContainerInfo& iContainerInfo;
    Mpeg4OutOfBandReader& iOutOfBandReader;
    IMsgAudioEncodedCache* iCache;
    MsgAudioEncodedRecogniser iAudioEncodedRecogniser;
    EState iState;
    TUint iChunk;
    TUint iSeekChunk;
    TBool iSeek;
    TUint iChunkBytesRemaining;
    TUint iBytes;
    TUint64 iOffset;
    TUint64 iBoxStartOffset;
    TUint64 iFileReadOffset;
    Mutex iLock;

    // Protection Support
    TUint iSampleIndex;
    Bwh* iDecryptionBuf;
    Bwh* iSampleBuf;
    MsgAudioEncoded* iChunkMsg;
};

class SampleSizeTable
{
public:
    SampleSizeTable();
    ~SampleSizeTable();
    void Init(TUint aMaxEntries);
    void Clear();
    void AddSampleSize(TUint aSampleSize);
    TUint SampleSize(TUint aIndex) const;
    TUint DefaultSampleSize() const;
    void SetDefaultSampleSize(TUint aDefaultSampleSize);
    TUint Count() const;
    void WriteInit();
    void Write(IWriter& aWriter, TUint aMaxBytes);
    TBool WriteComplete() const;
private:
    std::vector<TUint> iTable;
    TUint iWriteIndex;
    TUint iDefaultSampleSize;
};

// FIXME - should probably also include stss here.
// If stss box is present, it means that not all samples are sync samples, and the stss box is consulted to find the first sync sample prior to specified time.
// If stss not present, all samples are sync samples.
class SeekTable
{
public:
    SeekTable();
    ~SeekTable();
    // FIXME - rename the below to Init() and Clear(), respectively.
    void InitialiseSamplesPerChunk(TUint aEntries);
    void InitialiseAudioSamplesPerSample(TUint aEntries);
    void InitialiseOffsets(TUint aEntries);
    TBool Initialised() const;
    void Deinitialise();
    void SetSamplesPerChunk(TUint aFirstChunk, TUint aSamplesPerChunk, TUint aSampleDescriptionIndex);
    void SetAudioSamplesPerSample(TUint32 aSampleCount, TUint32 aAudioSamples);
    void SetOffset(TUint64 aOffset);    // FIXME - rename to AddOffset()? and similar with above methods?
    TUint ChunkCount() const;
    TUint AudioSamplesPerSample() const;
    TUint SamplesPerChunk(TUint aChunkIndex) const;
    TUint StartSample(TUint aChunkIndex) const;
    TUint64 Offset(TUint64& aAudioSample, TUint64& aSample);    // FIXME - aSample should be TUint.
    // FIXME - See if it's possible to split this class into its 3 separate components, to simplify it.
    TUint64 GetOffset(TUint aChunkIndex) const;
    void WriteInit();
    void Write(IWriter& aWriter, TUint aMaxBytes);   // Serialise.
    TBool WriteComplete() const;
private:
    // Find the codec sample that contains the given audio sample.
    TUint64 CodecSample(TUint64 aAudioSample) const;
    TUint SamplesPerChunkTotal(TUint aIndex) const;
    TUint ChunkWithinSamplesPerChunk(TUint aIndex, TUint aSampleOffset) const;
    // Find the chunk that contains the desired codec sample.
    TUint Chunk(TUint64 aCodecSample) const;
    TUint CodecSampleFromChunk(TUint aChunk) const;
    TUint AudioSampleFromCodecSample(TUint aCodecSample) const;
private:
    typedef struct {
        TUint   iFirstChunk;
        TUint   iSamples;
        TUint   iSampleDescriptionIndex;
    } TSamplesPerChunkEntry;
    typedef struct {
        TUint   iSampleCount;
        TUint   iAudioSamples;
    } TAudioSamplesPerSampleEntry;
private:
    std::vector<TSamplesPerChunkEntry> iSamplesPerChunk;
    std::vector<TAudioSamplesPerSampleEntry> iAudioSamplesPerSample;
    std::vector<TUint64> iOffsets;
    TUint iSpcWriteIndex;
    TUint iAspsWriteIndex;
    TUint iOffsetsWriteIndex;
};

class SeekTableInitialiser : public INonCopyable
{
public:
    SeekTableInitialiser(SeekTable& aSeekTable, IReader& aReader);
    void Init();
private:
    SeekTable& iSeekTable;
    IReader& iReader;
    TBool iInitialised;
};

class MsgAudioEncodedWriter : public IWriter
{
public:
    MsgAudioEncodedWriter(MsgFactory& aMsgFactory);
    ~MsgAudioEncodedWriter();
    MsgAudioEncoded* Msg();
public: // from IWriter
    void Write(TByte aValue) override;
    void Write(const Brx& aBuffer) override;
    void WriteFlush() override; // Flush any buffered data to a MsgAudioEncoded.
private:
    void AllocateMsg();
private:
    MsgFactory& iMsgFactory;
    MsgAudioEncoded* iMsg;
    Bws<EncodedAudio::kMaxBytes> iBuf;
};

class Mpeg4OutOfBandReader : public IMsgAudioEncodedCache
{
private:
    static const TUint kReadBytes = 1024;
    static const TUint kMaxAccumulateBytes = 1024;
public:
    Mpeg4OutOfBandReader(MsgFactory& aMsgFactory, IContainerUrlBlockWriter& aBlockWriter);
    void Reset(TUint64 aStreamBytes);
    void SetReadOffset(TUint64 aStartOffset);
public: // from IMsgAudioEncodedCache
    void Discard(TUint aBytes) override;
    void Inspect(Bwx& aBuf, TUint aBytes) override;
    void Accumulate(TUint aBytes) override;
    Msg* Pull() override;
private:
    TBool PopulateBuffer(Bwx& aBuf, TUint aBytes);
private:
    MsgFactory& iMsgFactory;
    IContainerUrlBlockWriter& iBlockWriter;
    TUint64 iOffset;
    TUint64 iStreamBytes;
    TUint iDiscardBytes;
    TUint iInspectBytes;
    TUint iAccumulateBytes;
    Bwx* iInspectBuffer;
    Bws<kReadBytes> iReadBuffer;
    Bws<kMaxAccumulateBytes> iAccumulateBuffer;
};

class Mpeg4MetadataChecker : public IMpeg4MetadataChecker, public IMpeg4MetadataNotifiable
{
public:
    Mpeg4MetadataChecker();
    void Reset();
public: // from IMpeg4MetadataChecker
    TBool MetadataAvailable() const override;
public: // from IMpeg4MetadataNotifiable
    void MetadataRetrieved() override;
private:
    TBool iMetadataAvailable;
};

class Mpeg4ProtectionDetails
{
public:
    static const TUint kInitialSampleIVBufferSize;
    static const TUint kSampleIVBufferGrowthSize;

public:
    Mpeg4ProtectionDetails();

public:
    TBool IsProtected() const;
    const Brx& KID() const;
    TUint PerSampleIVSizeBytes() const;
    const Brx& GetSampleIV(TUint aSampleIndex);
    TBool HasPerSampleIVs() const;

    void SetProtected();
    void SetPerSampleIVSize(TUint aPerSampleIVSize);
    void SetKID(const Brx& aKID);
    void AddSampleIV(const Brx& aIV);

    void Reset();
    void ClearSampleIVs();

private:
    void AlignIV16(Bwx& aBuffer, const Brx& aIV);

private:
    TBool iIsProtected;
    TUint iPerSampleIVSize;
    Bws<128> iKID;
    Bws<16> iIVBuffer;
    WriterBwh iSampleIVs;
};

class Mpeg4ContainerInfo
{
public:
    enum class EProcessingMode
    {
        Complete,   // 'moov' based stream
        Fragmented  // 'moof' based stream
    };

public:
    Mpeg4ContainerInfo();

public:
    TBool CanProcess(TUint64 aFileOffset) const;
    EProcessingMode ProcessingMode() const;

    void SetFragmented(TUint aMoofBoxSize);
    void SetBaseDataOffset(TUint64 aBaseDataOffset);
    void SetDefaultBaseIsMoof();
    void SetDataOffset(TUint64 aDataOffset);

    void Reset();
private:
    EProcessingMode iProcessingMode;
    TUint iMoofBoxSize;
    TUint64 iBaseDataOffset;
    TUint64 iDataOffset;
    TBool iDefaultBaseIsMoof;
};

class Mpeg4Container : public ContainerBase, public IMpeg4MetadataProvider, public IMpeg4ChunkSeekObservable
{
public:
    Mpeg4Container(IMimeTypeList& aMimeTypeList, Optional<IMpegDRMProvider> aDRMProvider);
    ~Mpeg4Container();
public: // from ContainerBase
    void Construct(IMsgAudioEncodedCache& aCache, MsgFactory& aMsgFactory, IContainerSeekHandler& aSeekHandler, IContainerUrlBlockWriter& aUrlBlockWriter, IContainerStopper& aContainerStopper) override;
    Msg* Recognise() override;
    TBool Recognised() const override;
    void Reset() override;
    void Init(TUint64 aStreamBytes) override;
    TBool TrySeek(TUint aStreamId, TUint64 aOffset) override;
    Msg* Pull() override;
private: // from IMpeg4MetadataProvider
    void ResetProvider() override;
    MsgAudioEncoded* GetMetadata() override;
    TBool Complete() override;
private: // from IMpeg4ChunkSeekObservable
    void RegisterChunkSeekObserver(IMpeg4ChunkSeekObserver& aChunkSeekObserver) override;
private:
    Optional<IMpegDRMProvider> iDRMProvider;
    Mpeg4BoxProcessorFactory iProcessorFactory;
    Mpeg4BoxSwitcherRoot iBoxRoot;  // Pull directly from this. All other processors should reside inside a factory that lives within this.
    Mpeg4BoxSwitcherRoot iBoxRootOutOfBand;
    Mpeg4MetadataChecker iMetadataChecker;
    Mpeg4Duration iDurationInfo;
    Mpeg4StreamInfo iStreamInfo;
    Mpeg4CodecInfo iCodecInfo;
    SampleSizeTable iSampleSizeTable;
    SeekTable iSeekTable;
    Mpeg4ProtectionDetails iProtectionDetails;
    Mpeg4ContainerInfo iContainerInfo;
    Mpeg4OutOfBandReader* iOutOfBandReader;
    IMpeg4ChunkSeekObserver* iSeekObserver;
    Bws<4> iRecogBuf;
    TBool iRecognitionStarted;
    TBool iRecognitionSuccess;
    mutable Mutex iLock;
private:
    enum EMdataState
    {
        eMdataNone,
        eMdataSizeTab,
        eMdataSeekTab,
        eMdataComplete
    };
    EMdataState iMdataState;
};

} // namespace Codec
} // namespace Media
} // namespace OpenHome


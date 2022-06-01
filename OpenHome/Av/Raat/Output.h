#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Media/Pipeline/Msg.h>

#include <vector>

#include <raat_plugin_output.h>
#include <rc_status.h>
#include <rc_allocator.h>
#include <jansson.h>
#include <raat_stream.h>

EXCEPTION(RaatUriError);
EXCEPTION(RaatPacketError)

namespace OpenHome {
    class Env;
namespace Media {
    class PipelineManager;
}
namespace Av {

class IRaatWriter
{
public:
    virtual ~IRaatWriter() {}
    virtual void WriteMetadata(const Brx& aMetadata) = 0;
    virtual void WriteDelay(TUint aJiffies) = 0;
    virtual void WriteData(const Brx& aData) = 0;
};

class IRaatReader
{
public:
    virtual ~IRaatReader() {}
    virtual void NotifyReady() = 0;
    virtual void Read(IRaatWriter& aWriter) = 0;
    virtual void Interrupt() = 0;
    virtual void Reset() = 0;
};

class ISourceRaat;
class RaatOutput;

typedef struct {
    RAAT__OutputPlugin iPlugin; // must be first member
    RaatOutput* iSelf;
} RaatOutputPluginExt;

class IRaatTime;

class RaatOutput : public IRaatReader
{
    static const TUint kPendingPacketsMax;
public:
    RaatOutput(
        Environment& aEnv,
        Media::PipelineManager& aPipeline,
        ISourceRaat& aSourceRaat,
        IRaatTime& aRaatTime);
    ~RaatOutput();
    RAAT__OutputPlugin* Plugin();
    void GetInfo(json_t** aInfo);
    void GetSupportedFormats(RC__Allocator* aAlloc, size_t* aNumFormats, RAAT__StreamFormat** aFormats);
    void SetupStream(
        RAAT__StreamFormat* aFormat,
        RAAT__OutputSetupCallback aCbSetup, void* aCbSetupData,
        RAAT__OutputLostCallback aCbLost, void* aCbLostData);
    RC__Status TeardownStream(int aToken);
    RC__Status StartStream(int aToken, int64_t aWallTime, int64_t aStreamTime, RAAT__Stream* aStream);
    RC__Status GetLocalTime(int aToken, int64_t* aTime);
    RC__Status SetRemoteTime(int aToken, int64_t aClockOffset, bool aNewSource);
    RC__Status TryStop(int aToken);
    RC__Status Stop();
    RC__Status AddListener(RAAT__OutputMessageCallback aCb, void* aCbUserdata);
    void RemoveListener(RAAT__OutputMessageCallback aCb, void* aCbUserdata);
private:
    TUint64 GetLocalTime() const;
    static void AddFormatPcm(RAAT__StreamFormat* aFormat, TUint aSampleRate, TUint aBitDepth);
    static void AddFormatDsd(RAAT__StreamFormat* aFormat, TUint aSampleRate);
    void OutputSignalPath();
private: // from IRaatReader
    void NotifyReady() override;
    void Read(IRaatWriter& aWriter) override;
    void Interrupt() override;
    void Reset() override;
private:
    class SetupCb
    {
    public:
        static const int kTokenInvalid;
    public:
        SetupCb();
        void Set(
            RAAT__OutputSetupCallback aCbSetup, void* aCbSetupData,
            RAAT__OutputLostCallback aCbLost, void* aCbLostData);
        TUint NotifyReady();
        void NotifyFailed();
    private:
        void Reset();
    private:
        int iNextToken;
        RAAT__OutputSetupCallback iCbSetup;
        void* iCbSetupData;
        RAAT__OutputLostCallback iCbLost;
        void* iCbLostData;
    };
private:
    RaatOutputPluginExt iPluginExt;
    RAAT__OutputMessageListeners iListeners;
    Environment& iEnv;
    Media::PipelineManager& iPipeline;
    ISourceRaat& iSourceRaat;
    IRaatTime& iRaatTime;
    Mutex iLockStream;
    RAAT__Stream* iStream;
    Semaphore iSemStarted;
    SetupCb iSetupCb;
    int iToken;
    int64_t iStreamPos;
    TUint iSampleRate;
    TUint iBytesPerSample;
    TUint iSamplesPerRead;
    TUint iPendingDelay;
    TBool iStarted;
    TBool iRunning;
    TByte iAudioData[Media::AudioData::kMaxBytes];
    std::vector<RAAT__AudioPacket> iPendingPackets;
};

class RaatUri
{
    static const Brn kKeyFormat;
    static const Brn kKeySampleRate;
    static const Brn kKeyBitDepth;
    static const Brn kKeyNumChannels;
    static const Brn kKeySampleStart;
public:
    static const Brn kScheme;
    static const Brn kFormatPcm;
    static const Brn kFormatDsd;
public:
    static void Create(
        Media::AudioFormat aFormat,
        TUint aSampleRate,
        TUint aBitDepth,
        TUint aNumChannels,
        TUint64 aSampleStart,
        Bwx& aUri);
public:
    RaatUri();
    void Parse(const Brx& aUri);
    const Brx& AbsoluteUri() const;
    Media::AudioFormat Format() const;
    TUint SampleRate() const;
    TUint BitDepth() const;
    TUint NumChannels() const;
    TUint SampleStart() const;
private:
    void Reset();
    static Brn Val(const std::map<Brn, Brn, BufferCmp>& aKvps, const Brx& aKey);
    static void SetValUint(const std::map<Brn, Brn, BufferCmp>& aKvps, const Brx& aKey, TUint& aVal);
    static void SetValUint64(const std::map<Brn, Brn, BufferCmp>& aKvps, const Brx& aKey, TUint64& aVal);
private:
    Uri iUri;
    Media::AudioFormat iFormat;
    TUint iSampleRate;
    TUint iBitDepth;
    TUint iNumChannels;
    TUint64 iSampleStart;
};

}
}

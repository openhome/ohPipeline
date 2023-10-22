#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Av/Raat/Plugin.h>
#include <OpenHome/Av/Raat/SignalPath.h>
#include <OpenHome/Av/Raat/Transport.h>
#include <OpenHome/Av/Raat/SourceSelection.h>

#include <vector>

#include <raat_plugin_output.h>
#include <rc_status.h>
#include <rc_allocator.h>
#include <jansson.h>
#include <raat_stream.h>

EXCEPTION(RaatPacketError)
EXCEPTION(RaatReaderStopped)

namespace OpenHome {
    class Env;
namespace Media {
    class PipelineManager;
    class IAudioTime;
    class IPullableClock;
}
namespace Av {
    class IMediaPlayer;

class IRaatWriter
{
public:
    virtual ~IRaatWriter() {}
    virtual void Write(const Brx& aData) = 0;
};

class RaatStreamFormat;
class IRaatReader
{
public:
    virtual ~IRaatReader() {}
    virtual const RaatStreamFormat& StreamFormat() = 0;
    virtual void NotifyReady() = 0;
    virtual void Read(IRaatWriter& aWriter) = 0;
    virtual void Interrupt() = 0;
};

class RaatSignalPath : public IRaatSignalPath
{
public:
    void Set(const IRaatSignalPath& aSignalPath)
    {
        iExakt = aSignalPath.Exakt();
        iSpaceOptimisation = aSignalPath.SpaceOptimisation();
        iAmplifier = aSignalPath.Amplifier();
        iOutput = aSignalPath.Output();
    }

public: // from IRaatSignalPath
    TBool Exakt() const override { return iExakt; }
    TBool SpaceOptimisation() const override { return iSpaceOptimisation; }
    TBool Amplifier() const override { return iAmplifier; }
    IRaatSignalPath::EOutput Output() const override { return iOutput; }
private:
    TBool iExakt;
    TBool iSpaceOptimisation;
    TBool iAmplifier;
    IRaatSignalPath::EOutput iOutput;
};

class RaatStreamFormat
{
public:
    RaatStreamFormat();
public:
    void Set(RAAT__StreamFormat*& aFormat);
public:
    Media::AudioFormat Format() const;
    TUint SampleRate() const;
    TUint BitDepth() const;
    TUint NumChannels() const;
private:
    Media::AudioFormat iFormat;
    TUint iSampleRate;
    TUint iBitDepth;
    TUint iNumChannels;
    mutable Mutex iLock;
};

class ISourceRaat;
class RaatOutput;

typedef struct {
    RAAT__OutputPlugin iPlugin; // must be first member
    RaatOutput* iSelf;
} RaatOutputPluginExt;

class IRaatTime;

class RaatOutput
    : public RaatPluginAsync 
    , public IRaatReader
    , public IRaatOutputControl
    , private IRaatSignalPathObserver
{
private:
    static const TUint kNanoSecsPerSec = 1000000000;
    static const TUint kDefaultDelayMs = 500;
    static const TUint64 kDefaultDelayNs = kDefaultDelayMs * 1000 * 1000;
    static const Brn kKeyDsdEnable;
    static const TUint kValDsdDisabled;
    static const TUint kValDsdEnabled;
public:
    RaatOutput(
        IMediaPlayer&               aMediaPlayer,
        ISourceRaat&                aSourceRaat,
        Media::IAudioTime&          aAudioTime,
        Media::IPullableClock&      aPullableClock,
        IRaatSignalPathObservable&  aSignalPathObservable);
    ~RaatOutput();
public:
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
    RC__Status StopStream(int aToken);
    RC__Status ForceTeardownStream();
    RC__Status AddListener(RAAT__OutputMessageCallback aCb, void* aCbUserdata);
    void RemoveListener(RAAT__OutputMessageCallback aCb, void* aCbUserdata);
    void GetDelay(int aToken, int64_t* aDelayNs);
private:
    TUint64 MclkToNs();
    TUint64 NsToMclk(TUint64 aTimeNs);
    TUint64 ConvertTime(TUint64 aTicksFrom, TUint aFreqFrom, TUint aFreqTo);
    RAAT__Stream* StreamRef();
    void ChangeStream(RAAT__Stream* aStream);
    void DsdEnableChanged(Configuration::KeyValuePair<TUint>& aKvp);
    static void AddFormatPcm(RAAT__StreamFormat* aFormat, TUint aSampleRate, TUint aBitDepth);
    static void AddFormatDsd(RAAT__StreamFormat* aFormat, TUint aSampleRate);
    void Stop();
private: // from IRaatReader
    const RaatStreamFormat& StreamFormat() override;
    void NotifyReady() override;
    void Read(IRaatWriter& aWriter) override;
    void Interrupt() override;
private: // from IRaatOutputControl
    void NotifyStandby() override;
    void NotifyDeselected() override;
private: // from RaatPluginAsync
    void ReportState() override;
private: // from IRaatSignalPathObserver
    void SignalPathChanged(const IRaatSignalPath& aSignalPath) override;
private:
    class ControlCallback
    {
    private:
        static const int kTokenInvalid = 0;
    public:
        ControlCallback();
        void Set(
            RAAT__OutputSetupCallback aCbSetup, void* aCbSetupData,
            RAAT__OutputLostCallback aCbLost, void* aCbLostData);
        TUint NotifyReady();
        void NotifyFinalise(const TChar* aReason);
    private:
        void Reset();
    private:
        int iToken;
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
    Media::IAudioTime& iAudioTime;
    Media::IPullableClock& iPullableClock;
    Mutex iLockStream;
    Mutex iLockSignalPath;
    Mutex iLockConfig;
    Configuration::ConfigChoice* iConfigDsdEnable;
    TUint iSubscriberIdDsdEnable;
    RAAT__Stream* iStream;
    ControlCallback iControlCallback;
    int iToken;
    RaatStreamFormat iStreamFormat;
    RaatSignalPath iSignalPath;
    int64_t iStreamPos;
    TUint iSampleRate;
    TUint64 iLastClockPullTicks;
    TUint iClockPull;
    TBool iClockSyncStarted;
    TBool iDsdEnabled;
};

class AutoStreamRef // constructed with ref already held, releases ref on destruction
{
public:
    AutoStreamRef(RAAT__Stream* aStream);
    ~AutoStreamRef();
private:
    RAAT__Stream* iStream;
};

}
}

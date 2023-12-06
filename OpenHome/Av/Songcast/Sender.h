#pragma once

#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Optional.h>
#include <OpenHome/Media/PipelineObserver.h>
#include <OpenHome/Configuration/ConfigManager.h>

#include <vector>

namespace OpenHome {
namespace Net {
    class DvDeviceStandard;
}
namespace Av {

class OhmSenderDriver;
class OhmSender;
class ZoneHandler;
class IOhmTimestamper;
class IUnicastOverrideObserver;

class Sender : public Media::IPipelineElementDownstream, private Media::IMsgProcessor, private Media::IPcmProcessor, private INonCopyable
{
    static const Brn kConfigIdEnabled;
    static const Brn kConfigIdChannel;
    static const Brn kConfigIdMode;
    static const Brn kConfigIdPreset;
    static const TInt kChannelMin = 0;
    static const TInt kChannelMax = 65535;
    static const TInt kPresetMin = 0;
    static const TInt kPresetMax = 0x7fffffff;
    static const TInt kPresetNone = 0;
    static const TUint kSongcastPacketMs = 5;
    static const TUint kSongcastPacketJiffies = Media::Jiffies::kPerMs * kSongcastPacketMs;
    static const TUint kSongcastPacketMaxBytes = 3 * Media::DecodedAudio::kMaxNumChannels * 192 * kSongcastPacketMs;
public:
    Sender(Environment& aEnv,
           Net::DvDeviceStandard& aDevice,
           ZoneHandler& aZoneHandler,
           Optional<IOhmTimestamper> aTimestamper,
           Configuration::IConfigInitialiser& aConfigInit,
           TUint aThreadPriority,
           const Brx& aName,
           TUint aMinLatencyMs,
           const Brx& aSongcastMode,
           IUnicastOverrideObserver& aUnicastOverrideObserver);
    ~Sender();
    void SetName(const Brx& aName);
    void SetImageUri(const Brx& aUri);
    void NotifyPipelineState(Media::EPipelineState aState);
private: // from Media::IPipelineElementDownstream
    void Push(Media::Msg* aMsg) override;
private: // from Media::IMsgProcessor
    Media::Msg* ProcessMsg(Media::MsgMode* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgTrack* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgDrain* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgDelay* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgEncodedStream* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgStreamSegment* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgAudioEncoded* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgMetaText* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgStreamInterrupted* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgHalt* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgFlush* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgWait* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgDecodedStream* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgAudioPcm* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgAudioDsd* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgSilence* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgPlayable* aMsg) override;
    Media::Msg* ProcessMsg(Media::MsgQuit* aMsg) override;
private:
    void ProcessAudio(Media::MsgAudio* aMsg);
    void SendPendingAudio(TBool aHalt = false);
    void ConfigEnabledChanged(Configuration::KeyValuePair<TUint>& aStringId);
    void ConfigChannelChanged(Configuration::KeyValuePair<TInt>& aValue);
    void ConfigModeChanged(Configuration::KeyValuePair<TUint>& aStringId);
    void ConfigPresetChanged(Configuration::KeyValuePair<TInt>& aValue);
private:
    static TUint FirstChannelToSend(TUint aNumChannels);
    void DoProcessFragment(const Brx& aData, TUint aNumChannels, TUint aBytesPerSample);
    // from IPcmProcessor
    void BeginBlock() override;
    void ProcessFragment(const Brx& aData, TUint aNumChannels, TUint aSubsampleBytes) override;
    void ProcessSilence(const Brx& aData, TUint aNumChannels, TUint aSubsampleBytes) override;
    void EndBlock() override;
    void Flush() override;
private:
    class PlayableCreator : private Media::IMsgProcessor
    {
    public:
        PlayableCreator();
        Media::MsgPlayable* Process(Media::MsgAudio* aMsg);
    private: // from Media::IMsgProcessor
        Media::Msg* ProcessMsg(Media::MsgMode* aMsg) override;
        Media::Msg* ProcessMsg(Media::MsgTrack* aMsg) override;
        Media::Msg* ProcessMsg(Media::MsgDrain* aMsg) override;
        Media::Msg* ProcessMsg(Media::MsgDelay* aMsg) override;
        Media::Msg* ProcessMsg(Media::MsgEncodedStream* aMsg) override;
        Media::Msg* ProcessMsg(Media::MsgStreamSegment* aMsg) override;
        Media::Msg* ProcessMsg(Media::MsgAudioEncoded* aMsg) override;
        Media::Msg* ProcessMsg(Media::MsgMetaText* aMsg) override;
        Media::Msg* ProcessMsg(Media::MsgStreamInterrupted* aMsg) override;
        Media::Msg* ProcessMsg(Media::MsgHalt* aMsg) override;
        Media::Msg* ProcessMsg(Media::MsgFlush* aMsg) override;
        Media::Msg* ProcessMsg(Media::MsgWait* aMsg) override;
        Media::Msg* ProcessMsg(Media::MsgDecodedStream* aMsg) override;
        Media::Msg* ProcessMsg(Media::MsgAudioPcm* aMsg) override;
        Media::Msg* ProcessMsg(Media::MsgAudioDsd* aMsg) override;
        Media::Msg* ProcessMsg(Media::MsgSilence* aMsg) override;
        Media::Msg* ProcessMsg(Media::MsgPlayable* aMsg) override;
        Media::Msg* ProcessMsg(Media::MsgQuit* aMsg) override;
    private:
        Media::MsgPlayable* iPlayable;
    };
private:
    OhmSenderDriver* iOhmSenderDriver;
    OhmSender* iOhmSender;
    Configuration::ConfigChoice* iConfigEnabled;
    TUint iListenerIdConfigEnabled;
    Configuration::ConfigNum* iConfigChannel;
    TUint iListenerIdConfigChannel;
    Configuration::ConfigChoice* iConfigMode;
    TUint iListenerIdConfigMode;
    Configuration::ConfigNum* iConfigPreset;
    TUint iListenerIdConfigPreset;
    std::vector<Media::MsgAudio*> iPendingAudio;
    Bwx* iAudioBuf;
    TUint iSampleRate;
    const TUint iMinLatencyMs;
    const Media::BwsMode iSongcastMode;
    IUnicastOverrideObserver& iUnicastOverrideObserver;
    TBool iEnabled;
    TBool iUserEnabled; // user config allows songcast sending
    TBool iUserEnabledInitialised;
    TBool iStreamForbidden; // current stream does not allow broadcast to other players
    TUint iFirstChannelIndex;
};

} // namespace Av
} // namespace OpenHome


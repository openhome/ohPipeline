#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Media/Pipeline/Msg.h>

namespace OpenHome {
namespace Media {

/*
Element which sits at the very right of the generic pipeline.
Passes on Format, Halt and Quit msgs.
Only passes on Format when either sample rate and/or bit depth changes.
Converts AudioPcm, Silence msgs to Playable.
Consumes StreamInterrupted
*/
class IAudioTime;
class PreDriver : public PipelineElement, public IPipelineElementUpstream, private INonCopyable
{
private:
    static const TUint kSupportedMsgTypes;
public:
    PreDriver(IPipelineElementUpstream& aUpstreamElement);
    virtual ~PreDriver();
public: // from IPipelineElementUpstream
    Msg* Pull() override;
private: // IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgDrain* aMsg) override;
    Msg* ProcessMsg(MsgStreamInterrupted* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgAudioDsd* aMsg) override;
    Msg* ProcessMsg(MsgSilence* aMsg) override;
    Msg* ProcessMsg(MsgQuit* aMsg) override;
private:
    IPipelineElementUpstream& iUpstreamElement;
    BwsMode iModeName;
    TUint iSampleRate;
    TUint iBitDepth;
    TUint iNumChannels;
    AudioFormat iFormat;
    Semaphore iShutdownSem;
    TUint64 iSilenceSinceLastAudio;
    TBool iSilenceSinceAudio;
    TBool iModeHasPullableClock;
    TBool iQuit;
};

} // namespace Media
} // namespace OpenHome


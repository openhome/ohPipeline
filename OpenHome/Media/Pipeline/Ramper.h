#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Media/Pipeline/Msg.h>

namespace OpenHome {
namespace Media {

/*
Element which applies a ramp up at the start of a stream when necessary.
Is NOT responsible for all ramping.  Many other elements also apply ramps in other circumstances.
*/

class Ramper : public PipelineElement, public IPipelineElementUpstream, private INonCopyable
{
    friend class SuiteRamper;

    static const TUint kSupportedMsgTypes;
public:
    Ramper(IPipelineElementUpstream& aUpstreamElement, TUint aRampJiffiesLong, TUint aRampJiffiesShort);
    virtual ~Ramper();
public: // from IPipelineElementUpstream
    Msg* Pull() override;
private: // IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgHalt* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgAudioDsd* aMsg) override;
    Msg* ProcessMsg(MsgSilence* aMsg) override;
private:
    Msg* ProcessAudio(MsgAudioDecoded* aMsg);
private:
    TBool IsRampApplicable(const DecodedStreamInfo& aInfo);
private:
    IPipelineElementUpstream& iUpstreamElement;
    TUint iStreamId;
    AudioFormat iFormat;
    TBool iRamping;
    const TUint iRampJiffiesLong;
    const TUint iRampJiffiesShort;
    TUint iRampJiffies;
    TUint iRemainingRampSize;
    TUint iCurrentRampValue;
    TUint iSampleRate;
    MsgQueueLite iQueue;
};

} // namespace Media
} // namespace OpenHome


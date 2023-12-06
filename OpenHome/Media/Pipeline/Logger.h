#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Media/Pipeline/Msg.h>

namespace OpenHome {
namespace Media {

/*
Element which logs msgs as they pass through.
Can be inserted [0..n] times through the pipeline, depending on your debugging needs.
*/

class Logger : public IPipelineElementUpstream, public IPipelineElementDownstream, private IMsgProcessor, private INonCopyable
{
    static const TUint kMaxLogBytes = 10 * 1024;
public:
    enum EMsgType
    {
        EMsgNone                = 0
       ,EMsgMode                = 1 <<  0
       ,EMsgTrack               = 1 <<  1
       ,EMsgDrain               = 1 <<  2
       ,EMsgDelay               = 1 <<  3
       ,EMsgEncodedStream       = 1 <<  4
       ,EMsgStreamSegment       = 1 <<  5
       ,EMsgAudioEncoded        = 1 <<  6
       ,EMsgMetaText            = 1 <<  7
       ,EMsgStreamInterrupted   = 1 <<  8
       ,EMsgHalt                = 1 <<  9
       ,EMsgFlush               = 1 << 10
       ,EMsgWait                = 1 << 11
       ,EMsgDecodedStream       = 1 << 12
       ,EMsgAudioPcm            = 1 << 13
       ,EMsgAudioDsd            = 1 << 14
       ,EMsgSilence             = 1 << 15
       ,EMsgAudioRamped         = 1 << 16
       ,EMsgPlayable            = 1 << 17
       ,EMsgQuit                = 1 << 18
       ,EMsgAll                 = 0x7fffffff
    };
public:
    Logger(IPipelineElementUpstream& aUpstreamElement, const TChar* aId);
    Logger(const TChar* aId, IPipelineElementDownstream& aDownstreamElement);
    virtual ~Logger();
    void SetEnabled(TBool aEnabled);
    void SetFilter(TUint aMsgTypes);
    void LogAudio();
public: // from IPipelineElementUpstream
    Msg* Pull() override;
public: // from IPipelineElementDownstream
    void Push(Msg* aMsg) override;
private: // IMsgProcessor
    Msg* ProcessMsg(MsgMode* aMsg) override;
    Msg* ProcessMsg(MsgTrack* aMsg) override;
    Msg* ProcessMsg(MsgDrain* aMsg) override;
    Msg* ProcessMsg(MsgDelay* aMsg) override;
    Msg* ProcessMsg(MsgEncodedStream* aMsg) override;
    Msg* ProcessMsg(MsgStreamSegment* aMsg) override;
    Msg* ProcessMsg(MsgAudioEncoded* aMsg) override;
    Msg* ProcessMsg(MsgMetaText* aMsg) override;
    Msg* ProcessMsg(MsgStreamInterrupted* aMsg) override;
    Msg* ProcessMsg(MsgHalt* aMsg) override;
    Msg* ProcessMsg(MsgFlush* aMsg) override;
    Msg* ProcessMsg(MsgWait* aMsg) override;
    Msg* ProcessMsg(MsgDecodedStream* aMsg) override;
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgAudioDsd* aMsg) override;
    Msg* ProcessMsg(MsgSilence* aMsg) override;
    Msg* ProcessMsg(MsgPlayable* aMsg) override;
    Msg* ProcessMsg(MsgQuit* aMsg) override;
private:
    void LogAudioDecoded(MsgAudioDecoded& aAudio, const TChar* aType);
    void LogRamp(const Media::Ramp& aRamp);
    inline TBool IsEnabled(EMsgType aType) const;
private:
    IPipelineElementUpstream* iUpstreamElement;
    IPipelineElementDownstream* iDownstreamElement;
    const TChar* iId;
    TBool iEnabled;
    TInt iFilter;
    Semaphore iShutdownSem;
    Bws<kMaxLogBytes> iBuf;
    TUint64 iJiffiesPcm;
    TUint64 iJiffiesDsd;
    TUint64 iJiffiesSilence;
    TUint64 iJiffiesPlayable;
};

} // namespace Media
} // namespace OpenHome


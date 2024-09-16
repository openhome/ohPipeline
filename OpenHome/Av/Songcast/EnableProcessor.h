#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Pipeline/Brancher.h>

namespace OpenHome {
namespace Av {

class SongcastEnableProcessor
    : public Media::IBranchEnableProcessor
    , private Media::IMsgProcessor
    , private INonCopyable
{
public:
    SongcastEnableProcessor(const Brx& aSongcastMode);
public: // from Media::IBranchEnableProcessor
    TBool Peek(Media::Msg& aMsg) override;
private: // Media::IMsgProcessor
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
    Brn iSongcastMode;
    TBool iEnabled;
    TBool iOverride;
};

} // namespace Av
} // namespace OpenHome


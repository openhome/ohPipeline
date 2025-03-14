#include <OpenHome/Av/Songcast/EnableProcessor.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Pipeline/Msg.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;

// SongcastEnableProcessor

SongcastEnableProcessor::SongcastEnableProcessor(const Brx& aSongcastMode)
    : iSongcastMode(aSongcastMode)
    , iEnabled(true)
    , iOverride(false)
{
}

TBool SongcastEnableProcessor::Peek(Msg& aMsg)
{
    iOverride = false;
    const auto wasEnabled = iEnabled;
    (void)aMsg.Process(*this);

    if (iEnabled || wasEnabled || iOverride) {
        // pass on the MsgMode that signals the branch being disabled
        // ...OhmSender needs to be halted to reduce demand on multicast sockets on old hardware targets
        // ...and we can't disable the sender outide the pipeline without risking audio glitches
        // Also pass on MsgDecodedStream that signals a non-sharable stream
        return true;
    }
    return false;
}

Msg* SongcastEnableProcessor::ProcessMsg(MsgMode* aMsg)
{
    iEnabled = (aMsg->Mode() != iSongcastMode);
    iOverride = true;
    return aMsg;
}

Msg* SongcastEnableProcessor::ProcessMsg(MsgTrack* aMsg)
{
    return aMsg;
}

Msg* SongcastEnableProcessor::ProcessMsg(MsgDrain* aMsg)
{
    return aMsg;
}

Msg* SongcastEnableProcessor::ProcessMsg(MsgDelay* aMsg)
{
    return aMsg;
}

Msg* SongcastEnableProcessor::ProcessMsg(MsgEncodedStream* aMsg)
{
    return aMsg;
}

Msg* SongcastEnableProcessor::ProcessMsg(MsgStreamSegment* /*aMsg*/)
{
    ASSERTS(); /* only expect to deal with decoded audio at this stage of the pipeline */
    return nullptr;
}

Msg* SongcastEnableProcessor::ProcessMsg(MsgAudioEncoded* /*aMsg*/)
{
    ASSERTS(); /* only expect to deal with decoded audio at this stage of the pipeline */
    return nullptr;
}

Msg* SongcastEnableProcessor::ProcessMsg(MsgMetaText* aMsg)
{
    return aMsg;
}

Msg* SongcastEnableProcessor::ProcessMsg(MsgStreamInterrupted* aMsg)
{
    return aMsg;
}

Msg* SongcastEnableProcessor::ProcessMsg(MsgHalt* aMsg)
{
    return aMsg;
}

Msg* SongcastEnableProcessor::ProcessMsg(MsgFlush* aMsg)
{
    return aMsg;
}

Msg* SongcastEnableProcessor::ProcessMsg(MsgWait* aMsg)
{
    return aMsg;
}

Msg* SongcastEnableProcessor::ProcessMsg(MsgDecodedStream* aMsg)
{
    iEnabled = (aMsg->StreamInfo().Multiroom() == Multiroom::Allowed);
    return aMsg;
}

Msg* SongcastEnableProcessor::ProcessMsg(MsgAudioPcm* aMsg)
{
    return aMsg;
}

Msg* SongcastEnableProcessor::ProcessMsg(MsgAudioDsd* aMsg)
{
    return aMsg;
}

Msg* SongcastEnableProcessor::ProcessMsg(MsgSilence* aMsg)
{
    return aMsg;
}

Msg* SongcastEnableProcessor::ProcessMsg(MsgPlayable* /*aMsg*/)
{
    ASSERTS();
    return nullptr;
}

Msg* SongcastEnableProcessor::ProcessMsg(MsgQuit* aMsg)
{
    iEnabled = true;
    return aMsg;
}

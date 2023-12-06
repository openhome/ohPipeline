#include <OpenHome/Media/Utils/Silencer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Private/Fifo.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/ClockPuller.h>

namespace OpenHome {
namespace Media {

class SilencerMsgInProcessor : public IMsgProcessor
{
public:
    static TBool IsMsgQuit(Msg* aMsg);
private:
    SilencerMsgInProcessor();
private: // from IMsgProcessor
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
    TBool iQuit;
};

} // namespace Media
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Media;


const TUint Silencer::kSupportedMsgTypes =   eMode
                                           | eDrain
                                           | eHalt
                                           | eDecodedStream
                                           | ePlayable
                                           | eQuit;

Silencer::Silencer(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstreamElement, TUint aThreadPriority, TUint aSilenceJiffies, TUint aMaxNumMsgs)
    : PipelineElement(kSupportedMsgTypes)
    , iMsgFactory(aMsgFactory)
    , iUpstreamElement(aUpstreamElement)
    , iSilenceJiffies(aSilenceJiffies)
    , iFifo(aMaxNumMsgs)
    , iSampleRate(0)
    , iBitDepth(0)
    , iNumChannels(0)
    , iQuit(false)
    , iHalted(true)
{
    iThread = new ThreadFunctor("Silencer", MakeFunctor(*this, &Silencer::Run), aThreadPriority);
    iThread->Start();
}

Silencer::~Silencer()
{
    delete iThread;
}

void Silencer::Run()
{
    while (!iQuit) {
        Msg* msg = iUpstreamElement.Pull();
        iQuit = SilencerMsgInProcessor::IsMsgQuit(msg);
        iFifo.Write(msg);
    }
}

Msg* Silencer::Pull()
{
    Msg* msg = nullptr;
    while (msg == nullptr) {
        if (iSampleRate == 0 || iFifo.SlotsUsed() != 0) { // process and return next msg
            msg = iFifo.Read()->Process(*this);
        }
        else { // generate silence
            ASSERT(iHalted);
            TUint size = iSilenceJiffies;
            auto msgSilence = iMsgFactory.CreateMsgSilence(size, iSampleRate, iBitDepth, iNumChannels);
            msg = msgSilence->CreatePlayable();
        }
    }
    return msg;
}

Msg* Silencer::ProcessMsg(MsgHalt* aMsg)
{
    // swallow halt messages - the driver presumably can't do anything with them if its using this class
    iHalted = true;
    aMsg->RemoveRef();
    return nullptr;
}

Msg* Silencer::ProcessMsg(MsgDecodedStream* aMsg)
{
    auto info = aMsg->StreamInfo();
    iSampleRate  = info.SampleRate();
    iBitDepth    = info.BitDepth();
    iNumChannels = info.NumChannels();
    return aMsg;
}

Msg* Silencer::ProcessMsg(MsgPlayable* aMsg)
{
    iHalted = false;
    return aMsg;
}


// SilencerMsgInProcessor

TBool SilencerMsgInProcessor::IsMsgQuit(Msg* aMsg)
{ // static
    SilencerMsgInProcessor self;
    aMsg->Process(self);
    return self.iQuit;
}

SilencerMsgInProcessor::SilencerMsgInProcessor()
    : iQuit(false)
{
}

Msg* SilencerMsgInProcessor::ProcessMsg(MsgMode* aMsg)                  { return aMsg; }
Msg* SilencerMsgInProcessor::ProcessMsg(MsgTrack* aMsg)                 { return aMsg; }
Msg* SilencerMsgInProcessor::ProcessMsg(MsgDrain* aMsg)                 { return aMsg; }
Msg* SilencerMsgInProcessor::ProcessMsg(MsgDelay* aMsg)                 { return aMsg; }
Msg* SilencerMsgInProcessor::ProcessMsg(MsgEncodedStream* aMsg)         { return aMsg; }
Msg* SilencerMsgInProcessor::ProcessMsg(MsgStreamSegment* aMsg)         { return aMsg; }
Msg* SilencerMsgInProcessor::ProcessMsg(MsgAudioEncoded* aMsg)          { return aMsg; }
Msg* SilencerMsgInProcessor::ProcessMsg(MsgMetaText* aMsg)              { return aMsg; }
Msg* SilencerMsgInProcessor::ProcessMsg(MsgStreamInterrupted* aMsg)     { return aMsg; }
Msg* SilencerMsgInProcessor::ProcessMsg(MsgHalt* aMsg)                  { return aMsg; }
Msg* SilencerMsgInProcessor::ProcessMsg(MsgFlush* aMsg)                 { return aMsg; }
Msg* SilencerMsgInProcessor::ProcessMsg(MsgWait* aMsg)                  { return aMsg; }
Msg* SilencerMsgInProcessor::ProcessMsg(MsgDecodedStream* aMsg)         { return aMsg; }
Msg* SilencerMsgInProcessor::ProcessMsg(MsgAudioPcm* aMsg)              { return aMsg; }
Msg* SilencerMsgInProcessor::ProcessMsg(MsgAudioDsd* aMsg)              { return aMsg; }
Msg* SilencerMsgInProcessor::ProcessMsg(MsgSilence* aMsg)               { return aMsg; }
Msg* SilencerMsgInProcessor::ProcessMsg(MsgPlayable* aMsg)              { return aMsg; }

Msg* SilencerMsgInProcessor::ProcessMsg(MsgQuit* aMsg)
{
    iQuit = true;
    return aMsg;
}

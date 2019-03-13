#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/MuteManager.h>

namespace OpenHome {
namespace Media {

class IVolumeMuterStepped
{
public:
    enum class Status
    {
        eInProgress,
        eComplete
    };
public:
    virtual Status BeginMute() = 0;
    virtual Status StepMute(TUint aJiffies) = 0;
    virtual void SetMuted() = 0;
    virtual Status BeginUnmute() = 0;
    virtual Status StepUnmute(TUint aJiffies) = 0;
    virtual void SetUnmuted() = 0;
    virtual ~IVolumeMuterStepped() {}
};

/*
    Similar to Muter but ramps volume rather than samples.
*/

class MuterVolume : public PipelineElement, public IPipelineElementUpstream, public IMute, private INonCopyable
{
    friend class SuiteMuterVolume;

    static const TUint kSupportedMsgTypes;
    static const TUint kJiffiesUntilMute;
public:
    MuterVolume(MsgFactory& aMsgFactory, IPipelineElementUpstream& aUpstream);
    ~MuterVolume();
    void Start(IVolumeMuterStepped& aVolumeMuter);
public: // from IMute
    void Mute() override;
    void Unmute() override;
private: // from IPipelineElementUpstream
    Msg* Pull() override;
private: // IMsgProcessor
    Msg* ProcessMsg(MsgHalt* aMsg) override;
    Msg* ProcessMsg(MsgAudioPcm* aMsg) override;
    Msg* ProcessMsg(MsgAudioDsd* aMsg) override;
    Msg* ProcessMsg(MsgSilence* aMsg) override;
private:
    void ProcessAudio(MsgAudio* aMsg);
    void PipelineHalted();
    const TChar* StateAsString() const;
private:
    enum class State
    {
        eRunning,
        eMutingRamp,
        eMutingWait,
        eUnmutingRamp,
        eMuted
    };
private:
    MsgFactory& iMsgFactory;
    IPipelineElementUpstream& iUpstream;
    IVolumeMuterStepped* iVolumeMuter;
    Mutex iLock;
    Semaphore iSemMuted;
    State iState;
    TUint iJiffiesUntilMute;
    MsgHalt* iMsgHalt;
    TBool iHalted;
};

class VolumeRamperStub : public IVolumeMuterStepped
{
private: // from IVolumeMuterStepped
    IVolumeMuterStepped::Status BeginMute()                       { return IVolumeMuterStepped::Status::eComplete; }
    IVolumeMuterStepped::Status StepMute(TUint /*aJiffies*/)      { return IVolumeMuterStepped::Status::eComplete; }
    void SetMuted()                                         {}
    IVolumeMuterStepped::Status BeginUnmute()                     { return IVolumeMuterStepped::Status::eComplete; }
    IVolumeMuterStepped::Status StepUnmute(TUint /*aJiffies*/)    { return IVolumeMuterStepped::Status::eComplete; }
    void SetUnmuted()                                       {}
};

} // namespace Media
} // namespace OpenHome


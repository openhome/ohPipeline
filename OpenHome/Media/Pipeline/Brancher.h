#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Media/Pipeline/Msg.h>

EXCEPTION(BranchProcessorInvalid);

namespace OpenHome {
namespace Media {

/*  IBranchPreProcessor
 *
 *  IMsgProcessor derivative that allows branches to receive messages when not enabled
 *  Useful for passing pipeline control and format messages so downstream components
 *  can maintain configuration even when not enabled
 */

class IBranchPreProcessor : public IMsgProcessor
{
public:
    virtual ~IBranchPreProcessor() {}
};

/*  IBranchEnableProcessor
 *
 *  Peeks messages and returns true/false if the branch wishes to receive the message
 */

class IBranchEnableProcessor
{
public:
    virtual TBool Peek(Msg& aMsg) = 0;

    virtual ~IBranchEnableProcessor() {}
};

/*  IBranchPostProcessor
 *
 *  IMsgProcessor derivative that performs operations on messages after they have been sent
 *  to the branch. Operations are independent of the branch and apply only to the out-going
 *  original path of the messages
 */

class IBranchPostProcessor : public IMsgProcessor
{
public:
    virtual ~IBranchPostProcessor() {}
};

/*  IBranch
 *
 *  Used to configure an IBrancher. Supplies the downstream element (often a sending thread),
 *  an IBranchEnableProcessor to determine if messages should be sent to the branch, and an
 *  IBranchPostProcessor to process messages continuing on the original path (e.g., silence
 *  out-going audio messages once the branch is enabled)
 */

class IBranch : public IPipelineElementDownstream
{
public:
    virtual Msg* PreProcess(Msg* aMsg) = 0;
    virtual TBool ProcessEnable(Msg* aMsg) = 0;
    virtual Msg* PostProcess(Msg* aMsg) = 0;

    virtual ~IBranch() {}
};

class IBrancher
{
public:
    enum class EPriority {
        Default,        // Remains active when no other branches are enabled
        Exclusive,      // Disables all other branches when enabled
        Concurrent      // Allow multiple branches to operate concurrently
    };
public:
    virtual const Brx& Id() const = 0;
    virtual EPriority Priority() const = 0;
    virtual void SetBranch(IBranch& aBranch) = 0;

    virtual ~IBrancher() {}
};

class IBrancherControllable : public IBrancher
{
public:
    virtual void SetEnabled(TBool aEnable) = 0;

    virtual ~IBrancherControllable() {}
};

class Branch : public IBranch
{
public:
    static IBranch* Create(
        IPipelineElementDownstream& aDownstream,
        IBranchPreProcessor*        aPreProcessor,
        IBranchEnableProcessor*     aEnableProcessor,
        IBranchPostProcessor*       aPostProcessor);
private:
    Branch(
        IPipelineElementDownstream& aDownstream,
        IBranchPreProcessor*        aPreProcessor,
        IBranchEnableProcessor*     aEnableProcessor,
        IBranchPostProcessor*       aPostProcessor);
public: // from IBranch
    void Push(Msg* aMsg) override;
    Msg* PreProcess(Msg* aMsg) override;
    TBool ProcessEnable(Msg* aMsg) override;
    Msg* PostProcess(Msg* aMsg) override;
private:
    IPipelineElementDownstream& iDownstream;
public:
    IBranchPreProcessor*        iPreProcessor;
    IBranchEnableProcessor*     iEnableProcessor;
    IBranchPostProcessor*       iPostProcessor;
};

/*  Brancher
 *
 *  Amalgamation of Router and Splitter pipeline components. Inserted into the pipeline at approriate location
 *  and added to IBranchController for access by out-of-band components. When enabled with a valid IBranch set,
 *  messages are peeked (optional), cloned and sent to the branch, and post-processed (optional)
 */

class Brancher
    : public IPipelineElementUpstream
    , public IBrancherControllable
    , private INonCopyable
{
public:
    Brancher(IPipelineElementUpstream& aUpstream, const Brx& aId, EPriority aPriority);
public: // from IBrancherControllable
    const Brx& Id() const override;
    EPriority Priority() const override;
    void SetBranch(IBranch& aBranch) override;
    void SetEnabled(TBool aEnable) override;
private: // from IPipelineElementUpstream
    Msg* Pull() override;
private:
    TBool IsEnabledLocked(Msg& aMsg);
private:
    class MsgCloner : private Media::IMsgProcessor
    {
    public:
        static Media::Msg* NewRef(Media::Msg& aMsg);
    private:
        MsgCloner();
    private: // from IMsgProcessor
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
    };
private:
    IPipelineElementUpstream&   iUpstream;
    Brh                         iId;
    EPriority                   iPriority;
    IBranch*                    iBranch;
    TBool                       iEnabled;
    Mutex                       iLock;
};

} // namespace Media
} // namespace OpenHome


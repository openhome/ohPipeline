#pragma once

#include <Generated/DvAvOpenhomeOrgReaction1.h>
#include <OpenHome/Net/Core/DvInvocationResponse.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Media/PipelineObserver.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Os.h>
#include <OpenHome/Observable.h>
#include <OpenHome/Av/Reactions.h>

namespace OpenHome {
namespace Av {

class ProviderReaction : public Net::DvProviderAvOpenhomeOrgReaction1
                       , public Media::IPipelineObserver
                       , public IReactionHandlerObserver
{
public:
    ProviderReaction(Net::DvDevice& aDevice, Media::PipelineManager& aPipelineManager);
    ~ProviderReaction();

public:
    void AddHandler(IReactionHandler* aHandler); // Ownership taken

private: // DvProviderAvOpenhomeOrgReaction1
    void GetCanReact(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseBool& aCanReact) override;
    void GetCurrentReaction(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseString& aReaction) override;
    void GetAvailableReactions(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseString& aAvailableReactions) override;
    void SetReaction(Net::IDvInvocation& aInvocation, const Brx& aReaction) override;
    void ClearReaction(Net::IDvInvocation& aInvocation) override;

private: // IPipelineObserver
    void NotifyPipelineState(Media::EPipelineState aState) override;
    void NotifyMode(const Brx& aMode, const Media::ModeInfo& aInfo, const Media::ModeTransportControls& aTransportControls) override;
    void NotifyTrack(Media::Track& aTrack, TBool aStartOfStream) override;
    void NotifyMetaText(const Brx& aText) override;
    void NotifyTime(TUint aSeconds) override;
    void NotifyStreamInfo(const Media::DecodedStreamInfo& aStreamInfo) override;

private: // IReactionHandlerObserver
    void OnReactionHandlerStateChanged() override;

private:
    void GetNewHandlerReactionStateLocked();

private:
    Media::PipelineManager& iPipelineManager;
    Media::Track* iCurrentTrack;
    Mutex iLock;

    IReactionHandler* iHandler;
};


} // namespace Av
} // namespace OpenHome



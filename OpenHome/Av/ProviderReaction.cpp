#include <OpenHome/Av/Reactions.h>
#include <OpenHome/Av/ProviderReaction.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Net;
using namespace OpenHome::Media;

// ProviderReaction

static const TInt kErrorNoCurrentTrack = 801;
static const Brn kErrorMsgNoCurrentTrack("No track is currently playing");

static const TInt kErrorTrackNotReactable = 802;
static const Brn kErrorMsgTrackNotReactable("Track is not reactable");

static const Brn kNoAvailableReactions("[]");

ProviderReaction::ProviderReaction(DvDevice& aDevice, PipelineManager& aPipelineManager)
    : DvProviderAvOpenhomeOrgReaction1(aDevice)
    , iPipelineManager(aPipelineManager)
    , iCurrentTrack(nullptr)
    , iLock("REAC")
{
    iPipelineManager.AddObserver(*this);

    EnablePropertyCanReact();
    EnablePropertyCurrentReaction();
    EnablePropertyAvailableReactions();

    EnableActionGetCanReact();
    EnableActionGetCurrentReaction();
    EnableActionGetAvailableReactions();

    EnableActionSetReaction();
    EnableActionClearReaction();

    SetPropertyCanReact(false);
    SetPropertyCurrentReaction(Brx::Empty());
    SetPropertyAvailableReactions(kNoAvailableReactions);
}

ProviderReaction::~ProviderReaction()
{
    iPipelineManager.RemoveObserver(*this);

    for (auto& handler : iHandlers) {
        handler->RemoveObserver(*this);
    }

    if (iCurrentTrack) {
        iCurrentTrack->RemoveRef();
        iCurrentTrack = nullptr;
    }
}

void ProviderReaction::AddHandler(IReactionHandler* aHandler)
{
    AutoMutex m(iLock);
    ASSERT_VA(aHandler != nullptr, "%s\n", "Provided handler must not be null.");

    aHandler->AddObserver(*this, "ProviderReaction");
    iHandlers.emplace_back(aHandler);
}

void ProviderReaction::GetCanReact(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseBool& aCanReact)
{
    TBool canReact = false;
    GetPropertyCanReact(canReact);

    aInvocation.StartResponse();

    aCanReact.Write(canReact);

    aInvocation.EndResponse();
}

void ProviderReaction::GetCurrentReaction(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseString& aReaction)
{
    Brhz reaction;
    GetPropertyCurrentReaction(reaction);

    aInvocation.StartResponse();

    aReaction.Write(reaction);
    aReaction.WriteFlush();

    aInvocation.EndResponse();
}

void ProviderReaction::GetAvailableReactions(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseString& aAvailableReactions)
{
    Brhz availableReactions;
    GetPropertyAvailableReactions(availableReactions);

    aInvocation.StartResponse();

    aAvailableReactions.Write(availableReactions);
    aAvailableReactions.WriteFlush();

    aInvocation.EndResponse();
}

void ProviderReaction::SetReaction(Net::IDvInvocation& aInvocation, const Brx& aReaction)
{
    {
        AutoMutex m(iLock);

        if (iCurrentTrack == nullptr) {
            aInvocation.Error(kErrorNoCurrentTrack, kErrorMsgNoCurrentTrack);
        }
        else {
            const Brx& currentTrackUri = iCurrentTrack->Uri();

            TBool handled = false;
            for (auto& handler : iHandlers) {
                if (aReaction.Bytes() == 0) {
                    handled |= handler->ClearReaction(currentTrackUri);
                }
                else {
                    handled |= handler->SetReaction(currentTrackUri, aReaction);
                }
            }

            if (!handled) {
                aInvocation.Error(kErrorTrackNotReactable, kErrorMsgTrackNotReactable);
            }
        }
    }

    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

void ProviderReaction::ClearReaction(Net::IDvInvocation& aInvocation)
{
    {
        AutoMutex m(iLock);

        if (iCurrentTrack == nullptr) {
            aInvocation.Error(kErrorNoCurrentTrack, kErrorMsgNoCurrentTrack);
        }
        else {
            const Brx& currentTrackUri = iCurrentTrack->Uri();
            TBool handled = false;
            for (auto& handler : iHandlers) {
                handled |= handler->ClearReaction(currentTrackUri);
            }

            if (!handled) {
                aInvocation.Error(kErrorTrackNotReactable, kErrorMsgTrackNotReactable);
            }
        }
    }

    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

void ProviderReaction::NotifyTrack(Media::Track& aTrack, TBool /*aStartOfStream*/)
{
    AutoMutex m(iLock);

    if (iCurrentTrack != nullptr) {
        iCurrentTrack->RemoveRef();
    }

    aTrack.AddRef();
    iCurrentTrack = &aTrack;

    GetNewHandlerReactionStateLocked();
}


void ProviderReaction::NotifyPipelineState(Media::EPipelineState /*aState*/)
{ }

void ProviderReaction::NotifyMode(const Brx& /*aMode*/, const Media::ModeInfo& /*aInfo*/, const Media::ModeTransportControls& /*aTransportControls*/)
{ }

void ProviderReaction::NotifyMetaText(const Brx& /*aText*/)
{ }

void ProviderReaction::NotifyTime(TUint /*aSeconds*/)
{ }

void ProviderReaction::NotifyStreamInfo(const Media::DecodedStreamInfo& /*aStreamInfo*/)
{ }

void ProviderReaction::OnReactionHandlerStateChanged()
{
    AutoMutex m(iLock);
    GetNewHandlerReactionStateLocked();
}

void ProviderReaction::GetNewHandlerReactionStateLocked()
{
    TBool canReact = false;
    Bws<32> currentReaction;
    Bws<64> availableReactions;

    currentReaction.SetBytes(0);
    availableReactions.SetBytes(0);

    WriterBuffer currentReactionWriter(currentReaction);
    WriterBuffer availableReactionsWriter(availableReactions);

    if (iCurrentTrack) {
        const Brx& currentTrackUri = iCurrentTrack->Uri();

        for (auto& handler : iHandlers) {
            handler->CurrentReactionState(currentTrackUri, canReact, currentReactionWriter, availableReactionsWriter);
        }
    }

    if (availableReactions.Bytes() == 0) {
        availableReactions.Replace(kNoAvailableReactions);
    }

    SetPropertyCanReact(canReact);
    SetPropertyCurrentReaction(currentReaction);
    SetPropertyAvailableReactions(availableReactions);
}



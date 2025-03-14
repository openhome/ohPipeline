#pragma once

#include <OpenHome/Observable.h>
#include <OpenHome/Private/Stream.h>

namespace OpenHome {
namespace Av {

class IReactionHandlerObserver
{
public:
    virtual ~IReactionHandlerObserver() {};
    virtual void OnReactionHandlerStateChanged() = 0;
};

class IReactionHandler : public IObservable<IReactionHandlerObserver>
{
public:
    virtual ~IReactionHandler() { }

    virtual TBool CurrentReactionState(const Brx& aTrackUri, TBool& aCanReact, IWriter& aCurrentReaction, IWriter& aAvailableReactions) = 0;

    virtual TBool SetReaction(const Brx& aTrackUri, const Brx& aReaction) = 0;
    virtual TBool ClearReaction(const Brx& aTrackUri) = 0;
};

} // namespace Av
} // namespace OpenHome



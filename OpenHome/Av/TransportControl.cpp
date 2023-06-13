#include <OpenHome/Av/TransportControl.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Thread.h>

#include <vector>

using namespace OpenHome;
using namespace OpenHome::Av;


TransportRepeatRandom::TransportRepeatRandom()
    : iLock("TCRR")
    , iRepeat(false)
    , iRandom(false)
    , iRepeatNotifyFunc(MakeFunctorGeneric(*this, &TransportRepeatRandom::DoNotifyRepeatChangedLocked))
    , iRandomNotifyFunc(MakeFunctorGeneric(*this, &TransportRepeatRandom::DoNotifyRandomChangedLocked))
{
}

void TransportRepeatRandom::SetRepeat(TBool aRepeat)
{
    AutoMutex _(iLock);
    if (iRepeat == aRepeat) {
        return;
    }
    iRepeat = aRepeat;

    iObservable.NotifyAll(iRepeatNotifyFunc);
}

void TransportRepeatRandom::SetRandom(TBool aRandom)
{
    AutoMutex _(iLock);
    if (iRandom == aRandom) {
        return;
    }
    iRandom = aRandom;

    iObservable.NotifyAll(iRandomNotifyFunc);
}

void TransportRepeatRandom::AddObserver(ITransportRepeatRandomObserver& aObserver, const TChar* aId)
{
    AutoMutex _(iLock);
    iObservable.AddObserver(aObserver, aId);

    aObserver.TransportRepeatChanged(iRepeat);
    aObserver.TransportRandomChanged(iRandom);
}

void TransportRepeatRandom::RemoveObserver(ITransportRepeatRandomObserver& aObserver)
{
    AutoMutex _(iLock);
    iObservable.RemoveObserver(aObserver);
}

void TransportRepeatRandom::DoNotifyRandomChangedLocked(ITransportRepeatRandomObserver& aObserver)
{
    aObserver.TransportRandomChanged(iRandom);
}

void TransportRepeatRandom::DoNotifyRepeatChangedLocked(ITransportRepeatRandomObserver& aObserver)
{
    aObserver.TransportRepeatChanged(iRepeat);
}

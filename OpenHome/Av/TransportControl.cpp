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
{
}

void TransportRepeatRandom::SetRepeat(TBool aRepeat)
{
    AutoMutex _(iLock);
    if (iRepeat == aRepeat) {
        return;
    }
    iRepeat = aRepeat;

    iObservable.NotifyAll([aRepeat](ITransportRepeatRandomObserver& o) {
        o.TransportRepeatChanged(aRepeat);
    });
}

void TransportRepeatRandom::SetRandom(TBool aRandom)
{
    AutoMutex _(iLock);
    if (iRandom == aRandom) {
        return;
    }
    iRandom = aRandom;

    iObservable.NotifyAll([aRandom] (ITransportRepeatRandomObserver& o) {
        o.TransportRandomChanged(aRandom);
    });
}

void TransportRepeatRandom::AddObserver(ITransportRepeatRandomObserver& aObserver)
{
    AutoMutex _(iLock);
    iObservable.AddObserver(aObserver);

    aObserver.TransportRepeatChanged(iRepeat);
    aObserver.TransportRandomChanged(iRandom);
}

void TransportRepeatRandom::RemoveObserver(ITransportRepeatRandomObserver& aObserver)
{
    AutoMutex _(iLock);
    iObservable.RemoveObserver(aObserver);
}

#include <OpenHome/Av/TransportControl.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Json.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Parser.h>

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


// PlayAsCommandTrack

const Brn PlayAsCommandTrack::kCommandTrack("track=");

TBool PlayAsCommandTrack::TryGetTrackFromCommand(const Brx& aCommand, Brn& aUri, Brn& aMetadata)
{
    if (!aCommand.BeginsWith(kCommandTrack)) {
        return false;
    }

    Parser parserCmd(aCommand);
    parserCmd.Next('=');
    Brn json = parserCmd.NextToEnd();
    Bwn jsonW(json.Ptr(), json.Bytes(), json.Bytes());
    JsonParser parserJson;
    parserJson.ParseAndUnescape(jsonW);
    aUri.Set(parserJson.StringOptional("uri"));
    if (aUri.Bytes() == 0) {
        return false;
    }
    aMetadata.Set(parserJson.StringOptional("metadata"));
    return true;
}

#pragma once

#include <OpenHome/Types.h>

namespace OpenHome {
namespace Av {

class IRaatSignalPathObserver
{
public:
    virtual ~IRaatSignalPathObserver() {}
    virtual void SignalPathChanged(TBool aExakt, TBool aAmplifier, TBool aSpeaker) = 0;
};

class IRaatSignalPathObservable
{
public:
    virtual ~IRaatSignalPathObservable() {}
    virtual void RegisterObserver(IRaatSignalPathObserver& aObserver) = 0;
};

} // Av
} // OpenHome

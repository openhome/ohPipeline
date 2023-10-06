#pragma once

#include <OpenHome/Types.h>

namespace OpenHome {
namespace Av {

class IRaatSignalPath
{
public:
    enum class EOutput {
        eDefault,
        eSpeakers,
        eHeadphones
    };
public:
    virtual ~IRaatSignalPath() {}
    virtual TBool Exakt() const = 0;
    virtual TBool SpaceOptimisation() const = 0;
    virtual TBool Amplifier() const = 0;
    virtual EOutput Output() const = 0;
};

class IRaatSignalPathMutable : public IRaatSignalPath
{
public:
    virtual ~IRaatSignalPathMutable() {}
    virtual void SetExakt(TBool aEnabled) = 0;
    virtual void SetSpaceOptimisation(TBool aEnabled) = 0;
    virtual void SetAmplifier(TBool aEnabled) = 0;
    virtual void SetOutput(IRaatSignalPath::EOutput aOutput) = 0;
};

class IRaatSignalPathDownstream
{
public:
    virtual ~IRaatSignalPathDownstream() {}
    virtual void GetSignalPath(IRaatSignalPathMutable& aSignalPath) = 0;
};

class IRaatSignalPathObserver
{
public:
    virtual ~IRaatSignalPathObserver() {}
    virtual void SignalPathChanged(const IRaatSignalPath& aSignalPath) = 0;
};

class IRaatSignalPathObservable
{
public:
    virtual ~IRaatSignalPathObservable() {}
    virtual void RegisterObserver(IRaatSignalPathObserver& aObserver) = 0;
};

class IRaatSignalPathController
{
public:
    virtual ~IRaatSignalPathController() {}
    virtual void NotifyChanged() = 0;
};

} // Av
} // OpenHome

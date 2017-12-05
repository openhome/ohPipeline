#pragma once

#include <OpenHome/Private/Debug.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Av/Debug.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Ascii.h>

#include <list>

namespace OpenHome {
    
class IDebugHandler {
public:
    virtual void Dump(const OpenHome::Brx& aString, OpenHome::IWriterAscii& aBuffer) = 0;
    virtual ~IDebugHandler() {}
};

class IDebugTestHandler {
public:
    virtual TBool Test(const OpenHome::Brx& aString, const OpenHome::Brx& aInput, OpenHome::IWriterAscii& aWriter) = 0;
    virtual ~IDebugTestHandler() {}
};

class IDebugEventObserver {
public:
    virtual void DebugValueChanged(const Brx& aValue) = 0;
    virtual ~IDebugEventObserver() {}
};

class DebugManager : IDebugHandler, IDebugTestHandler
{
public:
    DebugManager();
    void Add(IDebugHandler& aHandler);
    void Add(IDebugTestHandler& aTestHandler);
    void AddObserver(IDebugEventObserver& aObserver);
    void TestEvent(const Brx&  aEventDescription, const Brx& aValue);
    // IDebugHandler
    virtual void Dump(const OpenHome::Brx& aString, OpenHome::IWriterAscii& aBuffer);
    // IDebugTestHandler
    virtual TBool Test(const OpenHome::Brx& aType, const OpenHome::Brx& aInput, OpenHome::IWriterAscii& aWriter);
private:
    std::list<IDebugHandler*> iHandlers;
    std::list<IDebugTestHandler*> iTestHandlers;
    std::vector<IDebugEventObserver*> iObservers;
    OpenHome::Bwh iEventValue;
};

} // namespace OpenHome

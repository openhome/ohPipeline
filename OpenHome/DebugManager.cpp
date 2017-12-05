#include <OpenHome/DebugManager.h>

#include <algorithm>

using namespace OpenHome;

// DebugManager
DebugManager::DebugManager()
{
}

void DebugManager::Add(IDebugHandler& aHandler)
{
    iHandlers.push_back(&aHandler);
}

void DebugManager::Add(IDebugTestHandler& aTestHandler)
{
    iTestHandlers.push_back(&aTestHandler);
}

void DebugManager::Dump(const Brx& aString, IWriterAscii& aWriter)
{
    std::list<IDebugHandler*>::iterator it;
    for(it = iHandlers.begin(); it != iHandlers.end(); it++) {
        IDebugHandler* r = *it;
        r->Dump(aString, aWriter);
    }
}

TBool DebugManager::Test(const Brx& aType, const Brx& aInput, IWriterAscii& aWriter)
{
    TBool result = false;
    std::list<IDebugTestHandler*>::iterator it;
    for(it = iTestHandlers.begin(); it != iTestHandlers.end(); it++) {
        IDebugTestHandler* r = *it;
        result |= r->Test(aType, aInput, aWriter);
    }
    return result;
}

void DebugManager::AddObserver(IDebugEventObserver& aObserver)
{
    iObservers.push_back(&aObserver);
}

void DebugManager::TestEvent(const Brx&  aEventDescription, const Brx& aValue)
{
    Bwh val(aEventDescription.Bytes() + aValue.Bytes() + 2);
    val.Replace(aEventDescription);
    val.Append(": ");
    val.Append(aValue);
    for (auto it=iObservers.begin(); it!=iObservers.end(); ++it) {
        // notify event that new episoide is available for given IDs
        (*it)->DebugValueChanged(val);
    }
}
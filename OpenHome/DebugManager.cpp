#include <OpenHome/DebugManager.h>

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

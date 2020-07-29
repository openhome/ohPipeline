#include <OpenHome/Private/TestFramework.h>

extern void TestStore();

void OpenHome::TestFramework::Runner::Main(TInt /*aArgc*/, TChar* /*aArgv*/[], Net::InitialisationParams* aInitParams)
{
    Net::UpnpLibrary::InitialiseMinimal(aInitParams);

    TestStore();

#ifdef MAC_OSX
    /* Mac internally uses CFRunLoops for SleepWake threads.
     * We need to be careful not to do things too quickly 
     * otherwise the CF functions we use will race and 
     * crash the test runs */
    Thread::Sleep(500);
#endif

    Net::UpnpLibrary::Close();

    delete aInitParams;
}

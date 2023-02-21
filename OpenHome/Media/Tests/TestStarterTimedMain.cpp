#include <OpenHome/Private/TestFramework.h>

extern void TestStarterTimed();

void OpenHome::TestFramework::Runner::Main(TInt /*aArgc*/, TChar* /*aArgv*/[], Net::InitialisationParams* aInitParams)
{
    Net::UpnpLibrary::InitialiseMinimal(aInitParams);
    TestStarterTimed();
    delete aInitParams;
    Net::UpnpLibrary::Close();
}

#include <OpenHome/Private/TestFramework.h>

extern void TestSenderQueue();

void OpenHome::TestFramework::Runner::Main(TInt /*aArgc*/, TChar* /*aArgv*/[], Net::InitialisationParams* aInitParams)
{
    Net::UpnpLibrary::InitialiseMinimal(aInitParams);
    TestSenderQueue();
    delete aInitParams;
    Net::UpnpLibrary::Close();
}

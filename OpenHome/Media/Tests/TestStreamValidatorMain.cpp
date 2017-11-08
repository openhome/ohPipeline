#include <OpenHome/Private/TestFramework.h>

extern void TestStreamValidator();

void OpenHome::TestFramework::Runner::Main(TInt /*aArgc*/, TChar* /*aArgv*/[], Net::InitialisationParams* aInitParams)
{
    Net::UpnpLibrary::InitialiseMinimal(aInitParams);
    TestStreamValidator();
    delete aInitParams;
    Net::UpnpLibrary::Close();
}

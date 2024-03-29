#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Net/Private/Globals.h>

extern void TestPipeline(OpenHome::Environment& aEnv);

void OpenHome::TestFramework::Runner::Main(TInt /*aArgc*/, TChar* /*aArgv*/[], Net::InitialisationParams* aInitParams)
{
    Net::UpnpLibrary::InitialiseMinimal(aInitParams);
    TestPipeline(*gEnv);
    delete aInitParams;
    Net::UpnpLibrary::Close();
}

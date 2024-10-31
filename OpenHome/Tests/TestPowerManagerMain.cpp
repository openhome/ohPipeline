#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/Env.h>

extern void TestPowerManager(OpenHome::Environment& aEnv);

void OpenHome::TestFramework::Runner::Main(TInt /*aArgc*/, TChar* /*aArgv*/[], Net::InitialisationParams* aInitParams)
{
    auto env = Net::UpnpLibrary::Initialise(aInitParams);
    TestPowerManager(*env);
    Net::UpnpLibrary::Close();
}

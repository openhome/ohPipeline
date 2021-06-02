#include <OpenHome/Private/TestFramework.h>
extern void TestPhaseAdjuster();

void OpenHome::TestFramework::Runner::Main(TInt /*aArgc*/, TChar* /*aArgv*/[], Net::InitialisationParams* aInitParams)
{
    Net::UpnpLibrary::InitialiseMinimal(aInitParams);
    TestPhaseAdjuster();
    delete aInitParams;
    Net::UpnpLibrary::Close();
}

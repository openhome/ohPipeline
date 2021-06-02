#include <OpenHome/Private/TestFramework.h>
extern void TestSongcastPhaseAdjuster();

void OpenHome::TestFramework::Runner::Main(TInt /*aArgc*/, TChar* /*aArgv*/[], Net::InitialisationParams* aInitParams)
{
    Net::UpnpLibrary::InitialiseMinimal(aInitParams);
    TestSongcastPhaseAdjuster();
    delete aInitParams;
    Net::UpnpLibrary::Close();
}

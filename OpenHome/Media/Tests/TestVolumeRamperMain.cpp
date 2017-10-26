#include <OpenHome/Private/TestFramework.h>

extern void TestVolumeRamper();

void OpenHome::TestFramework::Runner::Main(TInt /*aArgc*/, TChar* /*aArgv*/[], Net::InitialisationParams* aInitParams)
{
    Net::UpnpLibrary::InitialiseMinimal(aInitParams);
    TestVolumeRamper();
    delete aInitParams;
    Net::UpnpLibrary::Close();
}

#include <OpenHome/Private/TestFramework.h>

using namespace OpenHome;
using namespace OpenHome::TestFramework;

extern void TestObservable();

void OpenHome::TestFramework::Runner::Main(TInt /*aArgc*/, TChar* /*aArgv*/[], Net::InitialisationParams* aInitParams)
{
    (void) Net::UpnpLibrary::InitialiseMinimal(aInitParams);
    TestObservable();
    delete aInitParams;
    Net::UpnpLibrary::Close();
}

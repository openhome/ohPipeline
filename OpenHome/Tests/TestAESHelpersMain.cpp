#include <OpenHome/Private/TestFramework.h>

using namespace OpenHome;
using namespace OpenHome::TestFramework;

extern void TestAESHelpers();

void OpenHome::TestFramework::Runner::Main(TInt /*aArgc*/, TChar* /*aArgv*/[], Net::InitialisationParams* aInitParams)
{
    TestAESHelpers();
    delete aInitParams;
}

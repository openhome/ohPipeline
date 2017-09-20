#include <OpenHome/Private/TestFramework.h>

using namespace OpenHome;

extern void TestSocket(Environment& aEnv);

void OpenHome::TestFramework::Runner::Main(TInt /*aArgc*/, TChar* /*aArgv*/[], Net::InitialisationParams* aInitParams)
{
    Net::Library* lib = new Net::Library(aInitParams);
    TestSocket(lib->Env());
    delete lib;
}

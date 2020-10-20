#include <OpenHome/Types.h>
#include <OpenHome/Private/TestFramework.h>


using namespace OpenHome;
using namespace OpenHome::Net;

extern void TestOAuth(Environment& aEnv);


void OpenHome::TestFramework::Runner::Main(TInt /* aArgc */, TChar* /* aArgv */[], Net::InitialisationParams* aInitParams)
{
    Environment* env = Net::UpnpLibrary::Initialise(aInitParams);
    TestOAuth(*env);
    Net::UpnpLibrary::Close();
}

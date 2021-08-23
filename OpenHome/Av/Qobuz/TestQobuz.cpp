#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/OptionParser.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Av/Qobuz/Qobuz.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Configuration/Tests/ConfigRamStore.h>
#include <OpenHome/UnixTimestamp.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Media/PipelineObserver.h>

namespace OpenHome {
namespace Av {

class TestQobuz : private ICredentialsState
{
public:
    TestQobuz(Environment& aEnv, SslContext& aSsl, const Brx& aId, const Brx& aSecret, const Brx& aDeviceId);
    virtual ~TestQobuz();
    void Start(const Brx& aUsername, const Brx& aPassword);
    void Test();
private: // from ICredentialsState
    void SetState(const Brx& aId, const Brx& aStatus, const Brx& aData) override;
private:
    Configuration::ConfigRamStore* iStore;
    Configuration::ConfigManager* iConfigManager;
    UnixTimestamp* iUnixTimestamp;
    ThreadPool* iThreadPool;
    Qobuz* iQobuz;
    Media::NullPipelineObservable iPipelineObservable;
};

} // namespace Av
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Av;

TestQobuz::TestQobuz(Environment& aEnv, SslContext& aSsl, const Brx& aId, const Brx& aSecret, const Brx& aDeviceId)
{
    iStore = new Configuration::ConfigRamStore();
    iConfigManager = new Configuration::ConfigManager(*iStore);
    iUnixTimestamp = new UnixTimestamp(aEnv);
    iThreadPool = new ThreadPool(1, 1, 1);
    iQobuz = new Qobuz(aEnv, aSsl, aId, aSecret, aDeviceId, *this, *iConfigManager,
                       *iUnixTimestamp, *iThreadPool, iPipelineObservable);
}

TestQobuz::~TestQobuz()
{
    delete iQobuz;
    delete iThreadPool;
    delete iUnixTimestamp;
    delete iConfigManager;
    delete iStore;
}

void TestQobuz::Start(const Brx& aUsername, const Brx& aPassword)
{
    iQobuz->CredentialsChanged(aUsername, aPassword);
    //iQobuz->UpdateStatus();
}

void TestQobuz::Test()
{
    iQobuz->TryLoginLocked();
    static const TChar* kTracks[] = { "7343778"
                                    };

    const TUint numElems = sizeof(kTracks)/sizeof(kTracks[0]);
    Bws<1024> streamUrl;
    for (TUint i=0; i<numElems; i++) {
        Brn trackId(kTracks[i]);
        auto track = iQobuz->StreamableTrack(trackId);
        Log::Print("trackId %s returned url %.*s\n", kTracks[i], PBUF(track->Url()));
        delete track;
    }
}

void TestQobuz::SetState(const Brx& /*aId*/, const Brx& aStatus, const Brx& aData)
{
    Log::Print("SetState: aStatus = ");
    Log::Print(aStatus);
    Log::Print(", aData = ");
    Log::Print(aData);
    Log::Print("\n");
}



void OpenHome::TestFramework::Runner::Main(TInt aArgc, TChar* aArgv[], Net::InitialisationParams* aInitParams)
{
    Environment* env = Net::UpnpLibrary::Initialise(aInitParams);

    OptionParser parser;
    OptionString optionId("", "--id", Brn(""), "Qobuz app id");
    parser.AddOption(&optionId);
    OptionString optionSecret("", "--secret", Brn(""), "Qobuz app secret");
    parser.AddOption(&optionSecret);
    OptionString optionUsername("", "--username", Brn(""), "Username");
    parser.AddOption(&optionUsername);
    OptionString optionPassword("", "--password", Brn(""), "Password");
    parser.AddOption(&optionPassword);
    std::vector<Brn> args = OptionParser::ConvertArgs(aArgc, aArgv);
    if (!parser.Parse(args) || parser.HelpDisplayed()) {
        return;
    }

    Debug::SetLevel(Debug::kApplication6);
    Debug::SetSeverity(Debug::kSeverityError);

    SslContext* ssl = new SslContext();

    TestQobuz* qobuz = new TestQobuz(*env, *ssl, optionId.Value(), optionSecret.Value(), Brn("12345"));
    qobuz->Start(optionUsername.Value(), optionPassword.Value());
    qobuz->Test();
    delete qobuz;
    delete ssl;
    Net::UpnpLibrary::Close();
}

#include "TestMediaPlayer.h"
#include <OpenHome/Types.h>
#include <OpenHome/Media/Tests/Cdecl.h>
#include <OpenHome/Media/Tests/GetCh.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Media/Utils/AnimatorBasic.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Functor.h>

#include <stdlib.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Av::Test;
using namespace OpenHome::Net;

class TestMediaPlayerThread
{
public:
    TestMediaPlayerThread(const TestMediaPlayerOptions& aOptions);
    ~TestMediaPlayerThread();
    void Run();
private:
    void RunInThread();
private:
    const TestMediaPlayerOptions& iOptions;
    Library* iLib;
    Media::PriorityArbitratorAnimator iArbAnimator;
    Media::PriorityArbitratorPipeline iArbPipeline;
};

TestMediaPlayerThread::TestMediaPlayerThread(const TestMediaPlayerOptions& aOptions)
    : iOptions(aOptions)
    , iArbAnimator(kPrioritySystemHighest)
    , iArbPipeline(kPrioritySystemHighest-1)
{
    iLib = TestMediaPlayerInit::CreateLibrary(iOptions.Room().CString(), iOptions.Loopback().Value(), iOptions.Adapter().Value(), iOptions.Shell().Value());
    ThreadPriorityArbitrator& priorityArbitrator = iLib->Env().PriorityArbitrator();
    priorityArbitrator.Add(iArbAnimator);
    priorityArbitrator.Add(iArbPipeline);
}

TestMediaPlayerThread::~TestMediaPlayerThread()
{
    delete iLib;
}

void TestMediaPlayerThread::Run()
{
    ThreadFunctor* th = new ThreadFunctor("TestMediaPlayer", MakeFunctor(*this, &TestMediaPlayerThread::RunInThread));
    th->Start();
    delete th;
}

void TestMediaPlayerThread::RunInThread()
{
    const TChar* cookie ="TestMediaPlayerMain";
    NetworkAdapter* adapter = iLib->CurrentSubnetAdapter(cookie);
    CpStack* cpStack = nullptr;
    DvStack* dvStack = nullptr;
    iLib->StartCombined(adapter->Subnet(), cpStack, dvStack);

    // Seed random number generator.
    TestMediaPlayerInit::SeedRandomNumberGenerator(dvStack->Env(), iOptions.Room().Value(), adapter->Address(), dvStack->ServerUpnp());
    adapter->RemoveRef(cookie);

    // Set/construct UDN.
    Bwh udn;
    // Note: prefix udn with 4c494e4e- to get older versions of Linn Konfig to recognise our devices
    TestMediaPlayerInit::AppendUniqueId(dvStack->Env(), iOptions.Udn().Value(), Brn("TestMediaPlayer"), udn);
    Log::Print("UDN is %.*s\n", PBUF(udn));

    // Create TestMediaPlayer.
    TestMediaPlayer* tmp = new TestMediaPlayer(*dvStack, *cpStack, udn, iOptions.Room().CString(), iOptions.Name().CString(),
        iOptions.TuneIn().Value(), iOptions.Tidal().Value(), iOptions.Qobuz().Value(),
        iOptions.UserAgent().Value(), iOptions.StoreFile().CString(), iOptions.DashEnabled().Value(), iOptions.OptionOdp().Value(), iOptions.OptionWebUi().Value());
    Media::AnimatorBasic* animator = new Media::AnimatorBasic(
        dvStack->Env(),
        tmp->Pipeline(),
        iOptions.ClockPull().Value(),
        tmp->DsdMaxSampleRate(),
        tmp->DsdSampleBlockWords(),
        tmp->DsdPadBytesPerChunk());
    tmp->SetPullableClock(*animator);
    tmp->Run();
    tmp->StopPipeline();
    delete animator;
    delete tmp;
}

int CDECL main(int aArgc, char* aArgv[])
{
#ifdef _WIN32
    char* noErrDlgs = getenv("NO_ERROR_DIALOGS");
    if (noErrDlgs != nullptr && strcmp(noErrDlgs, "1") == 0) {
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    }
#endif // _WIN32

    // Parse options.
    TestMediaPlayerOptions options;
    if (!options.Parse(aArgc, aArgv)) {
        return 1;
    }

    TestMediaPlayerThread* th = new TestMediaPlayerThread(options);
    th->Run();
    delete th;

    return 0;
}

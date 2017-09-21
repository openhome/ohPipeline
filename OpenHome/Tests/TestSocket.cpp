#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/Env.h>
#include <OpenHome/Private/NetworkAdapterList.h>
#include <OpenHome/Private/SuiteUnitTest.h>
#include <OpenHome/Private/Thread.h>

namespace OpenHome {
namespace Test {

class SuiteSocketUdp : public TestFramework::SuiteUnitTest, public INonCopyable
{
public:
    SuiteSocketUdp(Environment& aEnv, TIpAddress aInterface);
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    void TestInterruptNoData();
    void SocketReadThread();
private:
    Environment& iEnv;
    const TIpAddress iInterface;
    SocketUdp* iSocket;
};

} // namespace Test
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Test;


// SuiteSocketUdp

SuiteSocketUdp::SuiteSocketUdp(Environment& aEnv, TIpAddress aInterface)
    : SuiteUnitTest("SuiteSocketUdp")
    , iEnv(aEnv)
    , iInterface(aInterface)
{
    AddTest(MakeFunctor(*this, &SuiteSocketUdp::TestInterruptNoData), "TestInterruptNoData");
}

void SuiteSocketUdp::Setup()
{
    iSocket = new SocketUdp(iEnv, /*port*/ 0, iInterface);
}

void SuiteSocketUdp::TearDown()
{
    delete iSocket;
}

void SuiteSocketUdp::TestInterruptNoData()
{
    ThreadFunctor* tf = new ThreadFunctor("SuiteSocketUdp", MakeFunctor(*this, &SuiteSocketUdp::SocketReadThread), ThreadPriority::kPriorityNormal);
    tf->Start();
    // Toggle interrupts many times in an attempt to cause final interrupt to get lost/ignored by socket.
    for (TUint i=0; i<10; i++) {
        iSocket->Interrupt(true);
        iSocket->Interrupt(false);
    }
    iSocket->Interrupt(true);
    Log::Print("Attempting to join thread following iSocket interrupt...\n");
    tf->Join();
    Log::Print("...successfully joined thread.\n");
    delete tf;
}

void SuiteSocketUdp::SocketReadThread()
{
    Bws<1500> buf;
    try {
        Log::Print("SuiteSocketUdp::SocketReadThread before iSocket->Receive()\n");
        (void)iSocket->Receive(buf);
        Log::Print("SuiteSocketUdp::SocketReadThread after iSocket->Receive()\n");
    }
    catch (NetworkError&) {
        Log::Print("SuiteSocketUdp::SocketReadThread caught NetworkError\n");
    }
}



void TestSocket(Environment& aEnv)
{
    NetworkAdapterList& nifList = aEnv.NetworkAdapterList();
    AutoNetworkAdapterRef ref(aEnv, "TestUdpServer");
    NetworkAdapter* current = ref.Adapter();

    // get current subnet, otherwise choose first from a list
    if (current == nullptr) {
        std::vector<NetworkAdapter*>* subnetList = nifList.CreateSubnetList();
        if (subnetList->size() > 0) {
            current = (*subnetList)[0];
        }
        NetworkAdapterList::DestroySubnetList(subnetList);
    }

    ASSERT(current != nullptr); // should probably never be the case, but tests would fail if it was.

    Runner runner("Socket tests");
    for (TUint i=0; i<1000; i++) {
        runner.Add(new SuiteSocketUdp(aEnv, current->Address()));
    }
    runner.Run();
}

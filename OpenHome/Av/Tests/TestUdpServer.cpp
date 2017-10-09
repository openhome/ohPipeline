#include <OpenHome/Av/Raop/UdpServer.h>
#include <OpenHome/Private/Env.h>
#include <OpenHome/Private/NetworkAdapterList.h>
#include <OpenHome/Private/SuiteUnitTest.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::TestFramework;


// SuiteMsgUdp
class SuiteMsgUdp : public SuiteUnitTest, public INonCopyable
{
public:
    SuiteMsgUdp(Environment& aEnv, TIpAddress aInterface);
private: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    void TestRead();
private:
    static const TUint kMaxMsgSize = 1000;
    Environment& iEnv;
    TIpAddress iInterface;
    MsgUdp* iMsg;
    SocketUdp* iSender;
    SocketUdp* iReceiver;
    Endpoint* iEndpoint;
};

SuiteMsgUdp::SuiteMsgUdp(Environment& aEnv, TIpAddress aInterface)
    : SuiteUnitTest("SuiteMsgUdp")
    , iEnv(aEnv)
    , iInterface(aInterface)
{
    AddTest(MakeFunctor(*this, &SuiteMsgUdp::TestRead), "TestRead");
}

void SuiteMsgUdp::Setup()
{
    iMsg = new MsgUdp(kMaxMsgSize);
    iSender = new SocketUdp(iEnv);
    iReceiver = new SocketUdp(iEnv);
    iEndpoint = new Endpoint(iReceiver->Port(), iInterface);
}

void SuiteMsgUdp::TearDown()
{
    delete iEndpoint;
    delete iReceiver;
    delete iSender;
    delete iMsg;
}

void SuiteMsgUdp::TestRead()
{
    // This also tests the Buffer() and GetEndpoint() methods of MsgUdp.
    Brn sendBuf("SuiteMsgUdp test buffer");
    iSender->Send(sendBuf, *iEndpoint);
    iMsg->Read(*iReceiver);

    Endpoint& ep = iMsg->Endpoint();
    TEST(ep.Port() == iSender->Port());
    TEST(ep.Address() == iInterface);
    TEST(iMsg->Buffer() == sendBuf);
}


// SuiteSocketUdpServer

/**
 * Note: this suite assumes all UDP packets are received reliably and in order
 * as the tests will typically be run over loopback. However, this somewhat
 * contravenes the UDP spec, so, if some tests fail due to packet loss/ordering
 * problems, it is likely to be down to poor assumptions in the tests, rather
 * than the classes being tested.
 */
class SuiteSocketUdpServer : public SuiteUnitTest, public INonCopyable
{
public:
    SuiteSocketUdpServer(Environment& aEnv, TIpAddress aInterface);
private: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    static void PrintBufInfo(const char* aMsg, Brx& aBuf);
    void GenerateNextMsg(Bwx& aBuf);
    void SendNextMsg(Bwx& aBuf);
    void CheckMsgValue(Brx& aBuf, TByte aVal);
    void TestOpen();
    void TestClose();
    void TestReopen();

    void TestMsgOrdering();
    void TestInterrupt();
    void TestReadFlush();
    void TestMsgsDisposedStart();
    void TestMsgsDisposed();
    void TestMsgsDisposedCapacityExceeded();

    void TestSend();
    void TestPort();
private:
    static const TUint kUdpRecvBufSize = 8192;
    // ensure (kMaxMsgSize+8)*kMaxMsgCount < kUdpRecvBufSize
    static const TUint kMaxMsgSize = 64;
    static const TUint kMaxMsgCount = 50;
    static const TUint kPort = 0;
    static const TUint kSendWaitMs = 3;
    static const TUint kSemWaitMs = 500;
    static const TUint kDisposedCount = 10;
    Environment& iEnv;
    TIpAddress iInterface;
    SocketUdp* iSender;
    SocketUdpServer* iServer;
    TByte iCurrentVal;
    Endpoint iEndpoint;
    Bws<kMaxMsgSize> iOutBuf;
    Bws<kMaxMsgSize> iInBuf;
    TByte iMsgCount;
};

SuiteSocketUdpServer::SuiteSocketUdpServer(Environment& aEnv, TIpAddress aInterface)
    : SuiteUnitTest("SuiteSocketUdpServer")
    , iEnv(aEnv)
    , iInterface(aInterface)
{
    AddTest(MakeFunctor(*this, &SuiteSocketUdpServer::TestOpen), "TestOpen");
    AddTest(MakeFunctor(*this, &SuiteSocketUdpServer::TestClose), "TestClose");
    AddTest(MakeFunctor(*this, &SuiteSocketUdpServer::TestReopen), "TestReopen");
    AddTest(MakeFunctor(*this, &SuiteSocketUdpServer::TestMsgOrdering), "TestMsgOrdering");
    AddTest(MakeFunctor(*this, &SuiteSocketUdpServer::TestReadFlush), "TestReadFlush");
    AddTest(MakeFunctor(*this, &SuiteSocketUdpServer::TestInterrupt), "TestInterrupt");
    AddTest(MakeFunctor(*this, &SuiteSocketUdpServer::TestMsgsDisposedStart), "TestMsgsDisposedStart");
    AddTest(MakeFunctor(*this, &SuiteSocketUdpServer::TestMsgsDisposed), "TestMsgsDisposed");
    AddTest(MakeFunctor(*this, &SuiteSocketUdpServer::TestMsgsDisposedCapacityExceeded), "TestMsgsDisposedCapacityExceeded");
    AddTest(MakeFunctor(*this, &SuiteSocketUdpServer::TestSend), "TestSend");
    AddTest(MakeFunctor(*this, &SuiteSocketUdpServer::TestPort), "TestPort");
}

void SuiteSocketUdpServer::Setup()
{
    iSender = new SocketUdp(iEnv);
    iServer = new SocketUdpServer(iEnv, kMaxMsgSize, kMaxMsgCount, ThreadPriority::kPriorityNormal, kPort, iInterface);
    try {
        iServer->SetRecvBufBytes(kUdpRecvBufSize);
    }
    catch (NetworkError&) {
        Log::Print("Failed to set UDP receive buffer size to %u bytes\n", kUdpRecvBufSize);
    }
    iCurrentVal = 0;
    Endpoint ep(iServer->Port(), iInterface);
    iEndpoint.Replace(ep);
    iOutBuf.SetBytes(0);
    iInBuf.SetBytes(0);
    iMsgCount = 0;
}

void SuiteSocketUdpServer::TearDown()
{
    delete iServer;
    delete iSender;
}

void SuiteSocketUdpServer::PrintBufInfo(const char* aMsg, Brx& aBuf)
{
    Log::Print("%s: aBuf[0]: %d, aBuf[aBuf.Bytes()-1]: %d, aBuf.Bytes(): %u\n", aMsg, aBuf[0], aBuf[aBuf.Bytes()-1], aBuf.Bytes());
}

void SuiteSocketUdpServer::GenerateNextMsg(Bwx& aBuf)
{
    ASSERT(aBuf.MaxBytes() == kMaxMsgSize);
    memset((void*)(aBuf.Ptr()), iCurrentVal++, aBuf.MaxBytes());
    aBuf.SetBytes(aBuf.MaxBytes());
}

void SuiteSocketUdpServer::SendNextMsg(Bwx& aBuf)
{
    GenerateNextMsg(aBuf);
    iSender->Send(aBuf, iEndpoint);
    // have an artificial sleep, giving receiver a chance to pull msgs
    Thread::Sleep(kSendWaitMs);
}

void SuiteSocketUdpServer::CheckMsgValue(Brx& aBuf, TByte aVal)
{
    TEST(aBuf.Bytes() == kMaxMsgSize);
    TEST(aBuf[0] == aVal);
    if (aBuf[0] != aVal) {
        Log::Print("SuiteSocketUdpServer::CheckMsgValue comparison failed. aBuf[0]: %d, aVal: %d\n", aBuf[0], aVal);
    }
    TEST(aBuf[aBuf.Bytes()-1] == aVal);
}

void SuiteSocketUdpServer::TestOpen()
{
    // test calls to Receive are allowed immediately after call to Open()
    iServer->Open();
    SendNextMsg(iOutBuf);
    iServer->Receive(iInBuf);
    CheckMsgValue(iInBuf, iMsgCount++);
}

void SuiteSocketUdpServer::TestClose()
{
    // test calls to Receive are not allowed when server is closed
    Bws<kMaxMsgSize> buf;
    iServer->Open();
    iServer->Close();
    TEST_THROWS(iServer->Receive(buf), UdpServerClosed);
}

void SuiteSocketUdpServer::TestReopen()
{
    // test server can be successfully closed and re-opened
    iServer->Open();
    SendNextMsg(iOutBuf);
    iServer->Receive(iInBuf);
    CheckMsgValue(iInBuf, iMsgCount++);

    iServer->Close();
    iServer->Open();

    SendNextMsg(iOutBuf);
    iServer->Receive(iInBuf);
    CheckMsgValue(iInBuf, iMsgCount++);
}

void SuiteSocketUdpServer::TestMsgOrdering()
{
    // test msgs are read in correct order (when sent in a synchronised manner)
    iServer->Open();
    for (TUint i=0; i<kMaxMsgCount; i++) {
        SendNextMsg(iOutBuf);
        iServer->Receive(iInBuf);
        CheckMsgValue(iInBuf, iMsgCount++);
    }
}

void SuiteSocketUdpServer::TestReadFlush()
{
    // test that a read flush does nothing to the msg stream
    iServer->Open();
    for (TUint i=0; i<kMaxMsgCount; i++) {
        SendNextMsg(iOutBuf);
        iServer->Receive(iInBuf);
        CheckMsgValue(iInBuf, iMsgCount++);
    }
}

void SuiteSocketUdpServer::TestInterrupt()
{
    // interrupt server while it should be waiting on reading udp packet and try resume
    iServer->Open();

    // Interrupt server before any read, then try read.
    iServer->Interrupt(true);
    TEST_THROWS(iServer->Receive(iInBuf), NetworkError);
    // Further reads should result in exception until interrupt cleared.
    TEST_THROWS(iServer->Receive(iInBuf), NetworkError);
    iServer->Interrupt(false);

    // Now, send some data in.
    for (TUint i=0; i<kMaxMsgCount; i++) {
        SendNextMsg(iOutBuf);
        iServer->Receive(iInBuf);
        CheckMsgValue(iInBuf, iMsgCount++);
    }

    iServer->Interrupt(true);
    TEST_THROWS(iServer->Receive(iInBuf), NetworkError);
    iServer->Interrupt(false);

    // Now that interrupt has been cleared, read more packets.
    for (TUint i=0; i<kMaxMsgCount; i++) {
        SendNextMsg(iOutBuf);
        iServer->Receive(iInBuf);
        CheckMsgValue(iInBuf, iMsgCount++);
    }
}

void SuiteSocketUdpServer::TestMsgsDisposedStart()
{
    // test msgs are disposed of when server is closed from start and re-opened
    for (TUint i=0; i<kDisposedCount; i++) {
        SendNextMsg(iOutBuf);
    }

    iMsgCount += kDisposedCount;
    iServer->Open();

    const TUint totalMsgCount = iMsgCount + 10;
    TUint notDisposed = 0;
    for (;iMsgCount<totalMsgCount;) {
        SendNextMsg(iOutBuf);
        iServer->Receive(iInBuf);
        // Due to timing issues, server may have been re-opened and received one or more msgs intended to be dropped - dispose of these. This is because the server thread may not have had a chance to consume and discard all those messages prior to being re-opened.




        // FIXME - a way to address this would be to provide a mock IUdpSocket interface, and the implementation of that interface for test purposes would allow us to wait until Receive() had been called on it for all queued messages. At that point, we would know that server has consumed all messages, and that we can definitely test that none of those messages are pulled through when server re-opened.




        if (iInBuf[0] < iMsgCount) { // rcvd a message queued up after Close() was called.
            notDisposed++;
            continue;
        }
        // should definitely not receive any of first set of msgs, so if
        // ordering problems occur here and not down to network issues,
        // suggests msg queue wasn't cleared
        CheckMsgValue(iInBuf, iMsgCount++);
    }
    TEST(notDisposed <= kDisposedCount);
}

void SuiteSocketUdpServer::TestMsgsDisposed()
{
    // test msgs are disposed of when server is closed part-way through execution, then re-opened
    iServer->Open();
    for (TUint i=0; i<10; i++) {
        SendNextMsg(iOutBuf);
        iServer->Receive(iInBuf);
        CheckMsgValue(iInBuf, iMsgCount++);
    }

    // Send some messages that will not be read, and will be dropped when server closed.
    for (TUint i=0; i<kDisposedCount; i++) {
        SendNextMsg(iOutBuf);
    }
    iMsgCount += kDisposedCount;

    iServer->Close();
    for (TUint i=0; i<kDisposedCount; i++) {
        SendNextMsg(iOutBuf);
    }

    iMsgCount += kDisposedCount;
    iServer->Open();

    const TUint totalMsgCount = iMsgCount + 10;
    TUint notDisposed = 0;
    for (;iMsgCount<totalMsgCount;) {
        SendNextMsg(iOutBuf);
        iServer->Receive(iInBuf);
        // Due to timing issues, server may have been re-opened and received one or more msgs intended to be dropped - dispose of these. This is because the server thread may not have had a chance to consume and discard all those messages prior to being re-opened.
        if (iInBuf[0] < iMsgCount) { // rcvd a message queued up after Close() was called.
            notDisposed++;
            continue;
        }
        CheckMsgValue(iInBuf, iMsgCount++);
    }
    // Only possible undiposed messages received should have been those queued up after Close() was called. (i.e., kDisposedCount at most).
    TEST(notDisposed <= kDisposedCount);
}

void SuiteSocketUdpServer::TestMsgsDisposedCapacityExceeded()
{
    // test msgs are disposed of when server capacity exceeded
    iServer->Open();
    for (TUint i=0; i<kMaxMsgCount+kDisposedCount; i++) {
        SendNextMsg(iOutBuf);
    }

    for (TUint i=0; i<kMaxMsgCount; i++) {
        iServer->Receive(iInBuf);
        CheckMsgValue(iInBuf, iMsgCount++);
    }

    iMsgCount += kDisposedCount;

    TUint totalMsgCount = iMsgCount + 10;
    for (;iMsgCount<totalMsgCount;) {
        SendNextMsg(iOutBuf);
        iServer->Receive(iInBuf);
        CheckMsgValue(iInBuf, iMsgCount++);
    }
}

void SuiteSocketUdpServer::TestSend()
{
    // Switch roles of iSender and iServer only for this test.

    iServer->Open();
    Endpoint senderEp(iSender->Port(), iInterface);

    // packet 1
    GenerateNextMsg(iOutBuf);
    iServer->Send(iOutBuf, senderEp);
    iSender->Receive(iInBuf);
    CheckMsgValue(iInBuf, iMsgCount++);

    // packet 2
    GenerateNextMsg(iOutBuf);
    iServer->Send(iOutBuf, senderEp);
    iSender->Receive(iInBuf);
    CheckMsgValue(iInBuf, iMsgCount++);

    // packet 3
    GenerateNextMsg(iOutBuf);
    iServer->Send(iOutBuf, senderEp);
    iSender->Receive(iInBuf);
    CheckMsgValue(iInBuf, iMsgCount++);
}

void SuiteSocketUdpServer::TestPort()
{
    // Send packet from iServer to iSender; verify port value against that.

    iServer->Open();
    Endpoint senderEp(iSender->Port(), iInterface);

    GenerateNextMsg(iOutBuf);
    iServer->Send(iOutBuf, senderEp);
    Endpoint ep = iSender->Receive(iInBuf);

    TEST(iServer->Port() == ep.Port());
}

//void SuiteSocketUdpServer::TestSubnetChanged()
//{
//    // test that attempting to change the subnet adapter succeeds.
//    // there is no way to verify the server has changed subnet, so this test
//    // running to completion without crashing is the best that can be done
//    iServer->Open();
//    NetworkAdapterList& nifList = iEnv.NetworkAdapterList();
//    TUint newSubnet = 0; // dummy subnet
//    nifList.SetCurrentSubnet(newSubnet);
//}


// SuiteUdpServerManager

class SuiteUdpServerManager : public SuiteUnitTest, public INonCopyable
{
public:
    SuiteUdpServerManager(Environment& aEnv, TIpAddress aInterface);
private: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private:
    void TestEmpty();
    void TestCreateOneServer();
    void TestCreateMultipleServers();
    void TestFindInvalidId();
private:
    static const TUint kMaxMsgSize = 1000;
    static const TUint kMaxMsgCount = 100;
    static const TUint kPort = 0;
    Environment& iEnv;
    TIpAddress iInterface;
    UdpServerManager* iManager;
};

SuiteUdpServerManager::SuiteUdpServerManager(Environment& aEnv, TIpAddress aInterface)
    : SuiteUnitTest("SuiteUdpServerManager")
    , iEnv(aEnv)
    , iInterface(aInterface)
{
    AddTest(MakeFunctor(*this, &SuiteUdpServerManager::TestEmpty), "TestEmpty");
    AddTest(MakeFunctor(*this, &SuiteUdpServerManager::TestCreateOneServer), "TestCreateOneServer");
    AddTest(MakeFunctor(*this, &SuiteUdpServerManager::TestCreateMultipleServers), "TestCreateMultipleServers");
    AddTest(MakeFunctor(*this, &SuiteUdpServerManager::TestFindInvalidId), "TestFindInvalidId");
}

void SuiteUdpServerManager::Setup()
{
    iManager = new UdpServerManager(iEnv, kMaxMsgSize, kMaxMsgCount, ThreadPriority::kPriorityNormal);
}

void SuiteUdpServerManager::TearDown()
{
    delete iManager;
}

void SuiteUdpServerManager::TestEmpty()
{
    // test if the server manager has no servers when initially created
    // - ids should start at 0
    TEST_THROWS(iManager->Find(0), AssertionFailed);
}

void SuiteUdpServerManager::TestCreateOneServer()
{
    // test we can ask the manager to create a server and that it is then
    // findable. there is nothing explicitly testable for this - successful
    // completion of the test signifies correct operation
    TUint id = iManager->CreateServer(kPort, iInterface);
    iManager->Find(id);
}

void SuiteUdpServerManager::TestCreateMultipleServers()
{
    // test that we can ask the manager to create multiple servers and that
    // they remain accessible via the same ids (i.e., ensure there are no
    // problems such as servers being discarded or re-ordered)

    // create the servers (and record their port as we go, as this is a unique
    // enough identifier)
    TUint id1 = iManager->CreateServer(kPort, iInterface);
    TUint port1 = iManager->Find(id1).Port();
    TUint id2 = iManager->CreateServer(kPort, iInterface);
    TUint port2 = iManager->Find(id2).Port();
    TUint id3 = iManager->CreateServer(kPort, iInterface);
    TUint port3 = iManager->Find(id3).Port();

    // retrieve all the servers, checking the ids and ports map correctly
    TEST(iManager->Find(id1).Port() == port1);
    TEST(iManager->Find(id2).Port() == port2);
    TEST(iManager->Find(id3).Port() == port3);
}

void SuiteUdpServerManager::TestFindInvalidId()
{
    // test that trying to access an invalid id causes an assert
    TUint id = iManager->CreateServer(kPort, iInterface);
    TEST_THROWS(iManager->Find(id+1), AssertionFailed);
}



void TestUdpServer(Environment& aEnv)
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

    ASSERT(current != nullptr); // should probably never be the case, but tests would fail if it was

    Runner runner("UdpServer tests");
    runner.Add(new SuiteMsgUdp(aEnv, current->Address()));
    runner.Add(new SuiteSocketUdpServer(aEnv, current->Address()));
    runner.Add(new SuiteUdpServerManager(aEnv, current->Address()));
    runner.Run();
}

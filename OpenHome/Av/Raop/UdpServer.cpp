#include <OpenHome/Av/Raop/UdpServer.h>
#include <OpenHome/Private/Env.h>
#include <OpenHome/Private/NetworkAdapterList.h>
#include <OpenHome/Media/Debug.h>

using namespace OpenHome;
using namespace Av;


// MsgUdp

MsgUdp::MsgUdp(TUint aMaxSize)
    : iBuf(aMaxSize)
{
}

MsgUdp::~MsgUdp()
{
}

void MsgUdp::Read(SocketUdp& aSocket)
{
    iEndpoint.Replace(aSocket.Receive(iBuf));
}

const Brx& MsgUdp::Buffer()
{
    return iBuf;
}

Endpoint& MsgUdp::Endpoint()
{
    return iEndpoint;
}


// SocketUdpServer

const TChar* SocketUdpServer::kAdapterCookie = "SocketUdpServer";

SocketUdpServer::SocketUdpServer(Environment& aEnv, TUint aMaxSize, TUint aMaxPackets, TUint aThreadPriority, TUint aPort, TIpAddress aInterface)
    : iEnv(aEnv)
    , iSocket(aEnv, aPort, aInterface)
    , iMaxSize(aMaxSize)
    , iOpen(false)
    , iFifoWaiting(aMaxPackets)
    , iFifoReady(aMaxPackets)
    , iLock("UDPL")
    , iLockFifo("UDPF")
    , iSemRead("UDPR", 0)
    , iInterrupted(false)
    , iQuit(false)
    , iAdapterListenerId(0)
    , iRebindPosted(false)
{
    // Populate iFifoWaiting with empty packets/bufs
    while (iFifoWaiting.SlotsFree() > 0) {
        iFifoWaiting.Write(new MsgUdp(iMaxSize));
    }

    iDiscard = new MsgUdp(iMaxSize);

    iServerThread = new ThreadFunctor("UdpServer", MakeFunctor(*this, &SocketUdpServer::ServerThread), aThreadPriority);
    iServerThread->Start();

    Functor functor = MakeFunctor(*this, &SocketUdpServer::CurrentAdapterChanged);
    NetworkAdapterList& nifList = iEnv.NetworkAdapterList();
    iAdapterListenerId = nifList.AddCurrentChangeListener(functor, "SocketUdpServer", false);
}

SocketUdpServer::~SocketUdpServer()
{
    NetworkAdapterList& nifList = iEnv.NetworkAdapterList();
    nifList.RemoveCurrentChangeListener(iAdapterListenerId);

    {
        AutoMutex _a(iLock);
        iOpen = false; // Ensure that if server hasn't been Close()d, thread won't try to place message into queue after socket interrupt below.
        iQuit = true;
    }

    iSocket.Interrupt(true);
    // Can't hold iLock here, as server thread needs to acquire iLock to check iQuit value following iSocket.Interrupt().
    iServerThread->Join();
    delete iServerThread;
    iSocket.Close();

    AutoMutex _b(iLockFifo);
    while (iFifoReady.SlotsUsed() > 0) {
        MsgUdp* msg = iFifoReady.Read();
        delete msg;
    }
    while (iFifoWaiting.SlotsUsed() > 0) {
        MsgUdp* msg = iFifoWaiting.Read();
        delete msg;
    }
    delete iDiscard;
}

void SocketUdpServer::Open()
{
    LOG(kMedia, "SocketUdpServer::Open\n");
    {
        AutoMutex _(iLock);
        iOpen = true;
    }

    // Server starts in a closed state, where it is waiting on packets to discard.
    // Must interrupt socket to get out of that state.
    iSocket.Interrupt(true);
    iSocket.Interrupt(false);
}

void SocketUdpServer::Close()
{
    LOG(kMedia, "SocketUdpServer::Close\n");
    AutoMutex _a(iLock);
    iOpen = false;

    // Terminate any current read on server thread.
    iSocket.Interrupt(true);

    AutoMutex _b(iLockFifo);
    // Move all messages from ready queue back to waiting queue.
    // It's fine to clear iFifoReady.ReadInterrupt() state here, as any
    // call to Read() will result in a UdpServerClosed due to iOpen == false.
    while (iFifoReady.SlotsUsed() > 0) {
        MsgUdp* msg = iFifoReady.Read();
        iFifoWaiting.Write(msg);
    }

    iSocket.Interrupt(false);
}

void SocketUdpServer::Send(const Brx& aBuffer, const Endpoint& aEndpoint)
{
    iSocket.Send(aBuffer, aEndpoint);
}

TUint SocketUdpServer::Port() const
{
    return iSocket.Port();
}

void SocketUdpServer::Interrupt(TBool aInterrupt)
{
    // Clients only read from iFifoReady, so only need interrupt that.
    // Want to continue reading from iSocket and buffering packets in background.
    AutoMutex _(iLock);
    iInterrupted = aInterrupt;
    if (aInterrupt) {
        iSemRead.Signal();
    }
}

void SocketUdpServer::SetSendBufBytes(TUint aBytes)
{
    iSocket.SetSendBufBytes(aBytes);
}

void SocketUdpServer::SetRecvBufBytes(TUint aBytes)
{
    iSocket.SetRecvBufBytes(aBytes);
}

void SocketUdpServer::SetRecvTimeout(TUint aMs)
{
    iSocket.SetRecvTimeout(aMs);
}

void SocketUdpServer::SetTtl(TUint aTtl)
{
    iSocket.SetTtl(aTtl);
}

Endpoint SocketUdpServer::Receive(Bwx& aBuf)
{
    {
        AutoMutex _(iLock);
        if (iQuit) {
            ASSERTS();
        }
        if (!iOpen) {
            THROW(UdpServerClosed);
        }
        // Explicitly check if iInterrupted was previously set.
        // Otherwise, could block in here if a previous Receive() call already picked up the iSemRead.Signal() from ::Interrupt().
        if (iInterrupted) {
            THROW(NetworkError);
        }
    }

    // Use for loop to consume extra iSemRead signals when message not available (e.g., Interrupt() was called many times).
    for (;;) {
        iSemRead.Wait();
        {
            AutoMutex _(iLock);
            if (iInterrupted) {
                THROW(NetworkError);
            }
        }

        AutoMutex _(iLockFifo);
        if (iFifoReady.SlotsUsed() > 0) {
            // Get data from msg.
            MsgUdp* msg = iFifoReady.Read();
            Endpoint ep;
            CopyMsgToBuf(*msg, aBuf, ep);
            ASSERT(iFifoWaiting.SlotsUsed() < iFifoWaiting.Slots());
            iFifoWaiting.Write(msg);
            return ep;
        }
        else {
            continue;
        }
    }
}

void SocketUdpServer::CopyMsgToBuf(MsgUdp& aMsg, Bwx& aBuf, Endpoint& aEndpoint)
{
    const Brx& buf = aMsg.Buffer();
    ASSERT(aBuf.MaxBytes() >= buf.Bytes());
    aBuf.Replace(buf);
    aEndpoint.Replace(aMsg.Endpoint());
}

void SocketUdpServer::ServerThread()
{
    for (;;) {
        {
            AutoMutex _(iLock);
            if (iQuit) {
                return;
            }
        }

        try {
            iDiscard->Read(iSocket);
        }
        catch (NetworkError&) {
            // This thread will become a busy loop if network operations
            // repeatedly fail. Sleep here to give other threads a chance
            // to run in that scenario.
            Thread::Sleep(50);
            CheckRebind();
            continue;
        }

        AutoMutex _a(iLock);
        if (iOpen) {
            AutoMutex _b(iLockFifo);
            if (iFifoWaiting.SlotsUsed() == 0) {
                // No more packets to read into.
                // Drop this packet and reuse iDiscard to read next packet.
                continue;
            }

            ASSERT(iFifoReady.SlotsUsed() < iFifoReady.Slots());
            iFifoReady.Write(iDiscard);
            iDiscard = iFifoWaiting.Read();
            iSemRead.Signal();
        }
    }
}

void SocketUdpServer::PostRebind(TIpAddress aAddress, TUint aPort, Functor aCompleteFunctor)
{
    AutoMutex amx(iLock);

    iRebindJob.iAddress         = aAddress;
    iRebindJob.iPort            = aPort;
    iRebindJob.iCompleteFunctor = aCompleteFunctor;

    iRebindPosted = true;
    iSocket.Interrupt(true);
}

void SocketUdpServer::CheckRebind()
{
    AutoMutex amx(iLock);

    if (iRebindPosted)
    {
        iSocket.ReBind(iRebindJob.iPort, iRebindJob.iAddress);
        iRebindPosted = false;
        iRebindJob.iCompleteFunctor(); // we have to call this with iLock held. Should be ok unless the
    }                                  // functor tries to take the lock: We have control of this, so it's cool.
}

void SocketUdpServer::CurrentAdapterChanged()
{
    NetworkAdapterList& nifList = iEnv.NetworkAdapterList();
    NetworkAdapter* current = iEnv.NetworkAdapterList().CurrentAdapter(kAdapterCookie);

    // Get current subnet, otherwise choose first from a list
    if (current == nullptr) {
        std::vector<NetworkAdapter*>* subnetList = nifList.CreateSubnetList();
        if (subnetList->size() > 0) {
            current = (*subnetList)[0];
            current->AddRef(kAdapterCookie);
        }
        NetworkAdapterList::DestroySubnetList(subnetList);
    }

    // Only rebind if we have something to rebind to.
    if (current != nullptr) {
        Semaphore waiter("", 0);
        PostRebind(current->Address(), iSocket.Port(), MakeFunctor(waiter, &Semaphore::Signal));
        waiter.Wait();
        iSocket.Interrupt(false);

        // Finished with current now, so remove ref.
        current->RemoveRef(kAdapterCookie);
    }
}


// UdpServerManager

UdpServerManager::UdpServerManager(Environment& aEnv, TUint aMaxSize, TUint aMaxPackets, TUint aThreadPriority)
    : iEnv(aEnv)
    , iMaxSize(aMaxSize)
    , iMaxPackets(aMaxPackets)
    , iThreadPriority(aThreadPriority)
    , iLock("USML")
{
}

UdpServerManager::~UdpServerManager()
{
    AutoMutex a(iLock);
    for (size_t i=0; i<iServers.size(); i++) {
        delete iServers[i];
    }
}

TUint UdpServerManager::CreateServer(TUint aPort, TIpAddress aInterface)
{
    AutoMutex a(iLock);
    SocketUdpServer* server = new SocketUdpServer(iEnv, iMaxSize, iMaxPackets, iThreadPriority, aPort, aInterface);
    iServers.push_back(server);
    return iServers.size()-1;
}

SocketUdpServer& UdpServerManager::Find(TUint aId)
{
    AutoMutex a(iLock);
    ASSERT(aId < iServers.size());
    return *(iServers[aId]);
}

void UdpServerManager::CloseAll()
{
    AutoMutex a(iLock);
    for (size_t i=0; i<iServers.size(); i++) {
        iServers[i]->Close();
    }
}

void UdpServerManager::OpenAll()
{
    AutoMutex a(iLock);
    for (size_t i=0; i<iServers.size(); i++) {
        iServers[i]->Open();
    }
}

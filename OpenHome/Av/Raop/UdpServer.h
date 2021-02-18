#pragma once

#include <OpenHome/Private/Fifo.h>
#include <OpenHome/Private/Network.h>

EXCEPTION(UdpServerClosed);

namespace OpenHome {
namespace Av {

/**
 * Storage class for the output of a UdpSocketBase::Receive call
 */
class MsgUdp
{
public:
    MsgUdp(TUint aMaxSize);
    ~MsgUdp();
    void Read(SocketUdp& aSocket);
    const Brx& Buffer();
    OpenHome::Endpoint& Endpoint();
private:
    Bwh iBuf;
    OpenHome::Endpoint iEndpoint;
};

/**
 * Class for a continuously running server which buffers packets while active
 * and discards packets when deactivated
 */
class SocketUdpServer
{
private:
    static const TChar* kAdapterCookie;
public:
    SocketUdpServer(Environment& aEnv, TUint aMaxSize, TUint aMaxPackets, TUint aThreadPriority, TUint aPort, TIpAddress aInterface);
    ~SocketUdpServer();
    void Open();
    void Close();

    void Send(const Brx& aBuffer, const Endpoint& aEndpoint);
    TUint Port() const;
    void Interrupt(TBool aInterrupt);

    void SetSendBufBytes(TUint aBytes);
    void SetRecvBufBytes(TUint aBytes);
    void SetRecvTimeout(TUint aMs);
    void SetTtl(TUint aTtl);
    
    Endpoint Receive(Bwx& aBuf);
private:
    static void CopyMsgToBuf(MsgUdp& aMsg, Bwx& aBuf, Endpoint& aEndpoint);
    void ServerThread();
    void CurrentAdapterChanged();
    struct RebindJob {
        TIpAddress iAddress;
        TUint iPort;
        Functor iCompleteFunctor;
    };
    void PostRebind(TIpAddress aAddress, TUint aPort, Functor aCompleteFunctor);
    void CheckRebind();
private:
    Environment& iEnv;
    SocketUdp iSocket;
    TUint iMaxSize;
    TBool iOpen;
    FifoLiteDynamic<MsgUdp*> iFifoWaiting;
    FifoLiteDynamic<MsgUdp*> iFifoReady;
    MsgUdp* iDiscard;
    mutable Mutex iLock;
    Mutex iLockFifo;
    Semaphore iSemRead;
    ThreadFunctor* iServerThread;
    TBool iInterrupted;
    TBool iQuit;
    TUint iAdapterListenerId;
    TBool iRebindPosted;
    RebindJob iRebindJob;
};

/**
 * Class for managing a collection of SocketUdpServers. UdpServerManager owns
 * all the SocketUdpServers contained within it.
 */
class UdpServerManager
{
public:
    UdpServerManager(Environment& aEnv, TUint aMaxSize, TUint aMaxPackets, TUint aThreadPriority);
    ~UdpServerManager();
    TUint CreateServer(TUint aPort = 0, TIpAddress aInterface = kTIpAddressEmpty); // return ID of server
    SocketUdpServer& Find(TUint aId); // find server by ID
    void CloseAll();
    void OpenAll();
private:
    std::vector<SocketUdpServer*> iServers;
    Environment& iEnv;
    const TUint iMaxSize;
    const TUint iMaxPackets;
    const TUint iThreadPriority;
    Mutex iLock;
};

} // namespace Av
} // namespace OpenHome


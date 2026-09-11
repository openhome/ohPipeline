#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Env.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Thread.h>

#include <qobuz_connect.h>

#include <memory>

namespace OpenHome {
namespace Av {

/**
 * What QobuzConnectAdvertising needs to know about the local config server in order to
 * advertise it correctly - kept as a narrow interface so Advertising.h doesn't need to
 * depend on the whole of LocalConfigServer.h.
 */
class IQobuzConnectLocalConfigServerInfo
{
public:
    virtual ~IQobuzConnectLocalConfigServerInfo() {}
    virtual TUint Port() const = 0;
    virtual const Brx& PathTail() const = 0; // e.g. "/qobuz" - value of the mDNS "path" TXT record
};

/**
 * Lets QobuzConnectAdvertising find out when the local config server's port is actually known.
 * The SDK gives no guarantee about the order start_advertising_callback and
 * start_local_config_server_callback fire in - if advertising registers first, it would
 * otherwise publish port 0 (IQobuzConnectLocalConfigServerInfo::Port()'s default before
 * HandleStart() binds the listening socket), which is invalid for mDNS service discovery.
 */
class IQobuzConnectLocalConfigServerObserver
{
public:
    virtual ~IQobuzConnectLocalConfigServerObserver() {}
    virtual void QobuzLocalConfigServerStarted() = 0;
};

/**
 * Implements the SDK's LocalConfigServer delegate: a small hand-rolled HTTP server exposing
 * the three endpoints the SDK's pairing flow requires (see SDK README section 4.1):
 *   GET  <path>/get-display-info
 *   GET  <path>/get-connect-info
 *   POST <path>/connect-to-qconnect
 *
 * There's no generic REST/JSON HTTP server elsewhere in this codebase to build on (checked:
 * ArtworkHttpServer is GET-only/single-resource, WebAppFramework is hardcoded to the
 * Konfig/ConfigUi long-poll protocol) - this mirrors ArtworkHttpServer's approach of building
 * directly on SocketTcpServer/SocketTcpSession/ReaderHttpRequest/WriterHttpResponse, extended
 * with POST + a JSON body reader (via ReaderHttpEntity, the same building block
 * DviServerUpnp.cpp uses for SOAP POST bodies) and cJSON for parsing/building JSON (cJSON is
 * already a linked dependency here - see LIB_QOBUZCONNECT in wscript).
 *
 * v1 simplification: the server binds to the network adapter that's current when
 * start_local_config_server_callback fires, and does not react to later adapter changes
 * (contrast with e.g. DeviceAnnouncerMdns/ArtworkHttpServer, which do) - a reconnect on
 * adapter change would need re-registering with QobuzConnectAdvertising too, which the SDK's
 * start/stop advertising callbacks don't obviously support re-triggering on demand.
 */
class QobuzConnectLocalConfigServer : public IQobuzConnectLocalConfigServerInfo
{
public:
    QobuzConnectLocalConfigServer(
        Environment& aEnv,
        const Brx& aAppId,
        const Brx& aFriendlyName,
        const Brx& aManufacturer,
        const Brx& aModel,
        const Brx& aSerialNumber,
        QbzDeviceType aDeviceType,
        QbzAudioQuality aMaxAudioQuality);
    ~QobuzConnectLocalConfigServer();
public:
    QbzLocalConfigServerDelegate Delegate();
    void SetCore(QbzConnectCore* aCore);
    void SetObserver(IQobuzConnectLocalConfigServerObserver& aObserver);
public: // from IQobuzConnectLocalConfigServerInfo
    TUint Port() const override;
    const Brx& PathTail() const override;
public: // called by QobuzConnectLocalConfigSession
    void WriteDisplayInfoJson(IWriter& aWriter);
    void WriteConnectInfoJson(IWriter& aWriter);
    TBool HandleConnectToQConnect(const Brx& aBody); // parses aBody and calls qbz_connect_connect(); returns false on malformed JSON/fields
public:
    // Called by the extern "C" trampoline functions in LocalConfigServer.cpp (which aren't
    // members or friends of this class, so these must be public, not private).
    static void StartServerCb(QbzConnectCore* aCore, void* aUserData);
    static void StopServerCb(QbzConnectCore* aCore, void* aUserData);
private:
    void HandleStart();
    void HandleStop();
private:
    Environment& iEnv;
    Bws<64> iAppId;
    Bws<64> iFriendlyName;
    Bws<64> iManufacturer;
    Bws<64> iModel;
    Bws<64> iSerialNumber;
    QbzDeviceType iDeviceType;
    QbzAudioQuality iMaxAudioQuality;
    Mutex iLock;
    QbzConnectCore* iCore;
    std::unique_ptr<SocketTcpServer> iServer;
    TUint iPort;
    IQobuzConnectLocalConfigServerObserver* iObserver;
    static const Brn kPathTail;
};

class QobuzConnectLocalConfigSession : public SocketTcpSession
{
public:
    QobuzConnectLocalConfigSession(Environment& aEnv, QobuzConnectLocalConfigServer& aServer);
    ~QobuzConnectLocalConfigSession();
private: // from SocketTcpSession
    void Run() override;
private:
    QobuzConnectLocalConfigServer& iServer;
    Srx* iReadBuffer;
    ReaderUntil* iReaderUntil;
    ReaderHttpRequest* iReaderRequest;
    Swx* iWriterBuffer;
    WriterHttpResponse* iWriterResponse;
    HttpHeaderContentLength iHeaderContentLength;
    HttpHeaderTransferEncoding iHeaderTransferEncoding;
    ReaderHttpEntity* iReaderEntity;
};

}
}

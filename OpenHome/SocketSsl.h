#pragma once

#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Standard.h>

namespace OpenHome {

class Environment;
class SocketSslImpl;
class SslImpl;

class SslContext
{
    friend class SocketSslImpl;
public:
    SslContext();
    ~SslContext();
private:
    SslImpl* iImpl;
};

class SocketSsl : public IWriter, public IReaderSource
{
public:
    SocketSsl(Environment& aEnv, SslContext& aSsl, TUint aReadBytes);
    ~SocketSsl();
    void SetSecure(TBool aSecure);
    void Connect(const Endpoint& aEndpoint, TUint aTimeoutMs);
    /*
     * Allows use of Server Name Indication if hostname is specified.
     */
    void Connect(const Endpoint& aEndpoint, const Brx& aHostname, TUint aTimeoutMs);
    void Close();
    void Interrupt(TBool aInterrupt);
    void LogVerbose(TBool aVerbose);
    TBool IsConnected() const;
public: // from IWriter
    void Write(TByte aValue) override;
    void Write(const Brx& aBuffer) override;
    void WriteFlush() override;
public: // from IReaderSource
    void Read(Bwx& aBuffer) override;
    void ReadFlush() override;
    void ReadInterrupt() override;
private:
    SocketSslImpl* iImpl;
};

class AutoSocketSsl : private INonCopyable
{
public:
    AutoSocketSsl(SocketSsl& aSocket);
    ~AutoSocketSsl();
private:
    SocketSsl& iSocket;
};

} // namespace OpenHome


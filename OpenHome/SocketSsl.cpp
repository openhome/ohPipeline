#include <OpenHome/SocketSsl.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Env.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Debug-ohMediaPlayer.h>

#include "openssl/bio.h"
#include "openssl/ssl.h"
#include "openssl/err.h"
#include "openssl/engine.h"

#include <stdlib.h>

namespace OpenHome {

class SslImpl
{
public:
    SslImpl();
    ~SslImpl();
public:
    SSL_CTX* iCtx;
};

class SocketSslImpl : public IWriter, public IReaderSource
{
    static const TUint kDefaultHostNameBytes = 128;
public:
    SocketSslImpl(Environment& aEnv, SslContext& aSsl, TUint aReadBytes);
    ~SocketSslImpl();
    void SetSecure(TBool aSecure);
    void Connect(const Endpoint& aEndpoint, TUint aTimeoutMs);
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
    static int  BioCreate(BIO* b);
    static int  BioDestroy(BIO* b);
    static int  BioWrite(BIO* b, const char* in, int inl);
    static int  BioRead(BIO* b, char* out, int outl);
    static long BioCtrl(BIO* b, int cmd, long num, void* ptr);
    static BIO_METHOD* BioMethod();
private:
    Environment& iEnv;
    SocketTcpClient iSocketTcp;
    SSL_CTX* iCtx;
    SSL* iSsl;
    TBool iSecure;
    TBool iConnected;
    TBool iVerbose;
    Bwh iHostname;
};

} // namespace OpenHome


using namespace OpenHome;



// SslContext

static TBool sslInitialised = false; // Note - use of this is not thread-safe
                                     // It is just a rough check for multiple instantiations of a singleton

SslContext::SslContext()
{
    ASSERT(!sslInitialised);
    iImpl = new SslImpl();
}

SslContext::~SslContext()
{
    delete iImpl;
    sslInitialised = false;
}


// SslImpl

SslImpl::SslImpl()
{
    SSL_library_init();
    SSL_load_error_strings();
    ERR_load_BIO_strings();
    OpenSSL_add_all_algorithms();
    iCtx = SSL_CTX_new(TLSv1_2_client_method());
    SSL_CTX_set_verify(iCtx, SSL_VERIFY_NONE, nullptr);
}

SslImpl::~SslImpl()
{
    SSL_CTX_free(iCtx);
    iCtx = nullptr;
    CRYPTO_cleanup_all_ex_data();
    ERR_free_strings();
    ERR_remove_state(0);
    ENGINE_cleanup();
    EVP_cleanup();
}


// SocketSsl

SocketSsl::SocketSsl(Environment& aEnv, SslContext& aSsl, TUint aReadBytes)
{
    iImpl = new SocketSslImpl(aEnv, aSsl, aReadBytes);
}

SocketSsl::~SocketSsl()
{
    delete iImpl;
}

void SocketSsl::SetSecure(TBool aSecure)
{
    iImpl->SetSecure(aSecure);
}

void SocketSsl::ConnectNoSni(const Endpoint& aEndpoint, TUint aTimeoutMs)
{
    iImpl->Connect(aEndpoint, aTimeoutMs);
}

void SocketSsl::Connect(const Endpoint& aEndpoint, const Brx& aHostname, TUint aTimeoutMs)
{
    iImpl->Connect(aEndpoint, aHostname, aTimeoutMs);
}

void SocketSsl::Close()
{
    iImpl->Close();
}

void SocketSsl::Interrupt(TBool aInterrupt)
{
    iImpl->Interrupt(aInterrupt);
}

void SocketSsl::LogVerbose(TBool aVerbose)
{
    iImpl->LogVerbose(aVerbose);
}

TBool SocketSsl::IsConnected() const
{
    return iImpl->IsConnected();
}

void SocketSsl::Write(TByte aValue)
{
    iImpl->Write(aValue);
}

void SocketSsl::Write(const Brx& aBuffer)
{
    iImpl->Write(aBuffer);
}

void SocketSsl::WriteFlush()
{
    iImpl->WriteFlush();
}

void SocketSsl::Read(Bwx& aBuffer)
{
    iImpl->Read(aBuffer);
}

void SocketSsl::ReadFlush()
{
    iImpl->ReadFlush();
}

void SocketSsl::ReadInterrupt()
{
    iImpl->ReadInterrupt();
}


// SocketSslImpl

#define SSL_WHERE_INFO(ssl, flag, logIfSet, msg) {                       \
    if(flag & logIfSet) {                                                \
      LOG(kSsl, "%20.20s - %30.30s  - %5.10s\n",                         \
                msg, SSL_state_string_long(ssl), SSL_state_string(ssl)); \
    }                                                                    \
  } 

static void SslInfoCallback(const SSL* ssl, int flag, int ret)
{
    if(ret == 0) {
        LOG(kSsl, "-- SslInfoCallback: error occured.\n");
    }
    SSL_WHERE_INFO(ssl, flag, SSL_CB_LOOP, "LOOP");
    SSL_WHERE_INFO(ssl, flag, SSL_CB_HANDSHAKE_START, "HANDSHAKE START");
    SSL_WHERE_INFO(ssl, flag, SSL_CB_HANDSHAKE_DONE, "HANDSHAKE DONE");
}

SocketSslImpl::SocketSslImpl(Environment& aEnv, SslContext& aSsl, TUint aReadBytes)
    : iEnv(aEnv)
    , iCtx(aSsl.iImpl->iCtx)
    , iSsl(nullptr)
    , iSecure(true)
    , iConnected(false)
    , iVerbose(false)
    , iHostname(kDefaultHostNameBytes)
{
    (void)aReadBytes; // no longer used; reads now stream directly from the TCP socket
}

SocketSslImpl::~SocketSslImpl()
{
    try {
        Close();
    }
    catch (NetworkError&) {
    }
}

void SocketSslImpl::SetSecure(TBool aSecure)
{
    iSecure = aSecure;
}

void SocketSslImpl::Connect(const Endpoint& aEndpoint, TUint aTimeoutMs)
{
    Connect(aEndpoint, Brx::Empty(), aTimeoutMs);
}

void SocketSslImpl::Connect(const Endpoint& aEndpoint, const Brx& aHostname, TUint aTimeoutMs)
{
    iSocketTcp.Open(iEnv);
    try {
        iSocketTcp.Connect(aEndpoint, aTimeoutMs);
    }
    catch (NetworkError&) {
        iSocketTcp.Close();
        throw;
    }
    catch (NetworkTimeout&) {
        iSocketTcp.Close();
        throw;
    }
    if (iSecure) {
        ASSERT(iSsl == nullptr);
        iSsl = SSL_new(iCtx);
        SSL_set_info_callback(iSsl, SslInfoCallback);
        BIO* bio = BIO_new(BioMethod());
        ASSERT(bio != nullptr);
        BIO_set_data(bio, this);
        // Single source/sink BIO drives both directions; SSL_set_bio frees it once on SSL_free.
        SSL_set_bio(iSsl, bio, bio);
        SSL_set_connect_state(iSsl);
        SSL_set_mode(iSsl, SSL_MODE_AUTO_RETRY);

        // Use "Server Name Indication" if hostname is specified.
        if (aHostname.Bytes() > 0) {
            const TUint cStringBytes = aHostname.Bytes() + 1; // +1 for '\0'.
            if (cStringBytes > iHostname.MaxBytes()) {
                iHostname.Grow(cStringBytes);
            }
            iHostname.Replace(aHostname);
            SSL_set_tlsext_host_name(iSsl, iHostname.PtrZ());
        }

        if (1 != SSL_connect(iSsl)) {
            SSL_free(iSsl);
            iSsl = nullptr;
            iSocketTcp.Close();
            THROW(NetworkError);
        }
    }
    iConnected = true;
}

void SocketSslImpl::Close()
{
    if (!iConnected) {
        ASSERT(iSsl == nullptr);
    }
    else {
        if (iSsl != nullptr) {
            (void)SSL_shutdown(iSsl);
            SSL_free(iSsl);
            iSsl = nullptr;
        }
        iConnected = false;
        iHostname.SetBytes(0);
        try {
            iSocketTcp.Close();
        }
        catch (NetworkError&) {}
    }
}

void SocketSslImpl::Interrupt(TBool aInterrupt)
{
    iSocketTcp.Interrupt(aInterrupt);
}

void SocketSslImpl::LogVerbose(TBool aVerbose)
{
    iVerbose = aVerbose;
}

TBool SocketSslImpl::IsConnected() const
{
    return iConnected;
}

void SocketSslImpl::Write(TByte aValue)
{
    Brn buf(&aValue, 1);
    Write(buf);
}

void SocketSslImpl::Write(const Brx& aBuffer)
{
    if (iVerbose) {
        LOG(kEssential, "SocketSsl writing\n");
        LOG(kEssential, aBuffer);
        LOG(kEssential, "\n");
    }
    if (iSecure) {
        const int bytes = (int)aBuffer.Bytes();
        if (bytes != SSL_write(iSsl, aBuffer.Ptr(), bytes)) {
            THROW(WriterError);
        }
    }
    else {
        iSocketTcp.Write(aBuffer);
    }
}

void SocketSslImpl::WriteFlush()
{
    iSocketTcp.WriteFlush();
}

void SocketSslImpl::Read(Bwx& aBuffer)
{
    if (iSecure) {
        int bytes = SSL_read(iSsl, ((void*)(aBuffer.Ptr() + aBuffer.Bytes())), aBuffer.MaxBytes() - aBuffer.Bytes());
        if (bytes <= 0) {
            LOG_ERROR(kSsl, "SSL_read returned %d, SSL_get_error()=%d\n", bytes, SSL_get_error(iSsl, bytes));
            THROW(ReaderError);
        }
        aBuffer.SetBytes(aBuffer.Bytes() + bytes);
    }
    else {
        iSocketTcp.Read(aBuffer);
    }
    if (iVerbose) {
        LOG(kEssential, "SocketSsl reading\n");
        LOG(kEssential, aBuffer);
        LOG(kEssential, "\n");
    }
}

void SocketSslImpl::ReadFlush()
{
    iSocketTcp.ReadFlush();
}

void SocketSslImpl::ReadInterrupt()
{
    iSocketTcp.ReadInterrupt();
}

int SocketSslImpl::BioCreate(BIO* b)
{ // static
    BIO_set_init(b, 1);
    return 1;
}

int SocketSslImpl::BioDestroy(BIO* /*b*/)
{ // static
    // SocketSslImpl owns the TCP socket; nothing per-BIO to release.
    return 1;
}

int SocketSslImpl::BioWrite(BIO* b, const char* in, int inl)
{ // static
    SocketSslImpl* self = static_cast<SocketSslImpl*>(BIO_get_data(b));
    BIO_clear_retry_flags(b);
    if (inl <= 0) {
        return 0;
    }
    Brn buf(reinterpret_cast<const TByte*>(in), (TUint)inl);
    try {
        self->iSocketTcp.Write(buf);
        return inl;
    }
    catch (AssertionFailed&) {
        throw;
    }
    catch (Exception& ex) {
        LOG_ERROR(kSsl, "%s thrown \n", ex.Message());
        return -1;
    }
    catch (...) {
        ASSERTS();
        return -1;
    }
}

int SocketSslImpl::BioRead(BIO* b, char* out, int outl)
{ // static
    SocketSslImpl* self = static_cast<SocketSslImpl*>(BIO_get_data(b));
    BIO_clear_retry_flags(b);
    if (outl <= 0) {
        return 0;
    }
    try {
        Bwn buf(reinterpret_cast<TByte*>(out), (TUint)outl);
        self->iSocketTcp.Read(buf);
        // 0 here means the peer closed; libssl treats that as EOF.
        return (int)buf.Bytes();
    }
    catch (AssertionFailed&) {
        throw;
    }
    catch (Exception& ex) {
        LOG_ERROR(kSsl, "%s thrown \n", ex.Message());
        return 0; // signal EOF to libssl
    }
    catch (...) {
        ASSERTS();
        return -1;
    }
}

long SocketSslImpl::BioCtrl(BIO* /*b*/, int cmd, long /*num*/, void* /*ptr*/)
{ // static
    switch (cmd) {
    case BIO_CTRL_FLUSH:
        return 1; // nothing buffered, so flush is a no-op success
    default:
        return 0;
    }
}

BIO_METHOD* SocketSslImpl::BioMethod()
{ // static
    // Initialised on first use; leaked at process exit, matching libssl's own built-in methods.
    // C++11 guarantees thread-safe initialisation of function-local statics.
    static BIO_METHOD* method = []() {
        BIO_METHOD* m = BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK, "OpenHome SocketSsl");
        ASSERT(m != nullptr);
        BIO_meth_set_create(m, &SocketSslImpl::BioCreate);
        BIO_meth_set_destroy(m, &SocketSslImpl::BioDestroy);
        BIO_meth_set_write(m, &SocketSslImpl::BioWrite);
        BIO_meth_set_read(m, &SocketSslImpl::BioRead);
        BIO_meth_set_ctrl(m, &SocketSslImpl::BioCtrl);
        return m;
    }();
    return method;
}


// AutoSocketSsl

AutoSocketSsl::AutoSocketSsl(SocketSsl& aSocket)
    : iSocket(aSocket)
{
}

AutoSocketSsl::~AutoSocketSsl()
{
    iSocket.Close();
}

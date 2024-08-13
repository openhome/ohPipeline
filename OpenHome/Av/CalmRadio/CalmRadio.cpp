#include <OpenHome/Av/CalmRadio/CalmRadio.h>
#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Json.h>
#include <OpenHome/SocketSsl.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Uri.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Av;

// CalmRadio

const Brn CalmRadio::kHost("api.calmradio.com");
const Brn CalmRadio::kId("calmradio.com");

CalmRadio::CalmRadio(Environment& aEnv, SslContext& aSsl, ICredentialsState& aCredentialsState, const Brx& aUserAgent)
    : iLock("CRD1")
    , iLockConfig("CRD2")
    , iCredentialsState(aCredentialsState)
    , iUserAgent(aUserAgent)
    , iSocket(aEnv, aSsl, kReadBufferBytes)
    , iReaderBuf(iSocket)
    , iReaderUntil(iReaderBuf)
    , iWriterBuf(iSocket)
    , iWriterRequest(iWriterBuf)
    , iReaderResponse(aEnv, iReaderUntil)
    , iReaderEntity(iReaderUntil)
    , iUsername(kGranularity)
    , iPassword(kGranularity)
    , iToken(kGranularity)
{
    iReaderResponse.AddHeader(iHeaderContentLength);
    iReaderResponse.AddHeader(iHeaderTransferEncoding);

    //iSocket.LogVerbose(true);
}

CalmRadio::~CalmRadio()
{
}

void CalmRadio::Interrupt(TBool aInterrupt)
{
    iSocket.Interrupt(aInterrupt);
}

void CalmRadio::GetStreamUrl(Bwx& aUrlBase)
{
    AutoMutex _(iLock);
    const Brx& token = iToken.Buffer();
    if (token.Bytes() == 0) {
        THROW(CalmRadioNoToken);
    }
    aUrlBase.Append("?user=");
    {
        AutoMutex __(iLockConfig);
        Uri::EscapeDataString(aUrlBase, iUsername.Buffer());
    }
    aUrlBase.Append("&pass=");
    Uri::EscapeDataString(aUrlBase, token);
}

void CalmRadio::ForceLogin()
{
    TryLoginLocked();
}

const Brx& CalmRadio::Id() const
{
    return kId;
}

void CalmRadio::CredentialsChanged(const Brx& aUsername, const Brx& aPassword)
{
    AutoMutex _(iLockConfig);
    iUsername.Reset();
    iUsername.Write(aUsername);
    iPassword.Reset();
    iPassword.Write(aPassword);
}

void CalmRadio::UpdateStatus()
{
    AutoMutex _(iLock);
    iLockConfig.Wait();
    const TBool noCredentials = (iUsername.Buffer().Bytes() == 0 && iPassword.Buffer().Bytes() == 0);
    iLockConfig.Signal();
    if (noCredentials) {
        iCredentialsState.SetState(kId, Brx::Empty(), Brx::Empty());
    }
    else {
        (void)TryLoginLocked();
    }
}

void CalmRadio::Login(Bwx& aToken)
{
    AutoMutex _(iLock);
    const Brx& token = iToken.Buffer();
    if (token.Bytes() > 0) {
        aToken.Replace(token);
    }
    else {
        if (!TryLoginLocked(aToken)) {
            THROW(CredentialsLoginFailed);
        }
    }
}

void CalmRadio::ReLogin(const Brx& aCurrentToken, Bwx& aNewToken)
{
    AutoMutex _(iLock);
    if (iToken.Buffer() == aCurrentToken) {
        if (!TryLoginLocked(aNewToken)) {
            THROW(CredentialsLoginFailed);
        }
    }
}

TBool CalmRadio::TryLoginLocked(Bwx& aToken)
{
    if (TryLoginLocked()) {
        aToken.Replace(iToken.Buffer());
        return true;
    }
    aToken.Replace(Brx::Empty());
    return false;
}

TBool CalmRadio::TryLoginLocked()
{
    iToken.Reset();

    Endpoint ep;
    try {
        ep.SetAddress(kHost);
        ep.SetPort(kPort);
        iSocket.Connect(ep, kHost, kConnectTimeoutMs);
    }
    catch (NetworkTimeout&) {
        LOG_ERROR(kPipeline, "CalmRadio::TryLoginLocked - connection failure\n");
        iCredentialsState.SetState(kId, Brn("Login Error (Connection Failed): Please Try Again."), Brx::Empty());
        return false;
    }
    catch (NetworkError&) {
        LOG_ERROR(kPipeline, "CalmRadio::TryLoginLocked - connection failure\n");
        iCredentialsState.SetState(kId, Brn("Login Error (Connection Failed): Please Try Again."), Brx::Empty());
        return false;
    }

    TBool updatedStatus = false;
    Bws<80> error;
    TBool success = false;
    {
        AutoSocketSsl _(iSocket);
        Bws<128> pathAndQuery("/get_token?user=");
        {
            AutoMutex __(iLockConfig);
            Uri::EscapeDataString(pathAndQuery, iUsername.Buffer());
            pathAndQuery.Append("&pass=");
            Uri::EscapeDataString(pathAndQuery, iPassword.Buffer());
        }
        try {
            iWriterRequest.WriteMethod(Http::kMethodGet, pathAndQuery, Http::eHttp11);
            Http::WriteHeaderHostAndPort(iWriterRequest, kHost, kPort);
            if (iUserAgent.Bytes() > 0) {
                iWriterRequest.WriteHeader(Http::kHeaderUserAgent, iUserAgent);
            }
            Http::WriteHeaderContentLength(iWriterRequest, 0);
            Http::WriteHeaderConnectionClose(iWriterRequest);
            iWriterRequest.WriteFlush();

            iReaderResponse.Read();

            const TUint code = iReaderResponse.Status().Code();

            iLoginResp.Replace(Brx::Empty());
            WriterBuffer writer(iLoginResp);

            iReaderEntity.ReadAll(writer,
                                  iHeaderContentLength,
                                  iHeaderTransferEncoding,
                                  ReaderHttpEntity::Mode::Client);

            if (code != 200) {
                if (iLoginResp.Bytes() > 0)
                {
                    iCredentialsState.SetState(kId, iLoginResp, Brx::Empty());
                }
                else
                {
                    error.AppendPrintf("Login Error (Response Code %d): Please Try Again.", code);
                    iCredentialsState.SetState(kId, error, Brx::Empty());
                }
                updatedStatus = true;
                LOG(kPipeline, "Http error - %d - in response to CalmRadio login.  Some/all of response is:\n%.*s\n", code, PBUF(iLoginResp));
                THROW(ReaderError);
            }

            iCredentialsState.SetState(kId, Brx::Empty(), iLoginResp);
            /* don't note that we've updatedStatus yet - if parsing
               iLoginResp fails, we'll reset state to report the error */
            JsonParser parser;
            parser.ParseAndUnescape(iLoginResp);
            iToken.Write(parser.String("token"));
            updatedStatus = true;
            success = true;
        }
        catch (AssertionFailed&) {
            throw;
        }
        catch (ReaderError&) {
            if (error.Bytes() == 0) {
                error.Append("Login Error (Read Failure): Please Try Again.");
            }
            LOG_ERROR(kPipeline, "ReaderError in CalmRadio::TryLoginLocked\n");
        }
        catch (Exception& ex) {
            error.AppendPrintf("Login Error. Please Try Again.", ex.Message());
            LOG_ERROR(kPipeline, "%s in CalmRadio::TryLoginLocked\n", ex.Message());
        }
    }

    if (!updatedStatus) {
        iCredentialsState.SetState(kId, error, Brx::Empty());
    }
    return success;
}

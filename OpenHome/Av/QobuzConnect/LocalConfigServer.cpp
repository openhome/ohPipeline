#include <OpenHome/Av/QobuzConnect/LocalConfigServer.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/NetworkAdapterList.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Debug-ohMediaPlayer.h>

#include <cjson/cJSON.h>

using namespace OpenHome;
using namespace OpenHome::Av;

namespace {
    const TChar* kAdapterCookie = "QobuzConnectLocalConfigServer";

    // Parses a QbzApiEndpointInfo ({endpoint, jwt, exp}) out of aParent[aKey]. Returns false
    // (leaving *aOut untouched) if the field is missing or malformed. Strings pointed to by
    // the returned QbzApiEndpointInfo are only valid while aParent (the parsed cJSON document)
    // is still alive.
    TBool ParseEndpointInfo(cJSON* aParent, const TChar* aKey, QbzApiEndpointInfo* aOut)
    {
        cJSON* obj = cJSON_GetObjectItemCaseSensitive(aParent, aKey);
        if (!cJSON_IsObject(obj)) {
            return false;
        }
        cJSON* endpoint = cJSON_GetObjectItemCaseSensitive(obj, "endpoint");
        cJSON* jwt = cJSON_GetObjectItemCaseSensitive(obj, "jwt");
        cJSON* exp = cJSON_GetObjectItemCaseSensitive(obj, "exp");
        if (!cJSON_IsString(endpoint) || !cJSON_IsString(jwt) || !cJSON_IsNumber(exp)) {
            return false;
        }
        aOut->endpoint = endpoint->valuestring;
        aOut->jwt = jwt->valuestring;
        aOut->exp = (uint64_t)exp->valuedouble;
        return true;
    }
}

// QobuzConnectLocalConfigServer

const Brn QobuzConnectLocalConfigServer::kPathTail("/qobuz");

extern "C" {

static void QobuzConnectLocalConfigServer_StartServer(QbzConnectCore* aCore, void* aUserData)
{
    OpenHome::Av::QobuzConnectLocalConfigServer::StartServerCb(aCore, aUserData);
}

static void QobuzConnectLocalConfigServer_StopServer(QbzConnectCore* aCore, void* aUserData)
{
    OpenHome::Av::QobuzConnectLocalConfigServer::StopServerCb(aCore, aUserData);
}

} // extern "C"

QobuzConnectLocalConfigServer::QobuzConnectLocalConfigServer(
    Environment& aEnv,
    const Brx& aAppId,
    const Brx& aFriendlyName,
    const Brx& aManufacturer,
    const Brx& aModel,
    const Brx& aSerialNumber,
    QbzDeviceType aDeviceType,
    QbzAudioQuality aMaxAudioQuality)
    : iEnv(aEnv)
    , iAppId(aAppId)
    , iFriendlyName(aFriendlyName)
    , iManufacturer(aManufacturer)
    , iModel(aModel)
    , iSerialNumber(aSerialNumber)
    , iDeviceType(aDeviceType)
    , iMaxAudioQuality(aMaxAudioQuality)
    , iLock("QCLC")
    , iCore(nullptr)
    , iPort(0)
    , iObserver(nullptr)
{
}

QobuzConnectLocalConfigServer::~QobuzConnectLocalConfigServer()
{
    AutoMutex _(iLock);
    iServer.reset();
}

QbzLocalConfigServerDelegate QobuzConnectLocalConfigServer::Delegate()
{
    QbzLocalConfigServerDelegate delegate;
    delegate.user_data = this;
    delegate.start_local_config_server_callback = &QobuzConnectLocalConfigServer_StartServer;
    delegate.stop_local_config_server_callback = &QobuzConnectLocalConfigServer_StopServer;
    return delegate;
}

void QobuzConnectLocalConfigServer::SetCore(QbzConnectCore* aCore)
{
    AutoMutex _(iLock);
    iCore = aCore;
}

void QobuzConnectLocalConfigServer::SetObserver(IQobuzConnectLocalConfigServerObserver& aObserver)
{
    AutoMutex _(iLock);
    iObserver = &aObserver;
}

TUint QobuzConnectLocalConfigServer::Port() const
{
    return iPort;
}

const Brx& QobuzConnectLocalConfigServer::PathTail() const
{
    return kPathTail;
}

void QobuzConnectLocalConfigServer::StartServerCb(QbzConnectCore* /*aCore*/, void* aUserData)
{
    reinterpret_cast<QobuzConnectLocalConfigServer*>(aUserData)->HandleStart();
}

void QobuzConnectLocalConfigServer::StopServerCb(QbzConnectCore* /*aCore*/, void* aUserData)
{
    reinterpret_cast<QobuzConnectLocalConfigServer*>(aUserData)->HandleStop();
}

void QobuzConnectLocalConfigServer::HandleStart()
{
    LOG(kQobuzConnect, "QobuzConnectLocalConfigServer::HandleStart()\n");
    IQobuzConnectLocalConfigServerObserver* observer = nullptr;
    {
        AutoMutex _(iLock);

        NetworkAdapter* current = iEnv.NetworkAdapterList().CurrentAdapter(kAdapterCookie).Ptr();
        if (current == nullptr) {
            auto* subnetList = iEnv.NetworkAdapterList().CreateSubnetList();
            if (subnetList->size() > 0) {
                current = (*subnetList)[0];
                current->AddRef(kAdapterCookie);
            }
            NetworkAdapterList::DestroySubnetList(subnetList);
        }
        if (current == nullptr) {
            LOG(kQobuzConnect, "QobuzConnectLocalConfigServer: no network adapter available, can't start\n");
            return;
        }

        iServer.reset(new SocketTcpServer(iEnv, "QobuzConnectLocalConfig", 0, current->Address()));
        iServer->Add("QobuzConnectLocalConfigSession", new QobuzConnectLocalConfigSession(iEnv, *this));
        iPort = iServer->Port();
        current->RemoveRef(kAdapterCookie);

        LOG(kQobuzConnect, "QobuzConnectLocalConfigServer: listening on port %u\n", iPort);
        observer = iObserver;
    }
    // Notify outside iLock: QobuzConnectAdvertising::QobuzLocalConfigServerStarted() re-reads
    // Port() (harmless re-entrant call, no lock needed for that) and takes its own lock to
    // re-register - advertising may have already registered with the port still at its default
    // of 0 if start_advertising_callback fired first (the SDK gives no ordering guarantee
    // between the two start callbacks).
    if (observer != nullptr) {
        observer->QobuzLocalConfigServerStarted();
    }
}

void QobuzConnectLocalConfigServer::HandleStop()
{
    LOG(kQobuzConnect, "QobuzConnectLocalConfigServer::HandleStop()\n");
    AutoMutex _(iLock);
    iServer.reset();
    iPort = 0;
}

void QobuzConnectLocalConfigServer::WriteDisplayInfoJson(IWriter& aWriter)
{
    AutoMutex _(iLock);
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "friendly_name", iFriendlyName.Bytes() > 0 ? Brhz(iFriendlyName).CString() : "");
    cJSON_AddStringToObject(root, "serial_number", Brhz(iSerialNumber).CString());
    cJSON_AddStringToObject(root, "brand_display_name", Brhz(iManufacturer).CString());
    cJSON_AddStringToObject(root, "model_display_name", Brhz(iModel).CString());
    cJSON_AddStringToObject(root, "max_audio_quality", qbz_audio_quality_to_string(iMaxAudioQuality));
    cJSON_AddStringToObject(root, "type", qbz_device_type_to_string(iDeviceType));

    char* json = cJSON_PrintUnformatted(root);
    aWriter.Write(Brn(json));
    cJSON_free(json);
    cJSON_Delete(root);
}

void QobuzConnectLocalConfigServer::WriteConnectInfoJson(IWriter& aWriter)
{
    QbzConnectCore* core;
    {
        AutoMutex _(iLock);
        core = iCore;
    }

    Brn sessionId;
    if (core != nullptr) {
        const char* sid = nullptr;
        if (qbz_connect_get_session_id(core, &sid) == QBZ_ERROR_OK && sid != nullptr) {
            sessionId.Set((const TByte*)sid, (TUint)strlen(sid));
        }
    }

    AutoMutex _(iLock);
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "app_id", Brhz(iAppId).CString());
    cJSON_AddStringToObject(root, "current_session_id", sessionId.Bytes() > 0 ? Brhz(sessionId).CString() : "");

    char* json = cJSON_PrintUnformatted(root);
    aWriter.Write(Brn(json));
    cJSON_free(json);
    cJSON_Delete(root);
}

TBool QobuzConnectLocalConfigServer::HandleConnectToQConnect(const Brx& aBody)
{
    QbzConnectCore* core;
    {
        AutoMutex _(iLock);
        core = iCore;
    }
    if (core == nullptr) {
        LOG(kQobuzConnect, "QobuzConnectLocalConfigServer::HandleConnectToQConnect: no core yet\n");
        return false;
    }

    cJSON* root = cJSON_ParseWithLength((const char*)aBody.Ptr(), aBody.Bytes());
    if (root == nullptr) {
        LOG(kQobuzConnect, "QobuzConnectLocalConfigServer::HandleConnectToQConnect: malformed JSON\n");
        return false;
    }

    TBool ok = false;
    cJSON* sessionId = cJSON_GetObjectItemCaseSensitive(root, "session_id");
    cJSON* becomeActive = cJSON_GetObjectItemCaseSensitive(root, "become_active");
    QbzApiEndpointInfo jwtApi = { nullptr, nullptr, 0 };
    QbzApiEndpointInfo jwtQConnect = { nullptr, nullptr, 0 };
    if (cJSON_IsString(sessionId) && cJSON_IsBool(becomeActive)
        && ParseEndpointInfo(root, "jwt_api", &jwtApi)
        && ParseEndpointInfo(root, "jwt_qconnect", &jwtQConnect)) {
        const QbzError error = qbz_connect_connect(
            core,
            sessionId->valuestring,
            jwtApi,
            jwtQConnect,
            cJSON_IsTrue(becomeActive));
        if (error != QBZ_ERROR_OK) {
            LOG(kQobuzConnect, "QobuzConnectLocalConfigServer::HandleConnectToQConnect: qbz_connect_connect failed: %s\n", qbz_error_to_string(error));
        }
        else {
            LOG(kQobuzConnect, "QobuzConnectLocalConfigServer::HandleConnectToQConnect: qbz_connect_connect OK, become_active=%u\n", cJSON_IsTrue(becomeActive) ? 1u : 0u);
            ok = true;
        }
    }
    else {
        LOG(kQobuzConnect, "QobuzConnectLocalConfigServer::HandleConnectToQConnect: missing/invalid fields\n");
    }

    cJSON_Delete(root);
    return ok;
}


// QobuzConnectLocalConfigSession

QobuzConnectLocalConfigSession::QobuzConnectLocalConfigSession(Environment& aEnv, QobuzConnectLocalConfigServer& aServer)
    : iServer(aServer)
{
    iReadBuffer = new Srs<1024>(*this);
    iReaderUntil = new ReaderUntilS<4096>(*iReadBuffer);
    iReaderRequest = new ReaderHttpRequest(aEnv, *iReaderUntil);
    iWriterBuffer = new Sws<4096>(*this);
    iWriterResponse = new WriterHttpResponse(*iWriterBuffer);
    iReaderEntity = new ReaderHttpEntity(*iReaderUntil);

    iReaderRequest->AddMethod(Http::kMethodGet);
    iReaderRequest->AddMethod(Http::kMethodPost);
    iReaderRequest->AddHeader(iHeaderContentLength);
    iReaderRequest->AddHeader(iHeaderTransferEncoding);
}

QobuzConnectLocalConfigSession::~QobuzConnectLocalConfigSession()
{
    Interrupt(true);
    delete iReaderEntity;
    delete iWriterResponse;
    delete iWriterBuffer;
    delete iReaderRequest;
    delete iReaderUntil;
    delete iReadBuffer;
}

void QobuzConnectLocalConfigSession::Run()
{
    const HttpStatus* status = &HttpStatus::kOk;
    Bws<2048> body;
    TBool writeJsonBody = false;
    try {
        try {
            iReaderRequest->Read();
        }
        catch (HttpError&) {
            status = &HttpStatus::kBadRequest;
            THROW(HttpError);
        }
        catch (ReaderError&) {
            status = &HttpStatus::kBadRequest;
            THROW(HttpError);
        }
        if (iReaderRequest->MethodNotAllowed()) {
            status = &HttpStatus::kMethodNotAllowed;
            THROW(HttpError);
        }

        const Brx& uri = iReaderRequest->Uri();
        const Brx& method = iReaderRequest->Method();
        Bws<64> path(iServer.PathTail());

        {
            if (method == Http::kMethodGet) {
                Bws<64> displayInfoPath(path);
                displayInfoPath.Append("/get-display-info");
                Bws<64> connectInfoPath(path);
                connectInfoPath.Append("/get-connect-info");

                WriterBuffer writer(body);
                if (uri == displayInfoPath) {
                    iServer.WriteDisplayInfoJson(writer);
                    writeJsonBody = true;
                }
                else if (uri == connectInfoPath) {
                    iServer.WriteConnectInfoJson(writer);
                    writeJsonBody = true;
                }
                else {
                    status = &HttpStatus::kNotFound;
                    THROW(HttpError);
                }
            }
            else { // POST
                Bws<64> connectPath(path);
                connectPath.Append("/connect-to-qconnect");
                if (uri != connectPath) {
                    status = &HttpStatus::kNotFound;
                    THROW(HttpError);
                }

                try {
                    iReaderEntity->Set(iHeaderContentLength, iHeaderTransferEncoding, ReaderHttpEntity::Server);
                    WriterBuffer writer(body);
                    iReaderEntity->ReadAll(writer);
                }
                catch (WriterError&) {
                    status = &HttpStatus::kRequestEntityTooLarge;
                    THROW(HttpError);
                }

                if (!iServer.HandleConnectToQConnect(body)) {
                    status = &HttpStatus::kBadRequest;
                    THROW(HttpError);
                }
                body.SetBytes(0); // 200 OK, empty body
            }

            try {
                iWriterResponse->WriteStatus(*status, Http::eHttp11);
                if (writeJsonBody) {
                    Http::WriteHeaderContentType(*iWriterResponse, Brn("application/json"));
                }
                Http::WriteHeaderContentLength(*iWriterResponse, body.Bytes());
                Http::WriteHeaderConnectionClose(*iWriterResponse);
                iWriterResponse->WriteFlush();
                if (body.Bytes() > 0) {
                    iWriterBuffer->Write(body);
                }
                iWriterBuffer->WriteFlush();
            }
            catch (WriterError&) {}
        }
    }
    catch (HttpError&) {
        try {
            iWriterResponse->WriteStatus(*status, Http::eHttp11);
            Http::WriteHeaderConnectionClose(*iWriterResponse);
            iWriterResponse->WriteFlush();
        }
        catch (Exception&) {}
    }
}

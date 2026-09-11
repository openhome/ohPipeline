#include <OpenHome/Av/QobuzConnect/App.h>
#include <OpenHome/Av/QobuzConnect/Advertising.h>
#include <OpenHome/Av/QobuzConnect/LocalConfigServer.h>
#include <OpenHome/Av/QobuzConnect/Logging.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Debug-ohMediaPlayer.h>

#include <uv.h>
#include <qobuz_connect.h>

using namespace OpenHome;
using namespace OpenHome::Av;

extern "C"
void QobuzConnectApp_Thread(void* aArg)
{
    reinterpret_cast<QobuzConnectApp*>(aArg)->QobuzThread();
}

QobuzConnectApp::QobuzConnectApp(
    IMediaPlayer& aMediaPlayer,
    Net::IMdnsProvider& aMdnsProvider,
    IQobuzConnectPlaybackObserver& aPlaybackObserver,
    const Brx& aAppId,
    const Brx& aAppSecret,
    const Brx& aDeviceName,
    const Brx& aManufacturer,
    const Brx& aModel,
    const Brx& aSerialNumber,
    const Brx& aUniqueDeviceId,
    QbzDeviceType aDeviceType,
    QbzAudioQuality aMaxAudioQuality)
    : iMediaPlayer(aMediaPlayer)
    , iCore(nullptr)
    , iAppId(aAppId)
    , iAppSecret(aAppSecret)
    , iDeviceName(aDeviceName)
    , iManufacturer(aManufacturer)
    , iModel(aModel)
    , iSerialNumber(aSerialNumber)
    , iUniqueDeviceId(aUniqueDeviceId)
    , iDeviceType(aDeviceType)
    , iMaxAudioQuality(aMaxAudioQuality)
    , iStarted(false)
{
    iAudioStream = new QobuzConnectAudioStream();
    iMediaControl = new QobuzConnectMediaControl(aMediaPlayer.ThreadPool(), aPlaybackObserver, *iAudioStream);
    iLocalConfigServer = new QobuzConnectLocalConfigServer(
        aMediaPlayer.Env(), iAppId, iDeviceName, iManufacturer, iModel, iSerialNumber, iDeviceType, iMaxAudioQuality);
    iAdvertising = new QobuzConnectAdvertising(aMediaPlayer.Env(), aMdnsProvider, iDeviceType, *iLocalConfigServer);
    // The SDK gives no ordering guarantee between start_advertising_callback and
    // start_local_config_server_callback - this lets iAdvertising re-register with the real
    // port if it started advertising before the local config server had bound its socket.
    iLocalConfigServer->SetObserver(*iAdvertising);
}

QobuzConnectApp::~QobuzConnectApp()
{
    if (iStarted) {
        (void)uv_async_send(&iAsyncQuit);
        (void)uv_thread_join(&iThread);
    }
    delete iAdvertising;
    delete iLocalConfigServer;
    delete iMediaControl;
    delete iAudioStream;
}

void QobuzConnectApp::Start()
{
    if (iStarted) {
        return;
    }
    QobuzConnectLogging::Install();
    const int err = uv_thread_create(&iThread, QobuzConnectApp_Thread, this);
    ASSERT(err == 0);
    iStarted = true;
}

IQobuzConnectAudioReader& QobuzConnectApp::Reader()
{
    ASSERT(iAudioStream != nullptr);
    return *iAudioStream;
}

QobuzConnectMediaControl& QobuzConnectApp::MediaControl()
{
    ASSERT(iMediaControl != nullptr);
    return *iMediaControl;
}

void QobuzConnectApp::QobuzThread()
{
    int err = uv_loop_init(&iLoop);
    ASSERT(err == 0);
    err = uv_async_init(&iLoop, &iAsyncQuit, &QobuzConnectApp::AsyncQuitCb);
    ASSERT(err == 0);
    iAsyncQuit.data = this;

    // Named locals, not temporaries: qbz_connect_create() only needs these to stay valid for the
    // duration of the call (it copies everything it needs - see qobuz_connect.h), but a Brhz
    // temporary is destroyed at the end of the statement that creates it, which would leave
    // config's char* fields dangling well before qbz_connect_create() is even reached.
    Brhz deviceName(iDeviceName);
    Brhz manufacturer(iManufacturer);
    Brhz model(iModel);
    Brhz serialNumber(iSerialNumber);
    Brhz uniqueDeviceId(iUniqueDeviceId);
    Brhz appId(iAppId);
    Brhz appSecret(iAppSecret);

    QbzConnectConfig config = {};
    config.api_version = QOBUZ_CONNECT_API_VERSION;
    config.device_info.device_name = deviceName.CString();
    config.device_info.device_type = iDeviceType;
    config.device_info.manufacturer = manufacturer.CString();
    config.device_info.model = model.CString();
    config.device_info.serial_number = serialNumber.CString();
    config.device_info.unique_device_id = uniqueDeviceId.CString();
    config.device_info.app_id = appId.CString();
    config.device_info.app_secret = appSecret.CString();
    config.device_info.maximum_supported_audio_quality = iMaxAudioQuality;
    config.device_info.volume_capability = QBZ_VOLUME_CAPABILITY_NONE; // see MediaControl.h - volume wiring not implemented yet
    config.advertising_delegate = iAdvertising->Delegate();
    config.local_config_server_delegate = iLocalConfigServer->Delegate();
    config.audio_stream_delegate = iAudioStream->Delegate();
    config.media_delegate = iMediaControl->Delegate();
    config.renderer_state_delegate = iMediaControl->RendererStateDelegate();

    QbzError error;
    iCore = qbz_connect_create(&iLoop, config, &error);
    if (error != QBZ_ERROR_OK || iCore == nullptr) {
        LOG(kQobuzConnect, "QobuzConnectApp: qbz_connect_create failed: %s\n", qbz_error_to_string(error));
        uv_close((uv_handle_t*)&iAsyncQuit, nullptr);
        uv_run(&iLoop, UV_RUN_DEFAULT); // pump the loop once so the close callback above actually runs
        (void)uv_loop_close(&iLoop);
        return;
    }

    iAudioStream->SetCore(iCore);
    iMediaControl->SetCore(iCore);
    iAdvertising->SetCore(iCore);
    iLocalConfigServer->SetCore(iCore);

    LOG(kQobuzConnect, "QobuzConnectApp: running (SDK version %s)\n", qbz_connect_get_sdk_version());

    (void)uv_timer_init(&iLoop, &iHeartbeat);
    iHeartbeat.data = this;
    const TUint kHeartbeatMs = 3000;
    (void)uv_timer_start(&iHeartbeat, &QobuzConnectApp::HeartbeatCb, kHeartbeatMs, kHeartbeatMs);

    uv_run(&iLoop, UV_RUN_DEFAULT);

    const int closeErr = uv_loop_close(&iLoop);
    if (closeErr != 0) {
        LOG(kQobuzConnect, "QobuzConnectApp: uv_loop_close returned %d\n", closeErr);
    }
}

void QobuzConnectApp::AsyncQuitCb(uv_async_t* aHandle)
{
    reinterpret_cast<QobuzConnectApp*>(aHandle->data)->AsyncQuit();
}

void QobuzConnectApp::AsyncQuit()
{
    uv_timer_stop(&iHeartbeat);
    uv_close((uv_handle_t*)&iHeartbeat, nullptr);
    if (iCore != nullptr) {
        qbz_connect_free(iCore);
        iCore = nullptr;
    }
    uv_close((uv_handle_t*)&iAsyncQuit, nullptr);
    uv_stop(&iLoop);
}

void QobuzConnectApp::HeartbeatCb(uv_timer_t* aHandle)
{
    reinterpret_cast<QobuzConnectApp*>(aHandle->data)->Heartbeat();
}

void QobuzConnectApp::Heartbeat()
{
    bool active = false;
    const QbzError error = qbz_connect_get_renderer_active_state(iCore, &active);
    if (error != QBZ_ERROR_OK) {
        LOG(kQobuzConnect, "QobuzConnectApp: heartbeat - loop alive, get_renderer_active_state failed: %s\n", qbz_error_to_string(error));
    }
    else {
        LOG(kQobuzConnect, "QobuzConnectApp: heartbeat - loop alive, renderer_active=%u\n", active);
    }
}

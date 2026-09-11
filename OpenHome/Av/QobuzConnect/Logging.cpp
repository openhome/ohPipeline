#include <OpenHome/Av/QobuzConnect/Logging.h>
#include <OpenHome/Types.h>
#include <OpenHome/Debug-ohMediaPlayer.h>

using namespace OpenHome;
using namespace OpenHome::Av;

extern "C" {

static void QobuzConnectLogging_Log(QbzLogLevel aLevel, const char* aModule, const char* aMessage, void* aUserData)
{
    OpenHome::Av::QobuzConnectLogging::LogCb(aLevel, aModule, aMessage, aUserData);
}

} // extern "C"

void QobuzConnectLogging::Install()
{
    QbzLogDelegate delegate;
    delegate.user_data = nullptr;
    delegate.log_callback = &QobuzConnectLogging_Log;
    const QbzError error = qbz_connect_set_log_delegate(delegate);
    if (error != QBZ_ERROR_OK) {
        LOG(kQobuzConnect, "QobuzConnectLogging::Install: qbz_connect_set_log_delegate failed: %s\n", qbz_error_to_string(error));
    }
}

void QobuzConnectLogging::LogCb(QbzLogLevel aLevel, const char* aModule, const char* aMessage, void* /*aUserData*/)
{
    const TChar* levelStr = "?";
    switch (aLevel) {
        case QBZ_LOG_LEVEL_TRACE:   levelStr = "TRACE"; break;
        case QBZ_LOG_LEVEL_DEBUG:   levelStr = "DEBUG"; break;
        case QBZ_LOG_LEVEL_INFO:    levelStr = "INFO"; break;
        case QBZ_LOG_LEVEL_WARNING: levelStr = "WARN"; break;
        case QBZ_LOG_LEVEL_ERROR:   levelStr = "ERROR"; break;
        case QBZ_LOG_LEVEL_FATAL:   levelStr = "FATAL"; break;
    }
    LOG(kQobuzConnect, "QobuzConnectSdk [%s] [%s] %s\n", levelStr, aModule, aMessage);
}

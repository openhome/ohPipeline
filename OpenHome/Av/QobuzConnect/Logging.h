#pragma once

#include <qobuz_connect.h>

namespace OpenHome {
namespace Av {

/**
 * Installs a QbzLogDelegate routing the SDK's own log messages through this codebase's
 * LOG(kQobuzConnect, ...) mechanism (see OpenHome/Media/Debug.h).
 *
 * Unlike the other delegates, qbz_connect_set_log_delegate() is a standalone global call, not
 * a field inside QbzConnectConfig - it takes effect immediately and isn't tied to a particular
 * QbzConnectCore instance (see qobuz_connect_logging.h). Call once, before qbz_connect_create().
 */
class QobuzConnectLogging
{
public:
    static void Install();
    // Called by the extern "C" trampoline function in Logging.cpp (which isn't a member or
    // friend of this class, so this must be public, not private).
    static void LogCb(QbzLogLevel aLevel, const char* aModule, const char* aMessage, void* aUserData);
};

}
}

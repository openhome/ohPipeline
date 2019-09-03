#pragma once

#include <OpenHome/Types.h>

namespace OpenHome {
    class Environment;
    class Brx;
    class SslContext;
    class IUnixTimestamp;
namespace Net {
    class CpStack;
}
namespace Configuration {
    class IStoreReadOnly;
    class IConfigInitialiser;
}
namespace Av {
    class Credentials;
    class IMediaPlayer;
}
namespace Media {

class Protocol;
class IServerObserver;

class ProtocolFactory
{
public:
    static Protocol* NewHls(Environment& aEnv, SslContext& aSsl, const Brx& aUserAgent);
    static Protocol* NewHttp(Environment& aEnv, SslContext& aSsl, const Brx& aUserAgent); // UA is optional so can be empty
    static Protocol* NewHttp(Environment& aEnv, SslContext& aSsl, const Brx& aUserAgent, IServerObserver& aServerObserver); // UA is optional so can be empty
    static Protocol* NewHttps(Environment& aEnv, SslContext& aSsl);
    static Protocol* NewFile(Environment& aEnv);
    static Protocol* NewTone(Environment& aEnv);
    static Protocol* NewRtsp(Environment& aEnv, const Brx& aGuid);
    static Protocol* NewTidal(Environment& aEnv, SslContext& aSsl, const Brx& aToken, Av::IMediaPlayer& aMediaPlayer);
    static Protocol* NewQobuz(const Brx& aAppId, const Brx& aAppSecret, Av::IMediaPlayer& aMediaPlayer);
    static Protocol* NewCalmRadio(Environment& aEnv, SslContext& aSsl, const Brx& aUserAgent, Av::IMediaPlayer& aMediaPlayer); // UA is optional so can be empty
};

} // namespace Media
} // namespace OpenHome


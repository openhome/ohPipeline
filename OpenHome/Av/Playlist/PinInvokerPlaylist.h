#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Av/Pins/Pins.h>

namespace OpenHome {
namespace Av {

class ITrackDatabaseWriter;
class IPlaylistLoader;

class PinInvokerPlaylist : public IPinInvoker
{
    const TUint kMinSupportedVersion = 1;
    const TUint kMaxSupportedVersion = 1;

public:
    PinInvokerPlaylist(ITrackDatabaseWriter& aTrackDatabase,
                       IPlaylistLoader& aPlaylistLoader);
private: // from IPinInvoker
    void BeginInvoke(const IPin& aPin, Functor aCompleted) override;
    void Cancel() override;
    const TChar* Mode() const override;
    TBool SupportsVersion(TUint version) const override;
private:
    ITrackDatabaseWriter& iTrackDatabase;
    IPlaylistLoader& iLoader;
    Uri iUri; // only used by Invoke() but too large for the stack
};

}
}

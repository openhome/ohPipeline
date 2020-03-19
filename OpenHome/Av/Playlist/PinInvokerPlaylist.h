#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Av/Pins/Pins.h>

namespace OpenHome {
namespace Av {

class ITrackDatabase;
class IPlaylistLoader;

class PinInvokerPlaylist : public IPinInvoker
{
    const TUint kMinSupportedVersion = 1;
    const TUint kMaxSupportedVersion = 1;

public:
    PinInvokerPlaylist(ITrackDatabase& aTrackDatabase,
                       IPlaylistLoader& aPlaylistLoader);
private: // from IPinInvoker
    void BeginInvoke(const IPin& aPin, Functor aCompleted) override;
    void Cancel() override;
    const TChar* Mode() const override;
    TBool SupportsVersion(TUint version) const override;
private:
    ITrackDatabase& iTrackDatabase;
    IPlaylistLoader& iLoader;
    Uri iUri; // only used by Invoke() but too large for the stack
};

}
}

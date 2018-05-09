#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Av/Pins.h>

namespace OpenHome {
namespace Av {

class ITrackDatabase;
class IPlaylistLoader;

class PinInvokerPlaylist : public IPinInvoker
{
public:
    PinInvokerPlaylist(ITrackDatabase& aTrackDatabase,
                       IPlaylistLoader& aPlaylistLoader);
private: // from IPinInvoker
    void Invoke(const IPin& aPin) override;
    const TChar* Mode() const override;
private:
    ITrackDatabase& iTrackDatabase;
    IPlaylistLoader& iLoader;
    Uri iUri; // only used by Invoke() but too large for the stack
};

}
}
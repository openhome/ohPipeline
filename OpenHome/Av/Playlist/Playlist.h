#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>

namespace OpenHome {
namespace Av {

class IPlaylistLoader
{
public:
    static const TUint kMaxPlaylistIdBytes = 64;
public:
    virtual ~IPlaylistLoader() {}
    virtual void LoadPlaylist(const Brx& aId, TUint aPlaylistInsertAfterId) = 0;
};

} // namespace Av
} // namespace OpenHome

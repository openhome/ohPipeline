#pragma once

#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Types.h>
#include <OpenHome/SocketSsl.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <Generated/CpAvOpenhomeOrgPlaylist1.h>
#include <OpenHome/Av/Playlist/TrackDatabase.h>
#include <OpenHome/Av/Pins/Pins.h>
        
namespace OpenHome {
    class Environment;
    class IThreadPool;
    class IThreadPoolHandle;
namespace Configuration {
    class IConfigInitialiser;
    class ConfigChoice;
}
namespace Av {

class TidalPins
    : public IPinInvoker
{
    static const TUint kTrackLimitPerRequest = 10;
    static const TUint kMaxPlaylistsPerSmartType = 15; // limit playlists in loop
    static const TUint kMaxAlbums = 25; // limit albums in loop (NOTE: tidal api will not give a conmtnet-length if the limit is too high - requires further investigation)
    static const TUint kJsonResponseChunks = 4 * 1024;
public:
    TidalPins(Tidal& aTidal,
              Net::DvDeviceStandard& aDevice,
              Media::TrackFactory& aTrackFactory,
              Net::CpStack& aCpStack,
              IThreadPool& aThreadPool);
    ~TidalPins();

private:
    void Invoke();
    TBool LoadTracksByArtist(const Brx& aArtist, TBool aShuffle); // tidal id or search string 
    TBool LoadTracksByAlbum(const Brx& aAlbum, TBool aShuffle); // tidal id or search string 
    TBool LoadTracksByTrack(const Brx& aTrack, TBool aShuffle); // tidal id or search string 
    TBool LoadTracksByPlaylist(const Brx& aPlaylist, TBool aShuffle); // tidal id or search string 
    TBool LoadTracksByGenre(const Brx& aGenre, TBool aShuffle); // tidal text string id
    TBool LoadTracksByMood(const Brx& aMood, TBool aShuffle); // tidal text string id
    TBool LoadTracksByNew(TBool aShuffle); // tidal smart playlist (featured)
    TBool LoadTracksByRecommended(TBool aShuffle); // tidal smart playlist (featured)
    TBool LoadTracksByTop20(TBool aShuffle); // tidal smart playlist (featured)
    TBool LoadTracksByExclusive(TBool aShuffle); // tidal smart playlist (featured)
    TBool LoadTracksByRising(TBool aShuffle); // tidal smart playlist
    TBool LoadTracksByDiscovery(TBool aShuffle); // tidal smart playlist
    TBool LoadTracksByFavorites(TBool aShuffle); // user's favorited tracks and albums
    TBool LoadTracksBySavedPlaylist(TBool aShuffle); // user's most recently created/updated tidal playlists

private: // from IPinInvoker
    void BeginInvoke(const IPin& aPin, Functor aCompleted) override;
    void Cancel() override;
    const TChar* Mode() const override;
private:
    TUint LoadTracksById(const Brx& aId, TidalMetadata::EIdType aType, TUint aPlaylistId);
    TBool LoadTracksBySmartType(TidalMetadata::EIdType aType, TBool aShuffle);
    TBool LoadTracksByQuery(const Brx& aQuery, TidalMetadata::EIdType aType, TBool aShuffle);
    TBool LoadTracksByPath(const Brx& aPath, TBool aShuffle);
    TBool LoadAlbumsByPath(const Brx& aPath, TBool aShuffle);
    TBool LoadTracksByMultiplePlaylists(TidalMetadata::EIdType aType, TBool aShuffle);
    TBool LoadTracksByMultiplePlaylists(const Brx& aMood, TidalMetadata::EIdType aType, TBool aShuffle);
    TBool IsValidId(const Brx& aRequest, TidalMetadata::EIdType aType);
    TBool IsValidUuid(const Brx& aRequest);
    void InitPlaylist(TBool aShuffle);
private:
    Mutex iLock;
    Tidal& iTidal;
    IThreadPoolHandle* iThreadPoolHandle;
    WriterBwh iJsonResponse;
    Media::TrackFactory& iTrackFactory;
    Net::CpProxyAvOpenhomeOrgPlaylist1* iCpPlaylist;
    Bws<128> iToken;
    Functor iCompleted;
    PinIdProvider iPinIdProvider;
    Pin iPin;
    Functor iTrackLoader;
    Bws<IPin::kMaxUriBytes> iId;
    TBool iShuffle;
};

};  // namespace Av
};  // namespace OpenHome

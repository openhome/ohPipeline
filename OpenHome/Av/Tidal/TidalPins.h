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
#include <OpenHome/DebugManager.h>
#include <OpenHome/Av/Playlist/TrackDatabase.h>
        
namespace OpenHome {
    class Environment;
namespace Configuration {
    class IConfigInitialiser;
    class ConfigChoice;
}
namespace Av {

class TidalPins
    : public IDebugTestHandler
{
    static const TUint kTrackLimitPerRequest = 10;
    static const TUint kMaxPlaylistsPerSmartType = 15; // limit playlists
    static const TUint kJsonResponseChunks = 4 * 1024;
public:
    TidalPins(Tidal& aTidal, Net::DvDeviceStandard& aDevice, Media::TrackFactory& aTrackFactory, Net::CpStack& aCpStack, TUint aMaxTracks = ITrackDatabase::kMaxTracks);
    ~TidalPins();

    TBool LoadTracksByArtist(const Brx& aArtist); // tidal id or search string 
    TBool LoadTracksByAlbum(const Brx& aAlbum); // tidal id or search string 
    TBool LoadTracksByTrack(const Brx& aTrack); // tidal id or search string 
    TBool LoadTracksByPlaylist(const Brx& aPlaylist); // tidal id or search string 
    TBool LoadTracksByGenre(const Brx& aGenre); // tidal text string id
    TBool LoadTracksByMood(const Brx& aMood); // tidal text string id
    TBool LoadTracksByNew(); // tidal smart playlist (featured)
    TBool LoadTracksByRecommended(); // tidal smart playlist (featured)
    TBool LoadTracksByTop20(); // tidal smart playlist (featured)
    TBool LoadTracksByExclusive(); // tidal smart playlist (featured)
    TBool LoadTracksByRising(); // tidal smart playlist
    TBool LoadTracksByDiscovery(); // tidal smart playlist
    TBool LoadTracksByFavorites(); // user's favorited tracks
    TBool LoadTracksBySavedPlaylists(); // user's most recently created/updated tidal playlists

public:  // IDebugTestHandler
    TBool Test(const OpenHome::Brx& aType, const OpenHome::Brx& aInput, OpenHome::IWriterAscii& aWriter);
private:
    TUint LoadTracksById(const Brx& aId, TidalMetadata::EIdType aType, TUint aPlaylistId);
    TBool LoadTracksBySmartType(TidalMetadata::EIdType aType);
    TBool LoadTracksByQuery(const Brx& aQuery, TidalMetadata::EIdType aType);
    TBool LoadTracksByMultiplePlaylists(TidalMetadata::EIdType aType);
    TBool LoadTracksByMultiplePlaylists(const Brx& aMood, TidalMetadata::EIdType aType);
    TBool IsValidId(const Brx& aRequest, TidalMetadata::EIdType aType);
    TBool IsValidUuid(const Brx& aRequest);
private:
    Mutex iLock;
    Tidal& iTidal;
    WriterBwh iJsonResponse;
    Media::TrackFactory& iTrackFactory;
    Net::CpProxyAvOpenhomeOrgPlaylist1* iCpPlaylist;
    Net::CpStack& iCpStack;
    TUint iMaxTracks;
};

};  // namespace Av
};  // namespace OpenHome



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
#include <OpenHome/Av/Pins.h>
        
namespace OpenHome {
    class Environment;
namespace Configuration {
    class IConfigInitialiser;
    class ConfigChoice;
}
namespace Av {

class QobuzPins
    : public IPinInvoker
{
    static const TUint kTrackLimitPerRequest = 10;
    static const TUint kMaxAlbumsPerSmartType = 15;
    static const TUint kJsonResponseChunks = 4 * 1024;
public:
    QobuzPins(Qobuz& aQobuz, Net::DvDeviceStandard& aDevice, Media::TrackFactory& aTrackFactory, Net::CpStack& aCpStack, TUint aMaxTracks = ITrackDatabase::kMaxTracks);
    ~QobuzPins();
    
private:
    TBool LoadTracksByArtist(const Brx& aArtist); // Qobuz id or search string 
    TBool LoadTracksByAlbum(const Brx& aAlbum); // Qobuz id or search string 
    TBool LoadTracksByTrack(const Brx& aTrack); // Qobuz id or search string 
    TBool LoadTracksByPlaylist(const Brx& aPlaylist); // Qobuz id or search string 
    TBool LoadTracksByFavorites(); // user's favorited tracks and albums (flattened list)
    TBool LoadTracksByPurchased(); // user's purchased tracks and albums (flattened list)
    TBool LoadTracksByCollection(); // collection of user's purchased, favorited and playlisted tracks and albums (flattened list)
    TBool LoadTracksBySavedPlaylist(); // user's most recently created/updated qobuz playlist
    TBool LoadTracksByNew(const Brx& aGenre); // Qobuz smart playlist (featured: new releases) by genre (optional id)
    TBool LoadTracksByRecommended(const Brx& aGenre); // Qobuz smart playlist (featured: qobuz picks) by genre (optional id)
    TBool LoadTracksByMostStreamed(const Brx& aGenre); // Qobuz smart playlist (featured: most streamed) by genre (optional id)
    TBool LoadTracksByBestSellers(const Brx& aGenre); // Qobuz smart playlist (featured: best sellers) by genre (optional id)
    TBool LoadTracksByAwardWinning(const Brx& aGenre); // Qobuz smart playlist (featured: press awards) by genre (optional id)
    TBool LoadTracksByMostFeatured(const Brx& aGenre); // Qobuz smart playlist (featured: most featured) by genre (optional id)

private: // from IPinInvoker
    void Invoke(const IPin& aPin) override;
    const TChar* Mode() const override;
private:
    TUint LoadTracksById(const Brx& aId, QobuzMetadata::EIdType aType, TUint aPlaylistId);
    TBool LoadTracksBySmartType(const Brx& aGenre, QobuzMetadata::EIdType aType);
    TBool LoadTracksByQuery(const Brx& aQuery, QobuzMetadata::EIdType aType);
    TBool IsValidId(const Brx& aRequest, QobuzMetadata::EIdType aType);
    TBool IsValidGenreId(const Brx& aRequest);
private:
    Mutex iLock;
    Qobuz& iQobuz;
    WriterBwh iJsonResponse;
    Media::TrackFactory& iTrackFactory;
    Net::CpProxyAvOpenhomeOrgPlaylist1* iCpPlaylist;
    Net::CpStack& iCpStack;
    TUint iMaxTracks;
};

};  // namespace Av
};  // namespace OpenHome



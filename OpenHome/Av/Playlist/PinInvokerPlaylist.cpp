#include <OpenHome/Av/Playlist/PinInvokerPlaylist.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Uri.h>
#include <OpenHome/Av/Debug.h>
#include <OpenHome/Av/Pins/Pins.h>
#include <OpenHome/Av/Playlist/TrackDatabase.h>
#include <OpenHome/Av/Playlist/Playlist.h>

using namespace OpenHome;
using namespace OpenHome::Av;


PinInvokerPlaylist::PinInvokerPlaylist(ITrackDatabase& aTrackDatabase,
                                       IPlaylistLoader& aPlaylistLoader)
    : iTrackDatabase(aTrackDatabase)
    , iLoader(aPlaylistLoader)
{
}

void PinInvokerPlaylist::BeginInvoke(const IPin& aPin, Functor aCompleted)
{
    AutoFunctor _(aCompleted);
    iUri.Replace(aPin.Uri());
    const Brx& scheme = iUri.Scheme();
    if (scheme != Brn("playlist")) {
        LOG_ERROR(kSources, "PinInvokerPlaylist::Invoke - unsupported uri scheme - %.*s\n", PBUF(scheme));
        return;
    }
    const Brx& host = iUri.Host();
    if (host != Brn("replace")) {
        LOG_ERROR(kSources, "PinInvokerPlaylist::Invoke - unsupported uri host - %.*s\n", PBUF(host));
        return;
    }
    const Brx& query = iUri.Query();
    if (!query.BeginsWith(Brn("id="))) {
        LOG_ERROR(kSources, "PinInvokerPlaylist::Invoke - unsupported uri query - %.*s\n", PBUF(query));
        return;
    }
    Brn id = query.Split(3); // remainder of query after "id="
    iTrackDatabase.DeleteAll();
    iLoader.LoadPlaylist(id, ITrackDatabase::kTrackIdNone);
}

void PinInvokerPlaylist::Cancel()
{
}

const TChar* PinInvokerPlaylist::Mode() const
{
    return "playlist";
}

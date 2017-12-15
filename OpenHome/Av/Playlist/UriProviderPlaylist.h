#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Filler.h>
#include <OpenHome/Av/Playlist/TrackDatabase.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/PipelineObserver.h>
#include <OpenHome/Media/Pipeline/TrackInspector.h>

#include <array>
#include <vector>

namespace OpenHome {
    class JsonParser;
namespace Media {
    class Track;
    class PipelineManager;
}
namespace Av {

    class IPlaylistLoader;

class UriProviderPlaylist : public Media::UriProvider
                          , private ITrackDatabaseObserver
                          , private Media::IPipelineObserver
                          , private Media::ITrackObserver
{
    static const Brn kCommandId;
    static const Brn kCommandIndex;
    static const Brn kCommandJukebox;
    static const Brn kCommandPlaylist;
    static const Brn kJukeboxMethodReplace;
    static const Brn kJukeboxMethodInsert;
    static const Brn kPlaylistMethodReplace;
    static const Brn kPlaylistMethodInsert;
    static const TUint kLoaderTimeoutMs = 30 * 1000;
public:
    UriProviderPlaylist(ITrackDatabaseReader& aDbReader, ITrackDatabase& aDbWriter, ITrackDatabaseObserver& aDbObserver,
                        Media::PipelineManager& aPipeline, Optional<IPlaylistLoader> aPlaylistLoader);
    ~UriProviderPlaylist();
    void SetActive(TBool aActive);
public: // from UriProvider
    TBool IsValid(TUint aTrackId) const override;
    void Begin(TUint aTrackId) override;
    void BeginLater(TUint aTrackId) override;
    Media::EStreamPlay GetNext(Media::Track*& aTrack) override;
    TUint CurrentTrackId() const override;
    void MoveNext() override;
    void MovePrevious() override;
    void MoveTo(const Brx& aCommand) override;
private: // from ITrackDatabaseObserver
    void NotifyTrackInserted(Media::Track& aTrack, TUint aIdBefore, TUint aIdAfter) override;
    void NotifyTrackDeleted(TUint aId, Media::Track* aBefore, Media::Track* aAfter) override;
    void NotifyAllDeleted() override;
private: // from Media::IPipelineObserver
    void NotifyPipelineState(Media::EPipelineState aState) override;
    void NotifyMode(const Brx& aMode, const Media::ModeInfo& aInfo,
                    const Media::ModeTransportControls& aTransportControls) override;
    void NotifyTrack(Media::Track& aTrack, const Brx& aMode, TBool aStartOfStream) override;
    void NotifyMetaText(const Brx& aText) override;
    void NotifyTime(TUint aSeconds, TUint aTrackDurationSeconds) override;
    void NotifyStreamInfo(const Media::DecodedStreamInfo& aStreamInfo) override;
private: // from Media::ITrackObserver
    void NotifyTrackPlay(Media::Track& aTrack) override;
    void NotifyTrackFail(Media::Track& aTrack) override;
private:
    void DoBegin(TUint aTrackId, Media::EStreamPlay aPendingCanPlay);
    TUint CurrentTrackIdLocked() const;
    TUint ParseCommand(const Brx& aCommand) const;
    Media::Track* ProcessCommandId(const Brx& aCommand);
    Media::Track* ProcessCommandIndex(const Brx& aCommand);
    void ProcessCommandJukebox(const Brx& aCommand);
    void ProcessCommandPlaylist(const Brx& aCommand);
private:
    enum EPendingDirection
    {
        eForwards
       ,eBackwards
       ,eJumpTo
    };
private:
    mutable Mutex iLock;
    ITrackDatabaseReader& iDbReader;
    ITrackDatabase& iDbWriter;
    ITrackDatabaseObserver& iDbObserver;
    Media::IPipelineIdManager& iIdManager;
    IPlaylistLoader* iPlaylistLoader;
    Media::Track* iPending;
    Media::EStreamPlay iPendingCanPlay;
    EPendingDirection iPendingDirection;
    TUint iLastTrackId;
    TUint iPlayingTrackId;
    TUint iFirstFailedTrackId; // first id from a string of failures; reset by any track generating audio
    TBool iActive;
    TBool iLoaderWait;
    Mutex iLockLoader;
    Semaphore iSemLoader;
    TUint iLoaderIdBefore;
};

} // namespace Av
} // namespace OpenHome


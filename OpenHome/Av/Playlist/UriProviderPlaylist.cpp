#include <OpenHome/Av/Playlist/UriProviderPlaylist.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Media/Filler.h>
#include <OpenHome/Media/Debug.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/PipelineManager.h>
#include <OpenHome/Av/Playlist/Playlist.h>
#include <OpenHome/Av/Playlist/TrackDatabase.h>
#include <OpenHome/Media/Pipeline/TrackInspector.h>
#include <OpenHome/Json.h>
#include <OpenHome/Av/TransportControl.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;

// UriProviderPlaylist

const Brn UriProviderPlaylist::kCommandId("id");
const Brn UriProviderPlaylist::kCommandIndex("index");
const Brn UriProviderPlaylist::kCommandPlaylist("playlist");
const Brn UriProviderPlaylist::kPlaylistMethodReplace("replace");
const Brn UriProviderPlaylist::kPlaylistMethodInsert("insert");

UriProviderPlaylist::UriProviderPlaylist(
    ITrackDatabaseReader& aDbReader,
    ITrackDatabaseWriter& aDbWriter,
    ITrackDatabaseTrackReader& aDbTrackReader,
    ITrackDatabaseObserver& aDbObserver,
    PipelineManager& aPipeline,
    Optional<IPlaylistLoader> aPlaylistLoader)
    : UriProvider("Playlist",
                  Latency::NotSupported, Pause::Supported,
                  Next::Supported, Prev::Supported,
                  Repeat::Supported, Random::Supported,
                  RampPauseResume::Long, RampSkip::Short)
    , iLock("UPP1")
    , iDbReader(aDbReader)
    , iDbWriter(aDbWriter)
    , iDbTrackReader(aDbTrackReader)
    , iDbObserver(aDbObserver)
    , iIdManager(aPipeline)
    , iPlaylistLoader(aPlaylistLoader.Ptr())
    , iPending(nullptr)
    , iLastTrackId(ITrackDatabaseReader::kTrackIdNone)
    , iPlayingTrackId(ITrackDatabaseReader::kTrackIdNone)
    , iFirstFailedTrackId(ITrackDatabaseReader::kTrackIdNone)
    , iActive(false)
    , iLoaderWait(false)
    , iLockLoader("UPP2")
    , iSemLoader("UPP3", 0)
    , iLoaderIdBefore(ITrackDatabaseReader::kTrackIdNone)
{
    aPipeline.AddObserver(static_cast<IPipelineObserver&>(*this));
    iDbTrackReader.SetObserver(*this);
    aPipeline.AddObserver(static_cast<ITrackObserver&>(*this));
}

UriProviderPlaylist::~UriProviderPlaylist()
{
    if (iPending != nullptr) {
        iPending->RemoveRef();
    }
}

void UriProviderPlaylist::SetActive(TBool aActive)
{
    iLock.Wait();
    iActive = aActive;
    iLock.Signal();
}

TBool UriProviderPlaylist::IsValid(TUint aTrackId) const
{
    AutoMutex a(iLock);
    return iDbTrackReader.IsValid(aTrackId);
}

void UriProviderPlaylist::Begin(TUint aTrackId)
{
    DoBegin(aTrackId, ePlayYes);
}

void UriProviderPlaylist::BeginLater(TUint aTrackId)
{
    DoBegin(aTrackId, ePlayLater);
}

EStreamPlay UriProviderPlaylist::GetNext(Media::Track*& aTrack)
{
    EStreamPlay canPlay = ePlayYes;
    {
        iLockLoader.Wait();
        const TBool wait = iLoaderWait;
        iLockLoader.Signal();
        if (wait) {
            try {
                iSemLoader.Wait(kLoaderTimeoutMs);
            }
            catch (Timeout&) {
                iLockLoader.Wait();
                iLoaderWait = false;
                iLockLoader.Signal();
            }
        }
    }
    AutoMutex a(iLock);
    const TUint prevLastTrackId = iLastTrackId;
    if (iPending != nullptr) {
        aTrack = iPending;
        iLastTrackId = iPending->Id();
        iPending = nullptr;
        canPlay = iPendingCanPlay;
    }
    else {
        aTrack = iDbTrackReader.NextTrackRef(iLastTrackId);
        if (aTrack == nullptr) {
            aTrack = iDbTrackReader.NextTrackRef(ITrackDatabaseReader::kTrackIdNone);
            canPlay = (aTrack==nullptr? ePlayNo : ePlayLater);
        }
        iLastTrackId = (aTrack != nullptr? aTrack->Id() : ITrackDatabaseReader::kTrackIdNone);
    }
    if (aTrack != nullptr && aTrack->Id() == iFirstFailedTrackId) {
        // every single track in a playlist has failed to generate any audio
        // set aTrack to nullptr to halt the Filler until the user takes action
        iLastTrackId = prevLastTrackId;
        if (aTrack != nullptr) {
            aTrack->RemoveRef();
            aTrack = nullptr;
        }
        canPlay = ePlayNo;
    }
    return canPlay;
}

TUint UriProviderPlaylist::CurrentTrackId() const
{
    iLock.Wait();
    const TUint id = CurrentTrackIdLocked();
    iLock.Signal();
    return id;
}

void UriProviderPlaylist::MoveNext()
{
    AutoMutex a(iLock);
    if (iPending != nullptr) {
        iPending->RemoveRef();
        iPending = nullptr;
    }
    const TUint trackId = CurrentTrackIdLocked();
    iPending = iDbTrackReader.NextTrackRef(trackId);
    if (iPending != nullptr) {
        iPendingCanPlay = ePlayYes;
        // allow additional loop round the playlist in case we've skipped discovering whether a track we started fetching is playable
        iFirstFailedTrackId = ITrackDatabaseReader::kTrackIdNone;
    }
    else {
        iPending = iDbTrackReader.NextTrackRef(ITrackDatabaseReader::kTrackIdNone);
        iPendingCanPlay = (iPending == nullptr? ePlayNo : ePlayLater);
    }
    iPendingDirection = eForwards;
}

void UriProviderPlaylist::MovePrevious()
{
    AutoMutex a(iLock);
    if (iPending != nullptr) {
        iPending->RemoveRef();
        iPending = nullptr;
    }
    const TUint trackId = CurrentTrackIdLocked();
    iPending = iDbTrackReader.PrevTrackRef(trackId);
    if (iPending != nullptr) {
        iPendingCanPlay = ePlayYes;
        // allow additional loop round the playlist in case we've skipped discovering whether a track we started fetching is playable
        iFirstFailedTrackId = ITrackDatabaseReader::kTrackIdNone;
    }
    else {
        iPending = iDbTrackReader.NextTrackRef(ITrackDatabaseReader::kTrackIdNone);
        iPendingCanPlay = (iPending == nullptr? ePlayNo : ePlayLater);
    }
    iPendingDirection = eBackwards;
}

void UriProviderPlaylist::MoveTo(const Brx& aCommand)
{
    Track* track = nullptr;
    if (aCommand.BeginsWith(kCommandId)) {
        track = ProcessCommandId(aCommand);
    }
    else if (aCommand.BeginsWith(kCommandIndex)) {
        track = ProcessCommandIndex(aCommand);
    }
    else if (TryProcessCommandTrack(aCommand, track)) {
        // nothing to do - TryProcessCommandTrack already populated 'track'
    }
    else {
        try {
            JsonParser parser;
            parser.Parse(aCommand);
            Brn mode = parser.String("mode");
            Brn cmd = parser.String("command");
            if (mode == kCommandPlaylist) {
                ProcessCommandPlaylist(cmd);
            }
            else {
                LOG_ERROR(kPipeline, "UriProviderPlaylist - unsupported command - %.*s\n", PBUF(aCommand));
                THROW(FillerInvalidCommand);
            }
        }
        catch (AssertionFailed&) {
            throw;
        }
        catch (Exception& ex) {
            LOG_ERROR(kPipeline, "UriProviderPlaylist: exception - %s - handling command %.*s\n", ex.Message(), PBUF(aCommand));
            THROW(FillerInvalidCommand);
        }
    }

    if (iPending != nullptr) {
        iPending->RemoveRef();
    }
    iPending = track;
    iPendingCanPlay = ePlayYes;
    iPendingDirection = eJumpTo;
}

void UriProviderPlaylist::DoBegin(TUint aTrackId, EStreamPlay aPendingCanPlay)
{
    AutoMutex a(iLock);
    if (iPending != nullptr) {
        iPending->RemoveRef();
        iPending = nullptr;
    }
    iPending = iDbTrackReader.TrackRef(aTrackId);
    if (iPending == nullptr) {
        iPending = iDbTrackReader.NextTrackRef(ITrackDatabaseReader::kTrackIdNone);
    }
    iPendingCanPlay = aPendingCanPlay;
    iPendingDirection = eJumpTo;
}

TUint UriProviderPlaylist::CurrentTrackIdLocked() const
{
    TUint id = iPlayingTrackId;
    if (iPending != nullptr) {
        id = iPending->Id();
    }
    return id;
}

TUint UriProviderPlaylist::ParseCommand(const Brx& aCommand) const
{
    Parser parser(aCommand);
    Brn buf = parser.Next('=');
    try {
        return Ascii::Uint(buf);
    }
    catch (AsciiError&) {
        THROW(FillerInvalidCommand);
    }
}

Track* UriProviderPlaylist::ProcessCommandId(const Brx& aCommand)
{
    const TUint id = ParseCommand(aCommand);
    try {
        return iDbTrackReader.TrackRef(id);
    }
    catch (TrackDbIdNotFound&) {
        THROW(FillerInvalidCommand);
    }
}

Track* UriProviderPlaylist::ProcessCommandIndex(const Brx& aCommand)
{
    const TUint index = ParseCommand(aCommand);
    auto track = iDbTrackReader.TrackRefByIndex(index);
    if (track == nullptr) {
        THROW(FillerInvalidCommand);
    }
    return track;
}

void UriProviderPlaylist::ProcessCommandPlaylist(const Brx& aCommand)
{
    if (iPlaylistLoader == nullptr) {
        THROW(FillerInvalidCommand);
    }
    JsonParser parser;
    parser.Parse(aCommand);
    Brn method = parser.String("method");
    Brn id = parser.String("id");
    TUint insertAfterId = ITrackDatabaseReader::kTrackIdNone;
    if (method == kPlaylistMethodReplace) {
        iDbWriter.DeleteAll();
    }
    else if (method == kPlaylistMethodInsert) {
        insertAfterId = (TUint)parser.Num("insertPos");
    }
    else {
        THROW(FillerInvalidCommand);
    }

    { // block GetNext until something has been added
        AutoMutex _(iLockLoader);
        iLoaderWait = true;
        iLoaderIdBefore = insertAfterId;
    }

    iPlaylistLoader->LoadPlaylist(id, insertAfterId);
}

TBool UriProviderPlaylist::TryProcessCommandTrack(const Brx& aCommand, Track*& aTrack)
{
    Brn uri;
    Brn metadata;
    if (!PlayAsCommandTrack::TryGetTrackFromCommand(aCommand, uri, metadata)) {
        return false;
    }
    // append track to end of playlist, deleting first track to make space if necessary
    std::vector<TUint32> idArray;
    TUint seq;
    iDbReader.GetIdArray(idArray, seq);
    const TUint tracksMax = iDbReader.TracksMax();
    if (idArray.size() == tracksMax) {
        iDbWriter.DeleteId(idArray[0]);
    }
    TUint id;
    iDbWriter.Insert(idArray[idArray.size() - 1], uri, metadata, id);
    aTrack = iDbTrackReader.TrackRef(id);
    return true;
}

void UriProviderPlaylist::NotifyTrackInserted(Track& aTrack, TUint aIdBefore, TUint aIdAfter)
{
    {
        AutoMutex a(iLock);
        if (iPending != nullptr) {
            if ((iPendingDirection == eForwards && iPending->Id() == aIdAfter) ||
                (iPendingDirection == eBackwards && iPending->Id() == aIdBefore)) {
                iPending->RemoveRef();
                iPending = &aTrack;
                iPending->AddRef();
            }
        }
        if (iActive) {
            iIdManager.InvalidateAfter(aIdBefore);
        }
        if (aIdBefore == iPlayingTrackId) {
            iLastTrackId = iPlayingTrackId;
        }

        // allow additional loop round the playlist in case the new track is the only one that is playable
        iFirstFailedTrackId = ITrackDatabaseReader::kTrackIdNone;
    }
    TBool consumed = false;
    {
        AutoMutex _(iLockLoader);
        if (iLoaderWait && aIdBefore == iLoaderIdBefore) {
            iLoaderWait = false;
            iSemLoader.Signal();
            consumed = true;
        }
    }

    if (!consumed) {
        /* iDbObserver (SourcePlaylist) calls StopPrefetch for the first track added to an empty playlist.
           This conflicts with async loading of a saved playlist (the thing that caused iLoaderWait to be
           set).  Avoid this by now by not passing the notification on.
           A better approach may be to refactor SourcePlaylist to move all of its database observation
           login into the uri provider. */
        iDbObserver.NotifyTrackInserted(aTrack, aIdBefore, aIdAfter);
    }
}

void UriProviderPlaylist::NotifyTrackDeleted(TUint aId, Track* aBefore, Track* aAfter)
{
    {
        AutoMutex a(iLock);
        if (iPending != nullptr && iPending->Id() == aId) {
            iPending->RemoveRef();
            iPending = nullptr;
            if (iPendingDirection == eForwards) {
                iLastTrackId = (aBefore==nullptr? ITrackDatabaseReader::kTrackIdNone : aBefore->Id());
            }
            else { // eBackwards || eJumpTo
                iPending = (aBefore!=nullptr? aAfter : aBefore);
                if (iPending == nullptr) {
                    iLastTrackId = ITrackDatabaseReader::kTrackIdNone;
                }
                else {
                    iPending->AddRef();
                }
            }
        }
        else if (iLastTrackId == aId) {
            iLastTrackId = (aBefore==nullptr? ITrackDatabaseReader::kTrackIdNone : aBefore->Id());
        }
        if (iActive) {
            iIdManager.InvalidateAt(aId);
        }
    }

    iDbObserver.NotifyTrackDeleted(aId, aBefore, aAfter);
}

void UriProviderPlaylist::NotifyAllDeleted()
{
    {
        AutoMutex a(iLock);
        if (iPending != nullptr) {
            iPending->RemoveRef();
            iPending = nullptr;
        }
        if (iActive) {
            iIdManager.InvalidateAll();
        }
    }

    iDbObserver.NotifyAllDeleted();
}

void UriProviderPlaylist::NotifyReordered(Track* aStart)
{
    {
        AutoMutex a(iLock);
        if (aStart != nullptr && aStart->Id() == CurrentTrackIdLocked()) {
            /* The track we're playing is already at the start of the new order so carries on
               playing.  Only the tracks queued behind it need to be fetched again. */
            iLastTrackId = aStart->Id();
            if (iActive) {
                iIdManager.InvalidateAfter(aStart->Id());
            }
        }
        else {
            if (iPending != nullptr) {
                iPending->RemoveRef();
            }
            iPending = aStart;
            if (iPending == nullptr) {
                iLastTrackId = ITrackDatabaseReader::kTrackIdNone;
            }
            else {
                iPending->AddRef();
            }
            if (iActive) {
                iIdManager.InvalidateAll();
            }
        }
    }
    iDbObserver.NotifyReordered(aStart);
}

void UriProviderPlaylist::NotifyPipelineState(EPipelineState /*aState*/)
{
}

void UriProviderPlaylist::NotifyMode(const Brx& aMode,
                                     const Media::ModeInfo& /*aInfo*/,
                                     const Media::ModeTransportControls& /*aTransportControls*/)
{
    iPlaylistMode = aMode == Mode();
}

void UriProviderPlaylist::NotifyTrack(Track& aTrack, TBool /*aStartOfStream*/)
{
    if (iPlaylistMode) {
        iLock.Wait();
        iPlayingTrackId = aTrack.Id();
        iLock.Signal();
    }
}

void UriProviderPlaylist::NotifyMetaText(const Brx& /*aText*/)
{
}

void UriProviderPlaylist::NotifyTime(TUint /*aSeconds*/)
{
}

void UriProviderPlaylist::NotifyStreamInfo(const DecodedStreamInfo& /*aStreamInfo*/)
{
}

void UriProviderPlaylist::NotifyTrackPlay(Track& /*aTrack*/)
{
    iLock.Wait();
    iFirstFailedTrackId = ITrackDatabaseReader::kTrackIdNone;
    iLock.Signal();
}

void UriProviderPlaylist::NotifyTrackFail(Track& aTrack)
{
    iLock.Wait();
    if (iFirstFailedTrackId == ITrackDatabaseReader::kTrackIdNone) {
        iFirstFailedTrackId = aTrack.Id();
    }
    iLock.Signal();
}

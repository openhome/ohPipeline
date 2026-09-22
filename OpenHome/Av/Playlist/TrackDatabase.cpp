#include <OpenHome/Av/Playlist/TrackDatabase.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Av/Debug.h>

#include <algorithm>
#include <vector>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;


const TUint ITrackDatabaseReader::kTrackIdNone = 0;

static inline void AddRefIfNonNull(Track* aTrack)
{
    if (aTrack != nullptr) {
        aTrack->AddRef();
    }
}

static inline void RemoveRefIfNonNull(Track* aTrack)
{
    if (aTrack != nullptr) {
        aTrack->RemoveRef();
    }
}


// TrackDatabase

TrackDatabase::TrackDatabase(TrackFactory& aTrackFactory, TUint aMaxTracks)
    : iLock("TDB1")
    , iObserverLock("TDB2")
    , iTrackFactory(aTrackFactory)
    , iMaxTracks(aMaxTracks)
    , iSeq(0)
{
    iTrackList.reserve(aMaxTracks);
}

TrackDatabase::~TrackDatabase()
{
    TrackListUtils::Clear(iTrackList);
}

void TrackDatabase::CopyIdArray(
    const std::vector<Track*>& aFrom,
    std::vector<TUint32>& aTo,
    TUint aMax)
{ // static
    TUint i;
    aTo.clear();
    for (i = 0; i < aFrom.size(); i++) {
        aTo.push_back(aFrom[i]->Id());
    }
    for (i = aFrom.size(); i < aMax; i++) {
        aTo.push_back(kTrackIdNone);
    }
}

void TrackDatabase::AddObserver(ITrackDatabaseObserver& aObserver)
{
    iObservers.push_back(&aObserver);
}

TUint TrackDatabase::IdArraySeq() const
{
    AutoMutex _(iLock);
    return iSeq;
}

void TrackDatabase::GetIdArray(std::vector<TUint32>& aIdArray, TUint& aSeq) const
{
    AutoMutex a(iLock);
    TrackDatabase::CopyIdArray(iTrackList, aIdArray, iMaxTracks);
    aSeq = iSeq;
}

void TrackDatabase::GetTrackById(TUint aId, Track*& aTrack) const
{
    AutoMutex a(iLock);
    return GetTrackByIdLocked(aId, aTrack);
}

void TrackDatabase::GetTrackByIdLocked(TUint aId, Track*& aTrack) const
{
    const TUint index = TrackListUtils::IndexFromId(iTrackList, aId);
    aTrack = iTrackList[index];
    aTrack->AddRef();
}

void TrackDatabase::GetTrackById(TUint aId, TUint aSeq, Track*& aTrack, TUint& aIndex) const
{
    AutoMutex a(iLock);
    aTrack = nullptr;
    if (iSeq != aSeq) {
        GetTrackByIdLocked(aId, aTrack);
        return;
    }
    if (!TryGetTrackById(aId, aTrack, aIndex, iTrackList.size(), aIndex)) {
        TUint endIndex = std::min(aIndex, (TUint)iTrackList.size());
        if (!TryGetTrackById(aId, aTrack, 0, endIndex, aIndex)) {
            THROW(TrackDbIdNotFound);
        }
    }
}

TUint TrackDatabase::TrackCount() const
{
    iLock.Wait();
    const TUint count = iTrackList.size();
    iLock.Signal();
    return count;
}

TUint TrackDatabase::TracksMax() const
{
    return iMaxTracks;
}

void TrackDatabase::Insert(TUint aIdAfter, const Brx& aUri, const Brx& aMetaData, TUint& aIdInserted)
{
    Track* track;
    TUint idBefore, idAfter;
    AutoMutex _(iObserverLock);
    {
        AutoMutex a(iLock);
        if (iTrackList.size() == iMaxTracks) {
            THROW(TrackDbFull);
        }
        TUint index = 0;
        if (aIdAfter != kTrackIdNone) {
            index = TrackListUtils::IndexFromId(iTrackList, aIdAfter) + 1;
        }
        track = iTrackFactory.CreateTrack(aUri, aMetaData);
        aIdInserted = track->Id();
        iTrackList.insert(iTrackList.begin() + index, track);
        iSeq++;
        idBefore = aIdAfter;
        idAfter = (index == iTrackList.size()-1? kTrackIdNone : iTrackList[index+1]->Id());
    }
    for (TUint i=0; i<iObservers.size(); i++) {
        iObservers[i]->NotifyTrackInserted(*track, idBefore, idAfter);
    }
}

void TrackDatabase::Move(const std::vector<TUint32>& aIdArray, TUint aIdAfter)
{
    TUint after = aIdAfter;
    for (auto id : aIdArray) {
        auto track = DoDeleteId(id);
        Insert(after, track);
        after = id;
    }
}

void TrackDatabase::DeleteId(TUint aId)
{
    auto track = DoDeleteId(aId);
    track->RemoveRef();
}

void TrackDatabase::DeleteIds(const std::vector<TUint32>& aIdArray)
{
    for (auto id : aIdArray) {
        DeleteId(id);
    }
}

void TrackDatabase::DeleteAll()
{
    AutoMutex _(iObserverLock);
    iLock.Wait();
    const TBool changed = (iTrackList.size() > 0);
    if (changed) {
        TrackListUtils::Clear(iTrackList);
        iSeq++;
    }
    iLock.Signal();
    if (changed) {
        for (TUint i=0; i<iObservers.size(); i++) {
            iObservers[i]->NotifyAllDeleted();
        }
    }
}

void TrackDatabase::SetObserver(ITrackDatabaseObserver& aObserver)
{
    iLock.Wait();
    AddObserver(aObserver);
    iLock.Signal();
}

Track* TrackDatabase::TrackRef(TUint aId)
{
    AutoMutex a(iLock);
    return TrackReaderUtils::TrackRef(iTrackList, aId);
}

Track* TrackDatabase::NextTrackRef(TUint aId)
{
    AutoMutex a(iLock);
    return TrackReaderUtils::NextTrackRef(iTrackList, aId);
}

Track* TrackDatabase::PrevTrackRef(TUint aId)
{
    AutoMutex a(iLock);
    return TrackReaderUtils::PrevTrackRef(iTrackList, aId);
}

Track* TrackDatabase::TrackRefByIndex(TUint aIndex)
{
    AutoMutex _(iLock);
    return TrackReaderUtils::TrackRefByIndex(iTrackList, aIndex);
}

TBool TrackDatabase::IsValid(TUint aId) const
{
    AutoMutex _(iLock);
    try {
        (void)TrackListUtils::IndexFromId(iTrackList, aId);
        return true;
    }
    catch (TrackDbIdNotFound&) {}
    return false;
}

void TrackDatabase::ReportReordered(Media::Track* aStart)
{
    AutoMutex _(iObserverLock);
    for (TUint i = 0; i < iObservers.size(); i++) {
        iObservers[i]->NotifyReordered(aStart);
    }
}

TBool TrackDatabase::TryGetTrackById(TUint aId, Track*& aTrack, TUint aStartIndex, TUint aEndIndex, TUint& aFoundIndex) const
{
    for (TUint i=aStartIndex; i<aEndIndex; i++) {
        if (iTrackList[i]->Id() == aId) {
            aTrack = iTrackList[i];
            aTrack->AddRef();
            aFoundIndex = i;
            return true;
        }
    }
    return false;
}

void TrackDatabase::Insert(TUint aIdAfter, Track* aTrack)
{
    TUint idBefore, idAfter;
    AutoMutex _(iObserverLock);
    {
        AutoMutex __(iLock);
        AutoTrack tr(aTrack);
        if (iTrackList.size() == iMaxTracks) {
            THROW(TrackDbFull);
        }
        TUint index = 0;
        if (aIdAfter != kTrackIdNone) {
            index = TrackListUtils::IndexFromId(iTrackList, aIdAfter) + 1;
        }
        iTrackList.insert(iTrackList.begin() + index, aTrack);
        tr.Clear();
        iSeq++;
        idBefore = aIdAfter;
        idAfter = (index == iTrackList.size() - 1 ? kTrackIdNone : iTrackList[index + 1]->Id());
    }
    for (TUint i = 0; i < iObservers.size(); i++) {
        iObservers[i]->NotifyTrackInserted(*aTrack, idBefore, idAfter);
    }
}

Track* TrackDatabase::DoDeleteId(TUint aId)
{
    Track* before = nullptr;
    Track* after = nullptr;
    Track* track = nullptr;
    AutoMutex _(iObserverLock);
    {
        AutoMutex a(iLock);
        TUint index = TrackListUtils::IndexFromId(iTrackList, aId);
        if (index > 0) {
            before = iTrackList[index - 1];
            before->AddRef();
        }
        if (index < iTrackList.size() - 1) {
            after = iTrackList[index + 1];
            after->AddRef();
        }
        track = iTrackList[index];
        (void)iTrackList.erase(iTrackList.begin() + index);
        iSeq++;
    }
    for (TUint i = 0; i < iObservers.size(); i++) {
        iObservers[i]->NotifyTrackDeleted(aId, before, after);
    }
    RemoveRefIfNonNull(before);
    RemoveRefIfNonNull(after);
    return track;
}


// Shuffler

const TUint Shuffler::kMinTracksForShuffle = 2;

Shuffler::Shuffler(
    ITrackDatabaseReader& aReader,
    ITrackDatabaseWriter& aWriter,
    ITrackDatabaseTrackReader& aTrackReader,
    ITrackShuffleReporter& aReporter,
    TUint aMaxTracks)
    : iLock("TSHF")
    , iWriteLock("TSHW")
    , iDbReader(aReader)
    , iDbWriter(aWriter)
    , iTrackReader(aTrackReader)
    , iReporter(aReporter)
    , iObserver(nullptr)
    , iPendingInsertIdAfter(ITrackDatabaseReader::kTrackIdNone)
    , iPendingInsert(false)
    , iShuffle(false)
{
    aTrackReader.SetObserver(*this);
    iShuffleList.reserve(aMaxTracks);
}

TBool Shuffler::Enabled() const
{
    iLock.Wait();
    const TBool enabled = iShuffle;
    iLock.Signal();
    return enabled;
}

Shuffler::~Shuffler()
{
    TrackListUtils::Clear(iShuffleList);
}

void Shuffler::SetShuffle(TBool aShuffle, TUint aCurrentTrackId)
{
    Track* track = nullptr;
    {
        AutoMutex _(iLock);
        if (iShuffle == aShuffle) {
            /* No change so nothing to report.  Note that DisableShuffleIfTooFewTracks() relies on
               this - the change in shuffle state it requests is reported back to us and must not
               be treated as a re-order. */
            return;
        }
        iShuffle = aShuffle;
        if (iShuffle) { // prefer re-shuffling over repeating the order of tracks if we play again
            std::random_shuffle(iShuffleList.begin(), iShuffleList.end());
            /* Any track playing as shuffle is enabled carries on playing so belongs at the start
               of the new order. */
            MoveToStartOfShuffleListLocked(aCurrentTrackId);
        }
        LogIds("SetShuffle");

        if (iShuffle) {
            if (iShuffleList.size() > 0) {
                track = iShuffleList[0];
                track->AddRef();
            }
        }
        else {
            /* The restored order starts at the track we're playing - it carries on playing and
               the rest of the playlist follows it.  If we're not playing, it starts at the first
               track as normal. */
            track = iTrackReader.TrackRef(aCurrentTrackId);
            if (track == nullptr) {
                track = iTrackReader.TrackRefByIndex(0);
            }
        }
    }
    AutoAllocatedRef __(track);
    iReporter.ReportReordered(track);
}

void Shuffler::SetShuffleOffHandler(Functor aHandler)
{
    AutoMutex _(iLock);
    iShuffleOffHandler = aHandler;
}

void Shuffler::AddObserver(ITrackDatabaseObserver& aObserver)
{
    iDbReader.AddObserver(aObserver);
}

TUint Shuffler::IdArraySeq() const
{
    return iDbReader.IdArraySeq();
}

void Shuffler::GetIdArray(std::vector<TUint32>& aIdArray, TUint& aSeq) const
{
    AutoMutex _(iLock);
    if (iShuffle) {
        TrackDatabase::CopyIdArray(iShuffleList, aIdArray, TracksMax());
        aSeq = IdArraySeq();
    }
    else {
        iDbReader.GetIdArray(aIdArray, aSeq);
    }
}

void Shuffler::GetTrackById(TUint aId, Media::Track*& aTrack) const
{
    iDbReader.GetTrackById(aId, aTrack);
}

void Shuffler::GetTrackById(TUint aId, TUint aSeq, Media::Track*& aTrack, TUint& aIndex) const
{
    iDbReader.GetTrackById(aId, aSeq, aTrack, aIndex);
}

TUint Shuffler::TrackCount() const
{
    return iDbReader.TrackCount();
}

TUint Shuffler::TracksMax() const
{
    return iDbReader.TracksMax();
}

void Shuffler::Insert(TUint aIdAfter, const Brx& aUri, const Brx& aMetaData, TUint& aIdInserted)
{
    AutoMutex _(iWriteLock);
    TUint idAfterDb = aIdAfter;
    {
        AutoMutex __(iLock);
        /* Tracks are added in the order the user asked for in both shuffled and unshuffled lists.
           Remember the position the user asked for - NotifyTrackInserted() applies it to the
           shuffled list once the database has accepted the track. */
        iPendingInsert = iShuffle;
        iPendingInsertIdAfter = aIdAfter;
        if (IsEndOfShuffledPlaylistLocked(aIdAfter)) {
            /* Adding after the last track of the shuffled playlist means 'add to the end of the
               playlist'.  Ignore aIdAfter for the unshuffled list and append there too. */
            idAfterDb = LastDbTrackIdLocked();
        }
    }
    try {
        iDbWriter.Insert(idAfterDb, aUri, aMetaData, aIdInserted);
    }
    catch (...) {
        ClearPendingInsert();
        throw;
    }
    ClearPendingInsert();
}

void Shuffler::Move(const std::vector<TUint32>& aIdArray, TUint aIdAfter)
{
    AutoMutex _(iWriteLock);
    TUint idAfterDb = aIdAfter;
    {
        AutoMutex __(iLock);
        if (IsEndOfShuffledPlaylistLocked(aIdAfter)) {
            /* As for Insert() - moving after the last track of the shuffled playlist means 'move
               to the end of the playlist' so move to the end of the unshuffled list too.  The
               anchor must be a track which isn't itself being moved; the database deletes then
               re-inserts each track in turn so would fail to find one that had already moved. */
            idAfterDb = LastDbTrackIdLocked(&aIdArray);
            /* Only the first track needs its shuffled position overridden.  Later tracks are
               inserted after their predecessor from aIdArray, which is the same in both lists. */
            iPendingInsert = true;
            iPendingInsertIdAfter = aIdAfter;
        }
    }
    try {
        iDbWriter.Move(aIdArray, idAfterDb);
    }
    catch (...) {
        ClearPendingInsert();
        throw;
    }
    ClearPendingInsert();
}

void Shuffler::DeleteId(TUint aId)
{
    {
        AutoMutex _(iWriteLock);
        iDbWriter.DeleteId(aId); // nothing is deleted if this throws
    }
    DisableShuffleIfTooFewTracks();
}

void Shuffler::DeleteIds(const std::vector<TUint32>& aIdArray)
{
    try {
        AutoMutex _(iWriteLock);
        iDbWriter.DeleteIds(aIdArray);
    }
    catch (...) { // some tracks may have been deleted before the failure
        DisableShuffleIfTooFewTracks();
        throw;
    }
    DisableShuffleIfTooFewTracks();
}

void Shuffler::DeleteAll()
{
    {
        AutoMutex _(iWriteLock);
        iDbWriter.DeleteAll();
    }
    DisableShuffleIfTooFewTracks();
}

void Shuffler::SetObserver(ITrackDatabaseObserver& aObserver)
{
    iLock.Wait();
    iObserver = &aObserver;
    iLock.Signal();
}

Track* Shuffler::TrackRef(TUint aId)
{
    AutoMutex a(iLock);
    if (iShuffle) {
        return TrackReaderUtils::TrackRef(iShuffleList, aId);
    }
    return iTrackReader.TrackRef(aId);
}

Track* Shuffler::NextTrackRef(TUint aId)
{
    AutoMutex a(iLock);
    if (iShuffle) {
        return TrackReaderUtils::NextTrackRef(iShuffleList, aId);
    }
    return iTrackReader.NextTrackRef(aId);
}

Track* Shuffler::PrevTrackRef(TUint aId)
{
    AutoMutex a(iLock);
    if (iShuffle) {
        return TrackReaderUtils::PrevTrackRef(iShuffleList, aId);
    }
    return iTrackReader.PrevTrackRef(aId);
}

Track* Shuffler::TrackRefByIndex(TUint aIndex)
{
    AutoMutex a(iLock);
    if (iShuffle) {
        return TrackReaderUtils::TrackRefByIndex(iShuffleList, aIndex);
    }
    return iTrackReader.TrackRefByIndex(aIndex);
}

TBool Shuffler::IsValid(TUint aId) const
{
    return iTrackReader.IsValid(aId);
}

void Shuffler::NotifyTrackInserted(Track& aTrack, TUint aIdBefore, TUint aIdAfter)
{
    TUint idBefore = aIdBefore;
    TUint idAfter = aIdAfter;
    {
        AutoMutex a(iLock);
        /* aIdBefore is the id the track was inserted after in the unshuffled list.  That is also
           the position the user asked for unless we adjusted it in Insert() above, so prefer the
           id the user gave us for any insertion which came via us. */
        const TUint idAfterShuffle = (iPendingInsert? iPendingInsertIdAfter : aIdBefore);
        iPendingInsert = false;
        const TUint index = ShuffleIndexForIdAfterLocked(idAfterShuffle);
        iShuffleList.insert(iShuffleList.begin() + index, &aTrack);
        aTrack.AddRef();
        if (iShuffle) {
            idBefore = (index == 0? ITrackDatabaseReader::kTrackIdNone : iShuffleList[index-1]->Id());
            idAfter = (index == iShuffleList.size()-1? ITrackDatabaseReader::kTrackIdNone : iShuffleList[index+1]->Id());
            LogIds("TrackInserted");
        }
    }
    iObserver->NotifyTrackInserted(aTrack, idBefore, idAfter);
}

void Shuffler::NotifyTrackDeleted(TUint aId, Track* aBefore, Track* aAfter)
{
    Track* before = aBefore;
    Track* after = aAfter;
    try {
        AutoMutex a(iLock);
        const TUint index = TrackListUtils::IndexFromId(iShuffleList, aId);
        if (iShuffle) {
            before = (index==0? nullptr : iShuffleList[index-1]);
            after = (index==iShuffleList.size()-1? nullptr : iShuffleList[index+1]);
        }
        iShuffleList[index]->RemoveRef();
        iShuffleList.erase(iShuffleList.begin() + index);
        LogIds("TrackDeleted");
        AddRefIfNonNull(before);
        AddRefIfNonNull(after);
    }
    catch (TrackDbIdNotFound&) {
        return;
    }
    iObserver->NotifyTrackDeleted(aId, before, after);
    RemoveRefIfNonNull(before);
    RemoveRefIfNonNull(after);
}

void Shuffler::NotifyAllDeleted()
{
    iLock.Wait();
    TrackListUtils::Clear(iShuffleList);
    iLock.Signal();
    iObserver->NotifyAllDeleted();
}

void Shuffler::NotifyReordered(Track* aStart)
{
    iObserver->NotifyReordered(aStart);
}

void Shuffler::MoveToStartOfShuffleListLocked(TUint aId)
{
    if (aId == ITrackDatabaseReader::kTrackIdNone) {
        return;
    }
    try {
        const TUint index = TrackListUtils::IndexFromId(iShuffleList, aId);
        if (index > 0) {
            Track* track = iShuffleList[index];
            (void)iShuffleList.erase(iShuffleList.begin() + index);
            iShuffleList.insert(iShuffleList.begin(), track); // no change in ref count
        }
    }
    catch (TrackDbIdNotFound&) {} // track is no longer in the playlist
}

TBool Shuffler::IsEndOfShuffledPlaylistLocked(TUint aIdAfter) const
{
    return (iShuffle
            && aIdAfter != ITrackDatabaseReader::kTrackIdNone
            && iShuffleList.size() > 0
            && iShuffleList[iShuffleList.size()-1]->Id() == aIdAfter);
}

TUint Shuffler::ShuffleIndexForIdAfterLocked(TUint aIdAfter) const
{
    if (aIdAfter == ITrackDatabaseReader::kTrackIdNone) {
        return 0;
    }
    try {
        return TrackListUtils::IndexFromId(iShuffleList, aIdAfter) + 1;
    }
    catch (TrackDbIdNotFound&) {
        return iShuffleList.size(); // shouldn't happen - append rather than lose the track
    }
}

TUint Shuffler::LastDbTrackIdLocked(const std::vector<TUint32>* aIgnoreIds)
{
    // returns kTrackIdNone if the unshuffled list is empty or holds only ignored tracks
    TUint count = iDbReader.TrackCount();
    while (count > 0) {
        Track* track = iTrackReader.TrackRefByIndex(--count);
        if (track == nullptr) {
            break;
        }
        const TUint id = track->Id();
        track->RemoveRef();
        if (aIgnoreIds == nullptr
            || std::find(aIgnoreIds->begin(), aIgnoreIds->end(), id) == aIgnoreIds->end()) {
            return id;
        }
    }
    return ITrackDatabaseReader::kTrackIdNone;
}

void Shuffler::ClearPendingInsert()
{
    AutoMutex _(iLock);
    iPendingInsert = false;
}

void Shuffler::DisableShuffleIfTooFewTracks()
{
    /* Shuffle has no meaning for a playlist of 0 or 1 tracks.  Disable it - both track lists are
       identical at this size so there is no re-order to report - and tell our owner so that the
       change in shuffle state can be reported to control points. */
    Functor handler;
    {
        AutoMutex _(iLock);
        if (!iShuffle || iShuffleList.size() >= kMinTracksForShuffle) {
            return;
        }
        iShuffle = false;
        LogIds("ShuffleDisabled");
        handler = iShuffleOffHandler;
    }
    handler();
}

void Shuffler::LogIds(const TChar* aPrefix)
{
    LOG(kSources, "%s.  New track order is: { ", aPrefix);
    if (iShuffleList.size() > 0) {
        LOG(kSources, "%u", iShuffleList[0]->Id());
        for (TUint i=1; i<iShuffleList.size(); i++) {
            LOG(kSources, ", %u", iShuffleList[i]->Id());
        }
    }
    LOG(kSources, "}\n");
}


// Repeater

Repeater::Repeater(ITrackDatabaseTrackReader& aTrackReader)
    : iLock("TRPT")
    , iTrackReader(aTrackReader)
    , iObserver(nullptr)
    , iRepeat(false)
    , iTrackCount(0)
{
}

void Repeater::SetRepeat(TBool aRepeat)
{
    iRepeat = aRepeat;
}

void Repeater::SetObserver(ITrackDatabaseObserver& aObserver)
{
    iObserver = &aObserver;
    iTrackReader.SetObserver(*this);
}

Track* Repeater::TrackRef(TUint aId)
{
    AutoMutex a(iLock);
    Track* track = iTrackReader.TrackRef(aId);
    if (track == nullptr && iRepeat) {
        track = iTrackReader.TrackRef(ITrackDatabaseReader::kTrackIdNone);
    }
    return track;
}

Track* Repeater::NextTrackRef(TUint aId)
{
    AutoMutex a(iLock);
    Track* track = iTrackReader.NextTrackRef(aId);
    if (track == nullptr && iRepeat) {
        track = iTrackReader.NextTrackRef(ITrackDatabaseReader::kTrackIdNone);
    }
    return track;
}

Track* Repeater::PrevTrackRef(TUint aId)
{
    AutoMutex a(iLock);
    Track* track = iTrackReader.PrevTrackRef(aId);
    if (track == nullptr && iRepeat) {
        track = iTrackReader.TrackRefByIndex(iTrackCount-1);
    }
    return track;
}

Track* Repeater::TrackRefByIndex(TUint aIndex)
{
    return iTrackReader.TrackRefByIndex(aIndex);
}

TBool Repeater::IsValid(TUint aId) const
{
    return iTrackReader.IsValid(aId);
}

void Repeater::NotifyTrackInserted(Track& aTrack, TUint aIdBefore, TUint aIdAfter)
{
    iLock.Wait();
    iTrackCount++;
    iLock.Signal();
    iObserver->NotifyTrackInserted(aTrack, aIdBefore, aIdAfter);
}

void Repeater::NotifyTrackDeleted(TUint aId, Track* aBefore, Track* aAfter)
{
    iLock.Wait();
    iTrackCount--;
    iLock.Signal();
    iObserver->NotifyTrackDeleted(aId, aBefore, aAfter);
}

void Repeater::NotifyAllDeleted()
{
    iLock.Wait();
    iTrackCount = 0;
    iLock.Signal();
    iObserver->NotifyAllDeleted();
}

void Repeater::NotifyReordered(Track* aStart)
{
    iObserver->NotifyReordered(aStart);
}


// TrackListUtils

TUint TrackListUtils::IndexFromId(const std::vector<Track*>& aList, TUint aId)
{ // static
    for (TUint i=0; i<aList.size(); i++) {
        if (aList[i]->Id() == aId) {
            return i;
        }
    }
    THROW(TrackDbIdNotFound);
}

void TrackListUtils::Clear(std::vector<Track*>& aList)
{ // static
    for (TUint i=0; i<aList.size(); i++) {
        aList[i]->RemoveRef();
    }
    aList.clear();
}


// TrackReaderUtils

Track* TrackReaderUtils::TrackRef(const std::vector<Media::Track*>& aList, TUint aId)
{
    Track* track = nullptr;
    try {
        const TUint index = TrackListUtils::IndexFromId(aList, aId);
        track = aList[index];
        track->AddRef();
    }
    catch (TrackDbIdNotFound&) {}
    return track;
}

Track* TrackReaderUtils::NextTrackRef(const std::vector<Media::Track*>& aList, TUint aId)
{
    Track* track = nullptr;
    if (aId == ITrackDatabaseReader::kTrackIdNone) {
        if (aList.size() > 0) {
            track = aList[0];
            track->AddRef();
        }
    }
    else {
        try {
            const TUint index = TrackListUtils::IndexFromId(aList, aId);
            if (index < aList.size() - 1) {
                track = aList[index + 1];
                track->AddRef();
            }
        }
        catch (TrackDbIdNotFound&) {}
    }
    return track;
}

Track* TrackReaderUtils::PrevTrackRef(const std::vector<Media::Track*>& aList, TUint aId)
{
    Track* track = nullptr;
    try {
        const TUint index = TrackListUtils::IndexFromId(aList, aId);
        if (index > 0) {
            track = aList[index - 1];
            track->AddRef();
        }
    }
    catch (TrackDbIdNotFound&) {}
    return track;
}

Track* TrackReaderUtils::TrackRefByIndex(const std::vector<Media::Track*>& aList, TUint aIndex)
{
    Track* track = nullptr;
    if (aIndex < aList.size()) {
        track = aList[aIndex];
        track->AddRef();
    }
    return track;
}


// AutoTrack

AutoTrack::AutoTrack(Track* aTrack)
    : iTrack(aTrack)
{
}

AutoTrack::~AutoTrack()
{
    if (iTrack != nullptr) {
        iTrack->RemoveRef();
    }
}

void AutoTrack::Clear()
{
    iTrack = nullptr;
}

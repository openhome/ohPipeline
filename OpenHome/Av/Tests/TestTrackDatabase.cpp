#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Av/Playlist/TrackDatabase.h>
#include <OpenHome/Private/SuiteUnitTest.h>
#include <OpenHome/Media/Utils/AllocatorInfoLogger.h>
#include <OpenHome/Media/Pipeline/Msg.h>

#include <limits.h>
#include <array>
#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Av;
using namespace OpenHome::Media;

namespace OpenHome {
namespace Av {

class SuiteTrackDatabase : public SuiteUnitTest, private ITrackDatabaseObserver
{
    static const TUint kMaxTracks = 100;
public:
    SuiteTrackDatabase();
private: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private: // from ITrackDatabaseObserver
    void NotifyTrackInserted(Media::Track& aTrack, TUint aIdBefore, TUint aIdAfter) override;
    void NotifyTrackDeleted(TUint aId, Media::Track* aBefore, Media::Track* aAfter) override;
    void NotifyAllDeleted() override;
    void NotifyReordered(Media::Track* aStart) override;
private:
    void InsertInitialTrack();
    void InsertFailsWhenIdAfterInvalid();
    void InsertFailsWhenFull();
    void GetIdArrayDbEmpty();
    void GetIdArrayDbPartiallyFull();
    void GetIdArrayDbFull();
    void InsertAtStart();
    void InsertInMiddle();
    void InsertAtEnd();
    void InsertAfterDeleteReportsIds();
    void DeleteValidId();
    void DeleteInvalidId();
    void DeleteAll();
    void DeleteMultiple();
    void Move();
    void SeqUpdatesOnChanges();
    void GetTrackByValidId();
    void GetTrackByInvalidIdFails();
    void GetTrackByIdValidSeq();
    void GetTrackByIdInvalidSeq();
    void MultipleObservers();
private:
    Media::AllocatorInfoLogger iInfoAggregator;
    TrackFactory* iTrackFactory;
    TrackDatabase* iDb;
    ITrackDatabaseReader* iTrackDbReader;
    ITrackDatabaseWriter* iTrackDbWriter;
    std::vector<TUint32> iIdArray;
    TUint iInsertedCount;
    TUint iIdLastInserted;
    TUint iIdLastInsertedBefore;
    TUint iIdLastInsertedAfter;
    TUint iDeletedCount;
    TUint iIdLastDeleted;
    TUint iIdLastDeletedBefore;
    TUint iIdLastDeletedAfter;
    TUint iAllDeletedCount;
};

class SuiteTrackReader : public SuiteUnitTest, private ITrackDatabaseObserver
{
    static const TUint kMaxTracks = 100;
public:
    SuiteTrackReader();
private: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private: // from ITrackDatabaseObserver
    void NotifyTrackInserted(Media::Track& aTrack, TUint aIdBefore, TUint aIdAfter) override;
    void NotifyTrackDeleted(TUint aId, Media::Track* aBefore, Media::Track* aAfter) override;
    void NotifyAllDeleted() override;
    void NotifyReordered(Media::Track* aStart) override;
private:
    void TrackRefValidId();
    void TrackRefInvalidId();
    void NextTrackRefValidId();
    void NextTrackRefInvalidId();
    void PrevTrackRefValidId();
    void PrevTrackRefInvalidId();
    void TrackRefByIndexValidId();
    void TrackRefByIndexInvalidId();
    void IsValidValidId();
    void IsValidInvalidId();
private:
    static const TUint kNumTracks = 3;
    Media::AllocatorInfoLogger iInfoAggregator;
    TrackFactory* iTrackFactory;
    TrackDatabase* iDb;
    ITrackDatabaseTrackReader* iReader;
    TUint iIds[kNumTracks];
};

class SuiteShuffler : public SuiteUnitTest, private ITrackDatabaseObserver
{
    static const TUint kMaxTracks = 60;
public:
    SuiteShuffler();
private: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private: // from ITrackDatabaseObserver
    void NotifyTrackInserted(Media::Track& aTrack, TUint aIdBefore, TUint aIdAfter) override;
    void NotifyTrackDeleted(TUint aId, Media::Track* aBefore, Media::Track* aAfter) override;
    void NotifyAllDeleted() override;
    void NotifyReordered(Media::Track* aStart) override;
private:
    void TrackRefShuffleOff();
    void TrackRefShuffleOn();
    void NextTrackRefShuffleOff();
    void NextTrackRefShuffleOn();
    void PrevTrackRefShuffleOff();
    void PrevTrackRefShuffleOn();
    void TrackRefByIndexShuffleOff();
    void TrackRefByIndexShuffleOn();
    void NextTrackBeyondEndNoReshuffle();
    void ShuffleOffRestoresUserOrder();
    void SetShuffleWhilePlayingPutsCurrentTrackFirst();
    void SetShuffleWhenNotPlayingStartsAtFirstShuffledTrack();
    void SetShuffleWithUnknownCurrentTrackShuffles();
    void SetShuffleOffWhilePlayingKeepsCurrentTrack();
    void SetShuffleOffWhenNotPlayingStartsAtFirstTrack();
    void InsertShuffleOnFollowsUserOrder();
    void InsertAfterLastShuffledTrackAppends();
    void MoveShuffleOnFollowsUserOrder();
    void MoveToEndOfShuffledPlaylistMovesToEnd();
    void DeleteToSingleTrackDisablesShuffle();
    void DeleteIdsToEmptyDisablesShuffle();
    void DeleteIdsPartialFailureDisablesShuffle();
    void DeleteAllDisablesShuffle();
private:
    void ShuffleOffRequested();
    void ReshuffleUntilLastTracksDiffer();
    std::vector<TUint32> ShuffledIds() const; // order the user sees (shuffled iff shuffle is on)
    std::vector<TUint32> UnshuffledIds() const;
private:
    static const TUint kNumTracks = 16; // gives us ~1 in 21 trillion chance of shuffling tracks into their original order
    Media::AllocatorInfoLogger iInfoAggregator;
    TrackFactory* iTrackFactory;
    TrackDatabase* iDb;
    Shuffler* iShuffler;
    ITrackDatabaseTrackReader* iReader;
    ITrackDatabaseWriter* iWriter;
    std::array<TUint, kNumTracks> iIds;
    TUint iShuffleOffCount;
    TUint iReorderedCount;
    TUint iIdLastReordered;
};

class SuiteRepeater : public SuiteUnitTest, private ITrackDatabaseObserver
{
    static const TUint kMaxTracks = 85;
public:
    SuiteRepeater();
private: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private: // from ITrackDatabaseObserver
    void NotifyTrackInserted(Media::Track& aTrack, TUint aIdBefore, TUint aIdAfter) override;
    void NotifyTrackDeleted(TUint aId, Media::Track* aBefore, Media::Track* aAfter) override;
    void NotifyAllDeleted() override;
    void NotifyReordered(Media::Track* aStart) override;
private:
    void TrackRefRepeatOff();
    void TrackRefRepeatOn();
    void NextFromLastTrackRepeatOff();
    void NextFromLastTrackRepeatOn();
    void PrevFromFirstTrackRepeatOff();
    void PrevFromFirstTrackRepeatOn();
    void TrackRefByIndexRepeatOff();
    void TrackRefByIndexRepeatOn();
private:
    static const TUint kNumTracks = 3;
    Media::AllocatorInfoLogger iInfoAggregator;
    TrackFactory* iTrackFactory;
    TrackDatabase* iDb;
    Shuffler* iShuffler;
    Repeater* iRepeater;
    ITrackDatabaseTrackReader* iReader;
    std::array<TUint, kNumTracks> iIds;
};

} // namespace Av
} // namespace OpenHome

// SuiteTrackDatabase

SuiteTrackDatabase::SuiteTrackDatabase()
    : SuiteUnitTest("Track database (ITrackDatabase)")
{
    AddTest(MakeFunctor(*this, &SuiteTrackDatabase::InsertInitialTrack), "InsertInitialTrack");
    AddTest(MakeFunctor(*this, &SuiteTrackDatabase::InsertFailsWhenIdAfterInvalid), "InsertFailsWhenIdAfterInvalid");
    AddTest(MakeFunctor(*this, &SuiteTrackDatabase::InsertFailsWhenFull), "InsertFailsWhenFull");
    AddTest(MakeFunctor(*this, &SuiteTrackDatabase::GetIdArrayDbEmpty), "GetIdArrayDbEmpty");
    AddTest(MakeFunctor(*this, &SuiteTrackDatabase::GetIdArrayDbPartiallyFull), "GetIdArrayDbPartiallyFull");
    AddTest(MakeFunctor(*this, &SuiteTrackDatabase::GetIdArrayDbFull), "GetIdArrayDbFull");
    AddTest(MakeFunctor(*this, &SuiteTrackDatabase::InsertAtStart), "InsertAtStart");
    AddTest(MakeFunctor(*this, &SuiteTrackDatabase::InsertInMiddle), "InsertInMiddle");
    AddTest(MakeFunctor(*this, &SuiteTrackDatabase::InsertAtEnd), "InsertAtEnd");
    AddTest(MakeFunctor(*this, &SuiteTrackDatabase::InsertAfterDeleteReportsIds), "InsertAfterDeleteReportsIds");
    AddTest(MakeFunctor(*this, &SuiteTrackDatabase::DeleteValidId), "DeleteValidId");
    AddTest(MakeFunctor(*this, &SuiteTrackDatabase::DeleteInvalidId), "DeleteInvalidId");
    AddTest(MakeFunctor(*this, &SuiteTrackDatabase::DeleteAll), "DeleteAll");
    AddTest(MakeFunctor(*this, &SuiteTrackDatabase::DeleteMultiple), "DeleteMultiple");
    AddTest(MakeFunctor(*this, &SuiteTrackDatabase::Move), "Move");
    AddTest(MakeFunctor(*this, &SuiteTrackDatabase::SeqUpdatesOnChanges), "SeqUpdatesOnChanges");
    AddTest(MakeFunctor(*this, &SuiteTrackDatabase::GetTrackByValidId), "GetTrackByValidId");
    AddTest(MakeFunctor(*this, &SuiteTrackDatabase::GetTrackByInvalidIdFails), "GetTrackByInvalidIdFails");
    AddTest(MakeFunctor(*this, &SuiteTrackDatabase::GetTrackByIdValidSeq), "GetTrackByIdValidSeq");
    AddTest(MakeFunctor(*this, &SuiteTrackDatabase::GetTrackByIdInvalidSeq), "GetTrackByIdInvalidSeq");
    AddTest(MakeFunctor(*this, &SuiteTrackDatabase::MultipleObservers), "MultipleObservers");
}

void SuiteTrackDatabase::Setup()
{
    iIdArray.reserve(kMaxTracks);
    iTrackFactory = new TrackFactory(iInfoAggregator, kMaxTracks);
    iDb = new TrackDatabase(*iTrackFactory, kMaxTracks);
    iTrackDbReader = iDb;
    iTrackDbWriter = iDb;

    iTrackDbReader->AddObserver(*this);
    iInsertedCount = iDeletedCount = iAllDeletedCount = 0;
    iIdLastInserted = iIdLastInsertedBefore = iIdLastInsertedAfter = 
        iIdLastDeleted = iIdLastDeletedBefore = iIdLastDeletedAfter = UINT_MAX;
}

void SuiteTrackDatabase::TearDown()
{
    delete iDb;
    delete iTrackFactory;
}

void SuiteTrackDatabase::NotifyTrackInserted(Track& aTrack, TUint aIdBefore, TUint aIdAfter)
{
    iInsertedCount++;
    iIdLastInserted = aTrack.Id();
    iIdLastInsertedBefore = aIdBefore;
    iIdLastInsertedAfter = aIdAfter;
}

void SuiteTrackDatabase::NotifyTrackDeleted(TUint aId, Track* aBefore, Track* aAfter)
{
    iDeletedCount++;
    iIdLastDeleted = aId;
    iIdLastDeletedBefore = (aBefore==nullptr? ITrackDatabaseReader::kTrackIdNone : aBefore->Id());
    iIdLastDeletedAfter = (aAfter==nullptr? ITrackDatabaseReader::kTrackIdNone : aAfter->Id());
}

void SuiteTrackDatabase::NotifyAllDeleted()
{
    iAllDeletedCount++;
}

void SuiteTrackDatabase::NotifyReordered(Track* /*aStart*/)
{
}

void SuiteTrackDatabase::InsertInitialTrack()
{
    TUint inserted;
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), inserted);
    TEST(iInsertedCount == 1);
    TEST(inserted != ITrackDatabaseReader::kTrackIdNone);
    TEST(iIdLastInserted == inserted);
    TEST(iIdLastInsertedBefore == ITrackDatabaseReader::kTrackIdNone);
    TEST(iIdLastInsertedAfter == ITrackDatabaseReader::kTrackIdNone);
}

void SuiteTrackDatabase::InsertFailsWhenIdAfterInvalid()
{
    TUint inserted;
    TEST_THROWS(iTrackDbWriter->Insert(1, Brx::Empty(), Brx::Empty(), inserted), TrackDbIdNotFound);
    TEST(iInsertedCount == 0);
}

void SuiteTrackDatabase::InsertFailsWhenFull()
{
    TUint after = ITrackDatabaseReader::kTrackIdNone;
    TUint newId;
    for (TUint i=0; i<kMaxTracks; i++) {
        iTrackDbWriter->Insert(after, Brx::Empty(), Brx::Empty(), newId);
        after = newId;
    }
    TEST_THROWS(iTrackDbWriter->Insert(after, Brx::Empty(), Brx::Empty(), newId), TrackDbFull);
    TEST_THROWS(iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), newId), TrackDbFull);
}

void SuiteTrackDatabase::GetIdArrayDbEmpty()
{
    TUint seq;
    iTrackDbReader->GetIdArray(iIdArray, seq);
    for (TUint i=0; i<kMaxTracks; i++) {
        TEST_QUIETLY(iIdArray[i] == ITrackDatabaseReader::kTrackIdNone);
    }
}

void SuiteTrackDatabase::GetIdArrayDbPartiallyFull()
{
    static const TUint kTrackCount = 100;
    TUint i;
    TUint after = ITrackDatabaseReader::kTrackIdNone;
    TUint newId;
    for (i=0; i<kTrackCount; i++) {
        iTrackDbWriter->Insert(after, Brx::Empty(), Brx::Empty(), newId);
        after = newId;
    }
    TUint seq;
    iTrackDbReader->GetIdArray(iIdArray, seq);
    std::array<TUint32, kTrackCount> trackIds;
    trackIds.fill((TUint)ITrackDatabaseReader::kTrackIdNone);
    for (i=0; i<kTrackCount; i++) {
        const TUint id = iIdArray[i];
        TEST_QUIETLY(id != ITrackDatabaseReader::kTrackIdNone);
        auto it = std::find(trackIds.begin(), trackIds.end(), id);
        TEST(it == trackIds.end()); // check that each track id is unique
        trackIds[i] = id;
    }
    for (i=kTrackCount; i<kMaxTracks; i++) {
        TEST_QUIETLY(iIdArray[i] == ITrackDatabaseReader::kTrackIdNone);
    }
}

void SuiteTrackDatabase::GetIdArrayDbFull()
{
    TUint after = ITrackDatabaseReader::kTrackIdNone;
    TUint newId;
    for (TUint i=0; i<kMaxTracks; i++) {
        iTrackDbWriter->Insert(after, Brx::Empty(), Brx::Empty(), newId);
        after = newId;
    }
    TUint seq;
    iTrackDbReader->GetIdArray(iIdArray, seq);
    for (TUint i=0; i<kMaxTracks; i++) {
        TEST_QUIETLY(iIdArray[i] != ITrackDatabaseReader::kTrackIdNone);
    }
}

void SuiteTrackDatabase::InsertAtStart()
{
    TUint ids[2];
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), ids[0]);
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), ids[1]);
    TEST(iInsertedCount == 2);
    TEST(iIdLastInserted == ids[1]);
    TEST(iIdLastInsertedBefore == ITrackDatabaseReader::kTrackIdNone);
    TEST(iIdLastInsertedAfter == ids[0]);
}

void SuiteTrackDatabase::InsertInMiddle()
{
    TUint ids[3];
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), ids[0]);
    iTrackDbWriter->Insert(ids[0], Brx::Empty(), Brx::Empty(), ids[1]);
    iTrackDbWriter->Insert(ids[0], Brx::Empty(), Brx::Empty(), ids[2]);
    TEST(iInsertedCount == 3);
    TEST(iIdLastInserted == ids[2]);
    TEST(iIdLastInsertedBefore == ids[0]);
    TEST(iIdLastInsertedAfter == ids[1]);
}

void SuiteTrackDatabase::InsertAtEnd()
{
    TUint ids[3];
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), ids[0]);
    iTrackDbWriter->Insert(ids[0], Brx::Empty(), Brx::Empty(), ids[1]);
    iTrackDbWriter->Insert(ids[1], Brx::Empty(), Brx::Empty(), ids[2]);
    TEST(iInsertedCount == 3);
    TEST(iIdLastInserted == ids[2]);
    TEST(iIdLastInsertedBefore == ids[1]);
    TEST(iIdLastInsertedAfter == ITrackDatabaseReader::kTrackIdNone);
}

void SuiteTrackDatabase::InsertAfterDeleteReportsIds()
{
    // ids of neighbouring tracks are only the same as their indices in a playlist which has
    // never had a track deleted from it
    TUint ids[4];
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), ids[0]);
    iTrackDbWriter->Insert(ids[0], Brx::Empty(), Brx::Empty(), ids[1]);
    iTrackDbWriter->Insert(ids[1], Brx::Empty(), Brx::Empty(), ids[2]);
    iTrackDbWriter->DeleteId(ids[0]); // playlist is now { ids[1], ids[2] }
    iTrackDbWriter->Insert(ids[1], Brx::Empty(), Brx::Empty(), ids[3]);
    TEST(iIdLastInserted == ids[3]);
    TEST(iIdLastInsertedBefore == ids[1]);
    TEST(iIdLastInsertedAfter == ids[2]);
}

void SuiteTrackDatabase::DeleteValidId()
{
    TUint id;
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), id);
    iTrackDbWriter->DeleteId(id);
    TEST(iDeletedCount == 1);
    TEST(iIdLastDeleted == id);
    TEST(iIdLastDeletedBefore == ITrackDatabaseReader::kTrackIdNone);
    TEST(iIdLastDeletedAfter == ITrackDatabaseReader::kTrackIdNone);

    TUint ids[3];
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), ids[0]);
    iTrackDbWriter->Insert(ids[0], Brx::Empty(), Brx::Empty(), ids[1]);
    iTrackDbWriter->Insert(ids[1], Brx::Empty(), Brx::Empty(), ids[2]);
    iTrackDbWriter->DeleteId(ids[1]);
    TEST(iDeletedCount == 2);
    TEST(iIdLastDeleted == ids[1]);
    TEST(iIdLastDeletedBefore == ids[0]);
    TEST(iIdLastDeletedAfter == ids[2]);

    TUint seq;
    iTrackDbReader->GetIdArray(iIdArray, seq);
    int count = std::count_if(iIdArray.begin(), iIdArray.end(), [](TUint aId) {return aId != ITrackDatabaseReader::kTrackIdNone;});
    TEST(count == 2);
}

void SuiteTrackDatabase::DeleteInvalidId()
{
    TEST_THROWS(iTrackDbWriter->DeleteId(ITrackDatabaseReader::kTrackIdNone), TrackDbIdNotFound);
    TEST_THROWS(iTrackDbWriter->DeleteId(1), TrackDbIdNotFound);
    TEST_THROWS(iTrackDbWriter->DeleteId(UINT_MAX), TrackDbIdNotFound);
    TUint id;
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), id);
    TEST_THROWS(iTrackDbWriter->DeleteId(id+1), TrackDbIdNotFound);
    TEST(iAllDeletedCount == 0);
}

void SuiteTrackDatabase::DeleteAll()
{
    TUint deleteCbCount = 0;
    TEST(iAllDeletedCount == deleteCbCount);
    iTrackDbWriter->DeleteAll();
    TEST(iAllDeletedCount == deleteCbCount);

    TUint id;
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), id);
    iTrackDbWriter->DeleteAll();
    TEST(iAllDeletedCount == ++deleteCbCount);
    TUint seq;
    iTrackDbReader->GetIdArray(iIdArray, seq);
    int count = std::count_if(iIdArray.begin(), iIdArray.end(), [](TUint aId) {return aId != ITrackDatabaseReader::kTrackIdNone;});
    TEST(count == 0);

    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), id);
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), id);
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), id);
    iTrackDbWriter->DeleteAll();
    TEST(iAllDeletedCount == ++deleteCbCount);
    iTrackDbReader->GetIdArray(iIdArray, seq);
    count = std::count_if(iIdArray.begin(), iIdArray.end(), [](TUint aId) {return aId != ITrackDatabaseReader::kTrackIdNone;});
    TEST(count == 0);
}

void SuiteTrackDatabase::DeleteMultiple()
{
    TUint seq;
    iTrackDbReader->GetIdArray(iIdArray, seq);
    int count = std::count_if(iIdArray.begin(), iIdArray.end(), [](TUint aId) {return aId != ITrackDatabaseReader::kTrackIdNone; });
    TEST(count == 0);

    TUint id;
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), id);
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), id);
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), id);
    iTrackDbReader->GetIdArray(iIdArray, seq);
    count = std::count_if(iIdArray.begin(), iIdArray.end(), [](TUint aId) {return aId != ITrackDatabaseReader::kTrackIdNone; });
    TEST(count == 3);

    std::vector<TUint32> toDelete;
    toDelete.push_back(iIdArray[1]);
    toDelete.push_back(iIdArray[2]);
    const auto remainingId = iIdArray[0];
    iTrackDbWriter->DeleteIds(toDelete);
    iTrackDbReader->GetIdArray(iIdArray, seq);
    count = std::count_if(iIdArray.begin(), iIdArray.end(), [](TUint aId) {return aId != ITrackDatabaseReader::kTrackIdNone; });
    TEST(count == 1);
    TEST(iIdArray[0] == remainingId);
}

void SuiteTrackDatabase::Move()
{
    TUint seq;
    iTrackDbReader->GetIdArray(iIdArray, seq);
    int count = std::count_if(iIdArray.begin(), iIdArray.end(), [](TUint aId) {return aId != ITrackDatabaseReader::kTrackIdNone; });
    TEST(count == 0);

    TUint id;
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), id);
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), id);
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), id);
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), id);
    iTrackDbReader->GetIdArray(iIdArray, seq);
    count = std::count_if(iIdArray.begin(), iIdArray.end(), [](TUint aId) {return aId != ITrackDatabaseReader::kTrackIdNone; });
    TEST(count == 4);

    std::vector<TUint32> toMove;
    toMove.push_back(iIdArray[3]);
    toMove.push_back(iIdArray[2]);
    std::vector<TUint32> expected{ iIdArray[0], iIdArray[3], iIdArray[2], iIdArray[1] };
    iTrackDbWriter->Move(toMove, iIdArray[0]);
    iTrackDbReader->GetIdArray(iIdArray, seq);
    count = std::count_if(iIdArray.begin(), iIdArray.end(), [](TUint aId) {return aId != ITrackDatabaseReader::kTrackIdNone; });
    TEST(count == 4);
    int index = 0;
    while (iIdArray[index] != ITrackDatabaseReader::kTrackIdNone) {
        TEST(iIdArray[index] == expected[index]);
        index++;
    }
}

void SuiteTrackDatabase::SeqUpdatesOnChanges()
{
    TUint seq;
    iTrackDbReader->GetIdArray(iIdArray, seq);
    TUint prevSeq = seq;
    TUint id;
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), id);
    iTrackDbReader->GetIdArray(iIdArray, seq);
    TEST(seq == prevSeq+1);
    prevSeq = seq;

    iTrackDbWriter->DeleteId(id);
    iTrackDbReader->GetIdArray(iIdArray, seq);
    TEST(seq == prevSeq+1);
    prevSeq = seq;

    try {
        iTrackDbWriter->DeleteId(id);
    }
    catch (TrackDbIdNotFound&) {}
    iTrackDbReader->GetIdArray(iIdArray, seq);
    TEST(seq == prevSeq);

    iTrackDbWriter->DeleteAll();
    iTrackDbReader->GetIdArray(iIdArray, seq);
    TEST(seq == prevSeq);

    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), id);
    iTrackDbReader->GetIdArray(iIdArray, seq);
    prevSeq = seq;
    iTrackDbWriter->DeleteAll();
    iTrackDbReader->GetIdArray(iIdArray, seq);
    TEST(seq == prevSeq+1);
}

void SuiteTrackDatabase::GetTrackByValidId()
{
    TUint id;
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), id);
    Track* track;
    iTrackDbReader->GetTrackById(id, track);
    TEST(track != nullptr);
    TEST(track->Id() == id);
    track->RemoveRef();
}

void SuiteTrackDatabase::GetTrackByInvalidIdFails()
{
    TUint id;
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), id);
    Track* track;
    TEST_THROWS(iTrackDbReader->GetTrackById(ITrackDatabaseReader::kTrackIdNone, track), TrackDbIdNotFound);
    TEST_THROWS(iTrackDbReader->GetTrackById(id+1, track), TrackDbIdNotFound);
}

void SuiteTrackDatabase::GetTrackByIdValidSeq()
{
    TUint ids[3];
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), ids[0]);
    iTrackDbWriter->Insert(ids[0], Brx::Empty(), Brx::Empty(), ids[1]);
    iTrackDbWriter->Insert(ids[1], Brx::Empty(), Brx::Empty(), ids[2]);
    TUint seq;
    iTrackDbReader->GetIdArray(iIdArray, seq);

    TUint index=0;
    Track* track;
    for (TUint i=0; i<sizeof(ids)/sizeof(ids[0]); i++) {
        iTrackDbReader->GetTrackById(ids[i], seq, track, index);
        TEST(track != nullptr);
        TEST(track->Id() == ids[i]);
        track->RemoveRef();
    }

    index = 0;
    track = nullptr;
    iTrackDbReader->GetTrackById(ids[2], seq, track, index);
    TEST(track != nullptr);
    TEST(track->Id() == ids[2]);
    track->RemoveRef();
    iTrackDbReader->GetTrackById(ids[1], seq, track, index);
    TEST(track != nullptr);
    TEST(track->Id() == ids[1]);
    track->RemoveRef();
    iTrackDbReader->GetTrackById(ids[0], seq, track, index);
    TEST(track != nullptr);
    TEST(track->Id() == ids[0]);
    track->RemoveRef();
}

void SuiteTrackDatabase::GetTrackByIdInvalidSeq()
{
    TUint ids[3];
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), ids[0]);
    iTrackDbWriter->Insert(ids[0], Brx::Empty(), Brx::Empty(), ids[1]);
    iTrackDbWriter->Insert(ids[1], Brx::Empty(), Brx::Empty(), ids[2]);
    TUint seq;
    iTrackDbReader->GetIdArray(iIdArray, seq);
    seq--;

    TUint index=0;
    Track* track;
    for (TUint i=0; i<sizeof(ids)/sizeof(ids[0]); i++) {
        iTrackDbReader->GetTrackById(ids[i], seq, track, index);
        TEST(track != nullptr);
        TEST(track->Id() == ids[i]);
        track->RemoveRef();
    }

    index = 0;
    track = nullptr;
    iTrackDbReader->GetTrackById(ids[2], seq, track, index);
    TEST(track != nullptr);
    TEST(track->Id() == ids[2]);
    track->RemoveRef();
    iTrackDbReader->GetTrackById(ids[1], seq, track, index);
    TEST(track != nullptr);
    TEST(track->Id() == ids[1]);
    track->RemoveRef();
    iTrackDbReader->GetTrackById(ids[0], seq, track, index);
    TEST(track != nullptr);
    TEST(track->Id() == ids[0]);
    track->RemoveRef();
}

void SuiteTrackDatabase::MultipleObservers()
{
    iTrackDbReader->AddObserver(*this);
    TUint id;
    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), id);
    TEST(iInsertedCount == 2);

    iTrackDbWriter->DeleteId(id);
    TEST(iDeletedCount == 2);

    iTrackDbWriter->Insert(ITrackDatabaseReader::kTrackIdNone, Brx::Empty(), Brx::Empty(), id);
    iTrackDbWriter->DeleteAll();
    TEST(iAllDeletedCount == 2);
}


// SuiteTrackReader

SuiteTrackReader::SuiteTrackReader()
    : SuiteUnitTest("Track database (ITrackDatabaseTrackReader)")
{
    AddTest(MakeFunctor(*this, &SuiteTrackReader::TrackRefValidId), "TrackRefValidId");
    AddTest(MakeFunctor(*this, &SuiteTrackReader::TrackRefInvalidId), "TrackRefInvalidId");
    AddTest(MakeFunctor(*this, &SuiteTrackReader::NextTrackRefValidId), "NextTrackRefValidId");
    AddTest(MakeFunctor(*this, &SuiteTrackReader::NextTrackRefInvalidId), "NextTrackRefInvalidId");
    AddTest(MakeFunctor(*this, &SuiteTrackReader::PrevTrackRefValidId), "PrevTrackRefValidId");
    AddTest(MakeFunctor(*this, &SuiteTrackReader::PrevTrackRefInvalidId), "PrevTrackRefInvalidId");
    AddTest(MakeFunctor(*this, &SuiteTrackReader::TrackRefByIndexValidId), "TrackRefByIndexValidId");
    AddTest(MakeFunctor(*this, &SuiteTrackReader::TrackRefByIndexInvalidId), "TrackRefByIndexInvalidId");
    AddTest(MakeFunctor(*this, &SuiteTrackReader::IsValidValidId), "IsValidValidId");
    AddTest(MakeFunctor(*this, &SuiteTrackReader::IsValidInvalidId), "IsValidInvalidId");
}

void SuiteTrackReader::Setup()
{
    iTrackFactory = new TrackFactory(iInfoAggregator, kMaxTracks);
    iDb = new TrackDatabase(*iTrackFactory, kMaxTracks);
    iReader = static_cast<ITrackDatabaseTrackReader*>(iDb);
    iReader->SetObserver(*this);
    
    ITrackDatabaseWriter* writer = static_cast<ITrackDatabaseWriter*>(iDb);
    TUint insertAfter = ITrackDatabaseReader::kTrackIdNone;
    for (TUint i=0; i<kNumTracks; i++) {
        writer->Insert(insertAfter, Brx::Empty(), Brx::Empty(), iIds[i]);
        insertAfter = iIds[i];
    }
}

void SuiteTrackReader::TearDown()
{
    delete iDb;
    delete iTrackFactory;
}

void SuiteTrackReader::NotifyTrackInserted(Track& /*aTrack*/, TUint /*aIdBefore*/, TUint /*aIdAfter*/)
{
}

void SuiteTrackReader::NotifyTrackDeleted(TUint /*aId*/, Track* /*aBefore*/, Track* /*aAfter*/)
{
}

void SuiteTrackReader::NotifyAllDeleted()
{
}

void SuiteTrackReader::NotifyReordered(Track* /*aStart*/)
{
}

void SuiteTrackReader::TrackRefValidId()
{
    for (TUint i=0; i<kNumTracks; i++) {
        Track* track = iReader->TrackRef(iIds[i]);
        TEST(track != nullptr);
        TEST(track->Id() == iIds[i]);
        track->RemoveRef();
    }
}

void SuiteTrackReader::TrackRefInvalidId()
{
    Track* track = iReader->TrackRef(ITrackDatabaseReader::kTrackIdNone);
    TEST(track == nullptr);
    track = iReader->TrackRef(iIds[kNumTracks-1]+1);
    TEST(track == nullptr);
}

void SuiteTrackReader::NextTrackRefValidId()
{
    TUint prev = ITrackDatabaseReader::kTrackIdNone;
    Track* track;
    for (TUint i=0; i<kNumTracks; i++) {
        track = iReader->NextTrackRef(prev);
        TEST(track != nullptr);
        TEST(track->Id() == iIds[i]);
        prev = track->Id();
        track->RemoveRef();
    }
    track = iReader->NextTrackRef(iIds[2]);
    TEST(track == nullptr);
}

void SuiteTrackReader::NextTrackRefInvalidId()
{
    Track* track = iReader->NextTrackRef(iIds[kNumTracks-1]+1);
    TEST(track == nullptr);
    track = iReader->NextTrackRef(UINT_MAX);
    TEST(track == nullptr);
}

void SuiteTrackReader::PrevTrackRefValidId()
{
    TUint next = iIds[kNumTracks-1];
    Track* track;
    for (TUint i=kNumTracks-1; i>0; i--) {
        track = iReader->PrevTrackRef(next);
        TEST(track != nullptr);
        TEST(track->Id() == iIds[i-1]);
        next = track->Id();
        track->RemoveRef();
    }
    track = iReader->PrevTrackRef(iIds[0]);
    TEST(track == nullptr);
}

void SuiteTrackReader::PrevTrackRefInvalidId()
{
    Track* track = iReader->PrevTrackRef(ITrackDatabaseReader::kTrackIdNone);
    TEST(track == nullptr);
    track = iReader->PrevTrackRef(iIds[kNumTracks-1] + 1);
    TEST(track == nullptr);
}

void SuiteTrackReader::TrackRefByIndexValidId()
{
    for (TUint i=0; i<kNumTracks; i++) {
        Track* track = iReader->TrackRefByIndex(i);
        TEST(track != nullptr);
        TEST(track->Id() == iIds[i]);
        track->RemoveRef();
    }
}

void SuiteTrackReader::TrackRefByIndexInvalidId()
{
    Track* track = iReader->TrackRefByIndex(kNumTracks);
    TEST(track == nullptr);
}

void SuiteTrackReader::IsValidValidId()
{
    TEST(iReader->IsValid(iIds[0]));
}

void SuiteTrackReader::IsValidInvalidId()
{
    TEST(!iReader->IsValid(iIds[kNumTracks-1]+1));
}


// SuiteShuffler

SuiteShuffler::SuiteShuffler()
    : SuiteUnitTest("Shuffler")
{
    AddTest(MakeFunctor(*this, &SuiteShuffler::TrackRefShuffleOff), "TrackRefShuffleOff");
    AddTest(MakeFunctor(*this, &SuiteShuffler::TrackRefShuffleOn), "TrackRefShuffleOn");
    AddTest(MakeFunctor(*this, &SuiteShuffler::NextTrackRefShuffleOff), "NextTrackRefShuffleOff");
    AddTest(MakeFunctor(*this, &SuiteShuffler::NextTrackRefShuffleOn), "NextTrackRefShuffleOn");
    AddTest(MakeFunctor(*this, &SuiteShuffler::PrevTrackRefShuffleOff), "PrevTrackRefShuffleOff");
    AddTest(MakeFunctor(*this, &SuiteShuffler::PrevTrackRefShuffleOn), "PrevTrackRefShuffleOn");
    AddTest(MakeFunctor(*this, &SuiteShuffler::TrackRefByIndexShuffleOff), "TrackRefByIndexShuffleOff");
    AddTest(MakeFunctor(*this, &SuiteShuffler::TrackRefByIndexShuffleOn), "TrackRefByIndexShuffleOn");
    AddTest(MakeFunctor(*this, &SuiteShuffler::NextTrackBeyondEndNoReshuffle), "NextTrackBeyondEndNoReshuffle");
    AddTest(MakeFunctor(*this, &SuiteShuffler::ShuffleOffRestoresUserOrder), "ShuffleOffRestoresUserOrder");
    AddTest(MakeFunctor(*this, &SuiteShuffler::SetShuffleWhilePlayingPutsCurrentTrackFirst), "SetShuffleWhilePlayingPutsCurrentTrackFirst");
    AddTest(MakeFunctor(*this, &SuiteShuffler::SetShuffleWhenNotPlayingStartsAtFirstShuffledTrack), "SetShuffleWhenNotPlayingStartsAtFirstShuffledTrack");
    AddTest(MakeFunctor(*this, &SuiteShuffler::SetShuffleWithUnknownCurrentTrackShuffles), "SetShuffleWithUnknownCurrentTrackShuffles");
    AddTest(MakeFunctor(*this, &SuiteShuffler::SetShuffleOffWhilePlayingKeepsCurrentTrack), "SetShuffleOffWhilePlayingKeepsCurrentTrack");
    AddTest(MakeFunctor(*this, &SuiteShuffler::SetShuffleOffWhenNotPlayingStartsAtFirstTrack), "SetShuffleOffWhenNotPlayingStartsAtFirstTrack");
    AddTest(MakeFunctor(*this, &SuiteShuffler::InsertShuffleOnFollowsUserOrder), "InsertShuffleOnFollowsUserOrder");
    AddTest(MakeFunctor(*this, &SuiteShuffler::InsertAfterLastShuffledTrackAppends), "InsertAfterLastShuffledTrackAppends");
    AddTest(MakeFunctor(*this, &SuiteShuffler::MoveShuffleOnFollowsUserOrder), "MoveShuffleOnFollowsUserOrder");
    AddTest(MakeFunctor(*this, &SuiteShuffler::MoveToEndOfShuffledPlaylistMovesToEnd), "MoveToEndOfShuffledPlaylistMovesToEnd");
    AddTest(MakeFunctor(*this, &SuiteShuffler::DeleteToSingleTrackDisablesShuffle), "DeleteToSingleTrackDisablesShuffle");
    AddTest(MakeFunctor(*this, &SuiteShuffler::DeleteIdsToEmptyDisablesShuffle), "DeleteIdsToEmptyDisablesShuffle");
    AddTest(MakeFunctor(*this, &SuiteShuffler::DeleteIdsPartialFailureDisablesShuffle), "DeleteIdsPartialFailureDisablesShuffle");
    AddTest(MakeFunctor(*this, &SuiteShuffler::DeleteAllDisablesShuffle), "DeleteAllDisablesShuffle");
}

static TUint IndexOfId(const std::vector<TUint32>& aIds, TUint aId)
{
    auto it = std::find(aIds.begin(), aIds.end(), aId);
    TEST(it != aIds.end());
    return (TUint)(it - aIds.begin());
}

void SuiteShuffler::Setup()
{
    iTrackFactory = new TrackFactory(iInfoAggregator, kMaxTracks);
    iDb = new TrackDatabase(*iTrackFactory, kMaxTracks);
    iShuffler = new Shuffler(*iDb, *iDb, *iDb, *iDb, kMaxTracks);
    iShuffler->SetShuffleOffHandler(MakeFunctor(*this, &SuiteShuffler::ShuffleOffRequested));
    iReader = static_cast<ITrackDatabaseTrackReader*>(iShuffler);
    iWriter = static_cast<ITrackDatabaseWriter*>(iShuffler);
    iReader->SetObserver(*this);
    iShuffleOffCount = 0;
    iReorderedCount = 0;
    iIdLastReordered = ITrackDatabaseReader::kTrackIdNone;

    TUint insertAfter = ITrackDatabaseReader::kTrackIdNone;
    for (TUint i=0; i<kNumTracks; i++) {
        iWriter->Insert(insertAfter, Brx::Empty(), Brx::Empty(), iIds[i]);
        insertAfter = iIds[i];
    }
}

void SuiteShuffler::ShuffleOffRequested()
{
    /* Shuffler has already disabled shuffle.  In a running system, this would report the new
       state, ending in a (no-op) call to SetShuffle(false). */
    iShuffleOffCount++;
    iShuffler->SetShuffle(false);
}

std::vector<TUint32> SuiteShuffler::ShuffledIds() const
{
    std::vector<TUint32> ids;
    TUint seq;
    static_cast<ITrackDatabaseReader*>(iShuffler)->GetIdArray(ids, seq);
    ids.erase(std::remove(ids.begin(), ids.end(), (TUint32)ITrackDatabaseReader::kTrackIdNone), ids.end());
    return ids;
}

std::vector<TUint32> SuiteShuffler::UnshuffledIds() const
{
    std::vector<TUint32> ids;
    TUint seq;
    static_cast<ITrackDatabaseReader*>(iDb)->GetIdArray(ids, seq);
    ids.erase(std::remove(ids.begin(), ids.end(), (TUint32)ITrackDatabaseReader::kTrackIdNone), ids.end());
    return ids;
}

void SuiteShuffler::TearDown()
{
    delete iShuffler;
    delete iDb;
    delete iTrackFactory;
}

void SuiteShuffler::NotifyTrackInserted(Track& /*aTrack*/, TUint /*aIdBefore*/, TUint /*aIdAfter*/)
{
}

void SuiteShuffler::NotifyTrackDeleted(TUint /*aId*/, Track* /*aBefore*/, Track* /*aAfter*/)
{
}

void SuiteShuffler::NotifyAllDeleted()
{
}

void SuiteShuffler::NotifyReordered(Track* aStart)
{
    iReorderedCount++;
    iIdLastReordered = (aStart==nullptr? ITrackDatabaseReader::kTrackIdNone : aStart->Id());
}

void SuiteShuffler::TrackRefShuffleOff()
{
    Track* track;
    for (TUint i=0; i<kNumTracks; i++) {
        track = iReader->TrackRef(iIds[i]);
        TEST(track != nullptr);
        TEST(track->Id() == iIds[i]);
        track->RemoveRef();
    }
    track = iReader->TrackRef(ITrackDatabaseReader::kTrackIdNone);
    TEST(track == nullptr);
    track = iReader->TrackRef(iIds[kNumTracks-1]+1);
    TEST(track == nullptr);
}

void SuiteShuffler::TrackRefShuffleOn()
{
    iShuffler->SetShuffle(true);
    // requesting track by id should have identical behaviour with/without shuffle
    TrackRefShuffleOff();
}

void SuiteShuffler::NextTrackRefShuffleOff()
{
    TUint prev = ITrackDatabaseReader::kTrackIdNone;
    for (TUint i=0; i<kNumTracks; i++) {
        Track* track = iReader->NextTrackRef(prev);
        TEST(track != nullptr);
        TEST(track->Id() == iIds[i]);
        prev = track->Id();
        track->RemoveRef();
    }
}

void SuiteShuffler::NextTrackRefShuffleOn()
{
    std::array<TUint, kNumTracks> availableIds;
    for (TUint i=0; i<kNumTracks; i++) {
        availableIds[i] = iIds[i];
    }
    iShuffler->SetShuffle(true);

    TBool shuffled = false;
    TUint id = ITrackDatabaseReader::kTrackIdNone;
    for (TUint i=0; i<kNumTracks; i++) {
        Track* track = iReader->NextTrackRef(id);
        TEST(track != nullptr);
        id = track->Id();
        if (id != iIds[i]) {
            shuffled = true;
        }
        auto it = std::find(availableIds.begin(), availableIds.end(), id);
        TEST(it != availableIds.end()); // i.e. check we haven't been given this track before
        *it = ITrackDatabaseReader::kTrackIdNone;
        track->RemoveRef();
    }
    TEST(shuffled);
    int count = std::count_if(availableIds.begin(), availableIds.end(), [](TUint aId) {return aId != ITrackDatabaseReader::kTrackIdNone;});
    TEST(count == 0);
}

void SuiteShuffler::PrevTrackRefShuffleOff()
{
    Track* track;
    TUint id = iIds[kNumTracks-1];
    for (TUint i=kNumTracks-1; i>0; i--) {
        track = iReader->PrevTrackRef(id);
        TEST(track != nullptr);
        TEST(track->Id() == iIds[i-1]);
        id = track->Id();
        track->RemoveRef();
    }
    track = iReader->PrevTrackRef(iIds[0]);
    TEST(track == nullptr);
    track = iReader->PrevTrackRef(iIds[kNumTracks-1]+1);
    TEST(track == nullptr);
}

void SuiteShuffler::PrevTrackRefShuffleOn()
{
    std::array<TUint, kNumTracks> availableIds;
    for (TUint i=0; i<kNumTracks; i++) {
        availableIds[i] = iIds[i];
    }
    iShuffler->SetShuffle(true);

    // find id of last shuffled track
    TUint id = iShuffler->iShuffleList[iShuffler->iShuffleList.size()-1]->Id();

    TBool shuffled = false;
    for (TInt i=kNumTracks-1; i>=0; i--) {
        auto it = std::find(availableIds.begin(), availableIds.end(), id);
        TEST(it != availableIds.end()); // i.e. check we haven't been given this track before
        *it = ITrackDatabaseReader::kTrackIdNone;
        Track* track = iReader->PrevTrackRef(id);
        if (track == nullptr) {
            break;
        }
        id = track->Id();
        if (id != iIds[i]) {
            shuffled = true;
        }
        track->RemoveRef();
    }
    TEST(shuffled);
    int count = std::count_if(availableIds.begin(), availableIds.end(), [](TUint aId) {return aId != ITrackDatabaseReader::kTrackIdNone;});
    TEST(count == 0);
}

void SuiteShuffler::TrackRefByIndexShuffleOff()
{
    Track* track;
    for (TUint i=0; i<kNumTracks; i++) {
        track = iReader->TrackRefByIndex(i);
        TEST(track != nullptr);
        TEST(track->Id() == iIds[i]);
        track->RemoveRef();
    }
    track = iReader->TrackRefByIndex(kNumTracks+1);
    TEST(track == nullptr);
    track = iReader->TrackRefByIndex(UINT_MAX);
    TEST(track == nullptr);
}

void SuiteShuffler::TrackRefByIndexShuffleOn()
{
    std::array<TUint, kNumTracks> availableIds;
    for (TUint i=0; i<kNumTracks; i++) {
        availableIds[i] = iIds[i];
    }
    iShuffler->SetShuffle(true);
    TBool shuffled = false;

    for (TUint i=0; i<kNumTracks; i++) {
        Track* track = iReader->TrackRefByIndex(i);
        TEST(track != nullptr);
        if (track->Id() != iIds[i]) {
            shuffled = true;
        }
        auto it = std::find(availableIds.begin(), availableIds.end(), track->Id());
        TEST(it != availableIds.end()); // i.e. check we haven't been given this track before
        *it = ITrackDatabaseReader::kTrackIdNone;
        track->RemoveRef();
    }
    TEST(shuffled);
    int count = std::count_if(availableIds.begin(), availableIds.end(), [](TUint aId) {return aId != ITrackDatabaseReader::kTrackIdNone;});
    TEST(count == 0);
}

void SuiteShuffler::NextTrackBeyondEndNoReshuffle()
{
    iShuffler->SetShuffle(true);
    std::array<TUint, kNumTracks> initialShuffle;
    TUint id = ITrackDatabaseReader::kTrackIdNone;
    for (TUint i=0; i<kNumTracks; i++) {
        Track* track = iReader->NextTrackRef(id);
        id = track->Id();
        initialShuffle[i] = id;
        track->RemoveRef();
    }
    TEST(iReader->NextTrackRef(id) == nullptr);

    id = ITrackDatabaseReader::kTrackIdNone;
    TBool reshuffled = false;
    for (TUint i=0; i<kNumTracks; i++) {
        Track* track = iReader->NextTrackRef(id);
        id = track->Id();
        if (id != initialShuffle[i]) {
            reshuffled = true;
        }
        track->RemoveRef();
    }
    TEST(!reshuffled);
}


void SuiteShuffler::ShuffleOffRestoresUserOrder()
{
    const std::vector<TUint32> original = UnshuffledIds();
    iShuffler->SetShuffle(true);
    TEST(ShuffledIds() != original);
    iShuffler->SetShuffle(false);
    TEST(ShuffledIds() == original);
}

void SuiteShuffler::SetShuffleWhilePlayingPutsCurrentTrackFirst()
{
    const TUint currentId = iIds[kNumTracks/2];
    iShuffler->SetShuffle(true, currentId);

    // the playing track is first in the new order and is reported as its start
    const std::vector<TUint32> shuffled = ShuffledIds();
    TEST(shuffled.size() == kNumTracks);
    TEST(shuffled[0] == currentId);
    TEST(iReorderedCount == 1);
    TEST(iIdLastReordered == currentId);
    // ...and every other track is still present, in a shuffled order
    TEST(shuffled != UnshuffledIds());
    for (TUint i=0; i<kNumTracks; i++) {
        TEST_QUIETLY(std::count(shuffled.begin(), shuffled.end(), iIds[i]) == 1);
    }
}

void SuiteShuffler::SetShuffleWhenNotPlayingStartsAtFirstShuffledTrack()
{
    iShuffler->SetShuffle(true); // nothing playing
    const std::vector<TUint32> shuffled = ShuffledIds();
    TEST(shuffled.size() == kNumTracks);
    TEST(iReorderedCount == 1);
    TEST(iIdLastReordered == shuffled[0]);
}

void SuiteShuffler::SetShuffleWithUnknownCurrentTrackShuffles()
{
    // a track which has been deleted from the playlist can't be moved to the start of it
    iShuffler->SetShuffle(true, iIds[kNumTracks-1] + 1);
    const std::vector<TUint32> shuffled = ShuffledIds();
    TEST(shuffled.size() == kNumTracks);
    TEST(iReorderedCount == 1);
    TEST(iIdLastReordered == shuffled[0]);
}

void SuiteShuffler::SetShuffleOffWhilePlayingKeepsCurrentTrack()
{
    const TUint currentId = iIds[kNumTracks/2];
    iShuffler->SetShuffle(true, currentId);
    iShuffler->SetShuffle(false, currentId);

    // the user's order is restored but the playing track is reported as its start
    TEST(ShuffledIds() == UnshuffledIds());
    TEST(iReorderedCount == 2);
    TEST(iIdLastReordered == currentId);
    TEST(iIdLastReordered != UnshuffledIds()[0]);
}

void SuiteShuffler::SetShuffleOffWhenNotPlayingStartsAtFirstTrack()
{
    iShuffler->SetShuffle(true);
    iShuffler->SetShuffle(false); // nothing playing
    TEST(ShuffledIds() == UnshuffledIds());
    TEST(iReorderedCount == 2);
    TEST(iIdLastReordered == UnshuffledIds()[0]);

    // ...as does a track which has since been deleted from the playlist
    iShuffler->SetShuffle(true);
    iShuffler->SetShuffle(false, iIds[kNumTracks-1] + 1);
    TEST(iReorderedCount == 4);
    TEST(iIdLastReordered == UnshuffledIds()[0]);
}

void SuiteShuffler::InsertShuffleOnFollowsUserOrder()
{
    static const TUint kNumInserts = 3;
    iShuffler->SetShuffle(true);
    // insert a block of tracks in the middle of the shuffled playlist
    const TUint idAfter = ShuffledIds()[2];
    std::array<TUint, kNumInserts> newIds;
    TUint after = idAfter;
    for (TUint i=0; i<kNumInserts; i++) {
        iWriter->Insert(after, Brx::Empty(), Brx::Empty(), newIds[i]);
        after = newIds[i];
    }

    // ...tracks appear in the order the user chose in both shuffled and unshuffled playlists
    const std::vector<TUint32> shuffled = ShuffledIds();
    TEST(shuffled.size() == kNumTracks + kNumInserts);
    TUint index = IndexOfId(shuffled, idAfter);
    for (TUint i=0; i<kNumInserts; i++) {
        TEST(shuffled[index + 1 + i] == newIds[i]);
    }
    const std::vector<TUint32> unshuffled = UnshuffledIds();
    TEST(unshuffled.size() == kNumTracks + kNumInserts);
    index = IndexOfId(unshuffled, idAfter);
    for (TUint i=0; i<kNumInserts; i++) {
        TEST(unshuffled[index + 1 + i] == newIds[i]);
    }
}

void SuiteShuffler::ReshuffleUntilLastTracksDiffer()
{
    /* Tests of 'add/move to the end of the shuffled playlist' are only interesting if the last
       track of the shuffled playlist isn't also the last track of the unshuffled one. */
    while (ShuffledIds()[kNumTracks-1] == UnshuffledIds()[kNumTracks-1]) {
        iShuffler->SetShuffle(false);
        iShuffler->SetShuffle(true);
    }
}

void SuiteShuffler::InsertAfterLastShuffledTrackAppends()
{
    static const TUint kNumInserts = 2;
    iShuffler->SetShuffle(true);
    ReshuffleUntilLastTracksDiffer();

    std::array<TUint, kNumInserts> newIds;
    TUint after = ShuffledIds()[kNumTracks-1];
    for (TUint i=0; i<kNumInserts; i++) {
        iWriter->Insert(after, Brx::Empty(), Brx::Empty(), newIds[i]);
        after = newIds[i];
    }

    // adding to the end of the shuffled playlist adds to the end of the unshuffled one too
    const std::vector<TUint32> shuffled = ShuffledIds();
    const std::vector<TUint32> unshuffled = UnshuffledIds();
    TEST(shuffled.size() == kNumTracks + kNumInserts);
    TEST(unshuffled.size() == kNumTracks + kNumInserts);
    for (TUint i=0; i<kNumInserts; i++) {
        TEST(shuffled[kNumTracks + i] == newIds[i]);
        TEST(unshuffled[kNumTracks + i] == newIds[i]);
    }
}

void SuiteShuffler::MoveShuffleOnFollowsUserOrder()
{
    iShuffler->SetShuffle(true);
    const std::vector<TUint32> shuffled = ShuffledIds();
    const TUint idAfter = shuffled[2]; // ...somewhere in the middle of the shuffled playlist
    std::vector<TUint32> toMove;
    toMove.push_back(shuffled[kNumTracks-1]);
    toMove.push_back(shuffled[0]);
    iWriter->Move(toMove, idAfter);

    // moved tracks follow idAfter, in the order given, in both playlists
    const std::vector<TUint32> shuffledAfter = ShuffledIds();
    const std::vector<TUint32> unshuffledAfter = UnshuffledIds();
    TEST(shuffledAfter.size() == kNumTracks);
    TEST(unshuffledAfter.size() == kNumTracks);
    TUint index = IndexOfId(shuffledAfter, idAfter);
    TEST(shuffledAfter[index+1] == toMove[0]);
    TEST(shuffledAfter[index+2] == toMove[1]);
    index = IndexOfId(unshuffledAfter, idAfter);
    TEST(unshuffledAfter[index+1] == toMove[0]);
    TEST(unshuffledAfter[index+2] == toMove[1]);
}

void SuiteShuffler::MoveToEndOfShuffledPlaylistMovesToEnd()
{
    iShuffler->SetShuffle(true);
    ReshuffleUntilLastTracksDiffer();
    const std::vector<TUint32> shuffled = ShuffledIds();
    const std::vector<TUint32> unshuffled = UnshuffledIds();
    const TUint idAfter = shuffled[kNumTracks-1];

    /* Move the last unshuffled track first - the database deletes then re-inserts each track in
       turn so would fail to find its anchor if we chose one of the tracks being moved. */
    std::vector<TUint32> toMove;
    toMove.push_back(unshuffled[kNumTracks-1]);
    for (TUint i=0; toMove.size()<2; i++) {
        if (unshuffled[i] != toMove[0] && unshuffled[i] != idAfter) {
            toMove.push_back(unshuffled[i]);
        }
    }
    iWriter->Move(toMove, idAfter);

    // moving to the end of the shuffled playlist moves to the end of the unshuffled one too
    const std::vector<TUint32> shuffledAfter = ShuffledIds();
    const std::vector<TUint32> unshuffledAfter = UnshuffledIds();
    TEST(shuffledAfter.size() == kNumTracks);
    TEST(unshuffledAfter.size() == kNumTracks);
    for (TUint i=0; i<toMove.size(); i++) {
        const TUint index = kNumTracks - (TUint)toMove.size() + i;
        TEST(shuffledAfter[index] == toMove[i]);
        TEST(unshuffledAfter[index] == toMove[i]);
    }
}

void SuiteShuffler::DeleteToSingleTrackDisablesShuffle()
{
    iShuffler->SetShuffle(true);
    TEST(iShuffler->Enabled());
    for (TUint i=0; i<kNumTracks-Shuffler::kMinTracksForShuffle; i++) { // ...leaving the fewest tracks which can be shuffled
        iWriter->DeleteId(iIds[i]);
        TEST(iShuffler->Enabled());
        TEST(iShuffleOffCount == 0);
    }
    iWriter->DeleteId(iIds[kNumTracks-Shuffler::kMinTracksForShuffle]); // ...leaving too few
    TEST(!iShuffler->Enabled());
    TEST(iShuffleOffCount == 1);
    TEST(ShuffledIds() == UnshuffledIds());
}

void SuiteShuffler::DeleteIdsToEmptyDisablesShuffle()
{
    iShuffler->SetShuffle(true);
    TEST(iShuffler->Enabled());
    std::vector<TUint32> toDelete;
    for (TUint i=0; i<kNumTracks; i++) {
        toDelete.push_back(iIds[i]);
    }
    iWriter->DeleteIds(toDelete);
    TEST(!iShuffler->Enabled());
    TEST(iShuffleOffCount == 1);
    TEST(ShuffledIds().size() == 0);
}

void SuiteShuffler::DeleteIdsPartialFailureDisablesShuffle()
{
    iShuffler->SetShuffle(true);
    TEST(iShuffler->Enabled());
    std::vector<TUint32> toDelete;
    for (TUint i=0; i<kNumTracks-1; i++) { // ...leaving too few tracks to shuffle...
        toDelete.push_back(iIds[i]);
    }
    toDelete.push_back(iIds[kNumTracks-1] + 1); // ...then fail
    TEST_THROWS(iWriter->DeleteIds(toDelete), TrackDbIdNotFound);
    // tracks deleted before the failure still count
    TEST(!iShuffler->Enabled());
    TEST(iShuffleOffCount == 1);
    TEST(ShuffledIds().size() == 1);
}

void SuiteShuffler::DeleteAllDisablesShuffle()
{
    iShuffler->SetShuffle(true);
    TEST(iShuffler->Enabled());
    iWriter->DeleteAll();
    TEST(!iShuffler->Enabled());
    TEST(iShuffleOffCount == 1);
    TEST(ShuffledIds().size() == 0);
}


// SuiteRepeater

SuiteRepeater::SuiteRepeater()
    : SuiteUnitTest("Repeater")
{
    AddTest(MakeFunctor(*this, &SuiteRepeater::TrackRefRepeatOff), "TrackRefRepeatOff");
    AddTest(MakeFunctor(*this, &SuiteRepeater::TrackRefRepeatOn), "TrackRefRepeatOn");
    AddTest(MakeFunctor(*this, &SuiteRepeater::NextFromLastTrackRepeatOff), "NextFromLastTrackRepeatOff");
    AddTest(MakeFunctor(*this, &SuiteRepeater::NextFromLastTrackRepeatOn), "NextFromLastTrackRepeatOn");
    AddTest(MakeFunctor(*this, &SuiteRepeater::PrevFromFirstTrackRepeatOff), "PrevFromFirstTrackRepeatOff");
    AddTest(MakeFunctor(*this, &SuiteRepeater::PrevFromFirstTrackRepeatOn), "PrevFromFirstTrackRepeatOn");
    AddTest(MakeFunctor(*this, &SuiteRepeater::TrackRefByIndexRepeatOff), "TrackRefByIndexRepeatOff");
    AddTest(MakeFunctor(*this, &SuiteRepeater::TrackRefByIndexRepeatOn), "TrackRefByIndexRepeatOn");
}

void SuiteRepeater::Setup()
{
    iTrackFactory = new TrackFactory(iInfoAggregator, kMaxTracks);
    iDb = new TrackDatabase(*iTrackFactory, kMaxTracks);
    iShuffler = new Shuffler(*iDb, *iDb, *iDb, *iDb, kMaxTracks);
    iRepeater = new Repeater(*iShuffler);
    iReader = static_cast<ITrackDatabaseTrackReader*>(iRepeater);
    iReader->SetObserver(*this);
    
    ITrackDatabaseWriter* writer = static_cast<ITrackDatabaseWriter*>(iDb);
    TUint insertAfter = ITrackDatabaseReader::kTrackIdNone;
    for (TUint i=0; i<kNumTracks; i++) {
        writer->Insert(insertAfter, Brx::Empty(), Brx::Empty(), iIds[i]);
        insertAfter = iIds[i];
    }
}

void SuiteRepeater::TearDown()
{
    delete iRepeater;
    delete iShuffler;
    delete iDb;
    delete iTrackFactory;
}

void SuiteRepeater::NotifyTrackInserted(Track& /*aTrack*/, TUint /*aIdBefore*/, TUint /*aIdAfter*/)
{
}

void SuiteRepeater::NotifyTrackDeleted(TUint /*aId*/, Track* /*aBefore*/, Track* /*aAfter*/)
{
}

void SuiteRepeater::NotifyAllDeleted()
{
}

void SuiteRepeater::NotifyReordered(Track* /*aStart*/)
{
}

void SuiteRepeater::TrackRefRepeatOff()
{
    Track* track;
    for (TUint i=0; i<kNumTracks; i++) {
        track = iReader->TrackRef(iIds[i]);
        TEST(track != nullptr);
        TEST(track->Id() == iIds[i]);
        track->RemoveRef();
    }
    track = iReader->TrackRef(ITrackDatabaseReader::kTrackIdNone);
    TEST(track == nullptr);
    track = iReader->TrackRef(iIds[kNumTracks-1]+1);
    TEST(track == nullptr);
}

void SuiteRepeater::TrackRefRepeatOn()
{
    static_cast<IRepeater*>(iRepeater)->SetRepeat(true);
    // requesting track by id should have identical behaviour with/without repeat
    TrackRefRepeatOff();
}

void SuiteRepeater::NextFromLastTrackRepeatOff()
{
    Track* track = iReader->NextTrackRef(iIds[kNumTracks-1]);
    TEST(track == nullptr);
}

void SuiteRepeater::NextFromLastTrackRepeatOn()
{
    static_cast<IRepeater*>(iRepeater)->SetRepeat(true);
    Track* track = iReader->NextTrackRef(iIds[kNumTracks-1]);
    TEST(track != nullptr);
    TEST(track->Id() == iIds[0]);
    track->RemoveRef();
}

void SuiteRepeater::PrevFromFirstTrackRepeatOff()
{
    Track* track = iReader->PrevTrackRef(iIds[0]);
    TEST(track == nullptr);
}

void SuiteRepeater::PrevFromFirstTrackRepeatOn()
{
    static_cast<IRepeater*>(iRepeater)->SetRepeat(true);
    Track* track = iReader->PrevTrackRef(iIds[0]);
    TEST(track != nullptr);
    TEST(track->Id() == iIds[kNumTracks-1]);
    track->RemoveRef();
}

void SuiteRepeater::TrackRefByIndexRepeatOff()
{
    Track* track;
    for (TUint i=0; i<kNumTracks; i++) {
        track = iReader->TrackRefByIndex(i);
        TEST(track != nullptr);
        TEST(track->Id() == iIds[i]);
        track->RemoveRef();
    }
    track = iReader->TrackRefByIndex(kNumTracks);
    TEST(track == nullptr);
}

void SuiteRepeater::TrackRefByIndexRepeatOn()
{
    static_cast<IRepeater*>(iRepeater)->SetRepeat(true);
    // requesting track by index should have identical behaviour with/without repeat
    TrackRefByIndexRepeatOff();
}



void TestTrackDatabase()
{
    Runner runner("Track database tests\n");
    // FIXME - Suite to validate assumptions about std::vector implementation (maybe only reserve behaviour?)
    runner.Add(new SuiteTrackDatabase());
    runner.Add(new SuiteTrackReader());
    runner.Add(new SuiteShuffler());
    runner.Add(new SuiteRepeater());
    runner.Run();
}

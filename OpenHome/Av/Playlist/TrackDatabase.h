#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Thread.h>

#include <vector>

EXCEPTION(TrackDbIdNotFound);
EXCEPTION(TrackDbFull);

namespace OpenHome {
    class Environment;
namespace Media {
    class Track;
    class TrackFactory;
}
namespace Av {
    
class ITrackDatabaseObserver
{
public:
    virtual ~ITrackDatabaseObserver() {}
    virtual void NotifyTrackInserted(Media::Track& aTrack, TUint aIdBefore, TUint aIdAfter) = 0;
    virtual void NotifyTrackDeleted(TUint aId, Media::Track* aBefore, Media::Track* aAfter) = 0;
    virtual void NotifyAllDeleted() = 0;
    virtual void NotifyReordered(Media::Track* aStart) = 0;
};

class ITrackShuffleReporter
{
public:
    virtual ~ITrackShuffleReporter() {}
    virtual void ReportReordered(Media::Track* aStart) = 0;
};

class ITrackDatabaseReader
{
public:
    static const TUint kTrackIdNone;
public:
    virtual ~ITrackDatabaseReader() {}
    virtual void AddObserver(ITrackDatabaseObserver& aObserver) = 0;
    virtual TUint IdArraySeq() const = 0;
    virtual void GetIdArray(std::vector<TUint32>& aIdArray, TUint& aSeq) const = 0;
    virtual void GetTrackById(TUint aId, Media::Track*& aTrack) const = 0;
    virtual void GetTrackById(TUint aId, TUint aSeq, Media::Track*& aTrack, TUint& aIndex) const = 0;
    virtual TUint TrackCount() const = 0;
    virtual TUint TracksMax() const = 0;
};

class ITrackDatabaseWriter
{
public:
    virtual ~ITrackDatabaseWriter() {}
    virtual void Insert(TUint aIdAfter, const Brx& aUri, const Brx& aMetaData, TUint& aIdInserted) = 0;
    virtual void Move(const std::vector<TUint32>& aIdArray, TUint aIdAfter) = 0;
    virtual void DeleteId(TUint aId) = 0;
    virtual void DeleteIds(const std::vector<TUint32>& aIdArray) = 0;
    virtual void DeleteAll() = 0;
};

class ITrackDatabaseTrackReader
{
public:
    virtual ~ITrackDatabaseTrackReader() {}
    virtual void SetObserver(ITrackDatabaseObserver& aObserver) = 0;
    virtual Media::Track* TrackRef(TUint aId) = 0;
    virtual Media::Track* NextTrackRef(TUint aId) = 0;
    virtual Media::Track* PrevTrackRef(TUint aId) = 0;
    virtual Media::Track* TrackRefByIndex(TUint aIndex) = 0;
    virtual TBool IsValid(TUint aId) const = 0;
};

class IRepeater
{
public:
    virtual ~IRepeater() {}
    virtual void SetRepeat(TBool aRepeat) = 0;
};

class TrackDatabase :
    public ITrackDatabaseReader,
    public ITrackDatabaseWriter,
    public ITrackDatabaseTrackReader,
    public ITrackShuffleReporter
{
public:
    TrackDatabase(Media::TrackFactory& aTrackFactory, TUint aMaxTracks);
    ~TrackDatabase();
    static void CopyIdArray(const std::vector<Media::Track*>& aFrom, std::vector<TUint32>& aTo, TUint aMax);
private: // from ITrackDatabaseReader
    void AddObserver(ITrackDatabaseObserver& aObserver) override;
    TUint IdArraySeq() const override;
    void GetIdArray(std::vector<TUint32>& aIdArray, TUint& aSeq) const override;
    void GetTrackById(TUint aId, Media::Track*& aTrack) const override;
    void GetTrackById(TUint aId, TUint aSeq, Media::Track*& aTrack, TUint& aIndex) const override;
    TUint TrackCount() const override;
    TUint TracksMax() const override;
private: // from ITrackDatabaseWriter
    void Insert(TUint aIdAfter, const Brx& aUri, const Brx& aMetaData, TUint& aIdInserted) override;
    void Move(const std::vector<TUint32>& aIdArray, TUint aIdAfter) override;
    void DeleteId(TUint aId) override;
    void DeleteIds(const std::vector<TUint32>& aIdArray) override;
    void DeleteAll() override;
private: // from ITrackDatabaseTrackReader
    void SetObserver(ITrackDatabaseObserver& aObserver) override;
    Media::Track* TrackRef(TUint aId) override;
    Media::Track* NextTrackRef(TUint aId) override;
    Media::Track* PrevTrackRef(TUint aId) override;
    Media::Track* TrackRefByIndex(TUint aIndex) override;
    TBool IsValid(TUint aId) const override;
private: // from ITrackShuffleReporter
    void ReportReordered(Media::Track* aStart) override;
private:
    void GetTrackByIdLocked(TUint aId, Media::Track*& aTrack) const;
    TBool TryGetTrackById(TUint aId, Media::Track*& aTrack, TUint aStartIndex, TUint aEndIndex, TUint& aFoundIndex) const;
    void Insert(TUint aIdAfter, Media::Track* aTrack);
    Media::Track* DoDeleteId(TUint aId);
private:
    mutable Mutex iLock;
    Mutex iObserverLock;
    Media::TrackFactory& iTrackFactory;
    std::vector<ITrackDatabaseObserver*> iObservers;
    std::vector<Media::Track*> iTrackList;
    const TUint iMaxTracks;
    TUint iSeq;
};

class Shuffler :
    public ITrackDatabaseReader,
    public ITrackDatabaseTrackReader,
    private ITrackDatabaseObserver
{
    friend class SuiteShuffler;
public:
    Shuffler(
        Environment& aEnv,
        ITrackDatabaseReader& aReader,
        ITrackDatabaseTrackReader& aTrackReader,
        ITrackShuffleReporter& aReporter,
        TUint aMaxTracks);
    ~Shuffler();
    TBool Enabled() const;
    void SetShuffle(TBool aShuffle);
private: // from ITrackDatabaseReader
    void AddObserver(ITrackDatabaseObserver& aObserver) override;
    TUint IdArraySeq() const override;
    void GetIdArray(std::vector<TUint32>& aIdArray, TUint& aSeq) const override;
    void GetTrackById(TUint aId, Media::Track*& aTrack) const override;
    void GetTrackById(TUint aId, TUint aSeq, Media::Track*& aTrack, TUint& aIndex) const override;
    TUint TrackCount() const override;
    TUint TracksMax() const override;
private: // from ITrackDatabaseTrackReader
    void SetObserver(ITrackDatabaseObserver& aObserver) override;
    Media::Track* TrackRef(TUint aId) override;
    Media::Track* NextTrackRef(TUint aId) override;
    Media::Track* PrevTrackRef(TUint aId) override;
    Media::Track* TrackRefByIndex(TUint aIndex) override;
    TBool IsValid(TUint aId) const override;
private: // from ITrackDatabaseObserver
    void NotifyTrackInserted(Media::Track& aTrack, TUint aIdBefore, TUint aIdAfter) override;
    void NotifyTrackDeleted(TUint aId, Media::Track* aBefore, Media::Track* aAfter) override;
    void NotifyAllDeleted() override;
    void NotifyReordered(Media::Track* aStart) override;
private:
    void LogIds(const TChar* aPrefix);
private:
    mutable Mutex iLock;
    Environment& iEnv;
    ITrackDatabaseReader& iDbReader;
    ITrackDatabaseTrackReader& iTrackReader;
    ITrackShuffleReporter& iReporter;
    ITrackDatabaseObserver* iObserver;
    std::vector<Media::Track*> iShuffleList;
    TUint iPrevTrackId;
    TBool iShuffle;
};

class Repeater : public IRepeater, public ITrackDatabaseTrackReader, private ITrackDatabaseObserver
{
public:
    Repeater(ITrackDatabaseTrackReader& aTrackReader);
private: // from IRepeater
    void SetRepeat(TBool aRepeat) override;
private: // from ITrackDatabaseTrackReader
    void SetObserver(ITrackDatabaseObserver& aObserver) override;
    Media::Track* TrackRef(TUint aId) override;
    Media::Track* NextTrackRef(TUint aId) override;
    Media::Track* PrevTrackRef(TUint aId) override;
    Media::Track* TrackRefByIndex(TUint aIndex) override;
    TBool IsValid(TUint aId) const override;
private: // from ITrackDatabaseObserver
    void NotifyTrackInserted(Media::Track& aTrack, TUint aIdBefore, TUint aIdAfter) override;
    void NotifyTrackDeleted(TUint aId, Media::Track* aBefore, Media::Track* aAfter) override;
    void NotifyAllDeleted() override;
    void NotifyReordered(Media::Track* aStart) override;
private:
    Mutex iLock;
    ITrackDatabaseTrackReader& iTrackReader;
    ITrackDatabaseObserver* iObserver;
    TBool iRepeat;
    TUint iTrackCount;
};

class TrackListUtils
{
public:
    static TUint IndexFromId(const std::vector<Media::Track*>& aList, TUint aId);
    static void Clear(std::vector<Media::Track*>& aList);
};

class TrackReaderUtils
{
public:
    static Media::Track* TrackRef(const std::vector<Media::Track*>& aList, TUint aId);
    static Media::Track* NextTrackRef(const std::vector<Media::Track*>& aList, TUint aId);
    static Media::Track* PrevTrackRef(const std::vector<Media::Track*>& aList, TUint aId);
    static Media::Track* TrackRefByIndex(const std::vector<Media::Track*>& aList, TUint aIndex);
};

class AutoTrack
{
public:
    AutoTrack(Media::Track* aTrack);
    ~AutoTrack();
    void Clear();
private:
    Media::Track* iTrack;
};

} // namespace Av
} // namespace OpenHome


#include <OpenHome/Media/UriProviderRepeater.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Filler.h>

using namespace OpenHome;
using namespace OpenHome::Media;

// UriProviderRepeater

UriProviderRepeater::UriProviderRepeater(
    const TChar* aMode,
    Latency aLatencyMode,
    TrackFactory& aTrackFactory,
    Pause aPauseSupported,
    Next aNextSupported,
    Prev aPrevSupported,
    Repeat aRepeatSupported,
    Random aRandomSupported,
    RampPauseResume aRampPauseResume,
    RampSkip aRampSkip)

    : UriProvider(
        aMode,
        aLatencyMode,
        aPauseSupported,
        aNextSupported,
        aPrevSupported,
        aRepeatSupported,
        aRandomSupported,
        aRampPauseResume,
        aRampSkip)

    , iLock("UPRP")
    , iTrackFactory(aTrackFactory)
    , iTrack(nullptr)
    , iRetrieved(true)
    , iPlayLater(false)
    , iFailed(false)
{
}

UriProviderRepeater::~UriProviderRepeater()
{
    if (iTrack != nullptr) {
        iTrack->RemoveRef();
    }
}

Track* UriProviderRepeater::SetTrack(const Brx& aUri, const Brx& aMetaData)
{
    AutoMutex a(iLock);
    if (iTrack != nullptr) {
        iTrack->RemoveRef();
    }
    if (aUri == Brx::Empty()) {
        iTrack = nullptr;
    }
    else {
        iTrack = iTrackFactory.CreateTrack(aUri, aMetaData);
        iTrack->AddRef();
    }
    iFailed = false;
    return iTrack;
}

void UriProviderRepeater::SetTrack(Track* aTrack)
{
    AutoMutex a(iLock);
    if (iTrack != nullptr) {
        iTrack->RemoveRef();
    }
    iTrack = aTrack;
    iFailed = false;
}

void UriProviderRepeater::Begin(TUint aTrackId)
{
    DoBegin(aTrackId, false);
}

void UriProviderRepeater::BeginLater(TUint aTrackId)
{
    DoBegin(aTrackId, true);
}

EStreamPlay UriProviderRepeater::GetNext(Track*& aTrack)
{
    AutoMutex a(iLock);
    if (iTrack == nullptr || iFailed) {
        aTrack = nullptr;
        return ePlayNo;
    }
    else if (iRetrieved) {
        iPlayLater = true;
    }
    aTrack = iTrack;
    aTrack->AddRef();
    iRetrieved = true;
    return (iPlayLater? ePlayLater : ePlayYes);
}

TUint UriProviderRepeater::CurrentTrackId() const
{
    TUint id = Track::kIdNone;
    iLock.Wait();
    if (iTrack != nullptr) {
        id = iTrack->Id();
    }
    iLock.Signal();
    return id;
}

void UriProviderRepeater::MoveNext()
{
    MoveCursor();
}

void UriProviderRepeater::MovePrevious()
{
    MoveCursor();
}

void UriProviderRepeater::MoveTo(const Brx& aCommand)
{
    if (aCommand.Bytes() != 0) {
        THROW(FillerInvalidCommand);
    }
    AutoMutex a(iLock);
    if (iTrack == nullptr) {
        return;
    }
    iRetrieved = false;
    iPlayLater = false;
}

void UriProviderRepeater::NotifyTrackPlay(Media::Track& aTrack)
{
    AutoMutex a(iLock);
    if (iTrack != nullptr && iTrack->Id() == aTrack.Id()) {
        iFailed = false;
    }
}

void UriProviderRepeater::NotifyTrackFail(Media::Track& aTrack)
{
    AutoMutex a(iLock);
    if (iTrack != nullptr && iTrack->Id() == aTrack.Id()) {
        iFailed = true;
    }
}

void UriProviderRepeater::DoBegin(TUint aTrackId, TBool aLater)
{
    iLock.Wait();
    iRetrieved = (iTrack == nullptr || iTrack->Id() != aTrackId);
    iPlayLater = aLater;
    iFailed = false;
    iLock.Signal();
}

void UriProviderRepeater::MoveCursor()
{
    AutoMutex a(iLock);
    if (iTrack == nullptr || iRetrieved) {
        return;
    }
    iRetrieved = true;
}

#include <OpenHome/Av/Radio/UriProviderRadio.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/Filler.h>
#include <OpenHome/Av/Radio/PresetDatabase.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Media;

// UriProviderRadio

const Brn UriProviderRadio::kCommandId("id");
const Brn UriProviderRadio::kCommandIndex("index");

UriProviderRadio::UriProviderRadio(TrackFactory& aTrackFactory,
                                   IPresetDatabaseReaderTrack& aDbReader)
    : UriProvider("Radio",
                  Latency::NotSupported,
                  Pause::Supported,
                  Next::Supported,
                  Prev::Supported,
                  Repeat::NotSupported,
                  Random::NotSupported,
                  RampPauseResume::Long,
                  RampSkip::Short)
    , iLock("UPRD")
    , iTrackFactory(aTrackFactory)
    , iDbReader(aDbReader)
    , iTrack(nullptr)
    , iIgnoreNext(true)
    , iPlayLater(false)
{
}

UriProviderRadio::~UriProviderRadio()
{
    if (iTrack != nullptr) {
        iTrack->RemoveRef();
    }
}

Track* UriProviderRadio::SetTrack(const Brx& aUri, const Brx& aMetaData)
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
    return iTrack;
}

void UriProviderRadio::SetTrack(Track* aTrack)
{
    if (iTrack != nullptr) {
        iTrack->RemoveRef();
    }
    iTrack = aTrack;
    iTrack->AddRef();
}

void UriProviderRadio::Begin(TUint aTrackId)
{
    DoBegin(aTrackId, false);
}

void UriProviderRadio::BeginLater(TUint aTrackId)
{
    DoBegin(aTrackId, true);
}

EStreamPlay UriProviderRadio::GetNext(Track*& aTrack)
{
    AutoMutex a(iLock);
    if (iIgnoreNext || iTrack == nullptr) {
        aTrack = nullptr;
        return ePlayNo;
    }
    aTrack = iTrack;
    aTrack->AddRef();
    iIgnoreNext = true;
    return (iPlayLater? ePlayLater : ePlayYes);
}

TUint UriProviderRadio::CurrentTrackId() const
{
    TUint id = Track::kIdNone;
    iLock.Wait();
    if (iTrack != nullptr) {
        id = iTrack->Id();
    }
    iLock.Signal();
    return id;
}

void UriProviderRadio::MoveNext()
{
    AutoMutex _(iLock);
    if (iTrack == nullptr) {
        return;
    }
    TUint id = iTrack->Id();
    auto track = iDbReader.NextTrackRef(id);
    iPlayLater = (track == nullptr);
    if (track == nullptr) {
        track = iDbReader.FirstTrackRef();
    }
    iIgnoreNext = false;
    iTrack->RemoveRef();
    iTrack = track;
}

void UriProviderRadio::MovePrevious()
{
    AutoMutex _(iLock);
    if (iTrack == nullptr) {
        return;
    }
    TUint id = iTrack->Id();
    auto track = iDbReader.PrevTrackRef(id);
    iPlayLater = (track == nullptr);
    if (track == nullptr) {
        track = iDbReader.LastTrackRef();
    }
    iIgnoreNext = false;
    iTrack->RemoveRef();
    iTrack = track;
}

void UriProviderRadio::MoveTo(const Brx& aCommand)
{
    const TBool byId = aCommand.BeginsWith(kCommandId);
    const TBool byIndex = aCommand.BeginsWith(kCommandIndex);
    if (!byId && !byIndex) {
        THROW(FillerInvalidCommand);
    }

    Parser parser(aCommand);
    parser.Next('=');
    Brn buf = parser.NextToEnd();
    TUint num = IPresetDatabaseReader::kPresetIdNone;
    try {
        num = Ascii::Uint(buf);
    }
    catch (AsciiError&) {
        THROW(FillerInvalidCommand);
    }
    Track* track = nullptr;
    if (byId) {
        track = iDbReader.TrackRefById(num);
    }
    else { // byIndex
        track = iDbReader.TrackRefByIndex(num);
    }
    if (track == nullptr) {
        THROW(FillerInvalidCommand);
    }

    AutoMutex _(iLock);
    if (iTrack != nullptr) {
        iTrack->RemoveRef();
    }
    iTrack = track;
    iIgnoreNext = false;
    iPlayLater = false;
}

void UriProviderRadio::DoBegin(TUint aTrackId, TBool aLater)
{
    iLock.Wait();
    iIgnoreNext = (iTrack == nullptr || iTrack->Id() != aTrackId);
    iPlayLater = aLater;
    iLock.Signal();
}

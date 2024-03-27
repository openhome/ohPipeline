#include <OpenHome/Types.h>
#include <OpenHome/Media/Pipeline/AsyncTrackObserver.h>
#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Media;


// AsyncMetadataRequests

TBool AsyncMetadataRequests::Exists(const Brx& aMode)
{
    auto it = std::find_if(iRequests.begin(), iRequests.end(), [&](const std::unique_ptr<Brx>& aPtr) {
        return *aPtr == aMode;
    });
    return (it != iRequests.end());
}

void AsyncMetadataRequests::Add(const Brx& aMode)
{
    ASSERT(!Exists(aMode));
    iRequests.push_back(std::unique_ptr<Brx>(new Brh(aMode)));
}

void AsyncMetadataRequests::Remove(const Brx& aMode)
{
    if (!Exists(aMode)) {
        return;
    }
    iRequests.remove_if([&](const std::unique_ptr<Brx>& aPtr) {
        return (*aPtr == aMode);
    });
}

void AsyncMetadataRequests::Trim(const Brx& aMode)
{
    iRequests.remove_if([&](const std::unique_ptr<Brx>& aPtr) {
        return (*aPtr != aMode);
    });
}

void AsyncMetadataRequests::Clear()
{
    iRequests.clear();
}


// AsyncTrackObserver

const TUint AsyncTrackObserver::kSupportedMsgTypes = eMode
                                                   | eTrack
                                                   | eDrain
                                                   | eDelay
                                                   | eMetatext
                                                   | eStreamInterrupted
                                                   | eHalt
                                                   | eFlush
                                                   | eWait
                                                   | eDecodedStream
                                                   | eAudioPcm
                                                   | eAudioDsd
                                                   | eSilence
                                                   | eQuit;

AsyncTrackObserver::AsyncTrackObserver(
    IPipelineElementUpstream& aUpstreamElement,
    MsgFactory& aMsgFactory,
    TrackFactory& aTrackFactory)

    : PipelineElement(kSupportedMsgTypes)
    , iUpstreamElement(aUpstreamElement)
    , iMsgFactory(aMsgFactory)
    , iTrackFactory(aTrackFactory)
    , iClient(nullptr)
    , iDecodedStream(nullptr)
    , iDecodedStreamPending(false)
    , iPipelineTrackSeen(false)
    , iDurationMs(0)
    , iLastKnownPositionMs(0)
    , iLock("ASTR")
{
}

AsyncTrackObserver::~AsyncTrackObserver()
{
    AutoMutex _(iLock);
    if (iDecodedStream != nullptr) {
        iDecodedStream->RemoveRef();
    }
}

Msg* AsyncTrackObserver::Pull()
{
    {
        AutoMutex _(iLock);
        if (iClient != nullptr && iPipelineTrackSeen && iDecodedStream != nullptr) {
            if (iRequests.Exists(iClient->Mode())) {
                BwsTrackMetaData buf;
                WriterBuffer writer(buf);
                iClient->WriteMetadata(iTrackUri, iDecodedStream->StreamInfo(), writer);

                Track* track = iTrackFactory.CreateTrack(iTrackUri, buf);
                Msg* msg = iMsgFactory.CreateMsgTrack(*track, false);
                track->RemoveRef();

                iRequests.Remove(iClient->Mode());
                return msg;
            }
            if (iDecodedStreamPending) {
                auto& boundary = iClient->GetTrackBoundary();
                iDurationMs = boundary.DurationMs();
                iLastKnownPositionMs = boundary.OffsetMs();
                UpdateDecodedStreamLocked();
                iDecodedStreamPending = false;
                return iDecodedStream;
            }
        }
    }

    Msg* msg = iUpstreamElement.Pull();
    return msg->Process(*this);
}

void AsyncTrackObserver::AddClient(IAsyncTrackClient& aClient)
{
    AutoMutex _(iLock);
    iClients.push_back(&aClient);
}

void AsyncTrackObserver::TrackMetadataChanged(const Brx& aMode)
{
    AutoMutex _(iLock);
    if (iRequests.Exists(aMode)) {
        return;
    }
    iRequests.Add(aMode);
    iDecodedStreamPending = true;
}

void AsyncTrackObserver::TrackBoundaryChanged(const IAsyncTrackBoundary& aBoundary)
{
    AutoMutex _(iLock);
    if (iClient == nullptr) {
        return;
    }
    if (aBoundary.Mode() != iClient->Mode()) {
        return;
    }

    iDurationMs = aBoundary.DurationMs();
    iLastKnownPositionMs = aBoundary.OffsetMs();
    iDecodedStreamPending = true;
}

void AsyncTrackObserver::TrackPositionChanged(const IAsyncTrackPosition& aPosition)
{
    AutoMutex _(iLock);
    if (iClient == nullptr) {
        return;
    }
    if (aPosition.Mode() != iClient->Mode()) {
        return;
    }

    const TUint kPositionDeltaMs = abs(aPosition.PositionMs() - iLastKnownPositionMs);
    if (kPositionDeltaMs > kPositionDeltaThresholdMs) {
        iDecodedStreamPending = true; // Loss of sync detected
    }
    iLastKnownPositionMs = aPosition.PositionMs();
}

Msg* AsyncTrackObserver::ProcessMsg(MsgMode* aMsg)
{
    AutoMutex _(iLock);
    iClient = nullptr;
    if (iDecodedStream != nullptr) {
        iDecodedStream->RemoveRef();
        iDecodedStream = nullptr;
    }
    iDecodedStreamPending = false;
    iPipelineTrackSeen = false;
    iDurationMs = 0;
    iLastKnownPositionMs = 0;

    for (auto* client : iClients) {
        if (aMsg->Mode() == client->Mode()) {
            iClient = client;
        }
    }

    // Remove requests that don't belong to this mode
    iClient == nullptr ? iRequests.Clear() : iRequests.Trim(iClient->Mode());
    return aMsg;
}

Msg* AsyncTrackObserver::ProcessMsg(MsgTrack* aMsg)
{
    AutoMutex _(iLock);
    if (iClient == nullptr) {
        return aMsg;
    }
    iTrackUri.Replace(aMsg->Track().Uri());
    iPipelineTrackSeen = true;
    return aMsg;
}

Msg* AsyncTrackObserver::ProcessMsg(MsgDecodedStream* aMsg)
{
    AutoMutex _(iLock);
    if (iClient == nullptr) {
        return aMsg;
    }
    if (iDecodedStream != nullptr) {
        iDecodedStream->RemoveRef();
    }
    iDecodedStream = aMsg;
    iDecodedStream->AddRef();
    iDecodedStreamPending = true;
    return aMsg;
}

void AsyncTrackObserver::UpdateDecodedStreamLocked()
{
    ASSERT(iDecodedStream != nullptr);
    const DecodedStreamInfo& info = iDecodedStream->StreamInfo();
    const TUint64 kTrackLengthJiffies = (TUint64)(static_cast<TUint64>(iDurationMs) * static_cast<TUint64>(Jiffies::kPerMs));
    const TUint64 kOffsetSamples = (TUint64)((static_cast<TUint64>(iLastKnownPositionMs) * static_cast<TUint64>(info.SampleRate())) / 1000llu);

    MsgDecodedStream* msg = iMsgFactory.CreateMsgDecodedStream(
        info.StreamId(),
        info.BitRate(),
        info.BitDepth(),
        info.SampleRate(),
        info.NumChannels(),
        info.CodecName(),
        kTrackLengthJiffies,
        kOffsetSamples,
        info.Lossless(),
        info.Seekable(),
        info.Live(),
        info.AnalogBypass(),
        info.Format(),
        info.Multiroom(),
        info.Profile(),
        info.StreamHandler(),
        info.Ramp());

    iDecodedStream->RemoveRef();
    iDecodedStream = msg;
    iDecodedStream->AddRef();
}
#pragma once

#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Types.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Net/Private/CpiStack.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Av/Playlist/TrackDatabase.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Av/Pins/Pins.h>
        
namespace OpenHome {
namespace Av {

class IPodcastPinsObserver
{
public:
    virtual void NewPodcastEpisodesAvailable(const Brx& aEpisodeIds) = 0; // comma separated list of episode IDs
    virtual ~IPodcastPinsObserver() {}
};

class IPodcastTransportHandler
{
public:
    virtual void Init(TBool aShuffle) = 0;
    virtual void Load(Media::Track& aTrack) = 0;
    virtual void Play() = 0;
    virtual TBool SingleShot() = 0;
    virtual ~IPodcastTransportHandler() {}
};

};  // namespace Av
};  // namespace OpenHome



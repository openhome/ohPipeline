#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Timer.h>
#include <OpenHome/Media/UriProviderRepeater.h>
#include <OpenHome/Media/ClockPuller.h>
#include <OpenHome/Av/Source.h>
#include <OpenHome/Av/QobuzConnect/MediaControl.h>

#include <qobuz_connect.h>

namespace OpenHome {
namespace Net {
    class IMdnsProvider;
}
namespace Media {
    class TrackFactory;
}
namespace Av {

    class IMediaPlayer;
    class QobuzConnectApp;
    class ProtocolQobuzConnect;

class UriProviderQobuzConnect : public Media::UriProviderRepeater
{
public:
    UriProviderQobuzConnect(const TChar* aMode, Media::TrackFactory& aTrackFactory);
private: // from UriProvider
    Optional<Media::IClockPuller> ClockPuller() override;
private:
    Media::ClockPullerMock iClockPuller;
};

/**
 * Wires the whole Qobuz Connect integration together and exposes it as a DS Source - mirrors
 * SourceRaat (OpenHome/Av/Raat/SourceRaat.h) closely; see QobuzConnectApp/ProtocolQobuzConnect/
 * MediaControl.h for how the underlying SDK integration itself differs from RAAT's.
 *
 * Known gaps versus a real, ready-to-ship integration (flagged here rather than silently, since
 * this is a first pass at proving the design, not a finished feature):
 *  - No real Qobuz-issued app ID/secret exists yet - whatever's passed in at construction only
 *    works once Linn has one; nothing here can be tested against the real Qobuz service without it.
 *  - "Now playing" metadata (title/artist/album/artwork) from stream_metadata_callback isn't
 *    surfaced - see the TODO in AudioStream.cpp.
 *  - Gapless/cross-fade between tracks isn't supported (QobuzConnectAudioStream only tracks one
 *    active stream at a time - see its class comment).
 *  - Volume/mute control isn't wired up (QBZ_VOLUME_CAPABILITY_NONE is advertised).
 *  - The local config HTTP server doesn't react to network adapter changes after it starts.
 */
class SourceQobuzConnect
    : public Source
    , public IQobuzConnectPlaybackObserver
{
private:
    static const TUint kStartupDelaySecs = 20; // mirrors SourceRaat's kStartupDelaySecs
    static const TUint kStartupDelayMs = kStartupDelaySecs * 1000;
public:
    SourceQobuzConnect(
        IMediaPlayer& aMediaPlayer,
        Net::IMdnsProvider& aMdnsProvider,
        const Brx& aAppId,
        const Brx& aAppSecret,
        const Brx& aManufacturer,
        const Brx& aModel,
        const Brx& aSerialNumber);
    ~SourceQobuzConnect();
private: // from ISource
    void Activate(TBool aAutoPlay, TBool aPrefetchAllowed) override;
    void PipelineStopped() override;
    TBool TryActivateNoPrefetch(const Brx& aMode) override;
    void StandbyEnabled() override;
private: // from IQobuzConnectPlaybackObserver
    void QobuzNotifyStreamReady() override;
    void QobuzNotifyPlaybackInitiated(TBool aStartPaused) override;
    void QobuzNotifyPlaybackPaused() override;
    void QobuzNotifyPlaybackResumed() override;
    void QobuzNotifyPlaybackStopped() override;
    void QobuzNotifySeekInProgress() override;
    void QobuzNotifyActiveStateChanged(TBool aActive) override;
private:
    void InitialiseSourceQobuzConnect();
    void Start();
private:
    UriProviderQobuzConnect* iUriProvider;
    QobuzConnectApp* iApp;
    ProtocolQobuzConnect* iProtocol;
    Media::Track* iTrack;
    Media::BwsTrackMetaData iDefaultMetadata;
    Timer* iTimer;
    TBool iBeginPending;
};

}
}

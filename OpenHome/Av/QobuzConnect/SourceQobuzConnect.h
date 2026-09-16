#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Timer.h>
#include <OpenHome/Media/UriProviderRepeater.h>
#include <OpenHome/Media/ClockPuller.h>
#include <OpenHome/Av/Source.h>
#include <OpenHome/Av/QobuzConnect/MediaControl.h>
#include <OpenHome/Av/QobuzConnect/Metadata.h>
#include <OpenHome/Av/VolumeManager.h>
#include <OpenHome/Media/MuteManager.h>
#include <OpenHome/Configuration/ConfigManager.h>

#include <qobuz_connect.h>

#include <atomic>

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
 *  - Gapless/cross-fade between tracks isn't supported (QobuzConnectAudioStream only tracks one
 *    active stream at a time - see its class comment).
 *  - The local config HTTP server doesn't react to network adapter changes after it starts.
 */
class SourceQobuzConnect
    : public Source
    , public IQobuzConnectPlaybackObserver
    , private IVolumeObserver
    , private Media::IMuteObserver
{
private:
    static const TUint kStartupDelaySecs = 20; // mirrors SourceRaat's kStartupDelaySecs
    static const TUint kStartupDelayMs = kStartupDelaySecs * 1000;
    // How long to wait, after a track finishes, for the SDK to auto-advance to the next one
    // (QobuzNotifyStreamStarted() arriving with iAutoAdvancePending set) before concluding
    // nothing more is coming and reporting that back - see QobuzNotifyStreamFinished()'s comment.
    // Generous relative to anything seen on hardware (typically well under a couple of seconds,
    // even accounting for the SDK's documented one retry on a failed network action) without
    // leaving the SDK's own state - and the Controller's display - waiting indefinitely on a
    // "transitioning" track that's actually just reached the end of the queue.
    static const TUint kAutoAdvanceTimeoutMs = 15000;
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
    void Deactivate() override;
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
    void QobuzNotifyMetadataChanged(const Brx& aTitle, const Brx& aArtist, const Brx& aAlbum, const Brx& aArtworkUri) override;
    void QobuzNotifyStreamSeeked(uint64_t aPositionMs) override;
    void QobuzNotifyStreamFinished() override;
    void QobuzNotifyStreamStarted() override;
    void QobuzNotifyVolumeChanged(TUint aVolumePercent) override;
    void QobuzNotifyMuteStateChanged(TBool aMuted) override;
private: // from IVolumeObserver
    void VolumeChanged(const IVolumeValue& aVolume) override;
private: // from Media::IMuteObserver
    void MuteChanged(TBool aValue) override;
private:
    void InitialiseSourceQobuzConnect();
    void Start();
    void LimitChanged(Configuration::ConfigNum::KvpNum& aKvp);
    void PushVolume();
    void HandleAutoAdvanceTimeout();
private:
    UriProviderQobuzConnect* iUriProvider;
    QobuzConnectApp* iApp;
    ProtocolQobuzConnect* iProtocol;
    QobuzConnectMetadataHandler* iMetadataHandler;
    IVolumeManager& iVolumeManager;
    // The Controller's volume slider is always a fixed 0-100 range (see qbz_connect_set_volume),
    // so it's scaled against the user-configured volume LIMIT (mirrors RaatVolume - see its class
    // comment), not IVolumeProfile::VolumeMax() (the theoretical hardware max, ignoring whatever
    // limit is currently configured) - otherwise the top of the Controller's slider range would
    // map onto DS volume values above the configured limit, which DS's own limiter then silently
    // clamps back down to the limit, making the last stretch of the slider a no-op.
    Configuration::ConfigNum& iConfigLimit;
    TUint iSubscriberIdLimit;
    // Cached from the most recent VolumeChanged()/MuteChanged()/LimitChanged() callback (which
    // also fire synchronously, with the current value, as soon as
    // AddVolumeObserver()/AddMuteObserver()/ConfigNum::Subscribe() are called) - re-pushed to the
    // SDK from QobuzNotifyActiveStateChanged() once the SDK core is guaranteed to actually exist,
    // since those initial synchronous callbacks fire at construction time, long before it does.
    std::atomic<TUint> iVolumeUser;
    std::atomic<TUint> iVolumeLimit;
    std::atomic<TBool> iMuted;
    Media::Track* iTrack;
    Media::BwsTrackMetaData iDefaultMetadata;
    Timer* iTimer;
    // Started by QobuzNotifyStreamFinished() whenever it sets iAutoAdvancePending, cancelled once
    // that's genuinely consumed by a following QobuzNotifyStreamStarted() (or superseded by an
    // explicit QobuzNotifyStreamReady()) - see HandleAutoAdvanceTimeout()/kAutoAdvanceTimeoutMs.
    Timer* iTimerAutoAdvanceTimeout;
    TBool iBeginPending;
    // Whether the DS Pipeline currently holds OUR track/mode - distinct from
    // iProtocol->IsStreaming() (the Qobuz SDK's own stream state). Both are normally in lockstep,
    // but Deactivate() clears this eagerly (see its comment) since DS's Pipeline interrupts
    // whichever protocol is streaming as part of any source switch, slightly ahead of
    // IsStreaming() reflecting that asynchronously. InitialiseSourceQobuzConnect() checks both,
    // so a reactivation racing in during that window still re-issues Pipeline::Begin() rather
    // than being incorrectly skipped.
    TBool iPipelineOwned;
    // Set when QobuzNotifyStreamFinished() tells the SDK it will continue automatically (no
    // initiate_playback_callback follows for that case - see QobuzNotifyStreamStarted()'s
    // comment); consumed (and cleared) by the next QobuzNotifyStreamStarted(), which is then
    // responsible for acknowledging that auto-advanced stream itself.
    std::atomic<TBool> iAutoAdvancePending;
};

}
}

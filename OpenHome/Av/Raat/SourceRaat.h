#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/UriProviderRepeater.h>
#include <OpenHome/Media/ClockPuller.h>
#include <OpenHome/Av/Product.h>
#include <OpenHome/Av/Source.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Private/Timer.h>

namespace OpenHome {
    namespace Media {
        class TrackFactory;
        class IClockPuller;
        class IAudioTime;
        class IPullableClock;
    }
namespace Av {

enum class RoonProtocol
{
    Raat,
    Scd
};

class RoonProtocolSelector
{
    static const Brn kKeyProtocol;
    static const TUint kValRaat;
    static const TUint kValScd;
public:
    RoonProtocolSelector(Configuration::IConfigInitialiser& aConfigInitialiser);
    ~RoonProtocolSelector();
    RoonProtocol Protocol() const;
    Configuration::ConfigChoice* Transfer();
private:
    void ProtocolChanged(Configuration::KeyValuePair<TUint>& aKvp);
private:
    Configuration::ConfigChoice* iConfigProtocol;
    RoonProtocol iProtocol;
    TUint iSubscriberId;
};

class UriProviderRaat : public Media::UriProviderRepeater
{
public:
    UriProviderRaat(const TChar* aMode, Media::TrackFactory& aTrackFactory); // should later pass IClockPuller& 
private: // from UriProvider
    Optional<Media::IClockPuller> ClockPuller() override;
private:
    Media::ClockPullerMock iClockPuller;
};


class ISourceRaat
{
public:
    virtual ~ISourceRaat() {}
    virtual void NotifySetup() = 0;
    virtual void NotifyStart() = 0;
    virtual void NotifyStop() = 0;
};

class IMediaPlayer;
class RaatApp;
class ProtocolRaat;
class IRaatSignalPathObservable;

class SourceRaat
    : public Source
    , public ISourceRaat
    , private IProductObserver
{
private:
    static const TUint kStartupDelaySecs = 20;
    static const TUint kStartupDelayMs = kStartupDelaySecs * 1000;
public:
    SourceRaat(
        IMediaPlayer& aMediaPlayer,
        Media::IAudioTime& aAudioTime,
        Media::IPullableClock& aPullableClock,
        IRaatSignalPathObservable* aSignalPathObservable,
        Optional<Configuration::ConfigChoice> aProtocolSelector,
        const Brx& aSerialNumber,
        const Brx& aSoftwareVersion,
        const Brx& aConfigUrl);
    ~SourceRaat();
private: // from ISource
    void Activate(TBool aAutoPlay, TBool aPrefetchAllowed) override;
    void PipelineStopped() override;
    TBool TryActivateNoPrefetch(const Brx& aMode) override;
    void StandbyEnabled() override;
private: // from ISourceRaat
    void NotifySetup() override;
    void NotifyStart() override;
    void NotifyStop() override;
private: // from IProductObserver
    void Started() override;
    void SourceIndexChanged() override;
    void SourceXmlChanged() override;
    void ProductUrisChanged() override;
private:
    void Initialise();
    void Start();
private:
    IRaatSignalPathObservable* iSignalPathObservable;
    Configuration::ConfigChoice* iProtocolSelector;
    UriProviderRaat* iUriProvider;
    RaatApp* iApp;
    ProtocolRaat* iProtocol;
    Media::Track* iTrack;
    Media::BwsTrackMetaData iDefaultMetadata;

    Timer* iTimer;
};

}
}
#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Media/UriProviderSingleTrack.h>
#include <OpenHome/Av/Product.h>
#include <OpenHome/Av/Source.h>

namespace OpenHome {
    namespace Media {
        class TrackFactory;
        class IClockPuller;
    }
namespace Av {

class UriProviderRaat : public Media::UriProviderSingleTrack
{
public:
    UriProviderRaat(const TChar* aMode, Media::TrackFactory& aTrackFactory); // should later pass IClockPuller& 
private: // from UriProvider
    Optional<Media::IClockPuller> ClockPuller() override;
};


class ISourceRaat
{
public:
    virtual ~ISourceRaat() {}
    virtual void Play(const Brx& aUri) = 0;
};

class IMediaPlayer;
class RaatApp;
class IRaatTime;

class SourceRaat : public Source, public ISourceRaat, private IProductObserver
{
public:
    SourceRaat(IMediaPlayer& aMediaPlayer, IRaatTime* aRaatTime);
    ~SourceRaat();
private: // from ISource
    void Activate(TBool aAutoPlay, TBool aPrefetchAllowed) override;
    void PipelineStopped() override;
    TBool TryActivateNoPrefetch(const Brx& aMode) override;
    void StandbyEnabled() override;
private: // from ISourceRaat
    void Play(const Brx& aUri) override;
private: // from IProductObserver
    void Started() override;
    void SourceIndexChanged() override;
    void SourceXmlChanged() override;
    void ProductUrisChanged() override;
private:
    void Play();
    void Stop();
private:
    Mutex iLock;
    IMediaPlayer& iMediaPlayer;
    IRaatTime* iRaatTime;
    UriProviderRaat* iUriProvider;
    RaatApp* iApp;
    Media::Track* iTrack;
    Media::BwsTrackMetaData iDefaultMetadata;
};

}
}
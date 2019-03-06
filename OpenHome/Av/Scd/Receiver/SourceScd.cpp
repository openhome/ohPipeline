#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Av/Source.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Av/SourceFactory.h>
#include <OpenHome/Av/Scd/Receiver/UriProviderScd.h>
#include <OpenHome/Av/Scd/Receiver/ProtocolScd.h>
#include <OpenHome/Media/PipelineManager.h>

#include <atomic>

namespace OpenHome {
namespace Scd {

class SourceScd : public Av::Source, private IScdObserver
{
    static const TBool kDefaultVisibility;
public:
    SourceScd(Av::IMediaPlayer& aMediaPlayer, TUint aDsdSampleBlockWords, TUint aDsdPadBytesPerChunk);
private: // from ISource
    void Activate(TBool aAutoPlay, TBool aPrefetchAllowed) override;
    TBool TryActivateNoPrefetch(const Brx& aMode) override;
    void PipelineStopped() override;
    void StandbyEnabled() override;
private:
    void Play();
private: // from IScdObserver
    void NotifyScdConnectionChange(TBool aConnected) override;
private:
    UriProviderScd* iUriProvider;
    std::atomic<TBool> iConnected;
};

} // namespace Scd
} // namespace OpenHome


using namespace OpenHome;
using namespace OpenHome::Scd;
using namespace OpenHome::Av;

Av::ISource* Av::SourceFactory::NewScd(Av::IMediaPlayer& aMediaPlayer, TUint aDsdSampleBlockWords, TUint aDsdPadBytesPerChunk)
{
    return new SourceScd(aMediaPlayer, aDsdSampleBlockWords, aDsdPadBytesPerChunk);
}

const Brn Av::SourceFactory::kSourceNameScd("Roon");
const TChar* Av::SourceFactory::kSourceTypeScd = "Scd";
const TBool SourceScd::kDefaultVisibility = false;

SourceScd::SourceScd(Av::IMediaPlayer& aMediaPlayer, TUint aDsdSampleBlockWords, TUint aDsdPadBytesPerChunk)
    : Source(Av::SourceFactory::kSourceNameScd,
             Av::SourceFactory::kSourceTypeScd,
             aMediaPlayer.Pipeline(),
             kDefaultVisibility)
{
    auto& trackFactory = aMediaPlayer.TrackFactory();
    auto protocol = new ProtocolScd(aMediaPlayer.Env(), trackFactory, aDsdSampleBlockWords, aDsdPadBytesPerChunk, *this);
    aMediaPlayer.Pipeline().Add(protocol); // ownership passed
    iUriProvider = new UriProviderScd(trackFactory);
    iUriProvider->SetTransportPlay(MakeFunctor(*this, &SourceScd::Play));
    aMediaPlayer.Add(iUriProvider); // ownership passed
}

void SourceScd::Activate(TBool /*aAutoPlay*/, TBool /*aPrefetchAllowed*/)
{
    iUriProvider->Reset();
    iPipeline.StopPrefetch(iUriProvider->Mode(), Media::Track::kIdNone);
}

TBool SourceScd::TryActivateNoPrefetch(const Brx& aMode)
{
    if (iUriProvider->Mode() != aMode) {
        return false;
    }
    EnsureActiveNoPrefetch();
    return true;
}

void SourceScd::PipelineStopped()
{
    // Nothing to do here.
}

void SourceScd::StandbyEnabled()
{
    iPipeline.Stop();
}

void SourceScd::Play()
{
    if (iConnected.load()) {
        iPipeline.Play();
    }
}

void SourceScd::NotifyScdConnectionChange(TBool aConnected)
{
    iConnected.store(aConnected);
}

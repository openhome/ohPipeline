#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Private/DviStack.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Net/Core/DvDevice.h>
#include <OpenHome/Media/PipelineObserver.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Av/Source.h>

#include <atomic>

namespace OpenHome {
namespace Media {
    class PipelineManager;
    class MimeTypeList;
    class UriProviderRepeater;
}
namespace Av {

class ISourceUpnpAv
{
public:
    virtual ~ISourceUpnpAv() {}
    virtual void SetTrack(const Brx& aUri, const Brx& aMetaData) = 0;
    virtual void Play() = 0;
    virtual void Pause() = 0;
    virtual void Stop() = 0;
    virtual void Next() = 0;
    virtual void Prev() = 0;
    virtual void Seek(TUint aSecondsAbsolute) = 0;
};

class IMediaPlayer;
class ProviderAvTransport;
class ProviderConnectionManager;
class ProviderRenderingControl;
    
class SourceUpnpAv : public Source, private ISourceUpnpAv, private Media::IPipelineObserver
{
public:
    SourceUpnpAv(IMediaPlayer& aMediaPlayer, Net::DvDevice& aDevice, Media::UriProviderRepeater& aUriProvider, Media::MimeTypeList& aMimeTypeList);
    ~SourceUpnpAv();
private:
    void NotifyState(Media::EPipelineState aState);
private: // from Source
    void Activate(TBool aAutoPlay, TBool aPrefetchAllowed) override;
    void Deactivate() override;
    TBool TryActivateNoPrefetch(const Brx& aMode) override;
    void StandbyEnabled() override;
    void PipelineStopped() override;
private: // from ISourceUpnpAv
    void SetTrack(const Brx& aUri, const Brx& aMetaData) override;
    void Play() override;
    void Pause() override;
    void Stop() override;
    void Next() override;
    void Prev() override;
    void Seek(TUint aSecondsAbsolute) override;
private: // from IPipelineObserver
    void NotifyPipelineState(Media::EPipelineState aState) override;
    void NotifyMode(const Brx& aMode, const Media::ModeInfo& aInfo,
                    const Media::ModeTransportControls& aTransportControls) override;
    void NotifyTrack(Media::Track& aTrack, TBool aStartOfStream) override;
    void NotifyMetaText(const Brx& aText) override;
    void NotifyTime(TUint aSeconds) override;
    void NotifyStreamInfo(const Media::DecodedStreamInfo& aStreamInfo) override;
private:
    Mutex iLock;
    Mutex iActivationLock;
    Net::DvDevice& iDevice;
    Media::UriProviderRepeater& iUriProvider;
    Media::Track* iTrack;
    ProviderAvTransport* iProviderAvTransport;
    ProviderConnectionManager* iProviderConnectionManager;
    ProviderRenderingControl* iProviderRenderingControl;
    Media::IPipelineObserver* iDownstreamObserver;
    std::atomic<TUint> iStreamId;
    Media::EPipelineState iTransportState;
    Media::EPipelineState iPipelineTransportState;
    TBool iIgnorePipelineStateUpdates;
};

} // namespace Av
} // namespace OpenHome


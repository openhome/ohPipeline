#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Av/Raat/Plugin.h>
#include <OpenHome/Media/PipelineObserver.h>

#include <rc_status.h>
#include <raat_plugin_transport.h>
#include <jansson.h>

namespace OpenHome {
namespace Av {
    class IMediaPlayer;

class RaatTransport;

typedef struct {
    RAAT__TransportPlugin iPlugin; // must be first member
    RaatTransport* iSelf;
} RaatTransportPluginExt;

class RaatTransport : public RaatPluginAsync, private Media::IPipelineObserver
{
public:
    RaatTransport(IMediaPlayer& aMediaPlayer);
    ~RaatTransport();
    RAAT__TransportPlugin* Plugin();
    void AddControlListener(RAAT__TransportControlCallback aCb, void *aCbUserdata);
    void RemoveControlListener(RAAT__TransportControlCallback aCb, void *aCbUserdata);
    void UpdateStatus(json_t *aStatus);
private: // from RaatPluginAsync
    void ReportState() override;
private: // from Media::IPipelineObserver
    void NotifyPipelineState(Media::EPipelineState aState) override;
    void NotifyMode(
        const Brx& aMode,
        const Media::ModeInfo& aInfo,
        const Media::ModeTransportControls& aTransportControls) override;
    void NotifyTrack(Media::Track& aTrack, TBool aStartOfStream) override;
    void NotifyMetaText(const Brx& aText) override;
    void NotifyTime(TUint aSeconds) override;
    void NotifyStreamInfo(const Media::DecodedStreamInfo& aStreamInfo) override;
private:
    RaatTransportPluginExt iPluginExt;
    RAAT__TransportControlListeners iListeners;
    Media::EPipelineState iTransportState;
    Bws<Media::kTrackMetaDataMaxBytes> iDidlLite;
};

} // nsamepsacenamespace Av
} // namespace OpenHome
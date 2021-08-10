#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Av/Source.h>
#include <OpenHome/Av/Product.h>
#include <OpenHome/Media/Pipeline/Msg.h>
#include <OpenHome/Media/PipelineObserver.h>
#include <OpenHome/Av/Radio/PresetDatabase.h>

namespace OpenHome {
    class Environment;
    class PowerManager;
    class StoreInt;
namespace Net {
    class DvDevice;
}
namespace Media {
    class PipelineManager;
    class MimeTypeList;
    class UriProviderSingleTrack;
}
namespace Av {

class ISourceRadio
{
public:
    virtual ~ISourceRadio() {}
    virtual TBool TryFetch(TUint aPresetId, const Brx& aUri) = 0;
    virtual void Fetch(const Brx& aUri, const Brx& aMetaData) = 0;
    virtual void Play() = 0;
    virtual void Pause() = 0;
    virtual void Stop() = 0;
    virtual void Next() = 0;
    virtual void Prev() = 0;
    virtual void SeekAbsolute(TUint aSeconds) = 0;
    virtual void SeekRelative(TInt aSeconds) = 0;
};

class ProviderRadio;
class IMediaPlayer;
class UriProviderRadio;
class RadioPresets;

class SourceRadio : public Source
                  , private ISourceRadio
                  , private Media::IPipelineObserver
                  , private IPresetDatabaseObserver
                  , private IProductObserver
{
public:
    SourceRadio(IMediaPlayer& aMediaPlayer, const Brx& aTuneInPartnerId);
    ~SourceRadio();
private: // from ISource
    void Activate(TBool aAutoPlay, TBool aPrefetchAllowed) override;
    void Deactivate() override;
    TBool TryActivateNoPrefetch(const Brx& aMode) override;
    void StandbyEnabled() override;
    void PipelineStopped() override;
private: // from ISourceRadio
    TBool TryFetch(TUint aPresetId, const Brx& aUri) override;
    void Fetch(const Brx& aUri, const Brx& aMetaData) override;
    void Play() override;
    void Pause() override;
    void Stop() override;
    void Next() override;
    void Prev() override;
    void SeekAbsolute(TUint aSeconds) override;
    void SeekRelative(TInt aSeconds) override;
private: // from IPresetDatabaseObserver
    void PresetDatabaseChanged() override;
private:
    void FetchLocked(const Brx& aUri, const Brx& aMetaData);
    void NextPrev(TBool aNext);
private: // from IPipelineObserver
    void NotifyPipelineState(Media::EPipelineState aState) override;
    void NotifyMode(const Brx& aMode, const Media::ModeInfo& aInfo,
                    const Media::ModeTransportControls& aTransportControls) override;
    void NotifyTrack(Media::Track& aTrack, TBool aStartOfStream) override;
    void NotifyMetaText(const Brx& aText) override;
    void NotifyTime(TUint aSeconds) override;
    void NotifyStreamInfo(const Media::DecodedStreamInfo& aStreamInfo) override;
private: // from IProductObserver
    void Started() override;
    void SourceIndexChanged() override;
    void SourceXmlChanged() override;
    void ProductUrisChanged() override;
private:
    Mutex iLock;
    UriProviderRadio* iUriProviderPresets;
    Media::UriProviderSingleTrack* iUriProviderSingle;
    Brn iCurrentMode;
    ProviderRadio* iProviderRadio;
    PresetDatabase* iPresetDatabase;
    RadioPresets* iPresets;
    Media::Track* iTrack;
    TUint iTrackPosSeconds;
    TUint iStreamId;
    TBool iLive;
    TBool iPresetsUpdated;
    TBool iAutoPlay;
    StoreInt* iStorePresetNumber;
    Media::BwsTrackUri iPresetUri; // only required by Fetch(TUint) but too large for the stack
    Media::BwsTrackMetaData iPresetMetadata; // only required by Fetch(TUint) but too large for the stack
};

} // namespace Av
} // namespace OpenHome


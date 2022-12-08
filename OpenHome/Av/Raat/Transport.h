#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Av/Raat/Plugin.h>
#include <OpenHome/Media/Pipeline/Msg.h>

#include <rc_status.h>
#include <raat_plugin_transport.h>
#include <jansson.h>

namespace OpenHome {
namespace Av {

class RaatTransport;

class RaatTransportInfo
{
public:
    RaatTransportInfo()
        : iPauseSupported(false)
        , iNextSupported(false)
        , iPrevSupported(false)
        , iSeekSupported(false)
        , iShuffle(false)
        , iRepeat(false)
    {}

    void SetPauseSupported(TBool aSupported) { iPauseSupported = aSupported; }
    void SetNextSupported(TBool aSupported) { iNextSupported = aSupported; }
    void SetPrevSupported(TBool aSupported) { iPrevSupported = aSupported; }
    void SetSeekSupported(TBool aSupported) { iSeekSupported = aSupported; }
    void SetShuffle(TBool aShuffle) { iShuffle = aShuffle; }
    void SetRepeat(TBool aRepeat) { iRepeat = aRepeat; }

    TBool PauseSupported() const { return iPauseSupported; }
    TBool NextSupported() const { return iNextSupported; }
    TBool PrevSupported() const { return iPrevSupported; }
    TBool SeekSupported() const { return iSeekSupported; }
    TBool Shuffle() const { return iShuffle; }
    TBool Repeat() const { return iRepeat; }
private:
    TBool iPauseSupported;
    TBool iNextSupported;
    TBool iPrevSupported;
    TBool iSeekSupported;
    TBool iShuffle;
    TBool iRepeat;
};

typedef struct {
    RAAT__TransportPlugin iPlugin; // must be first member
    RaatTransport* iSelf;
} RaatTransportPluginExt;

class IRaatTransport
{
public:
    virtual ~IRaatTransport() {}
    virtual void Play() = 0;
    virtual TBool CanPause() = 0;
    virtual void Stop() = 0;
    virtual TBool CanMoveNext() = 0;
    virtual TBool CanMovePrev() = 0;
};

class RaatTransport : public IRaatTransport
{
public:
    RaatTransport();
    ~RaatTransport();
    RAAT__TransportPlugin* Plugin();
    void AddControlListener(RAAT__TransportControlCallback aCb, void *aCbUserdata);
    void RemoveControlListener(RAAT__TransportControlCallback aCb, void *aCbUserdata);
    void UpdateStatus(json_t *aStatus);
private:
    static const TChar* ValueString(json_t* aObject, const TChar* aKey);
    static TBool ValueBool(json_t* aObject, const TChar* aKey);
    void DoReportState(const TChar* aState);
private: // from IRaatTransport
    void Play() override;
    TBool CanPause() override;
    void Stop() override;
    TBool CanMoveNext() override;
    TBool CanMovePrev() override;
private:
    RaatTransportPluginExt iPluginExt;
    RAAT__TransportControlListeners iListeners;
    RaatTransportInfo iTrackCapabilities;
    Bws<Media::kTrackMetaDataMaxBytes> iDidlLite;
};

} // nsamepsacenamespace Av
} // namespace OpenHome

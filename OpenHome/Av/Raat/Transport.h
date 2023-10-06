#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Av/Raat/SourceSelection.h>
#include <OpenHome/Av/Raat/Metadata.h>
#include <OpenHome/Av/Raat/Artwork.h>
#include <OpenHome/Av/TransportControl.h>
#include <OpenHome/Media/Pipeline/Msg.h>

#include <atomic>

#include <rc_status.h>
#include <raat_plugin_transport.h>
#include <jansson.h>

EXCEPTION(RaatTransportStatusParserError);

namespace OpenHome {
namespace Av {

class RaatTransport;

class RaatTransportInfo
{
public:
    enum class ERepeatMode {
        eOff,
        eRepeat,
        eRepeatOne
    };
public:
    RaatTransportInfo()
        : iPauseSupported(false)
        , iNextSupported(false)
        , iPrevSupported(false)
        , iSeekSupported(false)
        , iShuffle(false)
        , iRepeatMode(ERepeatMode::eOff)
    {}

    void Set(const RaatTransportInfo& aTransportInfo)
    {
        iPauseSupported = aTransportInfo.PauseSupported();
        iNextSupported = aTransportInfo.NextSupported();
        iPrevSupported = aTransportInfo.PrevSupported();
        iSeekSupported = aTransportInfo.SeekSupported();
        iShuffle = aTransportInfo.Shuffle();
        iRepeatMode = aTransportInfo.RepeatMode();
    }

    void SetPauseSupported(TBool aSupported) { iPauseSupported = aSupported; }
    void SetNextSupported(TBool aSupported) { iNextSupported = aSupported; }
    void SetPrevSupported(TBool aSupported) { iPrevSupported = aSupported; }
    void SetSeekSupported(TBool aSupported) { iSeekSupported = aSupported; }
    void SetShuffle(TBool aShuffle) { iShuffle = aShuffle; }
    void SetRepeat(ERepeatMode aMode) { iRepeatMode = aMode; }

    TBool PauseSupported() const { return iPauseSupported; }
    TBool NextSupported() const { return iNextSupported; }
    TBool PrevSupported() const { return iPrevSupported; }
    TBool SeekSupported() const { return iSeekSupported; }
    TBool Shuffle() const { return iShuffle; }
    ERepeatMode RepeatMode() const { return iRepeatMode; }
private:
    TBool iPauseSupported;
    TBool iNextSupported;
    TBool iPrevSupported;
    TBool iSeekSupported;
    TBool iShuffle;
    ERepeatMode iRepeatMode;
};

class RaatTrackInfo
{
private:
    static const TUint kDefaultInfoSize = 128;
public:
    enum class EState {
        ePlaying,
        eLoading,
        ePaused,
        eStopped,
        eUndefined
    };
public:
    RaatTrackInfo()
        : iState(EState::eUndefined)
        , iDurationSecs(0)
        , iPositionSecs(0)
    {}
public:
    void SetState(EState aState) { iState = aState; }
    void SetTitle(const Brx& aTitle) { iTitle.Replace(aTitle); }
    void SetSubtitle(const Brx& aSubtitle) { iSubtitle.Replace(aSubtitle); }
    void SetSubSubtitle(const Brx& aSubSubtitle) { iSubSubtitle.Replace(aSubSubtitle); }
    void SetDurationSecs(TUint aDurationSecs) { iDurationSecs = aDurationSecs; }
    void SetPositionSecs(TUint aPositionSecs) { iPositionSecs = aPositionSecs; }
public:
    EState GetState() const { return iState; }
    const Brx& GetTitle() const { return iTitle; }
    const Brx& GetSubtitle() const { return iSubtitle; }
    const Brx& GetSubSubtitle() const { return iSubSubtitle; }
    TUint GetDurationSecs() const { return iDurationSecs; }
    TUint GetPositionSecs() const { return iPositionSecs; }
private:
    EState iState;
    TUint iDurationSecs;
    TUint iPositionSecs;
    Bws<kDefaultInfoSize> iTitle;
    Bws<kDefaultInfoSize> iSubtitle;
    Bws<kDefaultInfoSize> iSubSubtitle;
};

typedef struct {
    RAAT__TransportPlugin iPlugin; // must be first member
    RaatTransport* iSelf;
} RaatTransportPluginExt;

class IRaatRepeatToggler
{
public:
    virtual void ToggleRepeat() = 0;
    virtual ~IRaatRepeatToggler() {}
};

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

class RaatTransportStatusParser
{
private:
    static const Brn kLoopDisabled;
    static const Brn kLoopEnabled;
    static const Brn kLoopOneEnabled;
    static const Brn kStatePlaying;
    static const Brn kStateLoading;
    static const Brn kStatePaused;
    static const Brn kStateStopped;

    static const std::map<Brn, RaatTrackInfo::EState, BufferCmp> kTrackStateMap;
    static const std::map<Brn, RaatTransportInfo::ERepeatMode, BufferCmp> kRepeatModeMap;
public:
    static void Parse(
        json_t*             aJson,
        RaatTransportInfo&  aTransportInfo,
        RaatTrackInfo&      aTrackInfo);
private:
    static const TChar* ValueString(json_t* aObject, const TChar* aKey);
    static TBool ValueBool(json_t* aObject, const TChar* aKey);
    static TUint ValueUint(json_t* aObject, const TChar* aKey);
};

class RaatTransportRepeatAdapter
{
public:
    RaatTransportRepeatAdapter(ITransportRepeatRandom& aTransportRepeatRandom, IRaatRepeatToggler& aRepeatToggler);
public:
    void RaatRepeatChanged(RaatTransportInfo::ERepeatMode aMode);
    void LinnRepeatChanged(TBool aRepeat);
    TBool RepeatEnabled() const;
private:
    ITransportRepeatRandom& iTransportRepeatRandom;
    IRaatRepeatToggler& iRepeatToggler;
    TBool iLinnRepeat;
    TBool iLinnRepeatChangePending;
    RaatTransportInfo::ERepeatMode iRaatRepeat;
    mutable Mutex iLock;
};

class IMediaPlayer;

class RaatTransport
    : public IRaatTransport
    , public IRaatSourceObserver
    , public IRaatRepeatToggler
    , private ITransportRepeatRandomObserver
{
public:
    RaatTransport(IMediaPlayer& aMediaPlayer);
    ~RaatTransport();
public:
    RAAT__TransportPlugin* Plugin();
    void AddControlListener(RAAT__TransportControlCallback aCb, void *aCbUserdata);
    void RemoveControlListener(RAAT__TransportControlCallback aCb, void *aCbUserdata);
    void UpdateStatus(json_t *aStatus);
    void UpdateArtwork(const char *mime_type, void *data, size_t data_len);
private:
    void DoReportState(const TChar* aState);
private: // from IRaatTransport
    void Play() override;
    TBool CanPause() override;
    void Stop() override;
    TBool CanMoveNext() override;
    TBool CanMovePrev() override;
private: // IRaatSourceObserver
    void RaatSourceActivated() override;
    void RaatSourceDeactivated() override;
private: // from IRaatRepeatToggler
    void ToggleRepeat() override;
private: // from ITransportRepeatRandomObserver
    void TransportRepeatChanged(TBool aRepeat) override;
    void TransportRandomChanged(TBool aRandom) override;
private:
    RaatTransportPluginExt iPluginExt;
    RAAT__TransportControlListeners iListeners;
    ITransportRepeatRandom& iTransportRepeatRandom;
    RaatTransportRepeatAdapter iRepeatAdapter;
    RaatArtworkHttpServer iArtworkServer;
    RaatMetadataHandler iMetadataHandler;
    RaatTransportInfo iTransportInfo;
    TBool iActive;
    RaatTrackInfo::EState iState;
    Mutex iLockStatus;
};

} // nsamepsacenamespace Av
} // namespace OpenHome

#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Av/Raat/Plugin.h>
#include <OpenHome/Av/Raat/SourceRaat.h>

#include <rc_status.h>
#include <raat_plugin_source_selection.h>

namespace OpenHome {
    class Brx;
    namespace Configuration {
        class ConfigNum;
    }
    namespace Net {
        class CpDeviceDv;
        class CpProxyAvOpenhomeOrgProduct4;
    }
namespace Av {

class RaatSourceSelection;

typedef struct {
    RAAT__SourceSelectionPlugin iPlugin; // must be first member
    RaatSourceSelection* iSelf;
} RaatSourceSelectionPluginExt;

class IRaatSourceObserver
{
public:
    virtual ~IRaatSourceObserver() {}
    virtual void RaatSourceActivated() = 0;
    virtual void RaatSourceDeactivated() = 0;
};

class IRaatOutputControl
{
public:
    virtual ~IRaatOutputControl() {}
    virtual void NotifyDeselected() = 0;
};

class IMediaPlayer;

class RaatSourceSelection : public RaatPluginAsync
{
private:
    enum class EStateStandby {
        eEnabled,
        eDisabled,
        eUndefined
    };
    enum class EStateSource {
        eSelected,
        eNotSelected,
        eUndefined
    };
    static const std::map<EStateStandby, Brn> kStandbyStringMap;
    static const std::map<EStateSource, Brn> kSourceStringMap;
public:
    RaatSourceSelection(
        IMediaPlayer& aMediaPlayer,
        const Brx& aSystemName,
        IRaatSourceObserver& aObserver,
        IRaatOutputControl& aOutputControl);
    ~RaatSourceSelection();
public:
    void Start();
public:
    RAAT__SourceSelectionPlugin* Plugin();
    RC__Status AddStateListener(RAAT__SourceSelectionStateCallback aCb, void *aCbUserdata);
    RC__Status RemoveStateListener(RAAT__SourceSelectionStateCallback aCb, void *aCbUserdata);
    void GetState(RAAT__SourceSelectionState *aState);
    void SetSource();
    void SetStandby();
private:
    void StandbyChanged();
    void SourceIndexChanged();
private: // from RaatPluginAsync
    void ReportState() override;
private:
    RAAT__SourceSelectionState GetStateLocked() const;
private:
    Bwh iSystemName;
    IRaatSourceObserver& iObserver;
    IRaatOutputControl& iOutputControl;
    EStateSource iStateSource;
    EStateStandby iStateStandby;
    TUint iSourceIndexRaat;
    mutable Mutex iLock;

    RAAT__SourceSelectionStateListeners iListeners;
    RaatSourceSelectionPluginExt iPluginExt;
    Net::CpDeviceDv* iCpDevice;
    Net::CpProxyAvOpenhomeOrgProduct4* iProxyProduct;
};

}
}

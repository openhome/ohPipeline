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

class IMediaPlayer;

class RaatSourceSelection : public RaatPluginAsync
{
public:
    RaatSourceSelection(IMediaPlayer& aMediaPlayer, const Brx& aSystemName, IRaatSourceObserver& aObserver, ISourceRaatStandbyControl& aSourceStandbyControl);
    ~RaatSourceSelection();
public:
    RAAT__SourceSelectionPlugin* Plugin();
    void AddStateListener(RAAT__SourceSelectionStateCallback aCb, void *aCbUserdata);
    void RemoveStateListener(RAAT__SourceSelectionStateCallback aCb, void *aCbUserdata);
    void GetState(RAAT__SourceSelectionState *aState);
    void ActivateRaatSource();
    void SetStandby();
private:
    void Initialise();
    void StandbyChanged();
    void SourceIndexChanged();
    RAAT__SourceSelectionState State() const;
    void TryReportStateChange();
private: // from RaatPluginAsync
    void ReportState() override;
private:
    RaatSourceSelectionPluginExt iPluginExt;
    RAAT__SourceSelectionStateListeners iListeners;
    Bwh iSystemName;
    IRaatSourceObserver& iObserver;
    ISourceRaatStandbyControl& iSourceStandbyControl;
    Net::CpDeviceDv* iCpDevice;
    Net::CpProxyAvOpenhomeOrgProduct4* iProxyProduct;
    IThreadPoolHandle* iRaatCallback;
    TUint iSourceIndexRaat;
    TUint iSourceIndexCurrent;
    TBool iStandby;
    TBool iStarted;
};

}
}

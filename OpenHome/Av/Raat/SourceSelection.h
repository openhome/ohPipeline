#pragma once

#include <OpenHome/Types.h>

#include <rc_status.h>
#include <raat_plugin_source_selection.h>

namespace OpenHome {
    class Brx;
    class IThreadPoolHandle;
    namespace Configuration {
        class ConfigNum;
    }
    namespace Net {
        class CpDeviceDv;
        class CpProxyAvOpenhomeOrgProduct3;
    }
namespace Av {

class RaatSourceSelection;

typedef struct {
    RAAT__SourceSelectionPlugin iPlugin; // must be first member
    RaatSourceSelection* iSelf;
} RaatSourceSelectionPluginExt;

class IMediaPlayer;

class RaatSourceSelection
{
public:
    RaatSourceSelection(IMediaPlayer& aMediaPlayer, const Brx& aSystemName);
    ~RaatSourceSelection(); 
    RAAT__SourceSelectionPlugin* Plugin();
    void AddStateListener(RAAT__SourceSelectionStateCallback aCb, void *aCbUserdata);
    void RemoveStateListener(RAAT__SourceSelectionStateCallback aCb, void *aCbUserdata);
    void GetState(RAAT__SourceSelectionState *aState);
    void ActivateRaatSource();
    void SetStandby();
private:
    void StandbyChanged();
    void SourceIndexChanged();
    RAAT__SourceSelectionState State() const;
    void ReportStateChange();
private:
    RaatSourceSelectionPluginExt iPluginExt;
    RAAT__SourceSelectionStateListeners iListeners;
    Net::CpDeviceDv* iCpDevice;
    Net::CpProxyAvOpenhomeOrgProduct3* iProxyProduct;
    TUint iSourceIndexRaat;
    TUint iSourceIndexCurrent;
    TBool iStandby;
};

}
}

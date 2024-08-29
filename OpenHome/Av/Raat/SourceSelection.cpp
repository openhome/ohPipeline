#include <OpenHome/Av/Raat/SourceSelection.h>
#include <OpenHome/Av/Raat/Plugin.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Functor.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Net/Core/CpDeviceDv.h>
#include <Generated/CpAvOpenhomeOrgProduct4.h>

#include <rc_status.h>
#include <raat_plugin_source_selection.h>
    

static inline OpenHome::Av::RaatSourceSelection* SourceSelection(void *self)
{
    auto ext = reinterpret_cast<OpenHome::Av::RaatSourceSelectionPluginExt*>(self);
    return ext->iSelf;
}

extern "C" {

RC__Status RaatSourceSelectionGetInfo(void * /*self*/, json_t **out_info)
{
    *out_info = nullptr;
    return RC__STATUS_SUCCESS;
}

RC__Status RaatSourceSelectionAddStateListener(void *self, RAAT__SourceSelectionStateCallback cb, void *cb_userdata)
{
    return SourceSelection(self)->AddStateListener(cb, cb_userdata);
}

RC__Status RaatSourceSelectionRemoveStateListener(void *self, RAAT__SourceSelectionStateCallback cb, void *cb_userdata)
{
    return SourceSelection(self)->RemoveStateListener(cb, cb_userdata);
}

RC__Status RaatSourceSelectionGetState(void *self, RAAT__SourceSelectionState *out_state)
{
    SourceSelection(self)->GetState(out_state);
    return RC__STATUS_SUCCESS;
}

void RaatSourceSelectionRequestSource(void *self, RAAT__SourceSelectionRequestSourceCallback cb_result, void *cb_userdata)
{
    SourceSelection(self)->SetSource();
    cb_result(cb_userdata, RC__STATUS_SUCCESS, nullptr);
}

void RaatSourceSelectionRequestStandby(void *self, RAAT__SourceSelectionRequestStandbyCallback cb_result, void *cb_userdata)
{
    SourceSelection(self)->SetStandby();
    cb_result(cb_userdata, RC__STATUS_SUCCESS, nullptr);
}

}

using namespace OpenHome;
using namespace OpenHome::Av;

// RaatSourceSelection

RaatSourceSelection::RaatSourceSelection(
    IMediaPlayer& aMediaPlayer,
    const Brx& aSystemName,
    IRaatSourceObserver& aObserver,
    IRaatOutputControl& aOutputControl)

    : RaatPluginAsync(aMediaPlayer.ThreadPool())
    , iSystemName(aSystemName)
    , iObserver(aObserver)
    , iOutputControl(aOutputControl)
    , iStateSource(EStateSource::eUndefined)
    , iStateStandby(EStateStandby::eUndefined)
    , iSourceIndexRaat(0)
    , iLock("RASS")
{
    auto ret = RAAT__source_selection_state_listeners_init(&iListeners, RC__allocator_malloc());
    ASSERT(ret == RC__STATUS_SUCCESS);

    (void)memset(&iPluginExt, 0, sizeof iPluginExt);
    iPluginExt.iPlugin.get_info = RaatSourceSelectionGetInfo;
    iPluginExt.iPlugin.add_state_listener = RaatSourceSelectionAddStateListener;
    iPluginExt.iPlugin.remove_state_listener = RaatSourceSelectionRemoveStateListener;
    iPluginExt.iPlugin.get_state = RaatSourceSelectionGetState;
    iPluginExt.iPlugin.request_source = RaatSourceSelectionRequestSource;
    iPluginExt.iPlugin.request_standby = RaatSourceSelectionRequestStandby;
    iPluginExt.iSelf = this;

    iCpDevice = Net::CpDeviceDv::New(aMediaPlayer.CpStack(), aMediaPlayer.Device());
    iProxyProduct = new Net::CpProxyAvOpenhomeOrgProduct4(*iCpDevice);
}

RaatSourceSelection::~RaatSourceSelection()
{
    RAAT__source_selection_state_listeners_destroy(&iListeners);
    iProxyProduct->Unsubscribe();
    delete iProxyProduct;
    iCpDevice->RemoveRef();
}

void RaatSourceSelection::Start()
{
    // Get RAAT source index
    TUint count;
    iProxyProduct->SyncSourceCount(count);
    for (TUint index = count-1; ; index--) {
        Brh systemName;
        Brh type;
        Brh name;
        TBool visible;
        iProxyProduct->SyncSource(index, systemName, type, name, visible);
        if (systemName == iSystemName) {
            iSourceIndexRaat = index;
            break;
        }
        ASSERT(index != 0); // no RAAT source registered
    }

    // Register callbacks
    Functor cb = MakeFunctor(*this, &RaatSourceSelection::StandbyChanged);
    iProxyProduct->SetPropertyStandbyChanged(cb);
    cb = MakeFunctor(*this, &RaatSourceSelection::SourceIndexChanged);
    iProxyProduct->SetPropertySourceIndexChanged(cb);
    iProxyProduct->Subscribe();

    // Start plugin
    RaatPluginAsync::Start();
}

RAAT__SourceSelectionPlugin* RaatSourceSelection::Plugin()
{
    return (RAAT__SourceSelectionPlugin*)&iPluginExt;
}

RC__Status RaatSourceSelection::AddStateListener(RAAT__SourceSelectionStateCallback aCb, void *aCbUserdata)
{
    return RAAT__source_selection_state_listeners_add(&iListeners, aCb, aCbUserdata);
}

RC__Status RaatSourceSelection::RemoveStateListener(RAAT__SourceSelectionStateCallback aCb, void *aCbUserdata)
{
    return RAAT__source_selection_state_listeners_remove(&iListeners, aCb, aCbUserdata);
}

void RaatSourceSelection::GetState(RAAT__SourceSelectionState *aState)
{
    AutoMutex _(iLock);
    *aState = GetStateLocked();
}

void RaatSourceSelection::SetSource()
{
    {
        // Some trickery here to prevent reporting our state until we've both come
        // out of standby and selected RAAT as the current source
        AutoMutex _(iLock);
        if (iStateStandby == EStateStandby::eEnabled) {
            iStateStandby = EStateStandby::eUndefined;
        }
        if (iStateSource == EStateSource::eNotSelected) {
            iStateSource = EStateSource::eUndefined;
        }
    }

    iProxyProduct->SyncSetSourceIndex(iSourceIndexRaat);
}

void RaatSourceSelection::SetStandby()
{
    iProxyProduct->SyncSetStandby(true);
}

void RaatSourceSelection::StandbyChanged()
{
    TBool standbyEnabled = false;
    iProxyProduct->PropertyStandby(standbyEnabled);

    AutoMutex _(iLock);
    iStateStandby = standbyEnabled ? EStateStandby::eEnabled : EStateStandby::eDisabled;

    if (iStateStandby == EStateStandby::eEnabled) {
        iObserver.RaatSourceDeactivated();
    }
    TryReportState();
}

void RaatSourceSelection::SourceIndexChanged()
{
    TUint sourceIndexCurrent = 0;
    iProxyProduct->PropertySourceIndex(sourceIndexCurrent);

    AutoMutex _(iLock);
    iStateSource = (sourceIndexCurrent == iSourceIndexRaat) ? EStateSource::eSelected : EStateSource::eNotSelected;
    if (iStateSource == EStateSource::eSelected) {
        iObserver.RaatSourceActivated();
    }
    if (iStateSource == EStateSource::eNotSelected) {
        iObserver.RaatSourceDeactivated();
        iOutputControl.NotifyDeselected();
    }
    TryReportState();
}

void RaatSourceSelection::ReportState()
{
    AutoMutex _(iLock);

    // Wait for both standby state and source state to be defined before reporting
    if (iStateStandby == EStateStandby::eUndefined || iStateSource == EStateSource::eUndefined) {
        return;
    } 

    auto state = GetStateLocked();
    RAAT__source_selection_state_listeners_invoke(&iListeners, &state);
}

RAAT__SourceSelectionState RaatSourceSelection::GetStateLocked() const
{
    RAAT__SourceSelectionState state;
    state.status = RAAT__SOURCE_SELECTION_STATUS_INDETERMINATE;

    if (iStateStandby == EStateStandby::eEnabled) {
        state.status = RAAT__SOURCE_SELECTION_STATUS_STANDBY;
    }
    else if (iStateSource == EStateSource::eSelected) {
        state.status = RAAT__SOURCE_SELECTION_STATUS_SELECTED;
    }
    else if (iStateSource == EStateSource::eNotSelected) {
        state.status = RAAT__SOURCE_SELECTION_STATUS_DESELECTED;
    }
    return state;
}


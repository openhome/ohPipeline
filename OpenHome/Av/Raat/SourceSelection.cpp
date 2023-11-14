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

extern "C"
RC__Status Raat_SourceSelection_Get_Info(void * /*self*/, json_t **out_info)
{
    *out_info = nullptr;
    return RC__STATUS_SUCCESS;
}

extern "C"
RC__Status Raat_SourceSelection_Add_State_Listener(void *self, RAAT__SourceSelectionStateCallback cb, void *cb_userdata)
{
    SourceSelection(self)->AddStateListener(cb, cb_userdata);
    return RC__STATUS_SUCCESS;
}

extern "C"
RC__Status Raat_SourceSelection_Remove_State_Listener(void *self, RAAT__SourceSelectionStateCallback cb, void *cb_userdata)
{
    SourceSelection(self)->RemoveStateListener(cb, cb_userdata);
    return RC__STATUS_SUCCESS;
}

extern "C"
RC__Status Raat_SourceSelection_Get_State(void *self, RAAT__SourceSelectionState *out_state)
{
    SourceSelection(self)->GetState(out_state);
    return RC__STATUS_SUCCESS;
}

extern "C"
void Raat_SourceSelection_Request_Source(void *self, RAAT__SourceSelectionRequestSourceCallback cb_result, void *cb_userdata)
{
    SourceSelection(self)->ActivateRaatSource();
    cb_result(cb_userdata, RC__STATUS_SUCCESS, nullptr);
}

extern "C"
void Raat_SourceSelection_Request_Standby(void *self, RAAT__SourceSelectionRequestStandbyCallback cb_result, void *cb_userdata)
{
    SourceSelection(self)->SetStandby();
    cb_result(cb_userdata, RC__STATUS_SUCCESS, nullptr);
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
    , iSourceIndexCurrent(0)
    , iStandby(true)
    , iState(EState::eNotSelected)
    , iStarted(false)
    , iActivationPending(false)
    , iLock("RASS")
{
    auto ret = RAAT__source_selection_state_listeners_init(&iListeners, RC__allocator_malloc());
    ASSERT(ret == RC__STATUS_SUCCESS);

    (void)memset(&iPluginExt, 0, sizeof iPluginExt);
    iPluginExt.iPlugin.get_info = Raat_SourceSelection_Get_Info;
    iPluginExt.iPlugin.add_state_listener = Raat_SourceSelection_Add_State_Listener;
    iPluginExt.iPlugin.remove_state_listener = Raat_SourceSelection_Remove_State_Listener;
    iPluginExt.iPlugin.get_state = Raat_SourceSelection_Get_State;
    iPluginExt.iPlugin.request_source = Raat_SourceSelection_Request_Source;
    iPluginExt.iPlugin.request_standby = Raat_SourceSelection_Request_Standby;
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

RAAT__SourceSelectionPlugin* RaatSourceSelection::Plugin()
{
    return (RAAT__SourceSelectionPlugin*)&iPluginExt;
}

void RaatSourceSelection::AddStateListener(RAAT__SourceSelectionStateCallback aCb, void *aCbUserdata)
{
    (void)RAAT__source_selection_state_listeners_add(&iListeners, aCb, aCbUserdata);
}

void RaatSourceSelection::RemoveStateListener(RAAT__SourceSelectionStateCallback aCb, void *aCbUserdata)
{
    (void)RAAT__source_selection_state_listeners_remove(&iListeners, aCb, aCbUserdata);
}

void RaatSourceSelection::GetState(RAAT__SourceSelectionState *aState)
{
    AutoMutex _(iLock);
    *aState = StateLocked();
}

void RaatSourceSelection::ActivateRaatSource()
{
    {
        AutoMutex _(iLock);
        if (iState == EState::eSelected) {
            TryReportState();
            return;
        }
        iActivationPending = true;
    }
    iProxyProduct->SyncSetSourceIndex(iSourceIndexRaat);
}

void RaatSourceSelection::SetStandby()
{
    {
        AutoMutex _(iLock);
        if (iState == EState::eStandby) {
            TryReportState();
            return;
        }
    }
    iProxyProduct->SyncSetStandby(true);
}

void RaatSourceSelection::Initialise()
{
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
    Functor cb = MakeFunctor(*this, &RaatSourceSelection::StandbyChanged);
    iProxyProduct->SetPropertyStandbyChanged(cb);
    cb = MakeFunctor(*this, &RaatSourceSelection::SourceIndexChanged);
    iProxyProduct->SetPropertySourceIndexChanged(cb);
    iProxyProduct->Subscribe();
}

void RaatSourceSelection::StandbyChanged()
{
    iProxyProduct->PropertyStandby(iStandby);

    AutoMutex _(iLock);
    UpdateStateLocked();
    if (iActivationPending && (iState != EState::eSelected)) {
        return;
    }
    iActivationPending = false;
    TryReportState();
}

void RaatSourceSelection::SourceIndexChanged()
{
    iProxyProduct->PropertySourceIndex(iSourceIndexCurrent);

    AutoMutex _(iLock);
    UpdateStateLocked();
    if (iActivationPending && (iState != EState::eSelected)) {
        return;
    }
    iActivationPending = false;
    TryReportState();
}

void RaatSourceSelection::UpdateStateLocked()
{
    if (!iStandby && (iSourceIndexCurrent == iSourceIndexRaat)) {
        iState = EState::eSelected;
    }
    else if (iStandby) {
        iState = EState::eStandby;
    }
    else {
        iState = EState::eNotSelected;
    }
}

RAAT__SourceSelectionState RaatSourceSelection::StateLocked() const
{
    RAAT__SourceSelectionState state;
    state.status = RAAT__SOURCE_SELECTION_STATUS_INDETERMINATE;
    if (iState == EState::eSelected) {
        state.status = RAAT__SOURCE_SELECTION_STATUS_SELECTED;
    }
    else if (iState == EState::eNotSelected) {
        state.status = RAAT__SOURCE_SELECTION_STATUS_DESELECTED;
    }
    else if (iState == EState::eStandby) {
        state.status = RAAT__SOURCE_SELECTION_STATUS_STANDBY;
    }
    return state;
}

void RaatSourceSelection::ReportState()
{
    if (!iStarted) {
        Initialise();
        iStarted = true;
        return;
    }

    AutoMutex _(iLock);
    auto state = StateLocked();
    RAAT__source_selection_state_listeners_invoke(&iListeners, &state);

    if (iState == EState::eSelected) {
        iObserver.RaatSourceActivated();
    }
    else if (iState == EState::eNotSelected) {
        iObserver.RaatSourceDeactivated();
        iOutputControl.NotifyDeselected();
    }
    else if (iState == EState::eStandby) {
        iObserver.RaatSourceDeactivated();
        iOutputControl.NotifyStandby();
    }
}

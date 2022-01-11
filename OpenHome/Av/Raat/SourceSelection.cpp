#include <OpenHome/Av/Raat/SourceSelection.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Av/MediaPlayer.h>
#include <OpenHome/Net/Core/CpDeviceDv.h>
#include <Generated/CpAvOpenHomeOrgProduct3.h>

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

RaatSourceSelection::RaatSourceSelection(IMediaPlayer& aMediaPlayer, const Brx& aSystemName)
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
    iProxyProduct = new Net::CpProxyAvOpenhomeOrgProduct3(*iCpDevice);

    TUint count;
    iProxyProduct->SyncSourceCount(count);
    for (TUint index = count-1; ; index--) {
        Brh systemName;
        Brh type;
        Brh name;
        TBool visible;
        iProxyProduct->SyncSource(index, systemName, type, name, visible);
        if (systemName == aSystemName) {
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

RaatSourceSelection::~RaatSourceSelection()
{
    RAAT__source_selection_state_listeners_destroy(&iListeners);
    iProxyProduct->Unsubscribe();
    delete iProxyProduct;
    iCpDevice->RemoveRef();
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
    *aState = State();
}

void RaatSourceSelection::ActivateRaatSource()
{
    iProxyProduct->SyncSetSourceIndex(iSourceIndexRaat);
}

void RaatSourceSelection::SetStandby()
{
    iProxyProduct->SyncSetStandby(true);
}

void RaatSourceSelection::StandbyChanged()
{
    iProxyProduct->PropertyStandby(iStandby);
    ReportStateChange();
}

void RaatSourceSelection::SourceIndexChanged()
{
    iProxyProduct->PropertySourceIndex(iSourceIndexCurrent);
    ReportStateChange();
}

RAAT__SourceSelectionState RaatSourceSelection::State() const
{
    RAAT__SourceSelectionState state;
    if (iStandby) {
        state.status = RAAT__SOURCE_SELECTION_STATUS_STANDBY;
    }
    else if (iSourceIndexCurrent == iSourceIndexRaat) {
        state.status = RAAT__SOURCE_SELECTION_STATUS_SELECTED;
    }
    else {
        state.status = RAAT__SOURCE_SELECTION_STATUS_DESELECTED;
    }
    return state;
}

void RaatSourceSelection::ReportStateChange()
{
    auto state = State();
    RAAT__source_selection_state_listeners_invoke(&iListeners, &state);
}

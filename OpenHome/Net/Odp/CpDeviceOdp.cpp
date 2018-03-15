#include <OpenHome/Net/Odp/CpDeviceOdp.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Net/Odp/CpiDeviceOdp.h>
#include <OpenHome/Net/Core/FunctorCpDevice.h>
#include <OpenHome/Net/Private/FunctorCpiDevice.h>

using namespace OpenHome;
using namespace OpenHome::Net;


// CpDeviceListOdpAll

CpDeviceListOdpAll::CpDeviceListOdpAll(CpStack& aCpStack, FunctorCpDevice aAdded, FunctorCpDevice aRemoved)
    : CpDeviceList(aAdded, aRemoved)
{
    FunctorCpiDevice added, removed;
    GetAddedFunctor(added);
    GetRemovedFunctor(removed);
    iList = new CpiDeviceListOdpAll(aCpStack, added, removed);
    iList->Start();
}
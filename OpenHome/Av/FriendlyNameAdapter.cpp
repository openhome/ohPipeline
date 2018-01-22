#include <OpenHome/Av/FriendlyNameAdapter.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Av/Product.h>
#include <OpenHome/Net/Core/DvDevice.h>

using namespace OpenHome;
using namespace OpenHome::Av;


FriendlyNameAttributeUpdater::FriendlyNameAttributeUpdater(IFriendlyNameObservable& aFriendlyNameObservable,
                                                           IThreadPool& aThreadPool,
                                                           Net::DvDevice& aDvDevice)
    : iFriendlyNameObservable(aFriendlyNameObservable)
    , iDvDevice(aDvDevice)
    , iLock("DNCL")
{
    iThreadPoolHandle = aThreadPool.CreateHandle(MakeFunctor(*this, &FriendlyNameAttributeUpdater::Run),
        "UpnpNameChanger", ThreadPoolPriority::Medium);
    iId = iFriendlyNameObservable.RegisterFriendlyNameObserver(
        MakeFunctorGeneric<const Brx&>(*this, &FriendlyNameAttributeUpdater::Observer));
}

FriendlyNameAttributeUpdater::~FriendlyNameAttributeUpdater()
{
    iFriendlyNameObservable.DeregisterFriendlyNameObserver(iId);
    iThreadPoolHandle->Destroy();
}

void FriendlyNameAttributeUpdater::Observer(const Brx& aNewFriendlyName)
{
    AutoMutex a(iLock);
    if (iFullName!=aNewFriendlyName)
    {
        iFullName.Replace(aNewFriendlyName);
        (void)iThreadPoolHandle->TrySchedule();
    }

}

void FriendlyNameAttributeUpdater::Run()
{
    Bws<kMaxNameBytes+1> fullName; // +1 for nul terminator added by PtrZ()
    {
        AutoMutex a(iLock);
        fullName.Replace(iFullName);
    }
    iDvDevice.SetAttribute("Upnp.FriendlyName", fullName.PtrZ());
}

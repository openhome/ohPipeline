#include <OpenHome/Private/SuiteUnitTest.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Av/Product.h>
#include <OpenHome/Av/FriendlyNameAdapter.h>
#include <OpenHome/Net/Private/DviStack.h>


namespace OpenHome {
namespace Av {
namespace Test {

class MockProductNameObservable : public IProductNameObservable
{
public:
    MockProductNameObservable(const Brx& aDefaultRoom, const Brx& aDefaultProduct);
    void SetRoomName(const Brx& aRoom);
    void SetProductName(const Brx& aProduct);
public: // from IProductNameObservable
    void AddNameObserver(IProductNameObserver& aObserver) override;
private:
    std::vector<std::reference_wrapper<IProductNameObserver>> iObservers;
    Bwh iRoom;
    Bwh iProduct;
    Mutex iLock;
};

class MockFriendlyNameObserver
{
public:
    MockFriendlyNameObserver();
    const Brx& FriendlyName() const;
    void FriendlyNameChanged(const Brx& aFriendlyName);
    void WaitForCallback();
private:
    Bws<IFriendlyNameObservable::kMaxFriendlyNameBytes> iFriendlyName;
    Semaphore iSem;
};

class DeviceBasic
{
public:
    static const Brn kDeviceNameDefault;
public:
    DeviceBasic(Net::DvStack& aDvStack);
    ~DeviceBasic();
    Net::DvDevice& Device();
private:
    Bwh iName;
    Net::DvDeviceStandard* iDevice;
};

class SuiteFriendlyNameManager : public OpenHome::TestFramework::SuiteUnitTest, private INonCopyable
{
public:
    SuiteFriendlyNameManager(Net::CpStack& aCpStack, Net::DvStack& aDvStack);
private: // from SuiteUnitTest
    void Setup();
    void TearDown();
private:
    void TestRegisterDeregister();
    void TestUpdate();  // Deregister one observer for second update.
    void TestDvUpdate();
    TBool WaitForNameChange(Net::DvDevice& aDevice, const Brx& aNewName);

private:
    Net::DvStack& iDvStack;
    FriendlyNameManager* iFriendlyNameManager;
    MockProductNameObservable* iObservable;
    ThreadPool iThreadPool;
};

} // namespace Test
} // namespace Av
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Net;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Av;
using namespace OpenHome::Av::Test;


// MockProductNameObservable

MockProductNameObservable::MockProductNameObservable(const Brx& aDefaultRoom, const Brx& aDefaultProduct)
    : iRoom(Product::kMaxRoomBytes)
    , iProduct(Product::kMaxNameBytes)
    , iLock("MPNO")
{
    iRoom.Replace(aDefaultRoom);
    iProduct.Replace(aDefaultProduct);
}

void MockProductNameObservable::SetRoomName(const Brx& aRoom)
{
    AutoMutex amx(iLock);
    iRoom.Replace(aRoom);
    for (auto& o : iObservers) {
        o.get().RoomChanged(aRoom);
    }
}

void MockProductNameObservable::SetProductName(const Brx& aProduct)
{
    AutoMutex amx(iLock);
    iProduct.Replace(aProduct);
    for (auto& o : iObservers) {
        o.get().NameChanged(aProduct);
    }
}

void MockProductNameObservable::AddNameObserver(IProductNameObserver& aObserver)
{
    AutoMutex amx(iLock);
    iObservers.push_back(aObserver);
    // Initial callback (mimicking Product::AddNameObserver()).
    aObserver.RoomChanged(iRoom);
    aObserver.NameChanged(iProduct);
}


// MockFriendlyNameObserver

MockFriendlyNameObserver::MockFriendlyNameObserver()
    : iSem("MFNO", 0)
{
}

const Brx& MockFriendlyNameObserver::FriendlyName() const
{
    return iFriendlyName;
}

void MockFriendlyNameObserver::FriendlyNameChanged(const Brx& aFriendlyName)
{
    iFriendlyName.Replace(aFriendlyName);
    iSem.Signal();
}

void MockFriendlyNameObserver::WaitForCallback()
{
    iSem.Wait();
}


// DeviceBasic

const Brn DeviceBasic::kDeviceNameDefault("device");

DeviceBasic::DeviceBasic(DvStack& aDvStack)
    : iName(kDeviceNameDefault)
{
    TestFramework::RandomiseUdn(aDvStack.Env(), iName);
    iDevice = new DvDeviceStandard(aDvStack, iName);
    iDevice->SetAttribute("Upnp.Domain", "openhome.org");
    iDevice->SetAttribute("Upnp.Type", "Test");
    iDevice->SetAttribute("Upnp.Version", "1");
    iDevice->SetAttribute("Upnp.FriendlyName", "ohNetTestDevice");
    iDevice->SetAttribute("Upnp.Manufacturer", "None");
    iDevice->SetAttribute("Upnp.ModelName", "ohNet test device");
    iDevice->SetEnabled();
}

DeviceBasic::~DeviceBasic()
{
    delete iDevice;
}

DvDevice& DeviceBasic::Device()
{
    return *iDevice;
}


// SuiteFriendlyNameManager

SuiteFriendlyNameManager::SuiteFriendlyNameManager(CpStack& /* aCpStack */, DvStack& aDvStack)
    : SuiteUnitTest("SuiteFriendlyNameManager")
    , iDvStack(aDvStack)
    , iThreadPool(1, 1, 1)
{
    AddTest(MakeFunctor(*this, &SuiteFriendlyNameManager::TestRegisterDeregister), "TestRegisterDeregister");
    AddTest(MakeFunctor(*this, &SuiteFriendlyNameManager::TestUpdate), "TestUpdate");
    AddTest(MakeFunctor(*this, &SuiteFriendlyNameManager::TestDvUpdate), "TestDvUpdate");
}

void SuiteFriendlyNameManager::Setup()
{
    iObservable = new MockProductNameObservable(Brn("Room"), Brn("Product"));
    iFriendlyNameManager = new FriendlyNameManager(*iObservable, iThreadPool);
}

void SuiteFriendlyNameManager::TearDown()
{
    delete iFriendlyNameManager;
    delete iObservable;
}

void SuiteFriendlyNameManager::TestRegisterDeregister()
{
    const Brn kFriendlyName("Room:Product");
    IFriendlyNameObservable& observable = *iFriendlyNameManager;

    MockFriendlyNameObserver observer1;
    MockFriendlyNameObserver observer2;

    const TUint id1 = observable.RegisterFriendlyNameObserver(MakeFunctorGeneric<const Brx&>(observer1, &MockFriendlyNameObserver::FriendlyNameChanged));
    observer1.WaitForCallback();    // Synchronous callback, but need to consume sem signal.
    TEST(observer1.FriendlyName() == kFriendlyName);

    const TUint id2 = observable.RegisterFriendlyNameObserver(MakeFunctorGeneric<const Brx&>(observer2, &MockFriendlyNameObserver::FriendlyNameChanged));
    observer2.WaitForCallback();    // Synchronous callback, but need to consume sem signal.
    TEST(observer2.FriendlyName() == kFriendlyName);

    observable.DeregisterFriendlyNameObserver(id2);
    observable.DeregisterFriendlyNameObserver(id1);
}

void SuiteFriendlyNameManager::TestUpdate()
{
    IFriendlyNameObservable& observable = *iFriendlyNameManager;

    MockFriendlyNameObserver observer1;
    MockFriendlyNameObserver observer2;

    const TUint id1 = observable.RegisterFriendlyNameObserver(MakeFunctorGeneric<const Brx&>(observer1, &MockFriendlyNameObserver::FriendlyNameChanged));
    observer1.WaitForCallback();    // Synchronous callback, but need to consume sem signal.
    const TUint id2 = observable.RegisterFriendlyNameObserver(MakeFunctorGeneric<const Brx&>(observer2, &MockFriendlyNameObserver::FriendlyNameChanged));
    observer2.WaitForCallback();    // Synchronous callback, but need to consume sem signal.

    iObservable->SetRoomName(Brn("NewRoom"));

    observer1.WaitForCallback();
    TEST(observer1.FriendlyName() == Brn("NewRoom:Product"));
    observer2.WaitForCallback();
    TEST(observer2.FriendlyName() == Brn("NewRoom:Product"));

    // Now, deregister the first observer, then issue an update.
    observable.DeregisterFriendlyNameObserver(id1);
    iObservable->SetProductName(Brn("NewProduct"));
    observer2.WaitForCallback();
    TEST(observer1.FriendlyName() == Brn("NewRoom:Product"));       // Observer 1 shouldn't be updated.
    TEST(observer2.FriendlyName() == Brn("NewRoom:NewProduct"));    // Observer 2 should be updated.

    observable.DeregisterFriendlyNameObserver(id2);

    iObservable->SetRoomName(Brn("RoomName2"));
    // Observers shouldn't have been updated.
    TEST(observer1.FriendlyName() == Brn("NewRoom:Product"));
    TEST(observer2.FriendlyName() == Brn("NewRoom:NewProduct"));
}

void SuiteFriendlyNameManager::TestDvUpdate()
{
    DeviceBasic* deviceBasic1 = new DeviceBasic(iDvStack);
    DvDevice& dvDevice1 = deviceBasic1->Device();

    // construct updaters for different device types
    auto updater1 = new FriendlyNameAttributeUpdater(*iFriendlyNameManager, iThreadPool, dvDevice1);

    // check initial updates
    TEST(WaitForNameChange(dvDevice1, Brn("Room:Product")) == true);

    iObservable->SetRoomName(Brn("NewRoom"));

    // check updates after room name modified
    TEST(WaitForNameChange(dvDevice1, Brn("NewRoom:Product")) == true);

    delete updater1;
    delete deviceBasic1;
}

TBool SuiteFriendlyNameManager::WaitForNameChange(DvDevice& aDevice, const Brx& aNewName)
{
    static const TUint kMaxRetries = 50;
    TUint retries;
    const TChar* updatedName;

    for (retries = kMaxRetries; retries > 0; retries--) {
        aDevice.GetAttribute("Upnp.FriendlyName", &updatedName);
        if (Brn(updatedName) == aNewName) {
            return true;
        }
        Thread::Sleep(20);              // wait for name to update
    }
    return false;
}



void TestFriendlyNameManager(CpStack& aCpStack, DvStack& aDvStack)
{
    Runner runner("FriendlyNameManager tests\n");
    runner.Add(new SuiteFriendlyNameManager(aCpStack, aDvStack));
    runner.Run();
}

#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/SuiteUnitTest.h>
#include <OpenHome/Av/Pins.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Configuration/Tests/ConfigRamStore.h>
#include <OpenHome/Json.h>

#include <limits.h>
#include <vector>


using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::TestFramework;

namespace OpenHome {
namespace Av {

class PinTestUtils
{
public:
    static const TUint kId;
    static const Brn kMode;
    static const Brn kType;
    static const Brn kUri;
    static const Brn kTitle;
    static const Brn kDescription;
    static const Brn kArtworkUri;
    static const TBool kShuffle;

    static const TUint kId2;
    static const Brn kMode2;
    static const Brn kType2;
    static const Brn kUri2;
    static const Brn kTitle2;
    static const Brn kDescription2;
    static const Brn kArtworkUri2;
    static const TBool kShuffle2;
public:
    static void Init(Pin& aPin);
    static void Init2(Pin& aPin);
};

class SuitePin : public SuiteUnitTest, private IPinIdProvider
{
public:
    SuitePin();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private: // from IPinIdProvider
    TUint NextId() override;
private:
    void TestUpdate();
    void TestUpdateReportsChange();
    void TestUpdateNoChange();
    void TestSaveLoad();
    void TestCopy();
    void TestClearReportsChange();
    void TestClearNoChange();
    void TestClearSetsEmptyId();
private:
    TUint iNextId;
};

class SuitePinSet : public SuiteUnitTest, private IPinIdProvider
{
public:
    SuitePinSet();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private: // from IPinIdProvider
    TUint NextId() override;
private:
    void TestLoadFromCtor();
    void TestLoadDeferred();
    void TestSaveLoad();
    void TestSet();
    void TestClear();
    void TestPinFromIndex();
    void TestSwap();
    void TestContains();
    void TestIdArray();
private:
    Configuration::ConfigRamStore* iStore;
    TUint iLastId;
};

class DummyPinInvoker : public IPinInvoker
{
public:
    DummyPinInvoker(const TChar* aMode);
    TUint InvocationCount() const;
public: // from IPinInvoker
    const TChar* Mode() const override;
private: // from IPinInvoker
    void Invoke(const IPin& aPin) override;
private:
    const TChar* iMode;
    TUint iInvocationCount;
};

class SuitePinsManager : public SuiteUnitTest
                       , private IPinsAccount
                       , private IPinsObserver
{
public:
    SuitePinsManager();
public: // from SuiteUnitTest
    void Setup() override;
    void TearDown() override;
private: // from IPinsAccount
    void Set(TUint aIndex, const Brx& aMode, const Brx& aType,
             const Brx& aUri, const Brx& aTitle, const Brx& aDescription,
             const Brx& aArtworkUri, TBool aShuffle) override;
    void Swap(TUint aId1, TUint aId2) override;
    void SetObserver(IPinsAccountObserver& aObserver) override;
private: // from IPinsObserver
    void NotifyDevicePinsMax(TUint aMax) override;
    void NotifyAccountPinsMax(TUint aMax) override;
    void NotifyModeAdded(const Brx& aMode) override;
    void NotifyUpdatesDevice(const std::vector<TUint>& aIdArray) override;
    void NotifyUpdatesAccount(const std::vector<TUint>& aIdArray) override;
private:
    inline IPinsManager* Manager();
    inline IPinsInvocable* Invocable();
    void SetPin(TUint aIndex);
private:
    void TestAccountObserverSet();
    void TestObserverDeviceMaxReported();
    void TestObserverAccountMaxReported();
    void TestObserverInitialIds();
    void TestObserverModes();
    void TestSetDevicePinObserverNotified();
    void TestSetDevicePin();
    void TestSetDevicePinInvalidIndex();
    void TestClearDevicePin();
    void TestClearDevicePinObserverNotified();
    void TestClearDevicePinInvalidId();
    void TestSwapDevicePins();
    void TestSwapDevicePinsObserverNotified();
    void TestSwapDevicePinsInvalidId();
    void TestNotifyAccountPin();
    void TestNotifyAccountPinObserverNotified();
    void TestSetAccountPin();
    void TestClearAccountPin();
    void TestSwapAccountPins();
    void TestSwapDeviceAccountPins();
    void TestWriteJson();
    void TestInvokeDevicePinId();
    void TestInvokeAccountPinId();
    void TestInvokePinInvalidId();
    void TestInvokeDevicePinIndex();
    void TestInvokeAccountPinIndex();
    void TestInvokePinInvalidIndex();
private:
    Configuration::ConfigRamStore* iStore;
    PinsManager* iPinsManager;
    TUint iAccountSetIndex;
    Bws<IPin::kMaxModeBytes> iAccountSetMode;
    Bws<IPin::kMaxTypeBytes> iAccountSetType;
    Bws<IPin::kMaxUriBytes> iAccountSetUri;
    Bws<IPin::kMaxTitleBytes> iAccountSetTitle;
    Bws<IPin::kMaxDescBytes> iAccountSetDescription;
    Bws<IPin::kMaxUriBytes> iAccountSetArtworkUri;
    TBool iAccountSetShuffle;
    TUint iAccountSwapId1;
    TUint iAccountSwapId2;
    IPinsAccountObserver* iAccountObserver;
    TUint iDevicePinsMax;
    TUint iAccountPinsMax;
    std::vector<Brn> iModes;
    std::vector<TUint> iIdArrayDevice;
    std::vector<TUint> iIdArrayAccount;
private:
    static const TUint kMaxDevicePins;
    static const TUint kMaxAccountPins;
    static const Brn kMode;
    static const Brn kType;
    static const Brn kUri;
    static const Brn kTitle;
    static const Brn kDescription;
    static const Brn kArtworkUri;
    static const TBool kShuffle;
};

} // namespace Av
} // namespace OpenHome


// PinTestUtils

const TUint PinTestUtils::kId = 1;
const Brn PinTestUtils::kMode("mode");
const Brn PinTestUtils::kType("type");
const Brn PinTestUtils::kUri("scheme://host");
const Brn PinTestUtils::kTitle("title");
const Brn PinTestUtils::kDescription("longer description");
const Brn PinTestUtils::kArtworkUri("scheme://host/path");
const TBool PinTestUtils::kShuffle = true;

const TUint PinTestUtils::kId2 = 2;
const Brn PinTestUtils::kMode2("MODE");
const Brn PinTestUtils::kType2("TYPE");
const Brn PinTestUtils::kUri2("schm://host2");
const Brn PinTestUtils::kTitle2("TITLE");
const Brn PinTestUtils::kDescription2("longer description 2");
const Brn PinTestUtils::kArtworkUri2("schm://host/path/2");
const TBool PinTestUtils::kShuffle2 = false;

void PinTestUtils::Init(Pin& aPin)
{
    (void)aPin.TryUpdate(kMode, kType, kUri, kTitle, kDescription, kArtworkUri, kShuffle);
}

void PinTestUtils::Init2(Pin& aPin)
{
    (void)aPin.TryUpdate(kMode2, kType2, kUri2, kTitle2, kDescription2, kArtworkUri2, kShuffle2);
}


// SuitePin

SuitePin::SuitePin()
    : SuiteUnitTest("Pin")
{
    AddTest(MakeFunctor(*this, &SuitePin::TestUpdate), "TestUpdate");
    AddTest(MakeFunctor(*this, &SuitePin::TestUpdateReportsChange), "TestUpdateReportsChange");
    AddTest(MakeFunctor(*this, &SuitePin::TestUpdateNoChange), "TestUpdateNoChange");
    AddTest(MakeFunctor(*this, &SuitePin::TestSaveLoad), "TestSaveLoad");
    AddTest(MakeFunctor(*this, &SuitePin::TestCopy), "TestCopy");
    AddTest(MakeFunctor(*this, &SuitePin::TestClearReportsChange), "TestClearReportsChange");
    AddTest(MakeFunctor(*this, &SuitePin::TestClearNoChange), "TestClearNoChange");
    AddTest(MakeFunctor(*this, &SuitePin::TestClearSetsEmptyId), "TestClearSetsEmptyId");
}

void SuitePin::Setup()
{
    iNextId = kIdEmpty + 1;
}

void SuitePin::TearDown()
{
}

TUint SuitePin::NextId()
{
    return iNextId++;
}

void SuitePin::TestUpdate()
{
    Pin pin(*this);
    auto id = pin.Id();
    TEST(id == kIdEmpty);
    TEST(pin.Mode() == Brx::Empty());
    TEST(pin.Type() == Brx::Empty());
    TEST(pin.Uri() == Brx::Empty());
    TEST(pin.Title() == Brx::Empty());
    TEST(pin.Description() == Brx::Empty());
    TEST(pin.ArtworkUri() == Brx::Empty());
    TEST(!pin.Shuffle());

    PinTestUtils::Init(pin);
    TEST(pin.Id() == id + 1);
    TEST(pin.Mode() == PinTestUtils::kMode);
    TEST(pin.Type() == PinTestUtils::kType);
    TEST(pin.Uri() == PinTestUtils::kUri);
    TEST(pin.Title() == PinTestUtils::kTitle);
    TEST(pin.Description() == PinTestUtils::kDescription);
    TEST(pin.ArtworkUri() == PinTestUtils::kArtworkUri);
    TEST(pin.Shuffle() == PinTestUtils::kShuffle);
}

void SuitePin::TestUpdateReportsChange()
{
    Pin pin(*this);
    const TUint id = pin.Id();
    TEST(pin.TryUpdate(PinTestUtils::kMode, PinTestUtils::kType, PinTestUtils::kUri,
                       PinTestUtils::kTitle, PinTestUtils::kDescription,
                       PinTestUtils::kArtworkUri, PinTestUtils::kShuffle));
    TEST(pin.Id() > id);
}

void SuitePin::TestUpdateNoChange()
{
    Pin pin(*this);
    PinTestUtils::Init(pin);
    const TUint id = pin.Id();
    TEST(!pin.TryUpdate(PinTestUtils::kMode, PinTestUtils::kType, PinTestUtils::kUri,
                        PinTestUtils::kTitle, PinTestUtils::kDescription,
                        PinTestUtils::kArtworkUri, PinTestUtils::kShuffle));
    TEST(pin.Id() == id);
}

void SuitePin::TestSaveLoad()
{
    Pin pin(*this);
    PinTestUtils::Init(pin);
    TUint id = pin.Id();
    TEST(id != kIdEmpty);
    WriterBwh writer(64);
    pin.Externalise(writer);
    TEST(pin.Id() == id);

    (void)pin.TryUpdate(Brx::Empty(), Brx::Empty(), Brx::Empty(), Brx::Empty(), Brx::Empty(), Brx::Empty(), false);

    pin.Internalise(writer.Buffer());
    TEST(pin.Mode() == PinTestUtils::kMode);
    TEST(pin.Type() == PinTestUtils::kType);
    TEST(pin.Uri() == PinTestUtils::kUri);
    TEST(pin.Title() == PinTestUtils::kTitle);
    TEST(pin.Description() == PinTestUtils::kDescription);
    TEST(pin.ArtworkUri() == PinTestUtils::kArtworkUri);
    TEST(pin.Shuffle() == PinTestUtils::kShuffle);
}

void SuitePin::TestCopy()
{
    Pin pin(*this);
    Pin pin2(*this);
    PinTestUtils::Init(pin);
    PinTestUtils::Init2(pin2);
    const TUint id2 = pin2.Id();

    pin.Copy(pin2);
    TEST(pin.Id() == id2);
    TEST(pin.Mode() == PinTestUtils::kMode2);
    TEST(pin.Type() == PinTestUtils::kType2);
    TEST(pin.Uri() == PinTestUtils::kUri2);
    TEST(pin.Title() == PinTestUtils::kTitle2);
    TEST(pin.Description() == PinTestUtils::kDescription2);
    TEST(pin.ArtworkUri() == PinTestUtils::kArtworkUri2);
    TEST(pin.Shuffle() == PinTestUtils::kShuffle2);

    TEST(pin2.Id() == id2);
    TEST(pin2.Mode() == PinTestUtils::kMode2);
    TEST(pin2.Type() == PinTestUtils::kType2);
    TEST(pin2.Uri() == PinTestUtils::kUri2);
    TEST(pin2.Title() == PinTestUtils::kTitle2);
    TEST(pin2.Description() == PinTestUtils::kDescription2);
    TEST(pin2.ArtworkUri() == PinTestUtils::kArtworkUri2);
    TEST(pin2.Shuffle() == PinTestUtils::kShuffle2);
}

void SuitePin::TestClearReportsChange()
{
    Pin pin(*this);
    PinTestUtils::Init(pin);
    TEST(pin.Clear());
}

void SuitePin::TestClearNoChange()
{
    Pin pin(*this);
    TEST(!pin.Clear());
    PinTestUtils::Init(pin);
    TEST(pin.Clear());
    TEST(!pin.Clear());
}

void SuitePin::TestClearSetsEmptyId()
{
    Pin pin(*this);
    PinTestUtils::Init(pin);
    TEST(pin.Id() != kIdEmpty);
    TEST(pin.Clear());
    TEST(pin.Id() == kIdEmpty);
}


// SuitePinSet

SuitePinSet::SuitePinSet()
    : SuiteUnitTest("PinSet")
{
    AddTest(MakeFunctor(*this, &SuitePinSet::TestLoadFromCtor), "TestLoadFromCtor");
    AddTest(MakeFunctor(*this, &SuitePinSet::TestLoadDeferred), "TestLoadDeferred");
    AddTest(MakeFunctor(*this, &SuitePinSet::TestSaveLoad), "TestSaveLoad");
    AddTest(MakeFunctor(*this, &SuitePinSet::TestSet), "TestSet");
    AddTest(MakeFunctor(*this, &SuitePinSet::TestClear), "TestClear");
    AddTest(MakeFunctor(*this, &SuitePinSet::TestPinFromIndex), "TestPinFromIndex");
    AddTest(MakeFunctor(*this, &SuitePinSet::TestSwap), "TestSwap");
    AddTest(MakeFunctor(*this, &SuitePinSet::TestContains), "TestContains");
    AddTest(MakeFunctor(*this, &SuitePinSet::TestIdArray), "TestIdArray");
}

void SuitePinSet::Setup()
{
    iStore = new Configuration::ConfigRamStore();
    iLastId = kIdEmpty;
}

void SuitePinSet::TearDown()
{
    delete iStore;
}

TUint SuitePinSet::NextId()
{
    return ++iLastId;
}

void SuitePinSet::TestLoadFromCtor()
{
    static const TUint kPinCount = 5;
    PinSet pinSet(kPinCount, *this, *iStore, "pt");
    TEST(pinSet.Count() == kPinCount);
    for (TUint i = 0; i < kPinCount; i++) {
        const auto pin = pinSet.iPins[i];
        TEST(pin->Id() == kIdEmpty);
        TEST(pin->Mode() == Brx::Empty());
        TEST(pin->Type() == Brx::Empty());
        TEST(pin->Uri() == Brx::Empty());
        TEST(pin->Title() == Brx::Empty());
        TEST(pin->Description() == Brx::Empty());
        TEST(pin->ArtworkUri() == Brx::Empty());
        TEST(!pin->Shuffle());
    }
}

void SuitePinSet::TestLoadDeferred()
{
    static const TUint kPinCount = 5;
    PinSet pinSet(0, *this, *iStore, "pt");
    TEST(pinSet.Count() == 0);
    TEST(iLastId == kIdEmpty); // another way of checking we haven't initialised any pins yet
    pinSet.SetCount(kPinCount);
    TEST(pinSet.Count() == kPinCount);
    for (TUint i = 0; i < kPinCount; i++) {
        const auto pin = pinSet.iPins[i];
        TEST(pin->Id() == kIdEmpty);
        TEST(pin->Mode() == Brx::Empty());
        TEST(pin->Type() == Brx::Empty());
        TEST(pin->Uri() == Brx::Empty());
        TEST(pin->Title() == Brx::Empty());
        TEST(pin->Description() == Brx::Empty());
        TEST(pin->ArtworkUri() == Brx::Empty());
        TEST(!pin->Shuffle());
    }
}

void SuitePinSet::TestSaveLoad()
{
    static const TUint kPinCount = 5;
    PinSet pinSet(kPinCount, *this, *iStore, "pt");
    auto pin = pinSet.iPins[1];
    PinTestUtils::Init(*pin);
    TEST(pin->Mode() == PinTestUtils::kMode);
    pinSet.WriteToStore(*pin);

    PinSet pinSet2(kPinCount, *this, *iStore, "pt");
    const auto pin2 = pinSet2.iPins[1];
    TEST(pin2->Mode() == PinTestUtils::kMode);
    TEST(pin2->Mode() == pin->Mode());
}

void SuitePinSet::TestSet()
{
    static const TUint kPinCount = 5;
    PinSet pinSet(kPinCount, *this, *iStore, "pt");
    TEST(pinSet.Set(3, Brn("mode"), Brn("type"), Brn("uri"), Brn("title"),
                    Brn("desc"), Brn("artworkUri"), true));
    const auto& pin = pinSet.PinFromId(iLastId);
    TEST(pin.Mode() == Brn("mode"));
    TEST(pin.Type() == Brn("type"));
    TEST(pin.Uri() == Brn("uri"));
    TEST(pin.Title() == Brn("title"));
    TEST(pin.Description() == Brn("desc"));
    TEST(pin.ArtworkUri() == Brn("artworkUri"));
    TEST(pin.Shuffle());
    TEST(!pinSet.Set(3, Brn("mode"), Brn("type"), Brn("uri"), Brn("title"),
                     Brn("desc"), Brn("artworkUri"), true));

}

void SuitePinSet::TestClear()
{
    static const TUint kPinCount = 5;
    PinSet pinSet(kPinCount, *this, *iStore, "pt");
    PinTestUtils::Init(*pinSet.iPins[1]);
    const auto& pin = pinSet.PinFromId(iLastId);
    TEST(pin.Mode() != Brx::Empty());
    TEST(pinSet.Clear(iLastId));
    TEST(pin.Mode() == Brx::Empty());
    TEST_THROWS(pinSet.Clear(iLastId), PinIdNotFound);
    TEST(!pinSet.Clear(kIdEmpty));
}

void SuitePinSet::TestPinFromIndex()
{
    static const TUint kPinCount = 5;
    PinSet pinSet(kPinCount, *this, *iStore, "pt");
    PinTestUtils::Init(*pinSet.iPins[1]);
    const auto& pin = pinSet.PinFromIndex(1);
    TEST(pin.Id() == iLastId);
    TEST(pin.Mode() == PinTestUtils::kMode);
    TEST(pin.Type() == PinTestUtils::kType);
    TEST(pin.Uri() == PinTestUtils::kUri);
    TEST(pin.Title() == PinTestUtils::kTitle);
    TEST(pin.Description() == PinTestUtils::kDescription);
    TEST(pin.ArtworkUri() == PinTestUtils::kArtworkUri);
    TEST(pin.Shuffle() == PinTestUtils::kShuffle);
}

void SuitePinSet::TestSwap()
{
    static const TUint kPinCount = 5;
    PinSet pinSet(kPinCount, *this, *iStore, "pt");
    PinTestUtils::Init(*pinSet.iPins[1]);
    Pin* pin1Before = new Pin(*this);
    pin1Before->Copy(*pinSet.iPins[1]);
    PinTestUtils::Init2(*pinSet.iPins[2]);
    Pin* pin2Before = new Pin(*this);
    pin2Before->Copy(*pinSet.iPins[2]);
    pinSet.Swap(1, 2);

    const auto pin1After = pinSet.iPins[1];
    const auto pin2After = pinSet.iPins[2];
    TEST(pin1Before->Id() == pin2After->Id());
    TEST(pin1Before->Mode() == pin2After->Mode());
    TEST(pin1Before->Type() == pin2After->Type());
    TEST(pin1Before->Uri() == pin2After->Uri());
    TEST(pin1Before->Title() == pin2After->Title());
    TEST(pin1Before->Description() == pin2After->Description());
    TEST(pin1Before->ArtworkUri() == pin2After->ArtworkUri());
    TEST(pin1Before->Shuffle() == pin2After->Shuffle());

    TEST(pin2Before->Id() == pin1After->Id());
    TEST(pin2Before->Mode() == pin1After->Mode());
    TEST(pin2Before->Type() == pin1After->Type());
    TEST(pin2Before->Uri() == pin1After->Uri());
    TEST(pin2Before->Title() == pin1After->Title());
    TEST(pin2Before->Description() == pin1After->Description());
    TEST(pin2Before->ArtworkUri() == pin1After->ArtworkUri());
    TEST(pin2Before->Shuffle() == pin1After->Shuffle());

    delete pin1Before;
    delete pin2Before;
}

void SuitePinSet::TestContains()
{
    static const TUint kPinCount = 5;
    PinSet pinSet(kPinCount, *this, *iStore, "pt");
    TEST(pinSet.Contains(kIdEmpty));
    PinTestUtils::Init(*pinSet.iPins[1]);
    TEST(pinSet.Contains(iLastId));
    TEST(pinSet.Contains(kIdEmpty));
}

void SuitePinSet::TestIdArray()
{
    static const TUint kPinCount = 3;
    PinSet pinSet(kPinCount, *this, *iStore, "pt");
    {
        const auto& idArray = pinSet.IdArray();
        for (auto id : idArray) {
            TEST(id == kIdEmpty);
        }
    }

    {
        (void)pinSet.Set(1, Brn("mode"), Brn("type"), Brn("uri"), Brn("title"),
                         Brn("desc"), Brn("artworkUri"), true);
        const auto& idArray = pinSet.IdArray();
        TEST(idArray[0] == kIdEmpty);
        TEST(idArray[1] == iLastId);
        TEST(idArray[2] == kIdEmpty);
    }

    {
        (void)pinSet.Clear(iLastId);
        const auto& idArray = pinSet.IdArray();
        TEST(idArray[0] == kIdEmpty);
        TEST(idArray[1] == kIdEmpty);
        TEST(idArray[2] == kIdEmpty);
    }

    {
        (void)pinSet.Set(1, Brn("mode"), Brn("type"), Brn("uri"), Brn("title"),
                         Brn("desc"), Brn("artworkUri"), true);
        TEST(pinSet.IdArray()[1] == iLastId);
        (void)pinSet.Swap(1, 2);
        const auto& idArray = pinSet.IdArray();
        TEST(idArray[0] == kIdEmpty);
        TEST(idArray[1] == kIdEmpty);
        TEST(idArray[2] == iLastId);
    }
}


// DummyPinInvoker

DummyPinInvoker::DummyPinInvoker(const TChar* aMode)
    : iMode(aMode)
    , iInvocationCount(0)
{
}

const TChar* DummyPinInvoker::Mode() const
{
    return iMode;
}

TUint DummyPinInvoker::InvocationCount() const
{
    return iInvocationCount;
}

void DummyPinInvoker::Invoke(const IPin& aPin)
{
    TEST(aPin.Mode() == Brn(iMode));
    iInvocationCount++;
}


// SuitePinsManager

const TUint SuitePinsManager::kMaxDevicePins = 6;
const TUint SuitePinsManager::kMaxAccountPins = 10;
const Brn SuitePinsManager::kMode("mode");
const Brn SuitePinsManager::kType("type");
const Brn SuitePinsManager::kUri("scheme://host");
const Brn SuitePinsManager::kTitle("title");
const Brn SuitePinsManager::kDescription("longer description");
const Brn SuitePinsManager::kArtworkUri("scheme://host/path");
const TBool SuitePinsManager::kShuffle = true;


SuitePinsManager::SuitePinsManager()
    : SuiteUnitTest("PinsManager")
{
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestAccountObserverSet), "TestAccountObserverSet");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestObserverDeviceMaxReported), "TestObserverDeviceMaxReported");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestObserverAccountMaxReported), "TestObserverAccountMaxReported");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestObserverInitialIds), "TestObserverInitialIds");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestObserverModes), "TestObserverModes");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestSetDevicePinObserverNotified), "TestSetDevicePinObserverNotified");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestSetDevicePin), "TestSetDevicePin");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestSetDevicePinInvalidIndex), "TestSetDevicePinInvalidIndex");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestClearDevicePin), "TestClearDevicePin");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestClearDevicePinObserverNotified), "TestClearDevicePinObserverNotified");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestClearDevicePinInvalidId), "TestClearDevicePinInvalidId");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestSwapDevicePins), "TestSwapDevicePins");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestSwapDevicePinsObserverNotified), "TestSwapDevicePinsObserverNotified");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestSwapDevicePinsInvalidId), "TestSwapDevicePinsInvalidId");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestNotifyAccountPin), "TestNotifyAccountPin");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestNotifyAccountPinObserverNotified), "TestNotifyAccountPinObserverNotified");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestSetAccountPin), "TestSetAccountPin");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestClearAccountPin), "TestClearAccountPin");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestSwapAccountPins), "TestSwapAccountPins");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestSwapDeviceAccountPins), "TestSwapDeviceAccountPins");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestWriteJson), "TestWriteJson");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestInvokeDevicePinId), "TestInvokeDevicePinId");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestInvokeAccountPinId), "TestInvokeAccountPinId");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestInvokePinInvalidId), "TestInvokePinInvalidId");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestInvokeDevicePinIndex), "TestInvokeDevicePinIndex");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestInvokeAccountPinIndex), "TestInvokeAccountPinIndex");
    AddTest(MakeFunctor(*this, &SuitePinsManager::TestInvokePinInvalidIndex), "TestInvokePinInvalidIndex");
}

void SuitePinsManager::Setup()
{
    iStore = new Configuration::ConfigRamStore();
    iPinsManager = new PinsManager(*iStore, kMaxDevicePins);

    iAccountSetIndex = UINT_MAX;
    iAccountSetMode.Replace(Brx::Empty());
    iAccountSetType.Replace(Brx::Empty());
    iAccountSetUri.Replace(Brx::Empty());
    iAccountSetTitle.Replace(Brx::Empty());
    iAccountSetDescription.Replace(Brx::Empty());
    iAccountSetArtworkUri.Replace(Brx::Empty());
    iAccountSetShuffle = false;
    iAccountSwapId1 = iAccountSwapId2 = UINT_MAX;
    iAccountObserver = nullptr;
    iDevicePinsMax = iAccountPinsMax = 0;
    iModes.clear();
    iIdArrayDevice.clear();
    iIdArrayAccount.clear();
}

void SuitePinsManager::TearDown()
{
    delete iPinsManager;
    delete iStore;
}

void SuitePinsManager::Set(TUint aIndex, const Brx& aMode, const Brx& aType, const Brx& aUri,
                           const Brx& aTitle, const Brx& aDescription, const Brx& aArtworkUri,
                           TBool aShuffle)
{
    iAccountSetIndex = aIndex;
    iAccountSetMode.Replace(aMode);
    iAccountSetType.Replace(aType);
    iAccountSetUri.Replace(aUri);
    iAccountSetTitle.Replace(aTitle);
    iAccountSetDescription.Replace(aDescription);
    iAccountSetArtworkUri.Replace(aArtworkUri);
    iAccountSetShuffle = aShuffle;
}

void SuitePinsManager::Swap(TUint aId1, TUint aId2)
{
    iAccountSwapId1 = aId1;
    iAccountSwapId2 = aId2;
}

void SuitePinsManager::SetObserver(IPinsAccountObserver& aObserver)
{
    ASSERT(iAccountObserver == nullptr);
    iAccountObserver = &aObserver;
}

void SuitePinsManager::NotifyDevicePinsMax(TUint aMax)
{
    iDevicePinsMax = aMax;
}

void SuitePinsManager::NotifyAccountPinsMax(TUint aMax)
{
    iAccountPinsMax = aMax;
}

void SuitePinsManager::NotifyModeAdded(const Brx& aMode)
{
    Brn mode(aMode);
    iModes.push_back(mode);
}

void SuitePinsManager::NotifyUpdatesDevice(const std::vector<TUint>& aIdArray)
{
    iIdArrayDevice = aIdArray;
}

void SuitePinsManager::NotifyUpdatesAccount(const std::vector<TUint>& aIdArray)
{
    iIdArrayAccount = aIdArray;
}

inline IPinsManager* SuitePinsManager::Manager()
{
    return static_cast<IPinsManager*>(iPinsManager);
}

inline IPinsInvocable* SuitePinsManager::Invocable()
{
    return static_cast<IPinsInvocable*>(iPinsManager);
}

void SuitePinsManager::SetPin(TUint aIndex)
{
    Manager()->Set(aIndex, kMode, kType, kUri, kTitle, kDescription, kArtworkUri, kShuffle);
}

void SuitePinsManager::TestAccountObserverSet()
{
    TEST(iAccountObserver == nullptr);
    iPinsManager->SetAccount(*this, kMaxAccountPins);
    TEST(iAccountObserver != nullptr);
}

void SuitePinsManager::TestObserverDeviceMaxReported()
{
    TEST(iDevicePinsMax == 0);
    Manager()->SetObserver(*this);
    TEST(iDevicePinsMax == kMaxDevicePins);
}

void SuitePinsManager::TestObserverAccountMaxReported()
{
    TEST(iAccountPinsMax == 0);
    Manager()->SetObserver(*this);
    iPinsManager->SetAccount(*this, 3);
    TEST(iAccountPinsMax == 3);
}

void SuitePinsManager::TestObserverInitialIds()
{
    Manager()->SetObserver(*this);
    TEST(iIdArrayDevice.size() == kMaxDevicePins);
    for (auto id : iIdArrayDevice) {
        TEST(id == IPinIdProvider::kIdEmpty);
    }

    iPinsManager->SetAccount(*this, kMaxAccountPins);
    TEST(iIdArrayAccount.size() == kMaxAccountPins);
    for (auto id : iIdArrayAccount) {
        TEST(id == IPinIdProvider::kIdEmpty);
    }
}

void SuitePinsManager::TestObserverModes()
{
    TEST(iModes.size() == 0);
    Manager()->SetObserver(*this);
    auto invoker = new DummyPinInvoker("dummy");
    Invocable()->Add(invoker);
    TEST(iModes.size() == 1);
    TEST(iModes[0] == Brn(invoker->Mode()));
}

void SuitePinsManager::TestSetDevicePinObserverNotified()
{
    Manager()->SetObserver(*this);
    TEST(iIdArrayDevice[1] == IPinIdProvider::kIdEmpty);
    SetPin(1);
    TEST(iIdArrayDevice[1] != IPinIdProvider::kIdEmpty);
}

void SuitePinsManager::TestSetDevicePin()
{
    Manager()->SetObserver(*this);
    SetPin(1);
    const auto id = iIdArrayDevice[1];
    TEST(id != IPinIdProvider::kIdEmpty);
    const auto& pin = iPinsManager->iPinsDevice.PinFromId(id);
    TEST(pin.Mode() == kMode);
    TEST(pin.Type() == kType);
    TEST(pin.Uri() == kUri);
    TEST(pin.Title() == kTitle);
    TEST(pin.Description() == kDescription);
    TEST(pin.ArtworkUri() == kArtworkUri);
    TEST(pin.Shuffle() == kShuffle);
}

void SuitePinsManager::TestSetDevicePinInvalidIndex()
{
    Manager()->SetObserver(*this);
    const auto index = iIdArrayDevice.size();
    TEST_THROWS(Manager()->Set(index, Brx::Empty(), Brx::Empty(), Brx::Empty(),
                               Brx::Empty(), Brx::Empty(), Brx::Empty(), false),
                PinIndexOutOfRange);
}

void SuitePinsManager::TestClearDevicePin()
{
    SetPin(1);
    TEST(iPinsManager->iPinsDevice.iPins[1]->Mode() == kMode);
    Manager()->Clear(iPinsManager->iPinsDevice.iIds[1]);
    const auto pin = iPinsManager->iPinsDevice.iPins[1];
    TEST(pin->Mode() == Brx::Empty());
    TEST(pin->Type() == Brx::Empty());
    TEST(pin->Uri() == Brx::Empty());
    TEST(pin->Title() == Brx::Empty());
    TEST(pin->Description() == Brx::Empty());
    TEST(pin->ArtworkUri() == Brx::Empty());
    TEST(!pin->Shuffle());
}

void SuitePinsManager::TestClearDevicePinObserverNotified()
{
    Manager()->SetObserver(*this);
    SetPin(1);
    TEST(iIdArrayDevice[1] != IPinIdProvider::kIdEmpty);
    Manager()->Clear(iIdArrayDevice[1]);
    TEST(iIdArrayDevice[1] == IPinIdProvider::kIdEmpty);
}

void SuitePinsManager::TestClearDevicePinInvalidId()
{
    Manager()->SetObserver(*this);
    TEST_THROWS(Manager()->Clear(123456), PinIdNotFound);
}

void SuitePinsManager::TestSwapDevicePins()
{
    Manager()->SetObserver(*this);
    SetPin(1);
    Manager()->Swap(1, 2);
    {
        const auto pin = iPinsManager->iPinsDevice.iPins[1];
        TEST(pin->Mode() == Brx::Empty());
        TEST(pin->Type() == Brx::Empty());
        TEST(pin->Uri() == Brx::Empty());
        TEST(pin->Title() == Brx::Empty());
        TEST(pin->Description() == Brx::Empty());
        TEST(pin->ArtworkUri() == Brx::Empty());
        TEST(!pin->Shuffle());
    }
    {
        const auto pin = iPinsManager->iPinsDevice.iPins[2];
        TEST(pin->Mode() == kMode);
        TEST(pin->Type() == kType);
        TEST(pin->Uri() == kUri);
        TEST(pin->Title() == kTitle);
        TEST(pin->Description() == kDescription);
        TEST(pin->ArtworkUri() == kArtworkUri);
        TEST(pin->Shuffle() == kShuffle);
    }
}

void SuitePinsManager::TestSwapDevicePinsObserverNotified()
{
    Manager()->SetObserver(*this);
    SetPin(1);
    const auto id = iIdArrayDevice[1];
    TEST(iIdArrayDevice[1] != IPinIdProvider::kIdEmpty);
    TEST(iIdArrayDevice[2] == IPinIdProvider::kIdEmpty);
    Manager()->Swap(1, 2);
    TEST(iIdArrayDevice[1] == IPinIdProvider::kIdEmpty);
    TEST(iIdArrayDevice[2] != IPinIdProvider::kIdEmpty);
    TEST(iIdArrayDevice[2] == id);
}

void SuitePinsManager::TestSwapDevicePinsInvalidId()
{
    TEST_THROWS(Manager()->Swap(1, kMaxDevicePins), PinIndexOutOfRange);
    TEST_THROWS(Manager()->Swap(kMaxDevicePins, 1), PinIndexOutOfRange);
}

void SuitePinsManager::TestNotifyAccountPin()
{
    iPinsManager->SetAccount(*this, kMaxAccountPins);
    auto pin = iPinsManager->iPinsAccount.iPins[0];
    TEST(pin->Mode() == Brx::Empty());
    iAccountObserver->NotifyAccountPin(0, kMode, kType, kUri, kTitle, kDescription, kArtworkUri, kShuffle);
    pin = iPinsManager->iPinsAccount.iPins[0];
    TEST(pin->Mode() == kMode);
    TEST(pin->Type() == kType);
    TEST(pin->Uri() == kUri);
    TEST(pin->Title() == kTitle);
    TEST(pin->Description() == kDescription);
    TEST(pin->ArtworkUri() == kArtworkUri);
    TEST(pin->Shuffle() == kShuffle);
}

void SuitePinsManager::TestNotifyAccountPinObserverNotified()
{
    Manager()->SetObserver(*this);
    iPinsManager->SetAccount(*this, kMaxAccountPins);
    TEST(iIdArrayAccount[1] == IPinIdProvider::kIdEmpty);
    iAccountObserver->NotifyAccountPin(1, kMode, kType, kUri, kTitle, kDescription, kArtworkUri, kShuffle);
    TEST(iIdArrayAccount[1] != IPinIdProvider::kIdEmpty);
}

void SuitePinsManager::TestSetAccountPin()
{
    iPinsManager->SetAccount(*this, kMaxAccountPins);
    TEST(iAccountSetMode != kMode);
    SetPin(kMaxDevicePins + 1);

    TEST(iAccountSetIndex == 1);
    TEST(iAccountSetMode == kMode);
    TEST(iAccountSetType == kType);
    TEST(iAccountSetUri == kUri);
    TEST(iAccountSetTitle == kTitle);
    TEST(iAccountSetDescription == kDescription);
    TEST(iAccountSetArtworkUri == kArtworkUri);
    TEST(iAccountSetShuffle == kShuffle);
}

void SuitePinsManager::TestClearAccountPin()
{
    Manager()->SetObserver(*this);
    iPinsManager->SetAccount(*this, kMaxAccountPins);
    TEST(iAccountSetMode != kMode);
    SetPin(kMaxDevicePins + 1);
    TEST(iAccountSetIndex == 1);
    TEST(iAccountSetMode == kMode);
    iAccountObserver->NotifyAccountPin(1, kMode, kType, kUri, kTitle, kDescription, kArtworkUri, kShuffle);
    TEST(iIdArrayAccount[1] != IPinIdProvider::kIdEmpty);
    Manager()->Clear(iIdArrayAccount[1]);
    TEST(iAccountSetIndex == 1);
    TEST(iAccountSetMode == Brx::Empty());
    TEST(iAccountSetType == Brx::Empty());
    TEST(iAccountSetUri == Brx::Empty());
    TEST(iAccountSetTitle == Brx::Empty());
    TEST(iAccountSetDescription == Brx::Empty());
    TEST(iAccountSetArtworkUri == Brx::Empty());
    TEST(!iAccountSetShuffle);
}

void SuitePinsManager::TestSwapAccountPins()
{
    iPinsManager->SetAccount(*this, kMaxAccountPins);
    Manager()->Swap(kMaxDevicePins, kMaxDevicePins + 1);
    TEST(iAccountSwapId1 == 0);
    TEST(iAccountSwapId2 == 1);
}

void SuitePinsManager::TestSwapDeviceAccountPins()
{
    iPinsManager->SetAccount(*this, kMaxAccountPins);
    const TUint indexDv = 1;
    const TUint indexAc = kMaxDevicePins + 1;
    TEST_THROWS(Manager()->Swap(indexDv, indexAc), PinError);
    TEST_THROWS(Manager()->Swap(indexAc, indexDv), PinError);
}

void SuitePinsManager::TestWriteJson()
{
    Manager()->SetObserver(*this);
    SetPin(1);
    const auto id = iIdArrayDevice[1];
    TEST(id != IPinIdProvider::kIdEmpty);
    WriterBwh writer(1024);
    std::vector<TUint> ids;
    ids.push_back(id);
    Manager()->WriteJson(writer, ids);

    auto parserArray = JsonParserArray::Create(writer.Buffer());
    TEST(parserArray.Type() == JsonParserArray::ValType::Object);

    Brn obj = parserArray.NextObject();
    Bwn objW(obj.Ptr(), obj.Bytes(), obj.Bytes());
    JsonParser parser;
    parser.ParseAndUnescape(objW);
    TEST(parser.Num("id") == (TInt)id);
    TEST(parser.String("mode") == kMode);
    TEST(parser.String("type") == kType);
    TEST(parser.String("uri") == kUri);
    TEST(parser.String("title") == kTitle);
    TEST(parser.String("description") == kDescription);
    TEST(parser.String("artworkUri") == kArtworkUri);
    TEST(parser.Bool("shuffle") == kShuffle);

    TEST_THROWS(parserArray.NextObject(), JsonArrayEnumerationComplete);
}

void SuitePinsManager::TestInvokeDevicePinId()
{
    Manager()->SetObserver(*this);
    auto invoker = new DummyPinInvoker("dummy");
    Invocable()->Add(invoker);
    TEST(invoker->InvocationCount() == 0);
    Manager()->Set(0, Brn("dummy"), Brx::Empty(), Brx::Empty(), Brx::Empty(), Brx::Empty(), Brx::Empty(), false);
    Manager()->InvokeId(iIdArrayDevice[0]);
    TEST(invoker->InvocationCount() == 1);
}

void SuitePinsManager::TestInvokeAccountPinId()
{
    Manager()->SetObserver(*this);
    iPinsManager->SetAccount(*this, kMaxAccountPins);
    auto invoker = new DummyPinInvoker("dummy");
    Invocable()->Add(invoker);
    TEST(invoker->InvocationCount() == 0);
    iAccountObserver->NotifyAccountPin(2, Brn("dummy"), Brx::Empty(), Brx::Empty(), Brx::Empty(), Brx::Empty(), Brx::Empty(), false);
    Manager()->InvokeId(iIdArrayAccount[2]);
    TEST(invoker->InvocationCount() == 1);
}

void SuitePinsManager::TestInvokePinInvalidId()
{
    Manager()->SetObserver(*this);
    auto invoker = new DummyPinInvoker("dummy");
    Invocable()->Add(invoker);
    TEST(invoker->InvocationCount() == 0);
    TEST_THROWS(Manager()->InvokeId(123456), PinIdNotFound);
}

void SuitePinsManager::TestInvokeDevicePinIndex()
{
    auto invoker = new DummyPinInvoker("dummy");
    Invocable()->Add(invoker);
    TEST(invoker->InvocationCount() == 0);
    Manager()->Set(0, Brn("dummy"), Brx::Empty(), Brx::Empty(), Brx::Empty(), Brx::Empty(), Brx::Empty(), false);
    Manager()->InvokeIndex(0);
    TEST(invoker->InvocationCount() == 1);
}

void SuitePinsManager::TestInvokeAccountPinIndex()
{
    iPinsManager->SetAccount(*this, kMaxAccountPins);
    auto invoker = new DummyPinInvoker("dummy");
    Invocable()->Add(invoker);
    TEST(invoker->InvocationCount() == 0);
    iAccountObserver->NotifyAccountPin(2, Brn("dummy"), Brx::Empty(), Brx::Empty(), Brx::Empty(), Brx::Empty(), Brx::Empty(), false);
    Manager()->InvokeIndex(kMaxDevicePins + 2);
    TEST(invoker->InvocationCount() == 1);
}

void SuitePinsManager::TestInvokePinInvalidIndex()
{
    auto invoker = new DummyPinInvoker("dummy");
    Invocable()->Add(invoker);
    TEST(invoker->InvocationCount() == 0);
    TEST_THROWS(Manager()->InvokeIndex(0), PinModeNotSupported);
    TEST_THROWS(Manager()->InvokeIndex(kMaxDevicePins), PinIndexOutOfRange);
    iPinsManager->SetAccount(*this, kMaxAccountPins);
    TEST_THROWS(Manager()->InvokeIndex(kMaxDevicePins + kMaxAccountPins), PinIndexOutOfRange);
}



void TestPins()
{
    Runner runner("Pins tests\n");
    runner.Add(new SuitePin());
    runner.Add(new SuitePinSet());
    runner.Add(new SuitePinsManager());
    runner.Run();
}

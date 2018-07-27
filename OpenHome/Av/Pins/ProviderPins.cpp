#include <OpenHome/Av/Pins/ProviderPins.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Thread.h>
#include <Generated/DvAvOpenhomeOrgPins1.h>
#include <OpenHome/Av/Pins/Pins.h>
#include <OpenHome/Json.h>
#include <OpenHome/Private/Debug.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Timer.h>

#include <vector>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Net;

static const TUint kCodeIndexOutOfRange = 801;
static const Brn kMsgIndexOutOfRange("Pin index out of range");
static const TUint kCodeIdNotFound = 802;
static const Brn kMsgIdNotFound("Pin id not found");
static const TUint kCodeModeNotSupported = 803;
static const Brn kMsgModeNotSupported("Pin mode not supported");

const TUint ProviderPins::kModerationMs = 50;

ProviderPins::ProviderPins(DvDevice& aDevice, Environment& aEnv, IPinsManager& aManager)
    : DvProviderAvOpenhomeOrgPins1(aDevice)
    , iLock("PPin")
    , iManager(aManager)
    , iDeviceMax(0)
    , iAccountMax(0)
    , iWriterIdArray(1024)
    , iStarted(false)
{
    iIdArrayModerator = new Timer(aEnv, MakeFunctor(*this, &ProviderPins::IdArrayModerationCallback), "ProviderPins");

    EnablePropertyDeviceMax();
    EnablePropertyAccountMax();
    EnablePropertyModes();
    EnablePropertyIdArray();
    EnablePropertyCloudConnected();

    EnableActionGetDeviceMax();
    EnableActionGetAccountMax();
    EnableActionGetModes();
    EnableActionGetIdArray();
    EnableActionGetCloudConnected();
    EnableActionReadList();
    EnableActionInvokeId();
    EnableActionInvokeIndex();
    EnableActionInvokeUri();
    EnableActionSetDevice();
    EnableActionSetAccount();
    EnableActionClear();
    EnableActionSwap();

    (void)SetPropertyDeviceMax(0);
    (void)SetPropertyAccountMax(0);
    (void)SetPropertyModes(Brx::Empty());
    (void)SetPropertyIdArray(Brx::Empty());
    (void)SetPropertyCloudConnected(false);
    iManager.SetObserver(*this);
}

ProviderPins::~ProviderPins()
{
    delete iIdArrayModerator;
}

void ProviderPins::Start()
{
    AutoMutex _(iLock);
    iStarted = true;

    WriterBwh modes(1024);
    WriterJsonArray writer(modes, WriterJsonArray::WriteOnEmpty::eEmptyArray);
    for (auto mode : iModes) {
        writer.WriteString(mode);
    }
    writer.WriteEnd();
    (void)SetPropertyModes(modes.Buffer());

    UpdateIdArrayLocked();
}

void ProviderPins::NotifyDevicePinsMax(TUint aMax)
{
    iDeviceMax = aMax;
    (void)SetPropertyDeviceMax(aMax);
}

void ProviderPins::NotifyAccountPinsMax(TUint aMax)
{
    iAccountMax = aMax;
    (void)SetPropertyAccountMax(aMax);
}

void ProviderPins::NotifyModeAdded(const Brx& aMode)
{
    ASSERT(!iStarted);
    Brn mode(aMode);
    iModes.push_back(mode);
}

void ProviderPins::NotifyCloudConnected(TBool aConnected)
{
    (void)SetPropertyCloudConnected(aConnected);
}

void ProviderPins::NotifyUpdatesDevice(const std::vector<TUint>& aIdArray)
{
    AutoMutex _(iLock);
    iIdArrayDevice = aIdArray;
    if (iStarted) {
        iIdArrayModerator->FireIn(kModerationMs);
    }
}

void ProviderPins::NotifyUpdatesAccount(const std::vector<TUint>& aIdArray)
{
    AutoMutex _(iLock);
    iIdArrayAccount = aIdArray;
    if (iStarted) {
        iIdArrayModerator->FireIn(kModerationMs);
    }
}

void ProviderPins::IdArrayModerationCallback()
{
    AutoMutex _(iLock);
    UpdateIdArrayLocked();
}

void ProviderPins::UpdateIdArrayLocked()
{
    iWriterIdArray.Reset();
    WriterJsonArray writer(iWriterIdArray, WriterJsonArray::WriteOnEmpty::eEmptyArray);
    for (auto id : iIdArrayDevice) {
        writer.WriteInt(id);
    }
    for (auto id : iIdArrayAccount) {
        writer.WriteInt(id);
    }
    writer.WriteEnd();
    (void)SetPropertyIdArray(iWriterIdArray.Buffer());
}

void ProviderPins::GetDeviceMax(IDvInvocation& aInvocation, IDvInvocationResponseUint& aDeviceMax)
{
    aInvocation.StartResponse();
    aDeviceMax.Write(iDeviceMax);
    aInvocation.EndResponse();
}

void ProviderPins::GetAccountMax(IDvInvocation& aInvocation, IDvInvocationResponseUint& aAccountMax)
{
    aInvocation.StartResponse();
    aAccountMax.Write(iAccountMax);
    aInvocation.EndResponse();
}

void ProviderPins::GetModes(IDvInvocation& aInvocation, IDvInvocationResponseString& aModes)
{
    aInvocation.StartResponse();
    WritePropertyModes(aModes);
    aModes.WriteFlush();
    aInvocation.EndResponse();
}

void ProviderPins::GetIdArray(IDvInvocation& aInvocation, IDvInvocationResponseString& aIdArray)
{
    aInvocation.StartResponse();
    WritePropertyIdArray(aIdArray);
    aIdArray.WriteFlush();
    aInvocation.EndResponse();
}

void ProviderPins::GetCloudConnected(IDvInvocation& aInvocation, IDvInvocationResponseBool& aCloudConnected)
{
    TBool cloudConnected;
    GetPropertyCloudConnected(cloudConnected);
    aInvocation.StartResponse();
    aCloudConnected.Write(cloudConnected);
    aInvocation.EndResponse();
}

void ProviderPins::ReadList(IDvInvocation& aInvocation, const Brx& aIds, IDvInvocationResponseString& aList)
{
    std::vector<TUint> ids;
    auto parser = JsonParserArray::Create(aIds);
    try {
        for (;;) {
            ids.push_back((TUint)parser.NextInt());
        }
    }
    catch (JsonArrayEnumerationComplete&) {}

    aInvocation.StartResponse();
    iManager.WriteJson(aList, ids);
    aList.WriteFlush();
    aInvocation.EndResponse();
}

void ProviderPins::InvokeId(IDvInvocation& aInvocation, TUint aId)
{
    try {
        iManager.InvokeId(aId);
    }
    catch (PinIdNotFound&) {
        aInvocation.Error(kCodeIdNotFound, kMsgIdNotFound);
    }
    catch (PinModeNotSupported&) {
        aInvocation.Error(kCodeModeNotSupported, kMsgModeNotSupported);
    }

    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

void ProviderPins::InvokeIndex(Net::IDvInvocation& aInvocation, TUint aIndex)
{
    try {
        iManager.InvokeIndex(aIndex);
    }
    catch (PinIndexOutOfRange&) {
        aInvocation.Error(kCodeIndexOutOfRange, kMsgIndexOutOfRange);
    }
    catch (PinModeNotSupported&) {
        aInvocation.Error(kCodeModeNotSupported, kMsgModeNotSupported);
    }

    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

void ProviderPins::InvokeUri(IDvInvocation& aInvocation, const Brx& aMode, const Brx& aType, const Brx& aUri, TBool aShuffle)
{
    try {
        iManager.InvokeUri(aMode, aType, aUri, aShuffle);
    }
    catch (PinModeNotSupported&) {
        aInvocation.Error(kCodeModeNotSupported, kMsgModeNotSupported);
    }

    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

void ProviderPins::SetDevice(IDvInvocation& aInvocation, TUint aIndex, const Brx& aMode, const Brx& aType,
                             const Brx& aUri, const Brx& aTitle, const Brx& aDescription, const Brx& aArtworkUri,
                             TBool aShuffle)
{
    try {
        iManager.Set(aIndex, aMode, aType, aUri, aTitle, aDescription, aArtworkUri, aShuffle);
    }
    catch (PinIndexOutOfRange&) {
        aInvocation.Error(kCodeIndexOutOfRange, kMsgIndexOutOfRange);
    }

    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

void ProviderPins::SetAccount(IDvInvocation& aInvocation, TUint aIndex, const Brx& aMode, const Brx& aType,
                             const Brx& aUri, const Brx& aTitle, const Brx& aDescription, const Brx& aArtworkUri,
                             TBool aShuffle)
{
    try {
        iManager.Set(iDeviceMax + aIndex, aMode, aType, aUri, aTitle, aDescription, aArtworkUri, aShuffle);
    }
    catch (PinIndexOutOfRange&) {
        aInvocation.Error(kCodeIndexOutOfRange, kMsgIndexOutOfRange);
    }

    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

void ProviderPins::Clear(IDvInvocation& aInvocation, TUint aId)
{
    try {
        iManager.Clear(aId);
    }
    catch (PinIdNotFound&) {
        aInvocation.Error(kCodeIdNotFound, kMsgIdNotFound);
    }

    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

void ProviderPins::Swap(IDvInvocation& aInvocation, TUint aIndex1, TUint aIndex2)
{
    try {
        iManager.Swap(aIndex1, aIndex2);
    }
    catch (PinIndexOutOfRange&) {
        aInvocation.Error(kCodeIndexOutOfRange, kMsgIndexOutOfRange);
    }

    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

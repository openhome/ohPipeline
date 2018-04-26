#include <OpenHome/Av/Pins.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Json.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Configuration/IStore.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Private/Uri.h>

#include <algorithm>
#include <iterator>
#include <map>
#include <vector>

using namespace OpenHome;
using namespace OpenHome::Av;


// Pin

Pin::Pin(IPinIdProvider& aIdProvider)
    : iIdProvider(aIdProvider)
    , iId(IPinIdProvider::kIdEmpty)
    , iShuffle(false)
{
}

TBool Pin::TryUpdate(const Brx& aMode, const Brx& aType, const Brx& aUri,
                     const Brx& aTitle, const Brx& aDescription, const Brx& aArtworkUri,
                     TBool aShuffle)
{
    const TBool changed = Set(aMode, aType, aUri, aTitle, aDescription, aArtworkUri, aShuffle);
    if (changed) {
        if (iMode.Bytes() == 0) {
            iId = IPinIdProvider::kIdEmpty;
        }
        else {
            iId = iIdProvider.NextId();
        }
    }
    return changed;
}

TBool Pin::Clear()
{
    const TBool changed = (iId != IPinIdProvider::kIdEmpty);
    iMode.Replace(Brx::Empty());
    iType.Replace(Brx::Empty());
    iUri.Replace(Brx::Empty());
    iTitle.Replace(Brx::Empty());
    iDescription.Replace(Brx::Empty());
    iArtworkUri.Replace(Brx::Empty());
    iShuffle = false;
    iId = IPinIdProvider::kIdEmpty;
    return changed;
}

void Pin::Internalise(const Brx& aBuf)
{
    ReaderBuffer rb(aBuf);
    ReaderBinary reader(rb);
    ReadBuf(reader, 1, iMode);
    ReadBuf(reader, 1, iType);
    ReadBuf(reader, 2, iUri);
    ReadBuf(reader, 2, iTitle);
    ReadBuf(reader, 2, iDescription);
    ReadBuf(reader, 2, iArtworkUri);
    iShuffle = reader.ReadUintBe(1);
    // following assumes this function is only called once, on startup
    if (iMode.Bytes() == 0) {
        iId = IPinIdProvider::kIdEmpty;
    }
    else {
        iId = iIdProvider.NextId();
    }
}

void Pin::Externalise(IWriter& aWriter) const
{
    WriterBinary writer(aWriter);
    writer.WriteUint8(iMode.Bytes());
    writer.Write(iMode);
    writer.WriteUint8(iType.Bytes());
    writer.Write(iType);
    writer.WriteUint16Be(iUri.Bytes());
    writer.Write(iUri);
    writer.WriteUint16Be(iTitle.Bytes());
    writer.Write(iTitle);
    writer.WriteUint16Be(iDescription.Bytes());
    writer.Write(iDescription);
    writer.WriteUint16Be(iArtworkUri.Bytes());
    writer.Write(iArtworkUri);
    writer.WriteUint8(iShuffle? 1 : 0);
}

const Pin& Pin::operator=(const Pin& aPin)
{
    (void)Set(aPin.Mode(), aPin.Type(), aPin.Uri(), aPin.Title(),
              aPin.Description(), aPin.ArtworkUri(), aPin.Shuffle());
    iId = aPin.iId;
    return *this;
}

void Pin::Write(WriterJsonObject& aWriter) const
{
    aWriter.WriteInt("id", iId);
    aWriter.WriteString("mode", iMode);
    aWriter.WriteString("type", iType);
    aWriter.WriteString("uri", iUri);
    aWriter.WriteString("title", iTitle);
    aWriter.WriteString("description", iDescription);
    aWriter.WriteString("artworkUri", iArtworkUri);
    aWriter.WriteBool("shuffle", iShuffle);
}

TBool Pin::Set(const Brx& aMode, const Brx& aType, const Brx& aUri,
               const Brx& aTitle, const Brx& aDescription, const Brx& aArtworkUri,
               TBool aShuffle)
{
    TBool changed = false;
    if (iMode != aMode) {
        changed = true;
        iMode.ReplaceThrow(aMode);
    }
    if (iType != aType) {
        changed = true;
        iType.ReplaceThrow(aType);
    }
    if (iUri != aUri) {
        changed = true;
        iUri.ReplaceThrow(aUri);
    }
    if (iTitle != aTitle) {
        changed = true;
        iTitle.ReplaceThrow(aTitle);
    }
    if (iDescription != aDescription) {
        changed = true;
        iDescription.ReplaceThrow(aDescription);
    }
    if (iArtworkUri != aArtworkUri) {
        changed = true;
        iArtworkUri.ReplaceThrow(aArtworkUri);
    }
    if (iShuffle != aShuffle) {
        changed = true;
        iShuffle = aShuffle;
    }

    return changed;
}

void Pin::ReadBuf(ReaderBinary& aReader, TUint aLenBytes, Bwx& aBuf)
{
    const TUint bytes = aReader.ReadUintBe(aLenBytes);
    if (bytes > aBuf.MaxBytes()) {
        THROW(ReaderError);
    }
    aReader.ReadReplace(bytes, aBuf);
}

TUint Pin::Id() const
{
    return iId;
}

const Brx& Pin::Mode() const
{
    return iMode;
}

const Brx& Pin::Type() const
{
    return iType;
}

const Brx& Pin::Uri() const
{
    return iUri;
}

const Brx& Pin::Title() const
{
    return iTitle;
}

const Brx& Pin::Description() const
{
    return iDescription;
}

const Brx& Pin::ArtworkUri() const
{
    return iArtworkUri;
}

TBool Pin::Shuffle() const
{
    return iShuffle;
}


// PinIdProvider

PinIdProvider::PinIdProvider()
    : iLock("PIdP")
    , iNextId(kIdEmpty)
{
}

TUint PinIdProvider::NextId()
{
    AutoMutex _(iLock);
    if (++iNextId == kIdEmpty) {
        ++iNextId;
    }
    return iNextId;
}


// PinSet

PinSet::PinSet(TUint aCount, IPinIdProvider& aIdProvider, Configuration::IStoreReadWrite& aStore, const TChar* aName)
    : iIdProvider(aIdProvider)
    , iStore(aStore)
    , iName(aName)
    , iStoreBuf(2048)
{
    if (aCount > 0) {
        SetCount(aCount);
    }
}

void PinSet::SetCount(TUint aCount)
{
    iPins.reserve(aCount);
    iIds.reserve(aCount);
    Bws<32> key;
    for (TUint i = 0; i < aCount; i++) {
        Pin pin(iIdProvider);
        try {
            GetStoreKey(i, key);
            iStore.Read(key, iStoreBuf);
            pin.Internalise(iStoreBuf.Buffer());
        }
        catch (StoreKeyNotFound&) {}
        iPins.push_back(pin);
        iIds.push_back(pin.Id());
    }
}

TUint PinSet::Count() const
{
    return (TUint)iPins.size();
}

TBool PinSet::Set(TUint aIndex, const Brx& aMode, const Brx& aType, const Brx& aUri,
                 const Brx& aTitle, const Brx& aDescription, const Brx& aArtworkUri,
                 TBool aShuffle)
{
    if (aIndex >= iPins.size()) {
        THROW(PinIndexOutOfRange);
    }
    auto& pin = iPins[aIndex];
    if (!pin.TryUpdate(aMode, aType, aUri, aTitle, aDescription, aArtworkUri, aShuffle)) {
        return false;
    }
    iIds[aIndex] = pin.Id();
    WriteToStore(pin);
    return true;
}

TBool PinSet::Clear(TUint aId)
{
    if (aId == IPinIdProvider::kIdEmpty) {
        return false;
    }
    const auto index = IndexFromId(aId);
    auto& pin = iPins[index];
    if (!pin.Clear()) {
        return false;
    }
    iIds[index] = IPinIdProvider::kIdEmpty;
    WriteToStore(pin);
    return true;
}

TBool PinSet::Swap(TUint aIndex1, TUint aIndex2)
{
    if (aIndex1 >= iPins.size() || aIndex2 >= iPins.size()) {
        THROW(PinIndexOutOfRange);
    }
    if (iPins[aIndex1].Id() == IPinIdProvider::kIdEmpty &&
        iPins[aIndex2].Id() == IPinIdProvider::kIdEmpty) {
        return false;
    }
    std::iter_swap(iPins.begin() + aIndex1, iPins.begin() + aIndex2);

    const auto& pin1 = iPins[aIndex1];
    iIds[aIndex1] = pin1.Id();
    WriteToStore(pin1);

    const auto& pin2 = iPins[aIndex2];
    iIds[aIndex2] = pin2.Id();
    WriteToStore(pin2);

    return true;
}

TBool PinSet::Contains(TUint aId) const
{
    try {
        (void)IndexFromId(aId);
        return true;
    }
    catch (PinIdNotFound&) {
        return false;
    }
}

const Pin& PinSet::PinFromId(TUint aId) const
{
    const auto index = IndexFromId(aId);
    return iPins[index];
}

const Pin& PinSet::PinFromIndex(TUint aIndex) const
{
    if (aIndex >= iPins.size()) {
        THROW(PinIndexOutOfRange);
    }
    return iPins[aIndex];
}

const std::vector<TUint>& PinSet::IdArray() const
{
    return iIds;
}

TUint PinSet::IndexFromId(TUint aId) const
{
    auto it = iPins.begin();
    for (; it != iPins.end(); ++it) {
        if (it->Id() == aId) {
            break;
        }
    }
    if (it == iPins.end()) {
        THROW(PinIdNotFound);
    }
    return std::distance(iPins.begin(), it);
}

void PinSet::WriteToStore(const Pin& aPin)
{
    aPin.Externalise(iStoreBuf);
    Bws<32> key;
    GetStoreKey(aPin.Id(), key);
    iStore.Write(key, iStoreBuf.Buffer());
}

void PinSet::GetStoreKey(TUint aId, Bwx& aKey)
{
    aKey.Replace("Pin.");
    aKey.Replace("iName");
    aKey.Replace(".");
    Ascii::AppendDec(aKey, aId);
}


// PinsManager

inline IPinsAccount& PinsManager::AccountSetter()
{
    AutoMutex _(iLock);
    if (iAccountSetter == nullptr) {
        THROW(PinError);
    }
    return *iAccountSetter;
}

PinsManager::PinsManager(Configuration::IStoreReadWrite& aStore, TUint aMaxDevice)
    : iLock("PinM")
    , iPinsDevice(aMaxDevice, iIdProvider, aStore, "Dv")
    , iPinsAccount(0, iIdProvider, aStore, "Ac")
    , iObserver(nullptr)
    , iAccountSetter(nullptr)
{
}

PinsManager::~PinsManager()
{
    for (auto kvp : iInvokers) {
        delete kvp.second;
    }
}

void PinsManager::SetAccount(IPinsAccount& aAccount, TUint aCount)
{
    {
        AutoMutex _(iLock);
        ASSERT(iAccountSetter == nullptr);
        iAccountSetter = &aAccount;
        iPinsAccount.SetCount(aCount);
        if (iObserver != nullptr) {
            iObserver->NotifyAccountPinsMax(iPinsAccount.Count());
            iObserver->NotifyUpdatesAccount(iPinsAccount.IdArray());
        }
    }
    AccountSetter().SetObserver(*this);
}

void PinsManager::Add(IPinInvoker* aInvoker)
{
    AutoMutex _(iLock);
    Brn mode(aInvoker->Mode());
    ASSERT(iInvokers.find(mode) == iInvokers.end());
    iInvokers.insert(std::pair<Brn, IPinInvoker*>(mode, aInvoker));
    if (iObserver != nullptr) {
        iObserver->NotifyModeAdded(mode);
    }
}

void PinsManager::SetObserver(IPinsObserver& aObserver)
{
    AutoMutex _(iLock);
    ASSERT(iObserver == nullptr);
    iObserver = &aObserver;
    iObserver->NotifyDevicePinsMax(iPinsDevice.Count());
    iObserver->NotifyUpdatesDevice(iPinsDevice.IdArray());
    iObserver->NotifyAccountPinsMax(iPinsAccount.Count());
    iObserver->NotifyUpdatesAccount(iPinsAccount.IdArray());
    for (auto kvp : iInvokers) {
        iObserver->NotifyModeAdded(kvp.first);
    }
}

void PinsManager::Set(TUint aIndex, const Brx& aMode, const Brx& aType, const Brx& aUri,
                     const Brx& aTitle, const Brx& aDescription, const Brx& aArtworkUri,
                     TBool aShuffle)
{
    if (IsAccountIndex(aIndex)) {
        const auto accountIndex = AccountFromCombinedIndex(aIndex);
        AccountSetter().Set(accountIndex, aMode, aType, aUri, aTitle, aDescription, aArtworkUri, aShuffle);
    }
    else {
        AutoMutex _(iLock);
        if (iPinsDevice.Set(aIndex, aMode, aType, aUri, aTitle, aDescription, aArtworkUri, aShuffle)) {
            if (iObserver != nullptr) {
                iObserver->NotifyUpdatesDevice(iPinsDevice.IdArray());
            }
        }
    }
}

void PinsManager::Clear(TUint aId)
{
    if (IsAccountId(aId)) {
        const TUint index = iPinsAccount.IndexFromId(aId);
        AccountSetter().Set(index, Brx::Empty(), Brx::Empty(), Brx::Empty(),
                            Brx::Empty(),Brx::Empty(), Brx::Empty(), false);
    }
    else {
        AutoMutex _(iLock);
        if (iPinsDevice.Clear(aId)) {
            if (iObserver != nullptr) {
                iObserver->NotifyUpdatesDevice(iPinsDevice.IdArray());
            }
        }
    }
}

void PinsManager::Swap(TUint aIndex1, TUint aIndex2)
{
    if (IsAccountIndex(aIndex1)) {
        if (!IsAccountIndex(aIndex2)) {
            THROW(PinError);
        }
        const auto index1 = AccountFromCombinedIndex(aIndex1);
        const auto index2 = AccountFromCombinedIndex(aIndex2);
        AccountSetter().Swap(index1, index2);
    }
    else {
        if (IsAccountIndex(aIndex2)) {
            THROW(PinError);
        }
        AutoMutex _(iLock);
        if (iPinsDevice.Swap(aIndex1, aIndex2))  {
            if (iObserver != nullptr) {
                iObserver->NotifyUpdatesDevice(iPinsDevice.IdArray());
            }
        }
    }
}

void PinsManager::WriteJson(IWriter& aWriter, const std::vector<TUint>& aIds)
{
    AutoMutex _(iLock);
    WriterJsonArray writerArray(aWriter);
    for (auto id : aIds) {
        try {
            auto& pin = PinFromId(id);
            auto writerPin = writerArray.CreateObject();
            pin.Write(writerPin);
            writerPin.WriteEnd();
        }
        catch (PinIdNotFound&) {}
    }
}

void PinsManager::InvokeId(TUint aId)
{
    Pin pin(iIdProvider);
    IPinInvoker* invoker = nullptr;
    {
        AutoMutex _(iLock);
        pin = PinFromId(aId);
        Brn mode(pin.Mode());
        if (mode.Bytes() == 0) {
            THROW(PinError);
        }
        auto it = iInvokers.find(mode);
        if (it == iInvokers.end()) {
            THROW(PinError);
        }
        invoker = it->second;
    }
    invoker->Invoke(pin);
}

void PinsManager::InvokeIndex(TUint aIndex)
{
    Pin pin(iIdProvider);
    IPinInvoker* invoker = nullptr;
    {
        AutoMutex _(iLock);
        if (IsAccountIndex(aIndex)) {
            const auto index = AccountFromCombinedIndex(aIndex);
            pin = iPinsAccount.PinFromIndex(index);
        }
        else {
            pin = iPinsDevice.PinFromIndex(aIndex);
        }

        Brn mode(pin.Mode());
        if (mode.Bytes() == 0) {
            THROW(PinError);
        }
        auto it = iInvokers.find(mode);
        if (it == iInvokers.end()) {
            THROW(PinError);
        }
        invoker = it->second;
    }
    invoker->Invoke(pin);
}

void PinsManager::NotifyAccountPin(TUint aIndex, const Brx& aMode, const Brx& aType,
                                  const Brx& aUri, const Brx& aTitle, const Brx& aDescription,
                                  const Brx& aArtworkUri, TBool aShuffle)
{
    AutoMutex _(iLock);
    if (iPinsAccount.Set(aIndex, aMode, aType, aUri, aTitle, aDescription, aArtworkUri, aShuffle)) {
        if (iObserver != nullptr) {
            iObserver->NotifyUpdatesAccount(iPinsAccount.IdArray());
        }
    }
}

TBool PinsManager::IsAccountId(TUint aId) const
{
    return !iPinsDevice.Contains(aId);
}

TBool PinsManager::IsAccountIndex(TUint aIndex) const
{
    const auto countDv = iPinsDevice.Count();
    const auto countAc = iPinsAccount.Count();
    return aIndex >= countDv && aIndex < countDv + countAc;
}

TUint PinsManager::AccountFromCombinedIndex(TUint aCombinedIndex) const
{
    return aCombinedIndex - iPinsDevice.Count();
}

const Pin& PinsManager::PinFromId(TUint aId) const
{
    try {
        return iPinsDevice.PinFromId(aId);
    }
    catch (PinIdNotFound&) {
        return iPinsAccount.PinFromId(aId);
    }
}

PinUri::PinUri(const IPin& aPin)
    : iMode(256)
    , iType(256)
    , iSubType(256)
    , iValue(256)
{
    OpenHome::Uri req(aPin.Uri());
    iMode.Replace(req.Scheme());
    iType.Replace(req.Host());
    OpenHome::Parser parser(req.Query());
    parser.Next('?');
    while (!parser.Finished()) {
        iSubType.Replace(parser.Next('='));
        iValue.Replace(parser.Next('&'));
        if (iSubType != Brn("version")) {
            break;
        }
    }
}

PinUri::~PinUri()
{
}

const Brx& PinUri::Mode()
{
    return iMode;
}

const Brx& PinUri::Type()
{
    return iType;
}

const Brx& PinUri::SubType()
{
    return iSubType;
}

const Brx& PinUri::Value()
{
    return iValue;
}
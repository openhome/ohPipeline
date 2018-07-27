#include <OpenHome/Av/Pins/Pins.h>
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

Pin::Pin(const Pin& aPin)
    : iIdProvider(aPin.iIdProvider)
    , iShuffle(false)
{
    Copy(aPin);
}

const Pin& Pin::operator=(const Pin& aPin)
{
    Copy(aPin);
    return *this;
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

void Pin::Copy(const Pin& aPin)
{
    (void)Set(aPin.Mode(), aPin.Type(), aPin.Uri(), aPin.Title(),
              aPin.Description(), aPin.ArtworkUri(), aPin.Shuffle());
    iIdProvider = aPin.iIdProvider;
    iId = aPin.iId;
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

PinSet::~PinSet()
{
    for (auto pin : iPins) {
        delete pin;
    }
}

void PinSet::SetCount(TUint aCount)
{
    iPins.reserve(aCount);
    iIds.reserve(aCount);
    Bws<32> key;
    for (TUint i = 0; i < aCount; i++) {
        auto pin = new Pin(iIdProvider);
        try {
            iStoreBuf.Reset();
            GetStoreKey(i, key);
            iStore.Read(key, iStoreBuf);
            pin->Internalise(iStoreBuf.Buffer());
        }
        catch (StoreKeyNotFound&) {}
        iPins.push_back(pin);
        iIds.push_back(pin->Id());
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
    auto pin = iPins[aIndex];
    if (!pin->TryUpdate(aMode, aType, aUri, aTitle, aDescription, aArtworkUri, aShuffle)) {
        return false;
    }
    iIds[aIndex] = pin->Id();
    WriteToStore(aIndex);
    return true;
}

TBool PinSet::Clear(TUint aId)
{
    if (aId == IPinIdProvider::kIdEmpty) {
        return false;
    }
    const auto index = IndexFromId(aId);
    auto pin = iPins[index];
    if (!pin->Clear()) {
        return false;
    }
    iIds[index] = IPinIdProvider::kIdEmpty;
    WriteToStore(index);
    return true;
}

TBool PinSet::Swap(TUint aIndex1, TUint aIndex2)
{
    if (aIndex1 >= iPins.size() || aIndex2 >= iPins.size()) {
        THROW(PinIndexOutOfRange);
    }
    if (iPins[aIndex1]->Id() == IPinIdProvider::kIdEmpty &&
        iPins[aIndex2]->Id() == IPinIdProvider::kIdEmpty) {
        return false;
    }
    std::iter_swap(iPins.begin() + aIndex1, iPins.begin() + aIndex2);

    iIds[aIndex1] = iPins[aIndex1]->Id();
    WriteToStore(aIndex1);

    iIds[aIndex2] = iPins[aIndex2]->Id();
    WriteToStore(aIndex2);

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

TBool PinSet::IsEmpty() const
{
    for (auto pin : iPins) {
        if (pin->Id() != IPinIdProvider::kIdEmpty) {
            return false;
        }
    }
    return true;
}

const Pin& PinSet::PinFromId(TUint aId) const
{
    const auto index = IndexFromId(aId);
    return *(iPins[index]);
}

const Pin& PinSet::PinFromIndex(TUint aIndex) const
{
    if (aIndex >= iPins.size()) {
        THROW(PinIndexOutOfRange);
    }
    return *iPins[aIndex];
}

const std::vector<TUint>& PinSet::IdArray() const
{
    return iIds;
}

TUint PinSet::IndexFromId(TUint aId) const
{
    auto it = iPins.begin();
    for (; it != iPins.end(); ++it) {
        if ((*it)->Id() == aId) {
            break;
        }
    }
    if (it == iPins.end()) {
        THROW(PinIdNotFound);
    }
    return std::distance(iPins.begin(), it);
}

void PinSet::WriteToStore(TUint aIndex)
{
    const auto pin = iPins[aIndex];
    iStoreBuf.Reset();
    pin->Externalise(iStoreBuf);
    Bws<32> key;
    GetStoreKey(aIndex, key);
    iStore.Write(key, iStoreBuf.Buffer());
}

void PinSet::GetStoreKey(TUint aIndex, Bwx& aKey)
{
    aKey.Replace("Pin.");
    aKey.Append(iName);
    aKey.Append(".");
    Ascii::AppendDec(aKey, aIndex);
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
    : iLock("Pin1")
    , iLockInvoke("Pin2")
    , iPinsDevice(aMaxDevice, iIdProvider, aStore, "Dv")
    , iPinsAccount(0, iIdProvider, aStore, "Ac")
    , iObserver(nullptr)
    , iAccountSetter(nullptr)
    , iInvoke(iIdProvider)
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
    writerArray.WriteEnd();
}

void PinsManager::InvokeId(TUint aId)
{
    AutoMutex _(iLockInvoke);
    IPinInvoker* invoker = nullptr;
    {
        AutoMutex __(iLock);
        const auto& pin = PinFromId(aId);
        iInvoke.Copy(pin);
        Brn mode(iInvoke.Mode());
        if (mode.Bytes() == 0) {
            THROW(PinModeNotSupported);
        }
        auto it = iInvokers.find(mode);
        if (it == iInvokers.end()) {
            THROW(PinModeNotSupported);
        }
        invoker = it->second;
    }
    invoker->Invoke(iInvoke);
}

void PinsManager::InvokeIndex(TUint aIndex)
{
    AutoMutex _(iLockInvoke);
    IPinInvoker* invoker = nullptr;
    {
        AutoMutex __(iLock);
        if (IsAccountIndex(aIndex)) {
            const auto index = AccountFromCombinedIndex(aIndex);
            const auto& pin = iPinsAccount.PinFromIndex(index);
            iInvoke.Copy(pin);
        }
        else {
            const auto& pin = iPinsDevice.PinFromIndex(aIndex);
            iInvoke.Copy(pin);
        }

        Brn mode(iInvoke.Mode());
        if (mode.Bytes() == 0) {
            THROW(PinModeNotSupported);
        }
        auto it = iInvokers.find(mode);
        if (it == iInvokers.end()) {
            THROW(PinModeNotSupported);
        }
        invoker = it->second;
    }
    invoker->Invoke(iInvoke);
}

void PinsManager::InvokeUri(const Brx& aMode, const Brx& aType, const Brx& aUri, TBool aShuffle)
{
    Pin pin(iIdProvider);
    (void)pin.TryUpdate(aMode, aType, aUri, Brx::Empty(), Brx::Empty(), Brx::Empty(), aShuffle);

    AutoMutex _(iLockInvoke);
    IPinInvoker* invoker = nullptr;
    {
        AutoMutex __(iLock);
        Brn mode(aMode);
        if (mode.Bytes() == 0) {
            THROW(PinModeNotSupported);
        }
        auto it = iInvokers.find(mode);
        if (it == iInvokers.end()) {
            THROW(PinModeNotSupported);
        }
        invoker = it->second;
    }
    invoker->Invoke(pin);
}

void PinsManager::NotifySettable(TBool aSettable)
{
    AutoMutex _(iLock);
    iObserver->NotifyCloudConnected(aSettable);
    if (aSettable) {
        iObserver->NotifyAccountPinsMax(iPinsAccount.Count());
    }
    else {
        if (iPinsAccount.IsEmpty()) {
            iObserver->NotifyAccountPinsMax(0);
        }
    }
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

// <mode>://<type>?<subtype>=<value>[&genre=<genreFilter>][&version=1]
// <subtype> = 'id' or 'trackId' or anything (not checked)
// <value> = <smartType> if <type> = 'smart', otherwise id or text string
// <genreFilter> = OPTIONAL genre ID for 'smart' type filtering (qobuz only)
// version is not currently checked

PinUri::PinUri(const IPin& aPin)
    : iMode(256)
    , iType(256)
    , iSubType(256)
    , iValue(256)
    , iGenre(256)
{
    OpenHome::Uri req(aPin.Uri());
    iMode.Replace(req.Scheme());
    iType.Replace(req.Host());
    OpenHome::Parser parser(req.Query());
    parser.Next('?');
    while (!parser.Finished()) {
        Brn entry(parser.Next('&'));
        if (entry.Bytes() > 0) {
            OpenHome::Parser pe(entry);
            Brn left(pe.Next('='));
            Brn right(pe.Remaining());
            if (left == Brn("genre")) {
                iGenre.Replace(right);
            }
            else if (left != Brn("version")) {
                iSubType.Replace(left);
                iValue.Replace(right);
            }
        }
    }
}


PinUri::~PinUri()
{
}

const Brx& PinUri::Mode() const
{
    return iMode;
}

const Brx& PinUri::Type() const
{
    return iType;
}

const Brx& PinUri::SubType() const
{
    return iSubType;
}

const Brx& PinUri::Value() const
{
    return iValue;
}

const Brx& PinUri::Genre() const
{
    return iGenre;
}
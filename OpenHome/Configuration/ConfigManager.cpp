#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Private/Arch.h>
#include <OpenHome/Private/Ascii.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Stream.h>

#include <algorithm>

using namespace OpenHome;
using namespace OpenHome::Configuration;


// ConfigNum

ConfigNum::ConfigNum(IConfigInitialiser& aManager, const Brx& aKey,
                     TInt aMin, TInt aMax, TInt aDefault, TBool aRebootRequired,
                     ConfigValAccess aAccess)
    : ConfigVal(aManager, aKey, aRebootRequired, aAccess)
    , iMin(aMin)
    , iMax(aMax)
    , iDefault(aDefault)
    , iMutex("CVNM")
{
    ASSERT(iMax >= iMin);
    ASSERT(IsValid(iDefault));

    Bws<sizeof(TInt)> initialBuf;
    Bws<sizeof(TInt)> defaultBuf;
    WriterBuffer writerBuf(defaultBuf);
    WriterBinary writerBin(writerBuf);
    writerBin.WriteUint32Be(aDefault);
    iConfigManager.FromStore(iKey, initialBuf, defaultBuf);
    TInt initialVal = Converter::BeUint32At(initialBuf, 0);

    if (!IsValid(initialVal)) {
        // Stored value is no longer valid.  Report the default value to subscribers but leave the stored value unchanged.
        // If a future release reinstates previous limits, the stored value will be picked up again.
        Log::Print("ConfigNum(%.*s) stored value (%d) is no longer valid, using default (%d) instead\n",
            PBUF(aKey), initialVal, iDefault);
        initialVal = iDefault;
    }

    iVal = initialVal;

    iConfigManager.Add(*this);
    AddInitialSubscribers();
}

ConfigNum::~ConfigNum()
{
    iConfigManager.Remove(*this);
}

TInt ConfigNum::Min() const
{
    return iMin;
}

TInt ConfigNum::Max() const
{
    return iMax;
}

void ConfigNum::Set(TInt aVal)
{
    if (!IsValid(aVal)) {
        THROW(ConfigValueOutOfRange);
    }

    AutoMutex a(iMutex);
    if (aVal != iVal) {
        iVal = aVal;
        NotifySubscribers(iVal);
    }
}

TBool ConfigNum::IsValid(TInt aVal) const
{
    if (aVal < iMin || aVal > iMax) {
        return false;
    }
    return true;
}

TInt ConfigNum::Default() const
{
    return iDefault;
}

TUint ConfigNum::Subscribe(FunctorGeneric<KeyValuePair<TInt>&> aFunctor)
{
    AutoMutex a(iMutex);
    return ConfigVal::Subscribe(aFunctor, iVal);
}

void ConfigNum::Serialise(IWriter& aWriter) const
{
    Bws<kMaxNumLength> buf;
    AutoMutex a(iMutex);
    Ascii::AppendDec(buf, iVal);
    aWriter.Write(buf);
    aWriter.WriteFlush();
}

void ConfigNum::Deserialise(const Brx& aString)
{
    TInt val = 0;

    try {
        val = Ascii::Int(aString);
    }
    catch (AsciiError&) {
        THROW(ConfigNotANumber);
    }
    Set(val);
}

void ConfigNum::Write(KeyValuePair<TInt>& aKvp)
{
    Bws<sizeof(TInt)> valBuf;
    WriterBuffer writerBuf(valBuf);
    WriterBinary writerBin(writerBuf);
    writerBin.WriteUint32Be(aKvp.Value());
    iConfigManager.ToStore(iKey, valBuf);
}


// ConfigChoice

ConfigChoice::ConfigChoice(IConfigInitialiser& aManager, const Brx& aKey,
                           const std::vector<TUint>& aChoices, TUint aDefault,
                           TBool aRebootRequired, ConfigValAccess aAccess)
    : ConfigVal(aManager, aKey, aRebootRequired, aAccess)
    , iChoices(aChoices)
    , iDefault(aDefault)
    , iMapper(nullptr)
    , iMutex("CVCM")
{
    Init();
}

ConfigChoice::ConfigChoice(IConfigInitialiser& aManager, const Brx& aKey,
                           const std::vector<TUint>& aChoices, TUint aDefault,
                           IConfigChoiceMapper& aMapper, TBool aRebootRequired,
                           ConfigValAccess aAccess)
    : ConfigVal(aManager, aKey, aRebootRequired, aAccess)
    , iChoices(aChoices)
    , iDefault(aDefault)
    , iMapper(&aMapper)
    , iMutex("CVCM")
{
    Init();
}

ConfigChoice::~ConfigChoice()
{
    iConfigManager.Remove(*this);
}

const std::vector<TUint>& ConfigChoice::Choices() const
{
    return iChoices;
}

void ConfigChoice::Set(TUint aVal)
{
    if (!IsValid(aVal)) {
        THROW(ConfigInvalidSelection);
    }

    AutoMutex a(iMutex);
    if (aVal != iSelected) {
        iSelected = aVal;
        NotifySubscribers(iSelected);
    }
}

TBool ConfigChoice::HasInternalMapping() const
{
    if (iMapper != nullptr) {
        return true;
    }
    return false;
}

IConfigChoiceMapper& ConfigChoice::Mapper() const
{
    ASSERT(iMapper != nullptr);
    return *iMapper;
}

void ConfigChoice::Init()
{
    ASSERT(IsValid(iDefault));

    Bws<sizeof(TUint)> initialBuf;
    Bws<sizeof(TUint)> defaultBuf;
    WriterBuffer writerBuf(defaultBuf);
    WriterBinary writerBin(writerBuf);
    writerBin.WriteUint32Be(iDefault);
    iConfigManager.FromStore(iKey, initialBuf, defaultBuf);
    TUint initialVal = Converter::BeUint32At(initialBuf, 0);

    if (!IsValid(initialVal)) {
        // Bad value. Write default to store (so that there is no assertion in
        // future) and ASSERT() here to highlight programmer error.
        KvpChoice kvp(iKey, iDefault);
        Write(kvp);
        Log::Print("ConfigChoice::Init invalid initial value: %u\n", initialVal);
        ASSERTS();
    }

    iSelected = initialVal;

    iConfigManager.Add(*this);
    AddInitialSubscribers();
}

TBool ConfigChoice::IsValid(TUint aVal) const
{
    std::vector<TUint>::const_iterator it;
    it = std::find(iChoices.begin(), iChoices.end(), aVal);
    if (it == iChoices.end()) {
        return false;
    }
    return true;
}

TUint ConfigChoice::Default() const
{
    return iDefault;
}

TUint ConfigChoice::Subscribe(FunctorGeneric<KeyValuePair<TUint>&> aFunctor)
{
    AutoMutex a(iMutex);
    return ConfigVal::Subscribe(aFunctor, iSelected);
}

void ConfigChoice::Serialise(IWriter& aWriter) const
{
    Bws<kMaxChoiceLength> buf;
    AutoMutex a(iMutex);
    Ascii::AppendDec(buf, iSelected);
    aWriter.Write(buf);
    aWriter.WriteFlush();
}

void ConfigChoice::Deserialise(const Brx& aString)
{
    TUint val = 0;

    try {
        val = Ascii::Uint(aString);
    }
    catch (AsciiError&) {
        THROW(ConfigNotANumber);
    }
    Set(val);
}

void ConfigChoice::Write(KeyValuePair<TUint>& aKvp)
{
    Bws<sizeof(TUint)> valBuf;
    WriterBuffer writerBuf(valBuf);
    WriterBinary writerBin(writerBuf);
    writerBin.WriteUint32Be(aKvp.Value());
    iConfigManager.ToStore(iKey, valBuf);
}


// ConfigTextBase

ConfigTextBase::ConfigTextBase(IConfigInitialiser& aManager, const Brx& aKey, TUint aMinLength, TUint aMaxLength,
                               const Brx& aDefault, TBool aRebootRequired, ConfigValAccess aAccess)
    : ConfigVal(aManager, aKey, aRebootRequired, aAccess)
    , iMinLength(aMinLength)
    , iDefault(aDefault)
    , iText(aMaxLength)
    , iMutex("CVTM")
{
    ASSERT(aDefault.Bytes() >= aMinLength);
    ASSERT(aMaxLength <= kMaxBytes);

    ASSERT(iDefault.Bytes() >= iMinLength);
    ASSERT(iDefault.Bytes() <= iText.MaxBytes());

    Bwh initialBuf(aMaxLength);
    try {
        iConfigManager.FromStore(iKey, initialBuf, aDefault);
    }
    catch (StoreReadBufferUndersized&) {
        // This can only happen if store value is longer than aMaxLength.
        // Write (valid) default to store and assert on this occasion.

        // Size of value in store is unknown, and buffer used here was too
        // small to accomodate it, so unable to print the value for debugging
        // purposes.
        KvpText kvp(iKey, iDefault);
        Write(kvp);
        ASSERTS();
    }

    // Initial value fits into initial buf, so it is within max length limit.
    iText.Replace(initialBuf);
}

ConfigTextBase::~ConfigTextBase()
{
}

TUint ConfigTextBase::MinLengthInternal() const
{
    return iMinLength;
}

TUint ConfigTextBase::MaxLengthInternal() const
{
    return iText.MaxBytes();
}

void ConfigTextBase::SetInternal(const Brx& aText)
{
    if (aText.Bytes() < iMinLength) {
        THROW(ConfigValueTooShort);
    }
    if (aText.Bytes() > iText.MaxBytes()) {
        THROW(ConfigValueTooLong);
    }

    AutoMutex a(iMutex);
    if (aText != iText) {
        iText.Replace(aText);
        NotifySubscribers(iText);
    }
}

const Brx& ConfigTextBase::Default() const
{
    return iDefault;
}

TUint ConfigTextBase::Subscribe(FunctorGeneric<KeyValuePair<const Brx&>&> aFunctor)
{
    AutoMutex a(iMutex);
    return ConfigVal::Subscribe(aFunctor, iText);
}

void ConfigTextBase::Serialise(IWriter& aWriter) const
{
    AutoMutex a(iMutex);
    aWriter.Write(iText);
    aWriter.WriteFlush();
}

void ConfigTextBase::Write(KeyValuePair<const Brx&>& aKvp)
{
    iConfigManager.ToStore(iKey, aKvp.Value());
}


// ConfigText

ConfigText::ConfigText(IConfigInitialiser& aManager, const Brx& aKey, TUint aMinLength, TUint aMaxLength,
                       const Brx& aDefault, TBool aRebootRequired, ConfigValAccess aAccess)
    : ConfigTextBase(aManager, aKey, aMinLength, aMaxLength, aDefault, aRebootRequired, aAccess)
{
    iConfigManager.Add(*this);
    AddInitialSubscribers();
}

ConfigText::~ConfigText()
{
    iConfigManager.Remove(*this);
}

TUint ConfigText::MinLength() const
{
    return MinLengthInternal();
}

TUint ConfigText::MaxLength() const
{
    return MaxLengthInternal();
}

void ConfigText::Set(const Brx& aText)
{
    SetInternal(aText);
}

void ConfigText::Deserialise(const Brx& aString)
{
    SetInternal(aString);
}


// ConfigTextChoice

ConfigTextChoice::ConfigTextChoice(IConfigInitialiser& aManager, const Brx& aKey, IConfigTextChoices& aChoices,
                                   TUint aMinLength, TUint aMaxLength, const Brx& aDefault,
                                   TBool aRebootRequired, ConfigValAccess aAccess)
    : ConfigTextBase(aManager, aKey, aMinLength, aMaxLength, aDefault, aRebootRequired, aAccess)
    , iChoices(aChoices)
{
    iConfigManager.Add(*this);
    AddInitialSubscribers();
}

ConfigTextChoice::~ConfigTextChoice()
{
    iConfigManager.Remove(*this);
}

void ConfigTextChoice::AcceptChoicesVisitor(IConfigTextChoicesVisitor& aVisitor)
{
    iChoices.AcceptChoicesVisitor(aVisitor);
}

void ConfigTextChoice::Set(const Brx& aText)
{
    if (iChoices.IsValid(aText)) {
        try {
            SetInternal(aText);
        }
        catch (const ConfigValueTooShort&) {
            ASSERTS();
        }
        catch (const ConfigValueTooLong&) {
            ASSERTS();
        }
    }
    else {
        THROW(ConfigInvalidSelection);
    }
}

void ConfigTextChoice::Deserialise(const Brx& aString)
{
    Set(aString);
}


// WriterPrinter

void WriterPrinter::Write(TByte aValue)
{
    Bws<1> buf(aValue);
    Log::Print(buf);
}

void WriterPrinter::Write(const Brx& aBuffer)
{
    Log::Print(aBuffer);
}

void WriterPrinter::WriteFlush()
{
}


// ConfigManager

ConfigManager::ConfigManager(IStoreReadWrite& aStore)
    : iStore(aStore)
    , iOpen(false)
    , iLock("CFML")
    , iObserver(nullptr)
{
}

void ConfigManager::WriteKeys(IKeyWriter& aWriter) const
{
    AutoMutex a(iLock);
    ASSERT(iOpen);
    aWriter.WriteKeys(iKeyListOrdered);
}

TBool ConfigManager::HasNum(const Brx& aKey) const
{
    return iMapNum.Has(aKey);
}

ConfigNum& ConfigManager::GetNum(const Brx& aKey) const
{
    return iMapNum.Get(aKey);
}

TBool ConfigManager::HasChoice(const Brx& aKey) const
{
    return iMapChoice.Has(aKey);
}

ConfigChoice& ConfigManager::GetChoice(const Brx& aKey) const
{
    return iMapChoice.Get(aKey);
}

TBool ConfigManager::HasText(const Brx& aKey) const
{
    return iMapText.Has(aKey);
}

ConfigText& ConfigManager::GetText(const Brx& aKey) const
{
    return iMapText.Get(aKey);
}

TBool ConfigManager::HasTextChoice(const Brx& aKey) const
{
    return iMapTextChoice.Has(aKey);
}

ConfigTextChoice& ConfigManager::GetTextChoice(const Brx& aKey) const
{
    return iMapTextChoice.Get(aKey);
}

TBool ConfigManager::Has(const Brx& aKey) const
{
    return HasNum(aKey) || HasChoice(aKey) || HasText(aKey) || HasTextChoice(aKey);
}

ConfigValAccess ConfigManager::Access(const Brx& aKey) const
{
    if (HasNum(aKey)) {
        return iMapNum.Get(aKey).Access();
    }
    else if (HasChoice(aKey)) {
        return iMapChoice.Get(aKey).Access();
    }
    else if (HasText(aKey)) {
        return iMapText.Get(aKey).Access();
    }
    else if (HasTextChoice(aKey)) {
        return iMapTextChoice.Get(aKey).Access();
    }
    else {
        ASSERTS();
        return iMapNum.Get(aKey).Access(); // control will never reach here
    }
}

ISerialisable& ConfigManager::Get(const Brx& aKey) const
{
    // FIXME - ASSERT if !iOpen?
    if (HasNum(aKey)) {
        return iMapNum.Get(aKey);
    }
    else if (HasChoice(aKey)) {
        return iMapChoice.Get(aKey);
    }
    else if (HasText(aKey)) {
        return iMapText.Get(aKey);
    }
    else if (HasTextChoice(aKey)) {
        return iMapTextChoice.Get(aKey);
    }
    else {
        ASSERTS();
        return iMapNum.Get(aKey); // control will never reach here
    }
}

void ConfigManager::Print() const
{
    Log::Print("ConfigManager: [\n");

    Log::Print("ConfigNum:\n");
    Print(iMapNum);
    Log::Print("ConfigChoice:\n");
    Print(iMapChoice);
    Log::Print("ConfigText:\n");
    Print(iMapText);
    Log::Print("ConfigTextChoice:\n");
    Print(iMapTextChoice);

    Log::Print("]\n");
}

void ConfigManager::DumpToStore()
{
    StoreDumper dumper(*this);
    dumper.DumpToStore(iMapNum);
    dumper.DumpToStore(iMapChoice);
    dumper.DumpToStore(iMapText);
    dumper.DumpToStore(iMapTextChoice);
}

IStoreReadWrite& ConfigManager::Store()
{
    return iStore;
}

void ConfigManager::Open()
{
    AutoMutex a(iLock);
    // All keys should have been added, so sort key list.
    std::sort(iKeyListOrdered.begin(), iKeyListOrdered.end(), BufferPtrCmp());
    iOpen = true;
    if (iObserver != nullptr) {
        iObserver->AddsComplete();
    }
}

void ConfigManager::Add(ConfigNum& aNum)
{
    AddNum(aNum.Key(), aNum);
    iKeyListOrdered.push_back(&aNum.Key());

    AutoMutex _(iLock);
    if (iObserver != nullptr && aNum.Access() == ConfigValAccess::Public) {
        iObserver->Added(aNum);
    }
}

void ConfigManager::Add(ConfigChoice& aChoice)
{
    AddChoice(aChoice.Key(), aChoice);
    iKeyListOrdered.push_back(&aChoice.Key());

    AutoMutex _(iLock);
    if (iObserver != nullptr && aChoice.Access() == ConfigValAccess::Public) {
        iObserver->Added(aChoice);
    }
}

void ConfigManager::Add(ConfigText& aText)
{
    AddText(aText.Key(), aText);
    iKeyListOrdered.push_back(&aText.Key());

    AutoMutex _(iLock);
    if (iObserver != nullptr && aText.Access() == ConfigValAccess::Public) {
        iObserver->Added(aText);
    }
}

void ConfigManager::Add(ConfigTextChoice& aTextChoice)
{
    AddTextChoice(aTextChoice.Key(), aTextChoice);
    iKeyListOrdered.push_back(&aTextChoice.Key());

    AutoMutex _(iLock);
    if (iObserver != nullptr && aTextChoice.Access() == ConfigValAccess::Public) {
        iObserver->Added(aTextChoice);
    }
}

void ConfigManager::Remove(ConfigNum& aNum)
{
    if (iMapNum.TryRemove(aNum.Key())) {
        AutoMutex _(iLock);
        if (iObserver != nullptr) {
            iObserver->Removed(aNum);
        }
    }
}

void ConfigManager::Remove(ConfigChoice& aChoice)
{
    if (iMapChoice.TryRemove(aChoice.Key())) {
        AutoMutex _(iLock);
        if (iObserver != nullptr) {
            iObserver->Removed(aChoice);
        }
    }
}

void ConfigManager::Remove(ConfigText& aText)
{
    if (iMapText.TryRemove(aText.Key())) {
        AutoMutex _(iLock);
        if (iObserver != nullptr) {
            iObserver->Removed(aText);
        }
    }
}

void ConfigManager::Remove(ConfigTextChoice& aTextChoice)
{
    if (iMapTextChoice.TryRemove(aTextChoice.Key())) {
        AutoMutex _(iLock);
        if (iObserver != nullptr) {
            iObserver->Removed(aTextChoice);
        }
    }
}

void ConfigManager::FromStore(const Brx& aKey, Bwx& aDest, const Brx& aDefault)
{
    // try retrieve from store; create entry if it doesn't exist
    try {
        iStore.Read(aKey, aDest);
    }
    catch (StoreKeyNotFound&) {
        // Don't attempt to write default value out to store here. It will be
        // written if/when the value is changed.
        aDest.Replace(aDefault);
    }
}

void ConfigManager::ToStore(const Brx& aKey, const Brx& aValue)
{
    iStore.Write(aKey, aValue);
}

void ConfigManager::Add(IConfigObserver& aObserver)
{
    AutoMutex _(iLock);
    ASSERT(iObserver == nullptr); // don't support multiple observers (no obvious need for it)
    iObserver = &aObserver;

    for (auto it = iMapNum.Begin(); it != iMapNum.End(); ++it) {
        aObserver.Added(*(it->second));
    }
    for (auto it = iMapChoice.Begin(); it != iMapChoice.End(); ++it) {
        aObserver.Added(*(it->second));
    }
    for (auto it = iMapText.Begin(); it != iMapText.End(); ++it) {
        aObserver.Added(*(it->second));
    }
    for (auto it = iMapTextChoice.Begin(); it != iMapTextChoice.End(); ++it) {
        aObserver.Added(*(it->second));
    }

    if (iOpen) {
        aObserver.AddsComplete();
    }
}

void ConfigManager::Remove(IConfigObserver& aObserver)
{
    AutoMutex _(iLock);
    if (iObserver != nullptr) {
        ASSERT(iObserver == &aObserver);
        iObserver = nullptr;
    }
}

void ConfigManager::AddNum(const Brx& aKey, ConfigNum& aNum)
{
    Add(iMapNum, aKey, aNum);
}

void ConfigManager::AddChoice(const Brx& aKey, ConfigChoice& aChoice)
{
    Add(iMapChoice, aKey, aChoice);
}

void ConfigManager::AddText(const Brx& aKey, ConfigText& aText)
{
    Add(iMapText, aKey, aText);
}

void ConfigManager::AddTextChoice(const Brx& aKey, ConfigTextChoice& aTextChoice)
{
    Add(iMapTextChoice, aKey, aTextChoice);
}

template <class T> void ConfigManager::Add(SerialisedMap<T>& aMap, const Brx& aKey, T& aVal)
{
    {
        AutoMutex _(iLock);
        if (iOpen) {
            ASSERTS();
        }
    }
    if (HasNum(aKey) || HasChoice(aKey) || HasText(aKey)) {
        THROW(ConfigKeyExists);
    }

    aMap.Add(aKey, aVal);
}

template <class T> void ConfigManager::Print(const ConfigVal<T>& aVal) const
{
    WriterPrinter writerPrinter;
    Log::Print("    {");
    Log::Print(aVal.Key());
    Log::Print(", ");
    aVal.Serialise(writerPrinter);
    Log::Print("}\n");
}

template <class T> void ConfigManager::Print(const SerialisedMap<T>& aMap) const
{
    // Map iterators are not invalidated by any of the actions that
    // SerialisedMap allows, so don't need to lock.
    typename SerialisedMap<T>::Iterator it;
    for (it = aMap.Begin(); it != aMap.End(); ++it) {
        Print(*it->second);
    }
}


// ConfigManager::StoreDumper

ConfigManager::StoreDumper::StoreDumper(IConfigInitialiser& aConfigInit)
    : iConfigInit(aConfigInit)
{
}

void ConfigManager::StoreDumper::DumpToStore(const SerialisedMap<ConfigNum>& aMap)
{
    SerialisedMap<ConfigNum>::Iterator it;
    for (it = aMap.Begin(); it != aMap.End(); ++it) {
        ConfigNum& configVal = *it->second;
        TUint id = configVal.Subscribe(MakeFunctorConfigNum(*this, &ConfigManager::StoreDumper::NotifyChangedNum));
        configVal.Unsubscribe(id);
    }
}

void ConfigManager::StoreDumper::DumpToStore(const SerialisedMap<ConfigChoice>& aMap)
{
    SerialisedMap<ConfigChoice>::Iterator it;
    for (it = aMap.Begin(); it != aMap.End(); ++it) {
        ConfigChoice& configVal = *it->second;
        TUint id = configVal.Subscribe(MakeFunctorConfigChoice(*this, &ConfigManager::StoreDumper::NotifyChangedChoice));
        configVal.Unsubscribe(id);
    }
}

void ConfigManager::StoreDumper::DumpToStore(const SerialisedMap<ConfigText>& aMap)
{
    SerialisedMap<ConfigText>::Iterator it;
    for (it = aMap.Begin(); it != aMap.End(); ++it) {
        ConfigText& configVal = *it->second;
        TUint id = configVal.Subscribe(MakeFunctorConfigText(*this, &ConfigManager::StoreDumper::NotifyChangedText));
        configVal.Unsubscribe(id);
    }
}

void ConfigManager::StoreDumper::DumpToStore(const SerialisedMap<ConfigTextChoice>& aMap)
{
    SerialisedMap<ConfigTextChoice>::Iterator it;
    for (it = aMap.Begin(); it != aMap.End(); ++it) {
        ConfigTextChoice& configVal = *it->second;
        TUint id = configVal.Subscribe(MakeFunctorConfigText(*this, &ConfigManager::StoreDumper::NotifyChangedTextChoice));
        configVal.Unsubscribe(id);
    }
}

void ConfigManager::StoreDumper::NotifyChangedNum(ConfigNum::KvpNum& aKvp)
{
    Bws<sizeof(TInt)> valBuf;
    WriterBuffer writerBuf(valBuf);
    WriterBinary writerBin(writerBuf);
    writerBin.WriteUint32Be(aKvp.Value());
    iConfigInit.ToStore(aKvp.Key(), valBuf);
}

void ConfigManager::StoreDumper::NotifyChangedChoice(ConfigChoice::KvpChoice& aKvp)
{
    Bws<sizeof(TUint)> valBuf;
    WriterBuffer writerBuf(valBuf);
    WriterBinary writerBin(writerBuf);
    writerBin.WriteUint32Be(aKvp.Value());
    iConfigInit.ToStore(aKvp.Key(), valBuf);
}

void ConfigManager::StoreDumper::NotifyChangedText(ConfigText::KvpText& aKvp)
{
    iConfigInit.ToStore(aKvp.Key(), aKvp.Value());
}

void ConfigManager::StoreDumper::NotifyChangedTextChoice(ConfigTextChoice::KvpText& aKvp)
{
    iConfigInit.ToStore(aKvp.Key(), aKvp.Value());
}

#include <OpenHome/Configuration/ProviderConfigApp.h>
#include <OpenHome/Av/Product.h>
#include <OpenHome/Av/RebootHandler.h>
#include <OpenHome/Private/Ascii.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Configuration;
using namespace OpenHome::Net;


// ProviderFactory

IProvider* ProviderFactory::NewConfigApp(DvDevice& aDevice,
                                         IConfigManager& aConfigReader,
                                         IConfigObservable& aConfigObservable,
                                         IStoreReadWrite& aStore,
                                         IRebootHandler& aRebootHandler)
{ // static
    return new ProviderConfigApp(aDevice, aConfigReader, aConfigObservable, aStore, aRebootHandler);
}


// ProviderConfigApp::KeysWriter

const TUint ProviderConfigApp::KeysWriter::kBufGranularity = 4 * 1024;
const Brn ProviderConfigApp::KeysWriter::kKeyKey("key");
const Brn ProviderConfigApp::KeysWriter::kKeyType("type");
const Brn ProviderConfigApp::KeysWriter::kKeyMeta("meta");
const Brn ProviderConfigApp::KeysWriter::kKeyReboot("reboot");
const Brn ProviderConfigApp::KeysWriter::kValTypeNum("numeric");
const Brn ProviderConfigApp::KeysWriter::kValTypeChoice("enum");
const Brn ProviderConfigApp::KeysWriter::kValTypeText("string");
const Brn ProviderConfigApp::KeysWriter::kKeyNumMin("min");
const Brn ProviderConfigApp::KeysWriter::kKeyNumMax("max");
const Brn ProviderConfigApp::KeysWriter::kKeyNumDefault("default");
const Brn ProviderConfigApp::KeysWriter::kKeyEnumVals("vals");
const Brn ProviderConfigApp::KeysWriter::kKeyTextLen("max_len");

ProviderConfigApp::KeysWriter::KeysWriter()
    : iWriterBuf(kBufGranularity)
    , iWriterArray(iWriterBuf, WriterJsonArray::WriteOnEmpty::eEmptyArray)
{
}

void ProviderConfigApp::KeysWriter::Add(ConfigNum& aVal, const Brx& aKey)
{
    auto writerObject = iWriterArray.CreateObject();
    writerObject.WriteString(kKeyKey, aKey);
    writerObject.WriteString(kKeyType, kValTypeNum);
    {
        auto writerMeta = writerObject.CreateObject(kKeyMeta);
        writerMeta.WriteInt(kKeyNumMin, aVal.Min());
        writerMeta.WriteInt(kKeyNumMax, aVal.Max());
        writerMeta.WriteInt(kKeyNumDefault, aVal.Default());
        writerMeta.WriteEnd();
    }
    writerObject.WriteBool(kKeyReboot, aVal.RebootRequired());
    writerObject.WriteEnd();
}

void ProviderConfigApp::KeysWriter::Add(ConfigChoice& aVal, const Brx& aKey)
{
    auto writerObject = iWriterArray.CreateObject();
    writerObject.WriteString(kKeyKey, aKey);
    writerObject.WriteString(kKeyType, kValTypeChoice);
    {
        auto writerMeta = writerObject.CreateObject(kKeyMeta);
        {
            auto writerVals = writerMeta.CreateArray(kKeyEnumVals);
            const auto& choices = aVal.Choices();
            for (auto choice : choices) {
                writerVals.WriteInt(choice);
            }
            writerVals.WriteEnd();
        }
        writerMeta.WriteEnd();
    }
    writerObject.WriteBool(kKeyReboot, aVal.RebootRequired());
    writerObject.WriteEnd();
}

void ProviderConfigApp::KeysWriter::Add(ConfigText& aVal, const Brx& aKey)
{
    auto writerObject = iWriterArray.CreateObject();
    writerObject.WriteString(kKeyKey, aKey);
    writerObject.WriteString(kKeyType, kValTypeText);
    {
        auto writerMeta = writerObject.CreateObject(kKeyMeta);
        writerMeta.WriteInt(kKeyTextLen, aVal.MaxLength());
        writerMeta.WriteEnd();
    }
    writerObject.WriteBool(kKeyReboot, aVal.RebootRequired());
    writerObject.WriteEnd();
}

const Brx& ProviderConfigApp::KeysWriter::Flush()
{
    iWriterArray.WriteEnd();
    return iWriterBuf.Buffer();
}


// ProviderConfigApp

const TUint ProviderConfigApp::kErrorCodeInvalidKey = 800;
const Brn ProviderConfigApp::kErrorDescInvalidKey("Invalid key");
const TUint ProviderConfigApp::kErrorCodeNotANumber = 801;
const Brn ProviderConfigApp::kErrorDescNotANumber("Expected numerical value");
const TUint ProviderConfigApp::kErrorCodeValueOutOfRange = 802;
const Brn ProviderConfigApp::kErrorDescValueOutOfRange("Value outwith expected range");
const TUint ProviderConfigApp::kErrorCodeInvalidSelection = 803;
const Brn ProviderConfigApp::kErrorDescInvalidSelection("Expected value selected from list of options");
const TUint ProviderConfigApp::kErrorCodeValueTooLong = 804;
const Brn ProviderConfigApp::kErrorDescValueTooLong("Value too long");

const Brn ProviderConfigApp::kRebootReason("FacDef");

ProviderConfigApp::ProviderConfigApp(DvDevice& aDevice,
                                     IConfigManager& aConfigManager,
                                     IConfigObservable& aConfigObservable,
                                     IStoreReadWrite& aStore,
                                     Av::IRebootHandler& aRebootHandler)
    : DvProviderAvOpenhomeOrgConfigApp1(aDevice)
    , iConfigManager(aConfigManager)
    , iConfigObservable(aConfigObservable)
    , iStore(aStore)
    , iRebootHandler(aRebootHandler)
    , iLock("PCFG")
{
    EnablePropertyKeys();

    EnableActionGetKeys();
    EnableActionSetValue();
    EnableActionGetValue();
    EnableActionResetAll();

    iConfigObservable.Add(*this);
}

ProviderConfigApp::~ProviderConfigApp()
{
    iConfigObservable.Remove(*this);
    ClearMaps();
}

void ProviderConfigApp::Added(ConfigNum& aVal)
{
    AutoMutex _(iLock);
    Bwh keyStripped(aVal.Key().Bytes());
    StripKey(aVal.Key(), keyStripped);
    iKeysWriter.Add(aVal, keyStripped);
    Brn keyBuf(aVal.Key());
    Brn keyStrippedBuf(keyStripped);
    auto prop = new PropertyInt(new ParameterInt(keyStripped));
    iService->AddProperty(prop); // passes ownership
    auto item = new ConfigItemNum(aVal, *prop, keyStripped);
    iMapNum.insert(std::pair<Brn, ConfigItemNum*>(keyBuf, item));
    auto cb = MakeFunctorConfigNum(*this, &ProviderConfigApp::ConfigNumChanged);
    item->iListenerId = aVal.Subscribe(cb);
    iMapKeys.insert(std::pair<Brn, Brn>(keyStrippedBuf, keyBuf));
}

void ProviderConfigApp::Added(ConfigChoice& aVal)
{
    AutoMutex _(iLock);
    Bwh keyStripped(aVal.Key().Bytes());
    StripKey(aVal.Key(), keyStripped);
    iKeysWriter.Add(aVal, keyStripped);
    Brn keyBuf(aVal.Key());
    Brn keyStrippedBuf(keyStripped);
    auto prop = new PropertyUint(new ParameterUint(keyStripped));
    iService->AddProperty(prop); // passes ownership
    auto item = new ConfigItemChoice(aVal, *prop, keyStripped);
    iMapChoice.insert(std::pair<Brn, ConfigItemChoice*>(keyBuf, item));
    auto cb = MakeFunctorConfigChoice(*this, &ProviderConfigApp::ConfigChoiceChanged);
    item->iListenerId = aVal.Subscribe(cb);
    iMapKeys.insert(std::pair<Brn, Brn>(keyStrippedBuf, keyBuf));
}

void ProviderConfigApp::Added(ConfigText& aVal)
{
    AutoMutex _(iLock);
    Bwh keyStripped(aVal.Key().Bytes());
    StripKey(aVal.Key(), keyStripped);
    iKeysWriter.Add(aVal, keyStripped);
    Brn keyBuf(aVal.Key());
    Brn keyStrippedBuf(keyStripped);
    auto prop = new PropertyString(new ParameterString(keyStripped));
    iService->AddProperty(prop); // passes ownership
    auto item = new ConfigItemText(aVal, *prop, keyStripped);
    iMapText.insert(std::pair<Brn, ConfigItemText*>(keyBuf, item));
    auto cb = MakeFunctorConfigText(*this, &ProviderConfigApp::ConfigTextChanged);
    item->iListenerId = aVal.Subscribe(cb);
    iMapKeys.insert(std::pair<Brn, Brn>(keyStrippedBuf, keyBuf));
}

void ProviderConfigApp::AddsComplete()
{
    const Brx& keysJson = iKeysWriter.Flush();
    (void)SetPropertyKeys(keysJson);
}

void ProviderConfigApp::Removed(ConfigNum& aVal)
{
    AutoMutex _(iLock);
    Brn key(aVal.Key());
    auto it = iMapNum.find(key);
    if (it != iMapNum.end()) {
        delete it->second; // unsubscribe from aVal before it is destroyed
        iMapNum.erase(it);
    }
}

void ProviderConfigApp::Removed(ConfigChoice& aVal)
{
    AutoMutex _(iLock);
    Brn key(aVal.Key());
    auto it = iMapChoice.find(key);
    if (it != iMapChoice.end()) {
        delete it->second; // unsubscribe from aVal before it is destroyed
        iMapChoice.erase(it);
    }
}

void ProviderConfigApp::Removed(ConfigText& aVal)
{
    AutoMutex _(iLock);
    Brn key(aVal.Key());
    auto it = iMapText.find(key);
    if (it != iMapText.end()) {
        delete it->second; // unsubscribe from aVal before it is destroyed
        iMapText.erase(it);
    }
}

void ProviderConfigApp::StripKey(const Brx& aConfigKey, Bwx& aKey)
{
    aKey.SetBytes(0);
    const TUint bytes = aConfigKey.Bytes();
    for (TUint i = 0; i < bytes; i++) {
        const TByte ch = aConfigKey[i];
        if (Ascii::IsAlphabetic(ch) || Ascii::IsDigit(ch)) {
            aKey.Append(ch);
        }
    }
}

void ProviderConfigApp::ConfigNumChanged(KeyValuePair<TInt>& aKvp)
{
    Brn key(aKvp.Key());
    auto it = iMapNum.find(key);
    if (it != iMapNum.end()) {
        (void)SetPropertyInt(it->second->iProperty, aKvp.Value());
    }
}

void ProviderConfigApp::ConfigChoiceChanged(KeyValuePair<TUint>& aKvp)
{
    Brn key(aKvp.Key());
    auto it = iMapChoice.find(key);
    if (it != iMapChoice.end()) {
        (void)SetPropertyUint(it->second->iProperty, aKvp.Value());
    }
}

void ProviderConfigApp::ConfigTextChanged(KeyValuePair<const Brx&>& aKvp)
{
    Brn key(aKvp.Key());
    auto it = iMapText.find(key);
    if (it != iMapText.end()) {
        (void)SetPropertyString(it->second->iProperty, aKvp.Value());
    }
}

void ProviderConfigApp::ClearMaps()
{
    for (auto& kvp : iMapNum) {
        delete kvp.second;
    }
    iMapNum.clear();
    for (auto& kvp : iMapChoice) {
        delete kvp.second;
    }
    iMapChoice.clear();
    for (auto& kvp : iMapText) {
        delete kvp.second;
    }
    iMapText.clear();
}

void ProviderConfigApp::GetKeys(IDvInvocation& aInvocation, IDvInvocationResponseString& aKeys)
{
    aInvocation.StartResponse();
    WritePropertyKeys(aKeys);
    aKeys.WriteFlush();
    aInvocation.EndResponse();
}

void ProviderConfigApp::SetValue(IDvInvocation& aInvocation, const Brx& aKey, const Brx& aValue)
{
    Brn keyStripped(aKey);
    auto it = iMapKeys.find(keyStripped);
    if (it == iMapKeys.end()) {
        aInvocation.Error(kErrorCodeInvalidKey, kErrorDescInvalidKey);
    }
    Brn keyConfig(it->second);
    ISerialisable* ser = nullptr;
    auto it2 = iMapNum.find(keyConfig);
    if (it2 != iMapNum.end()) {
        ser = &it2->second->iVal;
    }
    else {
        auto it3 = iMapChoice.find(keyConfig);
        if (it3 != iMapChoice.end()) {
            ser = &it3->second->iVal;
        }
        else {
            auto it4 = iMapText.find(keyConfig);
            if (it4 == iMapText.end()) {
                aInvocation.Error(kErrorCodeInvalidKey, kErrorDescInvalidKey);
            }
            ser = &it4->second->iVal;
        }
    }

    try {
        ser->Deserialise(aValue);
    }
    catch (ConfigNotANumber&) {
        aInvocation.Error(kErrorCodeNotANumber, kErrorDescNotANumber);
    }
    catch (ConfigValueOutOfRange&) {
        aInvocation.Error(kErrorCodeValueOutOfRange, kErrorDescValueOutOfRange);
    }
    catch (ConfigInvalidSelection&) {
        aInvocation.Error(kErrorCodeInvalidSelection, kErrorDescInvalidSelection);
    }
    catch (ConfigValueTooLong&) {
        aInvocation.Error(kErrorCodeValueTooLong, kErrorDescValueTooLong);
    }

    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

void ProviderConfigApp::GetValue(IDvInvocation& aInvocation, const Brx& aKey, IDvInvocationResponseString& aValue)
{
    if (!iConfigManager.Has(aKey)) {
        aInvocation.Error(kErrorCodeInvalidKey, kErrorDescInvalidKey);
    }

    ISerialisable& ser = iConfigManager.Get(aKey);
    aInvocation.StartResponse();
    ser.Serialise(aValue);
    aInvocation.EndResponse();
}

void ProviderConfigApp::ResetAll(IDvInvocation& aInvocation)
{
    iStore.DeleteAll();
    iRebootHandler.Reboot(kRebootReason);
    aInvocation.StartResponse();
    aInvocation.EndResponse();
}

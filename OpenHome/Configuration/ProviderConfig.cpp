#include <OpenHome/Configuration/ProviderConfig.h>
#include <OpenHome/Av/Product.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Configuration;
using namespace OpenHome::Net;


// ProviderFactory

IProvider* ProviderFactory::NewConfiguration(DvDevice& aDevice,
                                             IConfigManager& aConfigReader,
                                             IConfigObservable& aConfigObservable)
{ // static
    return new ProviderConfig(aDevice, aConfigReader, aConfigObservable);
}


// KeyWriterJson

KeyWriterJson::KeyWriterJson(IWriter& aWriter)
    : iWriter(aWriter)
{
}

void KeyWriterJson::WriteKeys(const std::vector<const Brx*>& aKeys)
{
    iWriter.Write('[');
    auto it = aKeys.cbegin();
    for (;;) {
        iWriter.Write(Brn("\""));
        Json::Escape(iWriter, **it);
        iWriter.Write(Brn("\""));
        ++it;

        if (it != aKeys.cend()) {
            iWriter.Write(Brn(", "));
        }
        else {
            break;
        }
    }
    iWriter.Write(']');
    iWriter.WriteFlush();
}


// DetailsWriter

const TUint DetailsWriter::kBufGranularity = 4 * 1024;
const Brn DetailsWriter::kKeyKey("key");
const Brn DetailsWriter::kKeyType("type");
const Brn DetailsWriter::kKeyMeta("meta");
const Brn DetailsWriter::kKeyReboot("reboot");
const Brn DetailsWriter::kValTypeNum("numeric");
const Brn DetailsWriter::kValTypeChoice("enum");
const Brn DetailsWriter::kValTypeText("string");
const Brn DetailsWriter::kKeyNumMin("min");
const Brn DetailsWriter::kKeyNumMax("max");
const Brn DetailsWriter::kKeyNumDefault("default");
const Brn DetailsWriter::kKeyEnumVals("vals");
const Brn DetailsWriter::kKeyTextLen("max_len");

DetailsWriter::DetailsWriter()
    : iWriterBuf(kBufGranularity)
    , iWriterArray(iWriterBuf, WriterJsonArray::WriteOnEmpty::eEmptyArray)
{
}

void DetailsWriter::Add(ConfigNum& aVal)
{
    auto writerObject = iWriterArray.CreateObject();
    writerObject.WriteString(kKeyKey, aVal.Key());
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

void DetailsWriter::Add(ConfigChoice& aVal)
{
    auto writerObject = iWriterArray.CreateObject();
    writerObject.WriteString(kKeyKey, aVal.Key());
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

void DetailsWriter::Add(ConfigText& aVal)
{
    auto writerObject = iWriterArray.CreateObject();
    writerObject.WriteString(kKeyKey, aVal.Key());
    writerObject.WriteString(kKeyType, kValTypeText);
    {
        auto writerMeta = writerObject.CreateObject(kKeyMeta);
        writerMeta.WriteInt(kKeyTextLen, aVal.MaxLength());
        writerMeta.WriteEnd();
    }
    writerObject.WriteBool(kKeyReboot, aVal.RebootRequired());
    writerObject.WriteEnd();
}

const Brx& DetailsWriter::Flush()
{
    iWriterArray.WriteEnd();
    return iWriterBuf.Buffer();
}


// ProviderConfig::ConfigItemNum

ProviderConfig::ConfigItemNum::ConfigItemNum(ConfigNum& aConfigNum, PropertyInt& aProperty)
    : iVal(aConfigNum)
    , iProperty(aProperty)
    , iListenerId(IConfigManager::kSubscriptionIdInvalid)
{
}

ProviderConfig::ConfigItemNum::~ConfigItemNum()
{
    iVal.Unsubscribe(iListenerId);
}


// ProviderConfig::ConfigItemChoice

ProviderConfig::ConfigItemChoice::ConfigItemChoice(ConfigChoice& aConfigChoice, PropertyUint& aProperty)
    : iVal(aConfigChoice)
    , iProperty(aProperty)
    , iListenerId(IConfigManager::kSubscriptionIdInvalid)
{
}

ProviderConfig::ConfigItemChoice::~ConfigItemChoice()
{
    iVal.Unsubscribe(iListenerId);
}


// ProviderConfig::ConfigItemText

ProviderConfig::ConfigItemText::ConfigItemText(ConfigText& aConfigText, PropertyString& aProperty)
    : iVal(aConfigText)
    , iProperty(aProperty)
    , iListenerId(IConfigManager::kSubscriptionIdInvalid)
{
}

ProviderConfig::ConfigItemText::~ConfigItemText()
{
    iVal.Unsubscribe(iListenerId);
}


// ProviderConfig

const TUint ProviderConfig::kErrorCodeInvalidKey = 800;
const Brn ProviderConfig::kErrorDescInvalidKey("Invalid key");
const TUint ProviderConfig::kErrorCodeNotANumber = 801;
const Brn ProviderConfig::kErrorDescNotANumber("Expected numerical value");
const TUint ProviderConfig::kErrorCodeValueOutOfRange = 802;
const Brn ProviderConfig::kErrorDescValueOutOfRange("Value outwith expected range");
const TUint ProviderConfig::kErrorCodeInvalidSelection = 803;
const Brn ProviderConfig::kErrorDescInvalidSelection("Expected value selected from list of options");
const TUint ProviderConfig::kErrorCodeValueTooLong = 804;
const Brn ProviderConfig::kErrorDescValueTooLong("Value too long");

ProviderConfig::ProviderConfig(DvDevice& aDevice,
                               Configuration::IConfigManager& aConfigManager,
                               Configuration::IConfigObservable& aConfigObservable)
    : DvProviderAvOpenhomeOrgConfig3(aDevice)
    , iConfigManager(aConfigManager)
    , iConfigObservable(aConfigObservable)
    , iLock("PCFG")
{
    EnablePropertyDetails();

    EnableActionGetKeys();
    EnableActionGetDetails();
    EnableActionSetValue();
    EnableActionGetValue();
    EnableActionHasKey();

    iConfigObservable.Add(*this);
}

ProviderConfig::~ProviderConfig()
{
    iConfigObservable.Remove(*this);
    ClearMaps();
}

void ProviderConfig::Start()
{
    const Brx& detailsJson = iDetailsWriter.Flush();
    (void)SetPropertyDetails(detailsJson);
}

void ProviderConfig::Added(ConfigNum& aVal)
{
    AutoMutex _(iLock);
    iDetailsWriter.Add(aVal);
    Brn key(aVal.Key());
    auto prop = new PropertyInt(new ParameterInt(key));
    iService->AddProperty(prop); // passes ownership
    auto item = new ConfigItemNum(aVal, *prop);
    iMapNum.insert(std::pair<Brn, ConfigItemNum*>(key, item));
    auto cb = MakeFunctorConfigNum(*this, &ProviderConfig::ConfigNumChanged);
    item->iListenerId = aVal.Subscribe(cb);
}

void ProviderConfig::Added(ConfigChoice& aVal)
{
    AutoMutex _(iLock);
    iDetailsWriter.Add(aVal);
    Brn key(aVal.Key());
    auto prop = new PropertyUint(new ParameterUint(key));
    iService->AddProperty(prop); // passes ownership
    auto item = new ConfigItemChoice(aVal, *prop);
    iMapChoice.insert(std::pair<Brn, ConfigItemChoice*>(key, item));
    auto cb = MakeFunctorConfigChoice(*this, &ProviderConfig::ConfigChoiceChanged);
    item->iListenerId = aVal.Subscribe(cb);
}

void ProviderConfig::Added(ConfigText& aVal)
{
    AutoMutex _(iLock);
    iDetailsWriter.Add(aVal);
    Brn key(aVal.Key());
    auto prop = new PropertyString(new ParameterString(key));
    iService->AddProperty(prop); // passes ownership
    auto item = new ConfigItemText(aVal, *prop);
    iMapText.insert(std::pair<Brn, ConfigItemText*>(key, item));
    auto cb = MakeFunctorConfigText(*this, &ProviderConfig::ConfigTextChanged);
    item->iListenerId = aVal.Subscribe(cb);
}

void ProviderConfig::Removed(ConfigNum& aVal)
{
    AutoMutex _(iLock);
    Brn key(aVal.Key());
    auto it = iMapNum.find(key);
    if (it != iMapNum.end()) {
        delete it->second; // unsubscribe from aVal before it is destroyed
        iMapNum.erase(it);
    }
}

void ProviderConfig::Removed(ConfigChoice& aVal)
{
    AutoMutex _(iLock);
    Brn key(aVal.Key());
    auto it = iMapChoice.find(key);
    if (it != iMapChoice.end()) {
        delete it->second; // unsubscribe from aVal before it is destroyed
        iMapChoice.erase(it);
    }
}

void ProviderConfig::Removed(ConfigText& aVal)
{
    AutoMutex _(iLock);
    Brn key(aVal.Key());
    auto it = iMapText.find(key);
    if (it != iMapText.end()) {
        delete it->second; // unsubscribe from aVal before it is destroyed
        iMapText.erase(it);
    }
}

void ProviderConfig::ConfigNumChanged(KeyValuePair<TInt>& aKvp)
{
    Brn key(aKvp.Key());
    auto it = iMapNum.find(key);
    if (it != iMapNum.end()) {
        (void)it->second->iProperty.SetValue(aKvp.Value());
    }
}

void ProviderConfig::ConfigChoiceChanged(KeyValuePair<TUint>& aKvp)
{
    Brn key(aKvp.Key());
    auto it = iMapChoice.find(key);
    if (it != iMapChoice.end()) {
        (void)it->second->iProperty.SetValue(aKvp.Value());
    }
}

void ProviderConfig::ConfigTextChanged(KeyValuePair<const Brx&>& aKvp)
{
    Brn key(aKvp.Key());
    auto it = iMapText.find(key);
    if (it != iMapText.end()) {
        (void)it->second->iProperty.SetValue(aKvp.Value());
    }
}

void ProviderConfig::ClearMaps()
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

void ProviderConfig::GetKeys(IDvInvocation& aInvocation, IDvInvocationResponseString& aKeyList)
{
    KeyWriterJson keyWriter(aKeyList);
    aInvocation.StartResponse();
    iConfigManager.WriteKeys(keyWriter);
    aInvocation.EndResponse();
}

void ProviderConfig::GetDetails(IDvInvocation& aInvocation, IDvInvocationResponseString& aDetails)
{
    aInvocation.StartResponse();
    WritePropertyDetails(aDetails);
    aDetails.WriteFlush();
    aInvocation.EndResponse();
}

void ProviderConfig::SetValue(IDvInvocation& aInvocation, const Brx& aKey, const Brx& aValue)
{
    if (!iConfigManager.Has(aKey)) {
        aInvocation.Error(kErrorCodeInvalidKey, kErrorDescInvalidKey);
    }

    ISerialisable& ser = iConfigManager.Get(aKey);
    try {
        ser.Deserialise(aValue);
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

void ProviderConfig::GetValue(IDvInvocation& aInvocation, const Brx& aKey, IDvInvocationResponseString& aValue)
{
    if (!iConfigManager.Has(aKey)) {
        aInvocation.Error(kErrorCodeInvalidKey, kErrorDescInvalidKey);
    }

    ISerialisable& ser = iConfigManager.Get(aKey);
    aInvocation.StartResponse();
    ser.Serialise(aValue);
    aInvocation.EndResponse();
}

void ProviderConfig::HasKey(IDvInvocation& aInvocation, const Brx& aKey, IDvInvocationResponseBool& aValue) 
{
    aInvocation.StartResponse();
    aValue.Write(iConfigManager.Has(aKey));
    aInvocation.EndResponse();
}

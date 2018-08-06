#include <OpenHome/Configuration/ProviderConfig.h>
#include <OpenHome/Av/Product.h>

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Configuration;
using namespace OpenHome::Net;


// ProviderFactory

IProvider* ProviderFactory::NewConfiguration(DvDevice& aDevice, IConfigManager& aConfigManager)
{ // static
    return new ProviderConfig(aDevice, aConfigManager);
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
const TUint ProviderConfig::kErrorCodeValueTooShort = 805;
const Brn ProviderConfig::kErrorDescValueTooShort("Value too short");

ProviderConfig::ProviderConfig(DvDevice& aDevice, IConfigManager& aConfigManager)
    : DvProviderAvOpenhomeOrgConfig2(aDevice)
    , iConfigManager(aConfigManager)
{
    EnableActionGetKeys();
    EnableActionSetValue();
    EnableActionGetValue();
    EnableActionHasKey();
}

void ProviderConfig::GetKeys(IDvInvocation& aInvocation, IDvInvocationResponseString& aKeyList)
{
    KeyWriterJson keyWriter(aKeyList);
    aInvocation.StartResponse();
    iConfigManager.WriteKeys(keyWriter);
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
    catch (ConfigValueTooShort&) {
        aInvocation.Error(kErrorCodeValueTooShort, kErrorDescValueTooShort);
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

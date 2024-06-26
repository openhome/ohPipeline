#pragma once

#include <Generated/DvAvOpenhomeOrgConfig2.h>
#include <OpenHome/Av/ProviderFactory.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Json.h>

namespace OpenHome {
namespace Configuration {

class KeyWriterJson : public IKeyWriter, private INonCopyable
{
public:
    KeyWriterJson(IWriter& aWriter);
public: // from IKeyWriter
    void WriteKeys(const std::vector<const Brx*>& aKeys);
private:
    IWriter& iWriter;
};

class ProviderConfig : public Net::DvProviderAvOpenhomeOrgConfig2
                     , public Av::IProvider
{
    static const TUint kErrorCodeInvalidKey;
    static const TUint kErrorCodeNotANumber;
    static const TUint kErrorCodeValueOutOfRange;
    static const TUint kErrorCodeInvalidSelection;
    static const TUint kErrorCodeValueTooLong;
    static const TUint kErrorCodeValueTooShort;
    static const Brn kErrorDescInvalidKey;
    static const Brn kErrorDescNotANumber;
    static const Brn kErrorDescValueOutOfRange;
    static const Brn kErrorDescInvalidSelection;
    static const Brn kErrorDescValueTooLong;
    static const Brn kErrorDescValueTooShort;
public:
    ProviderConfig(Net::DvDevice& aDevice, Configuration::IConfigManager& aConfigManager);
private: // from DvProviderAvOpenhomeOrgConfiguration1
    void GetKeys(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseString& aKeyList) override;
    void SetValue(Net::IDvInvocation& aInvocation, const Brx& aKey, const Brx& aValue) override;
    void GetValue(Net::IDvInvocation& aInvocation, const Brx& aKey, Net::IDvInvocationResponseString& aValue) override;
    void HasKey(Net::IDvInvocation& aInvocation, const Brx& aKey, Net::IDvInvocationResponseBool& aValue) override;
private:
    Configuration::IConfigManager& iConfigManager;
};

}  // namespace Configuration
}  // namespace OpenHome

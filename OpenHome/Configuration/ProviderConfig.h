#pragma once

#include <Generated/DvAvOpenhomeOrgConfig3.h>
#include <OpenHome/Av/ProviderFactory.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Json.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Thread.h>

#include <map>

namespace OpenHome {
    namespace Net {
        class PropertyInt;
        class PropertyUint;
        class PropertyString;
    }
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

class DetailsWriter
{
    static const TUint kBufGranularity;
    static const Brn kKeyKey;
    static const Brn kKeyType;
    static const Brn kKeyMeta;
    static const Brn kKeyReboot;
    static const Brn kValTypeNum;
    static const Brn kValTypeChoice;
    static const Brn kValTypeText;
    static const Brn kKeyNumMin;
    static const Brn kKeyNumMax;
    static const Brn kKeyNumDefault;
    static const Brn kKeyEnumVals;
    static const Brn kKeyTextLen;
public:
    DetailsWriter();
    void Add(ConfigNum& aVal);
    void Add(ConfigChoice& aVal);
    void Add(ConfigText& aVal);
    const Brx& Flush();
private:
    WriterBwh iWriterBuf;
    WriterJsonArray iWriterArray;
    TBool iStarted;
};

class ProviderConfig : public Net::DvProviderAvOpenhomeOrgConfig3
                     , public Av::IProvider
                     , private IConfigObserver
{
    static const TUint kErrorCodeInvalidKey;
    static const TUint kErrorCodeNotANumber;
    static const TUint kErrorCodeValueOutOfRange;
    static const TUint kErrorCodeInvalidSelection;
    static const TUint kErrorCodeValueTooLong;
    static const Brn kErrorDescInvalidKey;
    static const Brn kErrorDescNotANumber;
    static const Brn kErrorDescValueOutOfRange;
    static const Brn kErrorDescInvalidSelection;
    static const Brn kErrorDescValueTooLong;
public:
    ProviderConfig(Net::DvDevice& aDevice,
                   Configuration::IConfigManager& aConfigManager,
                   Configuration::IConfigObservable& aConfigObservable);
    ~ProviderConfig();
    void Start();
private: // from IConfigObserver
    void Added(ConfigNum& aVal) override;
    void Added(ConfigChoice& aVal) override;
    void Added(ConfigText& aVal) override;
    void Removed(ConfigNum& aVal) override;
    void Removed(ConfigChoice& aVal) override;
    void Removed(ConfigText& aVal) override;
private:
    void ConfigNumChanged(KeyValuePair<TInt>& aKvp);
    void ConfigChoiceChanged(KeyValuePair<TUint>& aKvp);
    void ConfigTextChanged(KeyValuePair<const Brx&>& aKvp);
    void ClearMaps();
private: // from DvProviderAvOpenhomeOrgConfiguration1
    void GetKeys(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseString& aKeyList) override;
    void GetDetails(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseString& aKeys);
    void SetValue(Net::IDvInvocation& aInvocation, const Brx& aKey, const Brx& aValue) override;
    void GetValue(Net::IDvInvocation& aInvocation, const Brx& aKey, Net::IDvInvocationResponseString& aValue) override;
    void HasKey(Net::IDvInvocation& aInvocation, const Brx& aKey, Net::IDvInvocationResponseBool& aValue) override;
private:
    class ConfigItemNum
    {
    public:
        ConfigItemNum(ConfigNum& aConfigNum, Net::PropertyInt& aProperty);
        ~ConfigItemNum();
    public:
        ConfigNum& iVal;
        Net::PropertyInt& iProperty;
        TUint iListenerId;
    };
    class ConfigItemChoice
    {
    public:
        ConfigItemChoice(ConfigChoice& aConfigChoice, Net::PropertyUint& aProperty);
        ~ConfigItemChoice();
    public:
        ConfigChoice& iVal;
        Net::PropertyUint& iProperty;
        TUint iListenerId;
    };
    class ConfigItemText
    {
    public:
        ConfigItemText(ConfigText& aConfigText, Net::PropertyString& aProperty);
        ~ConfigItemText();
    public:
        ConfigText& iVal;
        Net::PropertyString& iProperty;
        TUint iListenerId;
    };
private:
    Configuration::IConfigManager& iConfigManager;
    Configuration::IConfigObservable& iConfigObservable;
    DetailsWriter iDetailsWriter;
    std::map<Brn, ConfigItemNum*, BufferCmp> iMapNum;
    std::map<Brn, ConfigItemChoice*, BufferCmp> iMapChoice;
    std::map<Brn, ConfigItemText*, BufferCmp> iMapText;
    Mutex iLock;
};

}  // namespace Configuration
}  // namespace OpenHome


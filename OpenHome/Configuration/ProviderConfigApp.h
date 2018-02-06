#pragma once

#include <Generated/DvAvOpenhomeOrgConfigApp1.h>
#include <OpenHome/Av/ProviderFactory.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Json.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Thread.h>

#include <map>

namespace OpenHome {
    namespace Av {
        class IRebootHandler;
    }
    namespace Net {
        class PropertyInt;
        class PropertyUint;
        class PropertyString;
    }
namespace Configuration {

class ProviderConfigApp : public Net::DvProviderAvOpenhomeOrgConfigApp1
                        , public Av::IProvider
                        , private IConfigObserver
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
    static const Brn kRebootReason;
public:
    ProviderConfigApp(Net::DvDevice& aDevice,
                      Configuration::IConfigManager& aConfigManager,
                      Configuration::IConfigObservable& aConfigObservable,
                      Configuration::IStoreReadWrite& aStore);
    ~ProviderConfigApp();
    void Attach(Av::IRebootHandler& aRebootHandler);
private: // from IConfigObserver
    void Added(ConfigNum& aVal) override;
    void Added(ConfigChoice& aVal) override;
    void Added(ConfigText& aVal) override;
    void AddsComplete() override;
    void Removed(ConfigNum& aVal) override;
    void Removed(ConfigChoice& aVal) override;
    void Removed(ConfigText& aVal) override;
private:
    void StripKey(const Brx& aConfigKey, Bwx& aKey);
    void ConfigNumChanged(KeyValuePair<TInt>& aKvp);
    void ConfigChoiceChanged(KeyValuePair<TUint>& aKvp);
    void ConfigTextChanged(KeyValuePair<const Brx&>& aKvp);
    void ClearMaps();
private: // from DvProviderAvOpenhomeOrgConfiguration1
    void GetKeys(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseString& aKeys) override;
    void SetValue(Net::IDvInvocation& aInvocation, const Brx& aKey, const Brx& aValue) override;
    void GetValue(Net::IDvInvocation& aInvocation, const Brx& aKey, Net::IDvInvocationResponseString& aValue) override;
    void ResetAll(Net::IDvInvocation& aInvocation) override;
private:
    class KeysWriter
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
        KeysWriter();
        void Add(ConfigNum& aVal);
        void Add(ConfigChoice& aVal);
        void Add(ConfigText& aVal);
        const Brx& Flush();
    private:
        WriterBwh iWriterBuf;
        WriterJsonArray iWriterArray;
    };
    template <class T, class S> class ConfigItem
    {
    public:
        ConfigItem(T& aConfigVal, S& aProperty, Bwh& aKey)
            : iVal(aConfigVal)
            , iProperty(aProperty)
            , iListenerId(IConfigManager::kSubscriptionIdInvalid)
        {
            aKey.TransferTo(iKeyStripped);
        }
        ~ConfigItem()
        {
            iVal.Unsubscribe(iListenerId);
        }
    public:
        T& iVal;
        S& iProperty;
        TUint iListenerId;
        Brh iKeyStripped;
    };
    typedef ConfigItem<ConfigNum,    Net::PropertyInt>    ConfigItemNum;
    typedef ConfigItem<ConfigChoice, Net::PropertyUint>   ConfigItemChoice;
    typedef ConfigItem<ConfigText,   Net::PropertyString> ConfigItemText;
private:
    Configuration::IConfigManager& iConfigManager;
    Configuration::IConfigObservable& iConfigObservable;
    IStoreReadWrite& iStore;
    Av::IRebootHandler* iRebootHandler;
    KeysWriter iKeysWriter;
    std::map<Brn, ConfigItemNum*, BufferCmp> iMapNum;
    std::map<Brn, ConfigItemChoice*, BufferCmp> iMapChoice;
    std::map<Brn, ConfigItemText*, BufferCmp> iMapText;
    std::map<Brn, Brn, BufferCmp> iMapKeys;
    Mutex iLock;
};

}  // namespace Configuration
}  // namespace OpenHome


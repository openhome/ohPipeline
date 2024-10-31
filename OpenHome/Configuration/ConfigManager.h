#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Configuration/IStore.h>

#include <map>
#include <vector>

EXCEPTION(ConfigKeyExists);
EXCEPTION(ConfigNotANumber);
EXCEPTION(ConfigValueOutOfRange);
EXCEPTION(ConfigInvalidSelection);
EXCEPTION(ConfigValueTooShort);
EXCEPTION(ConfigValueTooLong);

namespace OpenHome {
    class IWriter;
namespace Configuration {

template <class T>
class KeyValuePair : public INonCopyable
{
public:
    // Does not make a copy; owner is responsible for persisting parameter values.
    KeyValuePair(const Brx& aKey, T aValue);
    const Brx& Key() const;
    T Value() const;
private:
    const Brx& iKey;
    T iValue;
};

// KeyValuePair
template <class T> KeyValuePair<T>::KeyValuePair(const Brx& aKey, T aValue)
    : iKey(aKey)
    , iValue(aValue)
{
}

template <class T> const Brx& KeyValuePair<T>::Key() const
{
    return iKey;
}

template <class T> T KeyValuePair<T>::Value() const
{
    return iValue;
}


template <class T>
class IObservable
{
public:
    typedef FunctorGeneric<KeyValuePair<T>&> FunctorObserver;
public:
    virtual TUint Subscribe(FunctorObserver aFunctor) = 0;
    virtual void Unsubscribe(TUint aId) = 0;
    virtual ~IObservable() {}
};

/*
 * Helper function for creating a FunctorObserver.
 */
template<class Type, class Object, class CallType>
inline MemberTranslatorGeneric<KeyValuePair<Type>&,Object,void (CallType::*)(KeyValuePair<Type>&)>
    MakeFunctorObserver(Object& aC, void(CallType::* const &aF)(KeyValuePair<Type>&))
{
    typedef void(CallType::*MemFunc)(KeyValuePair<Type>&);
    return MemberTranslatorGeneric<KeyValuePair<Type>&,Object,MemFunc>(aC,aF);
}

class IConfigInitialiser;

class ISerialisable
{
public:
    virtual void Serialise(IWriter& aWriter) const = 0;
    virtual void Deserialise(const Brx& aString) = 0;
    virtual ~ISerialisable() {}
};

class ConfigNum;
class ConfigChoice;
class ConfigText;
class ConfigTextChoice;

class IKeyWriter
{
public:
    virtual void WriteKeys(const std::vector<const Brx*>& aKeys) = 0;
    virtual ~IKeyWriter() {}
};

enum class ConfigValAccess
{
    Public, // associated value is user-settable
    Private // associated value is an internal implementation detail and is not user-settable
};

/*
 * Interface for reading config vals from a configuration manager.
 */
class IConfigManager
{
public:
    static const TUint kSubscriptionIdInvalid = 0;
public:
    virtual void WriteKeys(IKeyWriter& aWriter) const = 0;
    virtual TBool HasNum(const Brx& aKey) const = 0;
    virtual ConfigNum& GetNum(const Brx& aKey) const = 0;
    virtual TBool HasChoice(const Brx& aKey) const = 0;
    virtual ConfigChoice& GetChoice(const Brx& aKey) const = 0;
    virtual TBool HasText(const Brx& aKey) const = 0;
    virtual ConfigText& GetText(const Brx& aKey) const = 0;
    virtual TBool HasTextChoice(const Brx& aKey) const = 0;
    virtual ConfigTextChoice& GetTextChoice(const Brx& aKey) const = 0;
    virtual TBool Has(const Brx& aKey) const = 0;
    virtual ConfigValAccess Access(const Brx& aKey) const = 0;
    virtual ISerialisable& Get(const Brx& aKey) const = 0;

    // Debugging.
    virtual void Print() const = 0;
    virtual void DumpToStore() = 0;

    virtual ~IConfigManager() {}
};

template <class T>
class ConfigVal : public IObservable<T>, public ISerialisable
{
    using typename IObservable<T>::FunctorObserver;
protected:
    ConfigVal(IConfigInitialiser& aManager, const Brx& aKey, TBool aRebootRequired, ConfigValAccess aAccess);
public:
    virtual ~ConfigVal();
    const Brx& Key() const;
    TBool RebootRequired() const;
    ConfigValAccess Access() const;
    virtual T Default() const = 0;
public: // from IObservable
    virtual TUint Subscribe(FunctorObserver aFunctor) override = 0;
    void Unsubscribe(TUint aId) override;
public: // from ISerialisable
    virtual void Serialise(IWriter& aWriter) const override = 0;
    virtual void Deserialise(const Brx& aString) override = 0;
protected:
    TUint Subscribe(FunctorObserver aFunctor, T aVal);
    void NotifySubscribers(T aVal);
    void AddInitialSubscribers();
    virtual void Write(KeyValuePair<T>& aKvp) = 0;
private:
    TUint SubscribeNoCallback(FunctorObserver aFunctor);
protected:
    IConfigInitialiser& iConfigManager;
    Bwh iKey;
private:
    typedef std::map<TUint,FunctorObserver> Map;
    Map iObservers;
    Mutex iObserverLock;
    TUint iWriteObserverId; // ID for own Write() observer
    TUint iNextObserverId;
    TBool iRebootRequired;
    ConfigValAccess iAccess;
};

// ConfigVal
template <class T> ConfigVal<T>::ConfigVal(IConfigInitialiser& aManager, const Brx& aKey,
                                           TBool aRebootRequired, ConfigValAccess aAccess)
    : iConfigManager(aManager)
    , iKey(aKey)
    , iObserverLock("CVOL")
    , iWriteObserverId(0)
    , iNextObserverId(IConfigManager::kSubscriptionIdInvalid+1)
    , iRebootRequired(aRebootRequired)
    , iAccess(aAccess)
{
}

template <class T> ConfigVal<T>::~ConfigVal()
{
    Unsubscribe(iWriteObserverId);
    if(iObservers.size() != 0)
    {
        Log::Print("Observer: %.*s \n", PBUF(iKey));
        ASSERTS();
    }
    //ASSERT(iObservers.size() == 0);
}

template <class T> const Brx& ConfigVal<T>::Key() const
{
    return iKey;
}

template <class T> TBool ConfigVal<T>::RebootRequired() const
{
    return iRebootRequired;
}

template <class T> ConfigValAccess ConfigVal<T>::Access() const
{
    return iAccess;
}

template <class T> void ConfigVal<T>::Unsubscribe(TUint aId)
{
    iObserverLock.Wait();
    typename Map::iterator it = iObservers.find(aId);
    if (it != iObservers.end()) {
        iObservers.erase(it);
    }
    iObserverLock.Signal();
}

template <class T> TUint ConfigVal<T>::SubscribeNoCallback(FunctorObserver aFunctor)
{
    iObserverLock.Wait();
    TUint id = iNextObserverId;
    iObservers.insert(std::pair<TUint,FunctorObserver>(id, aFunctor));
    iNextObserverId++;
    iObserverLock.Signal();
    return id;
}

template <class T> TUint ConfigVal<T>::Subscribe(FunctorObserver aFunctor, T aVal)
{
    KeyValuePair<T> kvp(iKey, aVal);
    TUint id = SubscribeNoCallback(aFunctor);
    aFunctor(kvp);
    return id;
}

template <class T> void ConfigVal<T>::NotifySubscribers(T aVal)
{
    ASSERT(iWriteObserverId != 0);
    KeyValuePair<T> kvp(iKey, aVal);
    AutoMutex a(iObserverLock);
    typename Map::iterator it;
    for (it = iObservers.begin(); it != iObservers.end(); it++) {
        it->second(kvp);
    }
}

template <class T> void ConfigVal<T>::AddInitialSubscribers()
{
    // Don't write initial val out at startup.
    // - If it already exists in store, no need to write it out.
    // - If it doesn't exist in store, it will be the default val regardless of
    //   whether it is ever written to store - only write to store on
    //   subsequent changes.
    ASSERT(iWriteObserverId == 0);
    iWriteObserverId = SubscribeNoCallback(MakeFunctorObserver<T>(*this, &ConfigVal::Write));
}

/*
 * Class representing a numerical value, which can be positive or negative,
 * with upper and lower limits.
 */
class ConfigNum : public ConfigVal<TInt>
{
    friend class SuiteConfigManager;
public:
    typedef FunctorGeneric<KeyValuePair<TInt>&> FunctorConfigNum;
    typedef KeyValuePair<TInt> KvpNum;
public:
    ConfigNum(IConfigInitialiser& aManager, const Brx& aKey,
              TInt aMin, TInt aMax, TInt aDefault,
              TBool aRebootRequired = false,
              ConfigValAccess aAccess = ConfigValAccess::Public);
    ~ConfigNum();
    TInt Min() const;
    TInt Max() const;
    void Set(TInt aVal);    // THROWS ConfigValueOutOfRange
private:
    TBool IsValid(TInt aVal) const;
public: // from ConfigVal
    TInt Default() const override;
    TUint Subscribe(FunctorConfigNum aFunctor) override;
    void Serialise(IWriter& aWriter) const override;
    void Deserialise(const Brx& aString) override;   // THROWS ConfigNotANumber, ConfigValueOutOfRange
private: // from ConfigVal
    void Write(KvpNum& aKvp) override;
private:
    inline TBool operator==(const ConfigNum& aNum) const;
private:
    static const TUint kMaxNumLength = 11;
    TInt iMin;
    TInt iMax;
    const TInt iDefault;
    TInt iVal;
    mutable Mutex iMutex;
};

inline TBool ConfigNum::operator==(const ConfigNum& aNum) const
{
    AutoMutex a(iMutex);
    return iMin == aNum.iMin
        && iVal == aNum.iVal
        && iMax == aNum.iMax;
}

/*
 * Helper function for creating a FunctorConfigNum.
 */
template<class Object, class CallType>
inline MemberTranslatorGeneric<KeyValuePair<TInt>&,Object,void (CallType::*)(KeyValuePair<TInt>&)>
    MakeFunctorConfigNum(Object& aC, void(CallType::* const &aF)(KeyValuePair<TInt>&))
{
    typedef void(CallType::*MemFunc)(KeyValuePair<TInt>&);
    return MemberTranslatorGeneric<KeyValuePair<TInt>&,Object,MemFunc>(aC,aF);
}

/**
 * Write() is called for each value the mapper is aware of.
 */
class IConfigChoiceMappingWriter
{
public:
    virtual void Write(IWriter& aWriter, TUint aChoice, const Brx& aMapping) = 0; // THROWS WriterError
    virtual void WriteComplete(IWriter& aWriter) = 0; // THROWS WriterError
    virtual ~IConfigChoiceMappingWriter() {}
};

class IConfigChoiceMapper
{
public:
    virtual void Write(IWriter& aWriter, IConfigChoiceMappingWriter& aMappingWriter) = 0; // THROWS WriterError
    virtual ~IConfigChoiceMapper() {}
};

/*
 * Class representing a multiple choice value (such as true/false, on/off,
 * monkey/chicken/meerkat, etc.)
 *
 * Empty when created. When first choice value is added, defaults to that value
 * as the selected one.
 */
class ConfigChoice : public ConfigVal<TUint>
{
    friend class SuiteConfigManager;
public:
    typedef FunctorGeneric<KeyValuePair<TUint>&> FunctorConfigChoice;
    typedef KeyValuePair<TUint> KvpChoice;
public:
    ConfigChoice(IConfigInitialiser& aManager, const Brx& aKey,
                 const std::vector<TUint>& aChoices, TUint aDefault,
                 TBool aRebootRequired = false,
                 ConfigValAccess aAccess = ConfigValAccess::Public);
    ConfigChoice(IConfigInitialiser& aManager, const Brx& aKey,
                 const std::vector<TUint>& aChoices, TUint aDefault,
                 IConfigChoiceMapper& aMapper,
                 TBool aRebootRequired = false,
                 ConfigValAccess aAccess = ConfigValAccess::Public);
    ~ConfigChoice();
    const std::vector<TUint>& Choices() const;
    void Set(TUint aVal);   // THROWS ConfigInvalidSelection
    TBool HasInternalMapping() const;
    IConfigChoiceMapper& Mapper() const;
protected:
    // use ConfigChoiceDynamic if you want to create a ConfigChoice parameter with a dynamic choice list
    ConfigChoice(IConfigInitialiser& aManager, const Brx& aKey,
                 TBool aChoicesAreDynamic, const std::vector<TUint>& aChoices,
                 TUint aDefault, TBool aRebootRequired = false,
                 ConfigValAccess aAccess = ConfigValAccess::Public);
    // use ConfigChoiceDynamic if you want to create a ConfigChoice parameter with a dynamic choice list
    ConfigChoice(IConfigInitialiser& aManager, const Brx& aKey,
                 TBool aChoicesAreDynamic, const std::vector<TUint>& aChoices, 
                 TUint aDefault, IConfigChoiceMapper& aMapper,
                 TBool aRebootRequired = false,
                 ConfigValAccess aAccess = ConfigValAccess::Public);
private:
    void Init();
    TBool IsValid(TUint aVal) const;
public: // from ConfigVal
    TUint Default() const override;
    TUint Subscribe(FunctorConfigChoice aFunctor) override;
    void Serialise(IWriter& aWriter) const override;
    void Deserialise(const Brx& aString) override;   // THROWS ConfigNotANumber, ConfigInvalidSelection
private: // from ConfigVal
    void Write(KvpChoice& aKvp) override;
private:
    inline TBool operator==(const ConfigChoice& aChoice) const;
private:
    static const TUint kMaxChoiceLength = 10;
    std::vector<TUint> iChoices;
    const TUint iDefault;
    TUint iSelected;
    IConfigChoiceMapper* iMapper;
    mutable Mutex iMutex;
    TBool iChoicesAreDynamic;
};

inline TBool ConfigChoice::operator==(const ConfigChoice& aChoice) const
{
    TBool choicesEqual = true;
    for (TUint i=0; i<iChoices.size(); i++) {
        if (iChoices[i] != aChoice.iChoices[i]) {
            choicesEqual = false;
            break;
        }
    }
    AutoMutex a(iMutex);
    return choicesEqual && (iSelected == aChoice.iSelected);
}

/*
 * Identical to Config choice except that the initial value may be invalid
 * as the choice list is dynamic. If this is the case the device will not
 * assert but the store value will be replaced with the valid default value
 */
class ConfigChoiceDynamic : public ConfigChoice
{
public:
    ConfigChoiceDynamic(IConfigInitialiser& aManager, const Brx& aKey,
                 const std::vector<TUint>& aChoices, TUint aDefault,
                 TBool aRebootRequired = false,
                 ConfigValAccess aAccess = ConfigValAccess::Public);
    ConfigChoiceDynamic(IConfigInitialiser& aManager, const Brx& aKey,
                 const std::vector<TUint>& aChoices, TUint aDefault,
                 IConfigChoiceMapper& aMapper,
                 TBool aRebootRequired = false,
                 ConfigValAccess aAccess = ConfigValAccess::Public);
};

/*
 * Helper function for creating a FunctorConfigChoice.
 */
template<class Object, class CallType>
inline MemberTranslatorGeneric<KeyValuePair<TUint>&,Object,void (CallType::*)(KeyValuePair<TUint>&)>
    MakeFunctorConfigChoice(Object& aC, void(CallType::* const &aF)(KeyValuePair<TUint>&))
{
    typedef void(CallType::*MemFunc)(KeyValuePair<TUint>&);
    return MemberTranslatorGeneric<KeyValuePair<TUint>&,Object,MemFunc>(aC,aF);
}

/*
 * Class representing a text value. Length of text that can be allocated is
 * fixed at construction.
 */
class ConfigTextBase : public ConfigVal<const Brx&>
{
public:
    static const TUint kMaxBytes = 512;
public:
    typedef FunctorGeneric<KeyValuePair<const Brx&>&> FunctorConfigText;
    typedef KeyValuePair<const Brx&> KvpText;
protected:
    ConfigTextBase(IConfigInitialiser& aManager, const Brx& aKey, TUint aMinLength,
                   TUint aMaxLength, const Brx& aDefault, TBool aRebootRequired,
                   ConfigValAccess aAccess);
    ~ConfigTextBase();
    TUint MinLengthInternal() const;
    TUint MaxLengthInternal() const;
    void SetInternal(const Brx& aText); // THROWS ConfigValueTooShort, ConfigValueTooLong
public: // from ConfigVal
    const Brx& Default() const override;
    TUint Subscribe(FunctorConfigText aFunctor) override;
    void Serialise(IWriter& aWriter) const override;
    virtual void Deserialise(const Brx& aString) override = 0;
private: // from ConfigVal
    void Write(KvpText& aKvp) override;
protected:
    inline TBool operator==(const ConfigTextBase& aText) const;
private:
    const TUint iMinLength;
    const Bwh iDefault;
    Bwh iText;
    mutable Mutex iMutex;
};

inline TBool ConfigTextBase::operator==(const ConfigTextBase& aText) const
{
    AutoMutex a(iMutex);
    return iText == aText.iText
        && iMinLength == aText.iMinLength
        && iDefault == aText.iDefault;
}

/*
 * Helper function for creating a FunctorConfigText.
 */
template<class Object, class CallType>
inline MemberTranslatorGeneric<KeyValuePair<const Brx&>&,Object,void (CallType::*)(KeyValuePair<const Brx&>&)>
    MakeFunctorConfigText(Object& aC, void(CallType::* const &aF)(KeyValuePair<const Brx&>&))
{
    typedef void(CallType::*MemFunc)(KeyValuePair<const Brx&>&);
    return MemberTranslatorGeneric<KeyValuePair<const Brx&>&,Object,MemFunc>(aC,aF);
}

/*
 * Class representing a text value. Length of text that can be allocated is
 * fixed at construction.
 */
class ConfigText : public ConfigTextBase
{
    friend class SuiteConfigManager;
public:
    ConfigText(IConfigInitialiser& aManager, const Brx& aKey, TUint aMinLength,
               TUint aMaxLength, const Brx& aDefault, TBool aRebootRequired = false,
               ConfigValAccess aAccess = ConfigValAccess::Public);
    ~ConfigText();
    TUint MinLength() const;
    TUint MaxLength() const;
    void Set(const Brx& aText); // THROWS ConfigValueTooShort, ConfigValueTooLong
public: // from ConfigVal
    void Deserialise(const Brx& aString) override; // THROWS ConfigValueTooShort, ConfigValueTooLong
private:
    inline TBool operator==(const ConfigText& aText) const;
};

inline TBool ConfigText::operator==(const ConfigText& aText) const
{
    return ConfigTextBase::operator==(aText);
}

class IConfigTextChoicesVisitor
{
public:
    virtual ~IConfigTextChoicesVisitor() {}
    virtual void VisitConfigTextChoice(const Brx& aId) = 0;
};

class IConfigTextChoices
{
public:
    virtual ~IConfigTextChoices() {}
    virtual void AcceptChoicesVisitor(IConfigTextChoicesVisitor& aVisitor) = 0;
    virtual TBool IsValid(const Brx& aBuf) const = 0;
};

/*
 * Current implementation expects choices to remain static.
 *
 * If it is valid for this to have an empty string (i.e., no value) set, or
 * some kind of sentinel "none" value, that should be included in the set of
 * choices, making this analogous with ConfigChoice (where, e.g., OFF, is
 * provided as one of the choices).
 *
 * A future string-type where it is valid to set an empty string could include
 * a meta "optional" boolean field, instead of explicitly including the "none"
 * value in its choice list.
 */
class ConfigTextChoice : public ConfigTextBase
{
    friend class SuiteConfigManager;
public:
    ConfigTextChoice(IConfigInitialiser& aManager, const Brx& aKey,
                     IConfigTextChoices& aChoices, TUint aMinLength,
                     TUint aMaxLength, const Brx& aDefault,
                     TBool aRebootRequired = false,
                     ConfigValAccess aAccess = ConfigValAccess::Public);
    ~ConfigTextChoice();
    void AcceptChoicesVisitor(IConfigTextChoicesVisitor& aVisitor);
    void Set(const Brx& aText); // THROWS ConfigInvalidSelection (and NOT ConfigValueTooShort, ConfigValueTooLong as ConfigText does, as this class does not accept free-form text; only values from the list of current values (which may change dynamically)).
public: // from ConfigVal
    void Deserialise(const Brx& aString) override; // THROWS ConfigInvalidSelection
private:
    IConfigTextChoices& iChoices;
};

/*
 * Interface for adding values to a configuration manager.
 * Should only ever be used by owners of ConfigVal items and the class
 * responsible for Open()ing the config manager once all values have been
 * added.
 *
 * Calling Open() ensures uniqueness of keys from that point on. If an attempt
 * is made to add a duplicate key at startup, before Open() is called, an
 * implementer of this should throw ConfigKeyExists. (And any attempt to create
 * a ConfigVal after Open() has been called should also cause an ASSERT.)
 */
class IConfigInitialiser
{
public:
    //static const TUint kMaxKeyLength = 100;
public:
    virtual IStoreReadWrite& Store() = 0;
    virtual void Open() = 0;
    virtual void Add(ConfigNum& aNum) = 0;
    virtual void Add(ConfigChoice& aChoice) = 0;
    virtual void Add(ConfigText& aText) = 0;
    virtual void Add(ConfigTextChoice& aTextChoice) = 0;
    virtual void Remove(ConfigNum& aNum) = 0;
    virtual void Remove(ConfigChoice& aChoice) = 0;
    virtual void Remove(ConfigText& aText) = 0;
    virtual void Remove(ConfigTextChoice& aTextChoice) = 0;
    virtual void FromStore(const Brx& aKey, Bwx& aDest, const Brx& aDefault) = 0;
    virtual void ToStore(const Brx& aKey, const Brx& aValue) = 0;
    virtual ~IConfigInitialiser() {}
};

/*
 * Helper class for ConfigManager.
 */
template <class T>
class SerialisedMap
{
private:
    typedef std::map<const Brx*, T*, BufferPtrCmp> Map;
public:
    typedef typename Map::const_iterator Iterator;
public:
    SerialisedMap();
    ~SerialisedMap();
    void Add(const Brx& aKey, T& aVal);
    TBool Has(const Brx& aKey) const;
    TBool TryRemove(const Brx& aKey);
    T& Get(const Brx& aKey) const;
    Iterator Begin() const;
    Iterator End() const;
private:
    Map iMap;
    mutable Mutex iLock;
};

// SerialisedMap
template <class T> SerialisedMap<T>::SerialisedMap()
    : iLock("SLML")
{
}

template <class T> SerialisedMap<T>::~SerialisedMap()
{
    // Delete all keys.
    AutoMutex a(iLock);
    typename Map::iterator it;
    for (it = iMap.begin(); it != iMap.end(); ++it) {
        delete (*it).first;
    }
}

template <class T> void SerialisedMap<T>::Add(const Brx& aKey, T& aVal)
{
    Brh* key = new Brh(aKey);
    AutoMutex a(iLock);
    typename Map::iterator it = iMap.find(key);
    if (it != iMap.end()) {
        THROW(ConfigKeyExists);
    }
    iMap.insert(std::pair<const Brx*, T*>(key, &aVal));
}

template <class T> TBool SerialisedMap<T>::Has(const Brx& aKey) const
{
    TBool found = false;
    Brn key(aKey);
    AutoMutex a(iLock);
    typename Map::const_iterator it = iMap.find(&key);
    if (it != iMap.end()) {
        found = true;
    }

    return found;
}

template <class T> TBool SerialisedMap<T>::TryRemove(const Brx& aKey)
{
    TBool found = false;
    Brn key(aKey);
    AutoMutex a(iLock);
    typename Map::const_iterator it = iMap.find(&key);
    if (it != iMap.end()) {
        delete it->first;
        iMap.erase(it);
        found = true;
    }

    return found;
}

template <class T> T& SerialisedMap<T>::Get(const Brx& aKey) const
{
    Brn key(aKey);
    AutoMutex a(iLock);
    typename Map::const_iterator it = iMap.find(&key);
    if (it == iMap.end()) {
        Log::Print("SerialisedMap: no element with key %.*s\n", PBUF(aKey));
        ASSERTS();  // value with ID of aKey does not exist
    }

    return *(it->second);
}

template <class T> typename SerialisedMap<T>::Iterator SerialisedMap<T>::Begin() const
{
    return iMap.cbegin();
}

template <class T> typename SerialisedMap<T>::Iterator SerialisedMap<T>::End() const
{
    return iMap.cend();
}

/**
 * Class implementing IWriter that writes all values using Log::Print().
 */
class WriterPrinter : public IWriter
{
public: // from IWriter
    void Write(TByte aValue) override;
    void Write(const Brx& aBuffer) override;
    void WriteFlush() override;
};

class IConfigObserver
{
public:
    virtual void Added(ConfigNum& aVal) = 0;
    virtual void Added(ConfigChoice& aVal) = 0;
    virtual void Added(ConfigText& aVal) = 0;
    virtual void Added(ConfigTextChoice& aVal) = 0;
    virtual void AddsComplete() = 0;
    virtual void Removed(ConfigNum& aVal) = 0;
    virtual void Removed(ConfigChoice& aVal) = 0;
    virtual void Removed(ConfigText& aVal) = 0;
    virtual void Removed(ConfigTextChoice& aVal) = 0;
    virtual~IConfigObserver() {}
};

class IConfigObservable
{
public:
    virtual void Add(IConfigObserver& aObserver) = 0;
    virtual void Remove(IConfigObserver& aObserver) = 0;
    virtual ~IConfigObservable() {}
};

/*
 * Class storing a collection of ConfigVals. Values are stored with, and
 * retrievable via, an ID of form "some.value.identifier". Classes that create
 * ConfigVals own them and are responsible for their destruction.
 *
 * Known identifiers are listed elsewhere.
 */
class ConfigManager : public IConfigManager
                    , public IConfigInitialiser
                    , public IConfigObservable
                    , private INonCopyable
{
    friend class SuiteVolumeConfig;
private:
    typedef SerialisedMap<ConfigNum> ConfigNumMap;
    typedef SerialisedMap<ConfigChoice> ConfigChoiceMap;
    typedef SerialisedMap<ConfigText> ConfigTextMap;
    typedef SerialisedMap<ConfigTextChoice> ConfigTextChoiceMap;
public:
    ConfigManager(IStoreReadWrite& aStore);
public: // from IConfigManager
    void WriteKeys(IKeyWriter& aWriter) const override;
    TBool HasNum(const Brx& aKey) const override;
    ConfigNum& GetNum(const Brx& aKey) const override;
    TBool HasChoice(const Brx& aKey) const override;
    ConfigChoice& GetChoice(const Brx& aKey) const override;
    TBool HasText(const Brx& aKey) const override;
    ConfigText& GetText(const Brx& aKey) const override;
    TBool HasTextChoice(const Brx& aKey) const override;
    ConfigTextChoice& GetTextChoice(const Brx& aKey) const override;
    TBool Has(const Brx& aKey) const override;
    ConfigValAccess Access(const Brx& aKey) const override;
    ISerialisable& Get(const Brx& aKey) const override;
    void Print() const override;
    void DumpToStore() override;
public: // from IConfigInitialiser
    IStoreReadWrite& Store() override;
    void Open() override;
    void Add(ConfigNum& aNum) override;
    void Add(ConfigChoice& aChoice) override;
    void Add(ConfigText& aText) override;
    void Add(ConfigTextChoice& aTextChoice) override;
    void Remove(ConfigNum& aNum) override;
    void Remove(ConfigChoice& aChoice) override;
    void Remove(ConfigText& aText) override;
    void Remove(ConfigTextChoice& aTextChoice) override;
    void FromStore(const Brx& aKey, Bwx& aDest, const Brx& aDefault) override;
    void ToStore(const Brx& aKey, const Brx& aValue) override;
private: // from IConfigObservable
    void Add(IConfigObserver& aObserver) override;
    void Remove(IConfigObserver& aObserver) override;
private:
    void AddNum(const Brx& aKey, ConfigNum& aNum);
    void AddChoice(const Brx& aKey, ConfigChoice& aChoice);
    void AddText(const Brx& aKey, ConfigText& aText);
    void AddTextChoice(const Brx& aKey, ConfigTextChoice& aTextChoice);
private:
    template <class T> void Add(SerialisedMap<T>& aMap, const Brx& aKey, T& aVal);
    template <class T> void Print(const ConfigVal<T>& aVal) const;
    template <class T> void Print(const SerialisedMap<T>& aMap) const;
private:
    class StoreDumper : private INonCopyable
    {
    public:
        StoreDumper(IConfigInitialiser& aConfigInit);
        void DumpToStore(const SerialisedMap<ConfigNum>& aMap);
        void DumpToStore(const SerialisedMap<ConfigChoice>& aMap);
        void DumpToStore(const SerialisedMap<ConfigText>& aMap);
        void DumpToStore(const SerialisedMap<ConfigTextChoice>& aMap);
    private:
        void NotifyChangedNum(ConfigNum::KvpNum& aKvp);
        void NotifyChangedChoice(ConfigChoice::KvpChoice& aKvp);
        void NotifyChangedText(ConfigText::KvpText& aKvp);
        void NotifyChangedTextChoice(ConfigTextChoice::KvpText& aKvp);
    private:
        IConfigInitialiser& iConfigInit;
    };
private:
    IStoreReadWrite& iStore;
    ConfigNumMap iMapNum;
    ConfigChoiceMap iMapChoice;
    ConfigTextMap iMapText;
    ConfigTextChoiceMap iMapTextChoice;
    std::vector<const Brx*> iKeyListOrdered;
    TBool iOpen;
    mutable Mutex iLock;
    IConfigObserver* iObserver;
};

} // namespace Configuration
} // namespace OpenHome

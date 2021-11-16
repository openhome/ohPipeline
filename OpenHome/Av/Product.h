#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Private/Network.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Net/Core/DvDevice.h>
#include <OpenHome/Configuration/BufferPtrCmp.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/PowerManager.h>
#include <OpenHome/Av/Source.h>
#include <OpenHome/Av/TransportControl.h>

#include <atomic>
#include <limits>
#include <map>
#include <vector>

EXCEPTION(AvSourceNotFound);

namespace OpenHome {
    class IThreadPool;
    class IThreadPoolHandle;

namespace Av {

class IReadStore;
class ISource;
class ProviderProduct;

class IProduct
{
public:
    virtual ~IProduct() {}
    /*
     * Must only activate the given source if it is not already active.
     *
     * If given source is already active, should do nothing.
     */
    virtual void ActivateIfNotActive(ISource& aSource, TBool aPrefetchAllowed) = 0;
    virtual void NotifySourceChanged(ISource& aSource) = 0;
};

class IProductNameObserver
{
public:
    virtual void RoomChanged(const Brx& aRoom) = 0;
    virtual void NameChanged(const Brx& aName) = 0;
    virtual ~IProductNameObserver() {}
};

class IProductNameObservable
{
public:
    virtual void AddNameObserver(IProductNameObserver& aObserver) = 0;
    virtual ~IProductNameObservable() {}
};

class IProductAttributesObserver
{
public:
    virtual ~IProductAttributesObserver() {}
    virtual void AttributesChanged() = 0;
};

class IProductObserver
{
public:
    virtual ~IProductObserver() {}
    virtual void Started() = 0;
    virtual void SourceIndexChanged() = 0;
    virtual void SourceXmlChanged() = 0;
    virtual void ProductUrisChanged() = 0; // only useful while we're limited to a single adapter
};

class ConfigStartupSource : private INonCopyable
{
public:
    static const Brn kKeySource;
    static const Brn kLastUsed;
public:
    ConfigStartupSource(Configuration::IConfigInitialiser& aConfigInit);
    ~ConfigStartupSource();
private:
    Configuration::ConfigText* iSourceStartup;
};

class Product : private IProduct
              , public IProductNameObservable
              , public ITransportActivator
              , private IStandbyHandler
              , private INonCopyable
{
private:
    static const Brn kKeyLastSelectedSource;
    static const TUint kCurrentSourceNone;
    static const TBool kPrefetchAllowedDefault = true;
    static const TUint kAttributeGranularityBytes = 128;
public:
    static const Brn kConfigIdRoomBase;
    static const Brn kConfigIdNameBase;
    static const Brn kConfigIdAutoPlay;
    static const TUint kAutoPlayDisable = 0;
    static const TUint kAutoPlayEnable = 1;
    static const TUint kMinNameBytes = 1;
    static const TUint kMaxNameBytes = 20;
    static const TUint kMinRoomBytes = 1;
    static const TUint kMaxRoomBytes = 40;
    static const TUint kMaxUriBytes = 128;
public:
    Product(Environment& aEnv, Net::DvDeviceStandard& aDevice, IReadStore& aReadStore,
            Configuration::IStoreReadWrite& aReadWriteStore, Configuration::IConfigManager& aConfigReader,
            Configuration::IConfigInitialiser& aConfigInit, IPowerManager& aPowerManager);
    ~Product();
    void AddObserver(IProductObserver& aObserver);
    void AddAttributesObserver(IProductAttributesObserver& aObserver);
    void Start();
    void Stop();
    void AddSource(ISource* aSource);
    void AddAttribute(const TChar* aAttribute);
    void AddAttribute(const Brx& aAttribute);
    void SetConfigAppUrl(const Brx& aUrl);
    /*
     * Set attributes that are reported in the form "aKey=aValue".
     *
     * If aKey has not previously been set, it will be created.
     *
     * If aKey has previously been set, it will be updated.
     */
    void SetAttribute(const Brx& aKey, const Brx& aValue);
    void GetManufacturerDetails(Brn& aName, Brn& aInfo, Bwx& aUrl, Bwx& aImageUri);
    void GetModelDetails(Brn& aName, Brn& aInfo, Bwx& aUrl, Bwx& aImageUri);
    void GetProductDetails(Bwx& aRoom, Bwx& aName, Brn& aInfo, Bwx& aImageUri);
    TUint SourceCount() const;
    TUint CurrentSourceIndex() const;
    void GetSourceXml(IWriter& aWriter);
    void SetCurrentSource(TUint aIndex);
    void SetCurrentSourceBySystemName(const Brx& aSystemName);
    void SetCurrentSourceByName(const Brx& aName);
    void GetSourceDetails(TUint aIndex, Bwx& aSystemName, Bwx& aType, Bwx& aName, TBool& aVisible) const;
    void GetSourceDetails(const Brx& aSystemName, Bwx& aType, Bwx& aName, TBool& aVisible) const;
    void GetAttributes(IWriter& aWriter) const;
    TUint SourceXmlChangeCount();
private:
    TBool DoSetCurrentSourceLocked(TUint aIndex, TBool aReActivateIfNoSourceChange);
    TBool DoSetCurrentSource(TUint aIndex, TBool aReActivateIfNoSourceChange);
    TBool DoSetCurrentSource(const Brx& aSystemName, TBool aReActivateIfNoSourceChange);
    void AppendTag(IWriter& aWriter, const TChar* aTag, const Brx& aValue);
    void GetConfigText(const Brx& aId, Bwx& aDest, const Brx& aDefault);
    void ProductRoomChanged(Configuration::KeyValuePair<const Brx&>& aKvp);
    void ProductNameChanged(Configuration::KeyValuePair<const Brx&>& aKvp);
    void StartupSourceChanged(Configuration::KeyValuePair<const Brx&>& aKvp);
    void AutoPlayChanged(Configuration::KeyValuePair<TUint>& aKvp);
    void CurrentAdapterChanged();
    void GetUri(const Brx& aStaticDataKey, Bwx& aUri);
    void StandbyDisableNoSourceSwitch();
private: // from IProduct
    void ActivateIfNotActive(ISource& aSource, TBool aPrefetchAllowed) override;
    void NotifySourceChanged(ISource& aSource) override;
public: // from IProductNameObservable
    void AddNameObserver(IProductNameObserver& aObserver) override;
public: // from ITransportActivator
    TBool TryActivate(const Brx& aMode) override;
private: // from IStandbyHandler
    void StandbyEnabled() override;
    void StandbyTransitioning() override;
    void StandbyDisabled(StandbyDisableReason aReason) override;
private:
    Environment& iEnv;
    Net::DvDeviceStandard& iDevice; // do we need to store this?
    IReadStore& iReadStore;
    Configuration::IConfigManager& iConfigReader;
    Configuration::IConfigInitialiser& iConfigInit;
    IPowerManager& iPowerManager;
    mutable Mutex iLock;
    Mutex iLockDetails;
    ProviderProduct* iProviderProduct;
    IStandbyObserver* iStandbyObserver;
    std::vector<IProductObserver*> iObservers;
    std::vector<IProductNameObserver*> iNameObservers;
    std::vector<IProductAttributesObserver*> iAttributeObservers;
    std::vector<ISource*> iSources;
    WriterBwh iAttributes;
    Endpoint::AddressBuf iConfigAppAddress;
    Bws<256> iConfigAppUrlTail;
    std::atomic<TBool> iStarted;
    TBool iStandby;
    TBool iAutoPlay;
    StoreText* iLastSelectedSource;
    TUint iCurrentSource;
    TUint iSourceXmlChangeCount; // FIXME - isn't updated when source names/visibility change
    Configuration::ConfigText* iConfigProductRoom;
    Configuration::ConfigText* iConfigProductName;
    Bws<kMaxRoomBytes> iProductRoom;
    TUint iListenerIdProductRoom;
    Bws<kMaxNameBytes> iProductName;
    TUint iListenerIdProductName;
    Configuration::ConfigText* iConfigStartupSource;
    TUint iListenerIdStartupSource;
    Bws<ISource::kMaxSystemNameBytes> iStartupSourceVal;
    Configuration::ConfigChoice* iConfigAutoPlay;
    TUint iListenerIdAutoPlay;
    TUint iAdapterChangeListenerId;
    Brh iUriPrefix;
    mutable Mutex iObserverLock;
    std::map<const Brx*, std::unique_ptr<Bwh>, Configuration::BufferPtrCmp> iAttributesMap;
};

class IFriendlyNameObservable
{
public:
    static const TUint kMaxFriendlyNameBytes = Product::kMaxRoomBytes + 1 + Product::kMaxNameBytes;
    static const TUint kIdInvalid = 0;
public:
    virtual TUint RegisterFriendlyNameObserver(FunctorGeneric<const Brx&> aObserver) = 0;
    virtual void DeregisterFriendlyNameObserver(TUint aId) = 0;
    virtual ~IFriendlyNameObservable() {}
};

class FriendlyNameManager : public IFriendlyNameObservable, public IProductNameObserver
{
public:
    FriendlyNameManager(const Brx& aPrefix, IProductNameObservable& aProduct, IThreadPool& aThreadPool);
    FriendlyNameManager(const Brx& aPrefix, IProductNameObservable& aProduct, IThreadPool& aThreadPool, const Brx& aSuffix);
    ~FriendlyNameManager();
private: // from IFriendlyNameObservable
    TUint RegisterFriendlyNameObserver(FunctorGeneric<const Brx&> aObserver) override;
    void DeregisterFriendlyNameObserver(TUint aId) override;
private: // from IProductNameObserver
    void RoomChanged(const Brx& aRoom) override;
    void NameChanged(const Brx& aName) override;
private:
    void UpdateDetails();
    void ConstructFriendlyNameLocked();
    void NotifyObservers();
private:
    Brh iPrefix;
    Brh iSuffix;
    Bws<Product::kMaxRoomBytes> iRoom;
    Bws<kMaxFriendlyNameBytes> iFriendlyName;
    TUint iNextObserverId;
    std::map<TUint, FunctorGeneric<const Brx&>> iObservers;
    Mutex iMutex;
    IThreadPoolHandle* iThreadPoolHandle;
    std::atomic<TBool> iStarted;
};

} // namespace Av
} // namespace OpenHome

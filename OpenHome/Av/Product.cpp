#include <OpenHome/Av/Product.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Av/ProviderProduct.h>
#include <OpenHome/Av/Source.h>
#include <OpenHome/Av/KvpStore.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Printer.h>
#include <OpenHome/Private/NetworkAdapterList.h>
#include <OpenHome/Net/Core/OhNet.h>
#include <OpenHome/Optional.h>
#include <OpenHome/Av/TransportControl.h>
#include <OpenHome/ThreadPool.h>

using namespace OpenHome;
using namespace OpenHome::Net;
using namespace OpenHome::Av;
using namespace OpenHome::Configuration;


// ConfigStartupSource

const Brn ConfigStartupSource::kKeySource("Source.StartupName");
const Brn ConfigStartupSource::kLastUsed("Last Used");

ConfigStartupSource::ConfigStartupSource(Configuration::IConfigInitialiser& aConfigInit)
{
    iSourceStartup = new ConfigText(aConfigInit, kKeySource, Product::kMinNameBytes, Product::kMaxNameBytes, kLastUsed);
}

ConfigStartupSource::~ConfigStartupSource()
{
    delete iSourceStartup;
}


// Product

const Brn Product::kKeyLastSelectedSource("Last.Source");
const TUint Product::kCurrentSourceNone = std::numeric_limits<TUint>::max();
const TBool Product::kPrefetchAllowedDefault;
const TUint Product::kAttributeGranularityBytes;
const Brn Product::kConfigIdRoomBase("Product.Room");
const Brn Product::kConfigIdNameBase("Product.Name");
const Brn Product::kConfigIdAutoPlay("Device.AutoPlay");
const TUint Product::kAutoPlayDisable;
const TUint Product::kAutoPlayEnable;
const TUint Product::kMinNameBytes;
const TUint Product::kMaxNameBytes;
const TUint Product::kMinRoomBytes;
const TUint Product::kMaxRoomBytes;
const TUint Product::kMaxUriBytes;

Product::Product(Environment& aEnv, Net::DvDeviceStandard& aDevice, IReadStore& aReadStore,
                 Configuration::IStoreReadWrite& aReadWriteStore, Configuration::IConfigManager& aConfigReader,
                 Configuration::IConfigInitialiser& aConfigInit, IPowerManager& aPowerManager)
    : iEnv(aEnv)
    , iDevice(aDevice)
    , iReadStore(aReadStore)
    , iConfigReader(aConfigReader)
    , iConfigInit(aConfigInit)
    , iPowerManager(aPowerManager)
    , iLock("PRDM")
    , iLockDetails("PRDD")
    , iAttributes(kAttributeGranularityBytes)
    , iStarted(false)
    , iStandby(true)
    , iAutoPlay(false)
    , iCurrentSource(kCurrentSourceNone)
    , iSourceXmlChangeCount(0)
    , iConfigStartupSource(nullptr)
    , iListenerIdStartupSource(IConfigManager::kSubscriptionIdInvalid)
    , iStartupSourceVal(ConfigStartupSource::kLastUsed)
    , iConfigAutoPlay(nullptr)
    , iListenerIdAutoPlay(IConfigManager::kSubscriptionIdInvalid)
    , iAdapterChangeListenerId(NetworkAdapterList::kListenerIdNull)
    , iObserverLock("PRDM2")
{
    iStandbyObserver = aPowerManager.RegisterStandbyHandler(*this, kStandbyHandlerPriorityLowest, "Product");
    iLastSelectedSource = new StoreText(aReadWriteStore, aPowerManager, kPowerPriorityHighest, kKeyLastSelectedSource, Brx::Empty(), ISource::kMaxSourceTypeBytes);
    iConfigProductRoom = &aConfigReader.GetText(kConfigIdRoomBase);
    iListenerIdProductRoom = iConfigProductRoom->Subscribe(MakeFunctorConfigText(*this, &Product::ProductRoomChanged));
    iConfigProductName = &aConfigReader.GetText(kConfigIdNameBase);
    iListenerIdProductName = iConfigProductName->Subscribe(MakeFunctorConfigText(*this, &Product::ProductNameChanged));
    if (aConfigReader.HasChoice(kConfigIdAutoPlay)) {
        iConfigAutoPlay = &aConfigReader.GetChoice(kConfigIdAutoPlay);
        iListenerIdAutoPlay = iConfigAutoPlay->Subscribe(MakeFunctorConfigChoice(*this, &Product::AutoPlayChanged));
    }
    iProviderProduct = new ProviderProduct(aDevice, *this, aPowerManager);
}

Product::~Product()
{
    iEnv.NetworkAdapterList().RemoveCurrentChangeListener(iAdapterChangeListenerId);
    delete iStandbyObserver;
    iConfigStartupSource->Unsubscribe(iListenerIdStartupSource);
    iConfigStartupSource = nullptr; // Didn't have ownership.
    for (TUint i=0; i<(TUint)iSources.size(); i++) {
        delete iSources[i];
    }
    iSources.clear();
    delete iProviderProduct;
    iConfigProductName->Unsubscribe(iListenerIdProductName);
    iConfigProductRoom->Unsubscribe(iListenerIdProductRoom);
    if (iConfigAutoPlay != nullptr) {
        iConfigAutoPlay->Unsubscribe(iListenerIdAutoPlay);
    }
    delete iLastSelectedSource;
}

void Product::AddObserver(IProductObserver& aObserver)
{
    AutoMutex amx(iObserverLock);
    iObservers.push_back(&aObserver);
}

void Product::AddAttributesObserver(IProductAttributesObserver& aObserver)
{
    AutoMutex amx(iObserverLock);
    iAttributeObservers.push_back(&aObserver);
}

void Product::Start()
{
    // All sources must have been registered; construct startup source config val.
    iConfigStartupSource = &iConfigReader.GetText(ConfigStartupSource::kKeySource);
    iListenerIdStartupSource = iConfigStartupSource->Subscribe(MakeFunctorConfigText(*this, &Product::StartupSourceChanged));

    iLock.Wait();
    const Bws<ISource::kMaxSystemNameBytes> startupSourceVal(iStartupSourceVal);
    iAdapterChangeListenerId = iEnv.NetworkAdapterList().AddCurrentChangeListener(MakeFunctor(*this, &Product::CurrentAdapterChanged), "OpenHome::Av::Product", false);
    iLock.Signal();
    CurrentAdapterChanged(); // NetworkAdapterList doesn't run callbacks on registration

    TBool sourceSelected = false;
    if (startupSourceVal != ConfigStartupSource::kLastUsed) {
        try {
            (void)DoSetCurrentSource(startupSourceVal, false);
            sourceSelected = true;
        }
        catch (AvSourceNotFound&) {
            // Invalid content in iStartupSourceVal.
        }
    }

    if (!sourceSelected) { // No startup source selected; use last selected source.
        Bws<ISource::kMaxSystemNameBytes> startupSource;
        iLastSelectedSource->Get(startupSource);
        if (startupSource == Brx::Empty()) {
            // If there is no stored startup source, select the first added source.
            (void)DoSetCurrentSource(0, false);
        }
        else {
            try {
                (void)DoSetCurrentSource(startupSource, false);
            }
            catch (AvSourceNotFound&) {
                (void)DoSetCurrentSource(0, false);
                iLastSelectedSource->Set(Brx::Empty());
            }
        }
    }

    iStarted = true;
    iSourceXmlChangeCount++;
    {
        AutoMutex amx(iObserverLock);
        for (auto observer : iObservers) {
            observer->Started();
        }
    }
}

void Product::Stop()
{
    auto it = iSources.begin();
    while (it != iSources.end()) {
        (*it)->PipelineStopped();
        it++;
    }
}

void Product::AddSource(ISource* aSource)
{
    ASSERT(!iStarted);
    iSources.push_back(aSource);
    aSource->Initialise(*this, iConfigInit, iConfigReader, iSources.size()-1);
}

void Product::AddAttribute(const TChar* aAttribute)
{
    ASSERT(!iStarted);
    Brn attr(aAttribute);
    AddAttribute(attr);
}

void Product::AddAttribute(const Brx& aAttribute)
{
    if (iAttributes.Buffer().Bytes() > 0) {
        iAttributes.Write(' ');
    }
    iAttributes.Write(aAttribute);
}

void Product::SetConfigAppUrl(const Brx& aUrl)
{
    iLock.Wait();
    iConfigAppUrlTail.Replace(aUrl);
    iLock.Signal();
    AutoMutex amx(iObserverLock);
    for (auto attributeObserver : iAttributeObservers) {
        attributeObserver->AttributesChanged();
    }
}

void Product::GetManufacturerDetails(Brn& aName, Brn& aInfo, Bwx& aUrl, Bwx& aImageUri)
{
    ASSERT(iReadStore.TryReadStoreStaticItem(StaticDataKey::kBufManufacturerName, aName));
    ASSERT(iReadStore.TryReadStoreStaticItem(StaticDataKey::kBufManufacturerInfo, aInfo));
    GetUri(StaticDataKey::kBufManufacturerUrl, aUrl);
    GetUri(StaticDataKey::kBufManufacturerImageUrl, aImageUri);
}

void Product::GetModelDetails(Brn& aName, Brn& aInfo, Bwx& aUrl, Bwx& aImageUri)
{
    ASSERT(iReadStore.TryReadStoreStaticItem(StaticDataKey::kBufModelName, aName));
    ASSERT(iReadStore.TryReadStoreStaticItem(StaticDataKey::kBufModelInfo, aInfo));
    GetUri(StaticDataKey::kBufModelUrl, aUrl);
    GetUri(StaticDataKey::kBufModelImageUrl, aImageUri);
}

void Product::GetProductDetails(Bwx& aRoom, Bwx& aName, Brn& aInfo, Bwx& aImageUri)
{
    iLockDetails.Wait();
    aRoom.Append(iProductRoom);
    aName.Append(iProductName);
    iLockDetails.Signal();
    ASSERT(iReadStore.TryReadStoreStaticItem(StaticDataKey::kBufModelInfo, aInfo));
    // presentation url
    GetUri(StaticDataKey::kBufModelImageUrl, aImageUri);
}

TUint Product::SourceCount() const
{
    return (TUint)iSources.size();
}

TUint Product::CurrentSourceIndex() const
{
    return iCurrentSource;
}

void Product::GetSourceXml(IWriter& aWriter)
{
    aWriter.Write(Brn("<SourceList>"));
    iLock.Wait();
    for (TUint i=0; i<iSources.size(); i++) {
        ISource* src = iSources[i];
        Bws<ISource::kMaxSourceNameBytes> name;
        src->Name(name);
        aWriter.Write(Brn("<Source>"));
        AppendTag(aWriter, "Name", name);
        AppendTag(aWriter, "Type", src->Type());
        AppendTag(aWriter, "Visible", src->IsVisible()? Brn("true") : Brn("false"));
        AppendTag(aWriter, "SystemName", src->SystemName());
        aWriter.Write(Brn("</Source>"));
    }
    iLock.Signal();
    aWriter.Write(Brn("</SourceList>"));
    aWriter.WriteFlush();
}

void Product::AppendTag(IWriter& aWriter, const TChar* aTag, const Brx& aValue)
{
    Brn tag(aTag);
    aWriter.Write('<');
    aWriter.Write(tag);
    aWriter.Write('>');
    Converter::ToXmlEscaped(aWriter, aValue);
    aWriter.Write(Brn("</"));
    aWriter.Write(tag);
    aWriter.Write('>');
}

void Product::ProductRoomChanged(KeyValuePair<const Brx&>& aKvp)
{
    AutoMutex a(iLockDetails);
    if (iProductRoom != aKvp.Value()) {
        iProductRoom.Replace(aKvp.Value());
        for (auto it=iNameObservers.begin(); it!=iNameObservers.end(); ++it) {
            (*it)->RoomChanged(iProductRoom);
        }
    }
}

void Product::ProductNameChanged(KeyValuePair<const Brx&>& aKvp)
{
    AutoMutex a(iLockDetails);
    if (iProductName != aKvp.Value()) {
        iProductName.Replace(aKvp.Value());
        for (auto it=iNameObservers.begin(); it!=iNameObservers.end(); ++it) {
            (*it)->NameChanged(iProductName);
        }
    }
}

void Product::StartupSourceChanged(KeyValuePair<const Brx&>& aKvp)
{
    ASSERT(aKvp.Key() == ConfigStartupSource::kKeySource);
    AutoMutex a(iLock);
    iStartupSourceVal.Replace(aKvp.Value());
}

void Product::AutoPlayChanged(KeyValuePair<TUint>& aKvp)
{
    iLock.Wait();
    iAutoPlay = (aKvp.Value() == kAutoPlayEnable);
    iLock.Signal();
}

void Product::CurrentAdapterChanged()
{
    {
        AutoMutex _(iLock);
        AutoNetworkAdapterRef ar(iEnv, "Av::Product");
        iConfigAppAddress.Replace(Brx::Empty());
        auto current = ar.Adapter();
        if (current == nullptr) {
            iUriPrefix.Set("");
        }
        else {
            iDevice.GetResourceManagerUri(*current, iUriPrefix);
            Endpoint ep(0, current->Address());
            ep.AppendAddress(iConfigAppAddress);
        }
    }

    {
        AutoMutex amx(iObserverLock);
        for (auto observer : iObservers) {
            observer->ProductUrisChanged();
        }
        for (auto attributeObserver : iAttributeObservers) {
            attributeObserver->AttributesChanged();
        }
    }
}

void Product::GetUri(const Brx& aStaticDataKey, Bwx& aUri)
{
    Brn uri;
    ASSERT(iReadStore.TryReadStoreStaticItem(aStaticDataKey, uri));
    static const Brn kPrefixHttp("http://");
    if (uri.BeginsWith(kPrefixHttp)) {
        aUri.Replace(uri);
    }
    else {
        iLock.Wait();
        aUri.Replace(iUriPrefix);
        iLock.Signal();
        aUri.Append(uri);
    }
}

void Product::StandbyDisableNoSourceSwitch()
{
    iPowerManager.StandbyDisable(StandbyDisableReason::SourceActivation);
}

void Product::SetCurrentSource(TUint aIndex)
{
    TBool reActivateIfNoSourceChange = iStandby;
    StandbyDisableNoSourceSwitch();
    (void)DoSetCurrentSource(aIndex, reActivateIfNoSourceChange);
}

TBool Product::DoSetCurrentSourceLocked(TUint aIndex, TBool aReActivateIfNoSourceChange)
{
    if (aIndex >= (TUint)iSources.size()) {
        THROW(AvSourceNotFound);
    }
    TBool activate = aReActivateIfNoSourceChange;
    if (iCurrentSource != aIndex) {
        activate = true;
        if (iCurrentSource != kCurrentSourceNone) {
            iSources[iCurrentSource]->Deactivate();
        }
        iCurrentSource = aIndex;
        iLastSelectedSource->Set(iSources[iCurrentSource]->SystemName());
        iLastSelectedSource->Write();
        {
            AutoMutex amx(iObserverLock);
            for (auto observer : iObservers) {
                observer->SourceIndexChanged();
            }
        }
    }
    if (activate && !iStandby) {
        iSources[iCurrentSource]->Activate(iAutoPlay, kPrefetchAllowedDefault);
        return true;
    }
    return false;
}

TBool Product::DoSetCurrentSource(TUint aIndex, TBool aReActivateIfNoSourceChange)
{
    AutoMutex a(iLock);
    return DoSetCurrentSourceLocked(aIndex, aReActivateIfNoSourceChange);
}

void Product::SetCurrentSourceByName(const Brx& aName)
{
    TBool reActivateIfNoSourceChange = iStandby;
    StandbyDisableNoSourceSwitch();
    AutoMutex a(iLock);
    Bws<ISource::kMaxSourceNameBytes> name;
    TUint i = 0;
    for (i = 0; i < (TUint)iSources.size(); i++) {
        iSources[i]->Name(name);
        if (name == aName) {
            break;
        }
    }
    (void)DoSetCurrentSourceLocked(i, reActivateIfNoSourceChange);
}

void Product::SetCurrentSourceBySystemName(const Brx& aSystemName)
{
    TBool reActivateIfNoSourceChange = iStandby;
    StandbyDisableNoSourceSwitch();
    (void)DoSetCurrentSource(aSystemName, reActivateIfNoSourceChange);
}

TBool Product::DoSetCurrentSource(const Brx& aSystemName, TBool aReActivateIfNoSourceChange)
{
    AutoMutex a(iLock);
    TUint i = 0;
    for (i = 0; i < (TUint)iSources.size(); i++) {
        if (iSources[i]->SystemName() == aSystemName) {
            break;
        }
    }
    return DoSetCurrentSourceLocked(i, aReActivateIfNoSourceChange);
}

void Product::GetSourceDetails(TUint aIndex, Bwx& aSystemName, Bwx& aType, Bwx& aName, TBool& aVisible) const
{
    AutoMutex a(iLock);
    if (aIndex >= (TUint)iSources.size()) {
        THROW(AvSourceNotFound);
    }
    ISource* source = iSources[aIndex];
    Bws<ISource::kMaxSourceNameBytes> name;
    source->Name(name);
    aSystemName.Replace(source->SystemName());
    aType.Replace(source->Type());
    aName.Replace(name);
    aVisible = source->IsVisible();
}

void Product::GetSourceDetails(const Brx& aSystemName, Bwx& aType, Bwx& aName, TBool& aVisible) const
{
    AutoMutex a(iLock);
    for (TUint i=0; i<(TUint)iSources.size(); i++) {
        ISource* source = iSources[i];
        if (source->SystemName() == aSystemName) {
            Bws<ISource::kMaxSourceNameBytes> name;
            source->Name(name);
            aType.Replace(source->Type());
            aName.Replace(name);
            aVisible = source->IsVisible();
            return;
        }
    }
    THROW(AvSourceNotFound);
}

void Product::GetAttributes(IWriter& aWriter) const
{
    AutoMutex _(iLock);
    aWriter.Write(iAttributes.Buffer());
    if (iConfigAppAddress.Bytes() > 0) {
        aWriter.Write(Brn(" App:Config="));
        aWriter.Write(Brn("http://"));
        aWriter.Write(iConfigAppAddress);
        aWriter.Write(iConfigAppUrlTail);
    }
    aWriter.WriteFlush();
}

TUint Product::SourceXmlChangeCount()
{
    return iSourceXmlChangeCount;
}

void Product::ActivateIfNotActive(ISource& aSource, TBool aPrefetchAllowed)
{
    StandbyDisableNoSourceSwitch();

    ISource* srcNew = nullptr;
    ISource* srcOld = nullptr;

    AutoMutex a(iLock);
    // deactivate current (old) source, if one exists
    if (iCurrentSource != kCurrentSourceNone) {
        srcOld = iSources[iCurrentSource];

        if (&aSource == srcOld) {
            // This source is already selected.
            // However, it may not be active (in particular, if Product started up with this as last selected source, which gets selected but not activated at that point, and Product was not taken out of standby prior to this ::ActivateIfNotActive() call).
            if (!aSource.IsActive()) {
                aSource.Activate(iAutoPlay, aPrefetchAllowed);
            }
            return;
        }

        srcOld->Deactivate();
    }

    // find and activate new source
    Bws<ISource::kMaxSourceNameBytes> name;
    Bws<ISource::kMaxSourceNameBytes> nameExpected;
    aSource.Name(nameExpected);
    for (TUint i=0; i<(TUint)iSources.size(); i++) {
        iSources[i]->Name(name);
        if (name == nameExpected) {
            iCurrentSource = i;
            iLastSelectedSource->Set(iSources[iCurrentSource]->SystemName());
            iLastSelectedSource->Write();
            srcNew = iSources[i];
            srcNew->Activate(iAutoPlay, aPrefetchAllowed);
            {
                AutoMutex amx(iObserverLock);
                for (auto observer : iObservers) {
                    observer->SourceIndexChanged();
                }
            }
            return;
        }
    }
    THROW(AvSourceNotFound);
}

void Product::NotifySourceChanged(ISource& /*aSource*/)
{
    iLock.Wait();
    iSourceXmlChangeCount++;
    iLock.Signal();
    {
        AutoMutex amx(iObserverLock);
        for (auto observer : iObservers) {
            observer->SourceXmlChanged();
        }
    }
}

void Product::AddNameObserver(IProductNameObserver& aObserver)
{
    AutoMutex a(iLockDetails);
    iNameObservers.push_back(&aObserver);
    // Notify new observer immediately with its initial values.
    aObserver.RoomChanged(iProductRoom);
    aObserver.NameChanged(iProductName);
}

TBool Product::TryActivate(const Brx& aMode)
{
    for (auto it=iSources.begin(); it!=iSources.end(); ++it) {
        if ((*it)->TryActivateNoPrefetch(aMode)) {
            return true;
        }
    }
    return false;
}

void Product::StandbyEnabled()
{
    AutoMutex _(iLock);
    iStandby = true;
    if (iCurrentSource != kCurrentSourceNone) {
        iSources[iCurrentSource]->StandbyEnabled();
    }
}

void Product::StandbyDisabled(StandbyDisableReason aReason)
{
    iLock.Wait();
    iStandby = false;
    iLock.Signal();

    TBool activated = false;
    if (aReason == StandbyDisableReason::Product || aReason == StandbyDisableReason::Boot) {
        iLock.Wait();
        const Bws<ISource::kMaxSystemNameBytes> startupSourceVal(iStartupSourceVal);
        iLock.Signal();
        if (startupSourceVal != ConfigStartupSource::kLastUsed) {
            try {
                activated = DoSetCurrentSource(startupSourceVal, true);
            }
            catch (AvSourceNotFound&) {
                // Invalid content in iStartupSourceVal. Leave last source set.
            }
        }

        if (!activated) {
            AutoMutex _(iLock);
            if (iCurrentSource != kCurrentSourceNone) {
                iSources[iCurrentSource]->Activate(iAutoPlay, kPrefetchAllowedDefault);
            }
        }
    }
}


// FriendlyNameManager

FriendlyNameManager::FriendlyNameManager(IProductNameObservable& aProduct, IThreadPool& aThreadPool)
    : iNextObserverId(1)
    , iMutex("FNHM")
{
    iThreadPoolHandle = aThreadPool.CreateHandle(MakeFunctor(*this, &FriendlyNameManager::NotifyObservers), "FriendlyNameManager", ThreadPoolPriority::Medium);
    aProduct.AddNameObserver(*this);    // Observer methods called during registration.
}

FriendlyNameManager::~FriendlyNameManager()
{
    // Note: no way to deregister name observer that was registered with aProduct in constructor.
    // So, it is only safe to call this destructor as long as aProduct does not attempt to call back into an instance of this class (i.e., aProduct must have somehow purged its observers, possibly by already being deleted, by this point).
    {
        AutoMutex a(iMutex);
        ASSERT(iObservers.size() == 0);
    }
    iThreadPoolHandle->Destroy();
}

TUint FriendlyNameManager::RegisterFriendlyNameObserver(FunctorGeneric<const Brx&> aObserver)
{
    const TUint id = iNextObserverId++;
    auto it = iObservers.insert(std::pair<TUint,FunctorGeneric<const Brx&>>(id, aObserver));
    ASSERT(it.second);

    Bws<kMaxFriendlyNameBytes> friendlyName;
    {
        AutoMutex a(iMutex);
        friendlyName.Replace(iFriendlyName);
    }
    aObserver(friendlyName);

    return id;
}

void FriendlyNameManager::DeregisterFriendlyNameObserver(TUint aId)
{
    const TUint count = iObservers.erase(aId);
    ASSERT(count == 1);
}

void FriendlyNameManager::RoomChanged(const Brx& aRoom)
{
    {
        AutoMutex a(iMutex);
        iRoom.Replace(aRoom);
        ConstructFriendlyNameLocked();
    }
    (void)iThreadPoolHandle->TrySchedule();
}

void FriendlyNameManager::NameChanged(const Brx& aName)
{
    {
        AutoMutex a(iMutex);
        iName.Replace(aName);
        ConstructFriendlyNameLocked();
    }
    (void)iThreadPoolHandle->TrySchedule();
}

void FriendlyNameManager::ConstructFriendlyNameLocked()
{
    iFriendlyName.Replace(iRoom);
    iFriendlyName.Append(':');
    iFriendlyName.Append(iName);
}

void FriendlyNameManager::NotifyObservers()
{
    // It is known that some existing observers undertake time-consuming (many seconds up to tens-of-seconds) tasks during the callbacks from here.
    // While it would be desirable that observers offload time-consuming tasks to another thread, the simplest solution for now is to perform these callbacks on either a dedicated thread, or via the thread pool.

    Bws<kMaxFriendlyNameBytes> friendlyName;
    {
        AutoMutex a(iMutex);
        friendlyName.Replace(iFriendlyName);
    }

    for (auto observer : iObservers) {
        observer.second(friendlyName);
    }
}

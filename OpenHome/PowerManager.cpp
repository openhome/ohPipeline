#include <OpenHome/PowerManager.h>
#include <OpenHome/Types.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Debug-ohMediaPlayer.h>
#include <OpenHome/Private/Arch.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Configuration/ConfigManager.h>

using namespace OpenHome;
using namespace OpenHome::Configuration;

// PowerManager

const Brn PowerManager::kConfigKey("Device.StartupMode");
const TUint PowerManager::kConfigIdStartupStandbyEnabled = 0;
const TUint PowerManager::kConfigIdStartupStandbyDisabled  = 1;

PowerManager::PowerManager(Optional<IConfigInitialiser> aConfigInit)
    : iNextPowerId(0)
    , iNextStandbyId(0)
    , iNextFsFlushId(0)
    , iPowerDown(false)
    , iStandby(Standby::Undefined)
    , iLock("PMLO")
{
    if (aConfigInit.Ok()) {
        const int arr[] = { kConfigIdStartupStandbyEnabled, kConfigIdStartupStandbyDisabled };
        std::vector<TUint> options(arr, arr + sizeof(arr) / sizeof(arr[0]));
        iConfigStartupStandby = new ConfigChoice(aConfigInit.Unwrap(), kConfigKey, options, kConfigIdStartupStandbyEnabled);
    }
    else {
        iConfigStartupStandby = nullptr;
        iStandby = Standby::Off;
    }
}

PowerManager::~PowerManager()
{
    AutoMutex _(iLock);
    ASSERT(iPowerObservers.empty());
    ASSERT(iFsFlushObservers.empty());
    delete iConfigStartupStandby;
}

void PowerManager::Start()
{
    if (iConfigStartupStandby == nullptr) {
        StartupStandbyExecute(Standby::On);
    }
    else {
        const TUint id = iConfigStartupStandby->Subscribe(MakeFunctorConfigChoice(*this, &PowerManager::StartupStandbyChanged));
        // we don't care about run-time changes to iConfigStartupStandby so can unsubscribe as soon as we've read its initial value
        iConfigStartupStandby->Unsubscribe(id);
    }
}

void PowerManager::NotifyPowerDown()
{
    // FIXME - the caller of power down should provide some kind of interrupt
    // for stopping any non-essential store tasks in progress
    AutoMutex _(iLock);
    LOG(kPowerManager, ">PowerManager::NotifyPowerDown. iPowerDown: %s, iPowerObservers.size(): %u\n", PBool(iPowerDown), iPowerObservers.size());
    if (!iPowerDown)
    {
        iPowerDown = true;
        TUint i = 0;
        for (auto it = iPowerObservers.cbegin(); it != iPowerObservers.cend(); ++it) {
            IPowerHandler& handler = (*it)->PowerHandler();
            handler.PowerDown();
            LOG(kPowerManager, "PowerManager::NotifyPowerDown %u, %s\n", ++i, (*it)->ClientId());
        }
    }
    LOG(kPowerManager, "<PowerManager::NotifyPowerDown\n");
}

void PowerManager::StandbyEnable()
{
    AutoMutex _(iLock);
    if (iStandby == Standby::On) {
        return;
    }
    StandbyTransitioning();
    iStandby = Standby::On;
    for (auto it = iStandbyObservers.rbegin(); it != iStandbyObservers.rend(); ++it) {
        LOG(kPowerManager, "PowerManager::StandbyEnable %s\n", (*it)->ClientId());
        (*it)->Handler().StandbyEnabled();
    }
    LOG(kPowerManager, "PowerManager::StandbyEnable complete\n");
}

void PowerManager::StandbyTransitioning()
{
    for (auto it = iStandbyObservers.rbegin(); it != iStandbyObservers.rend(); ++it) {
        (*it)->Handler().StandbyTransitioning();
    }
}

void PowerManager::StandbyDisable(StandbyDisableReason aReason)
{
    AutoMutex _(iLock);
    if (iStandby == Standby::Off) {
        return;
    }
    StandbyTransitioning();
    iStandby = Standby::Off;
    iLastDisableReason = aReason;
    for (auto it = iStandbyObservers.begin(); it != iStandbyObservers.end(); ++it) {
        LOG(kPowerManager, "PowerManager::StandbyDisable %s\n", (*it)->ClientId());
        (*it)->Handler().StandbyDisabled(aReason);
    }
    LOG(kPowerManager, "PowerManager::StandbyDisable complete\n");
}

void PowerManager::FsFlush()
{
    AutoMutex _(iLock);
    for (auto it = iFsFlushObservers.begin(); it != iFsFlushObservers.end(); ++it) {
        try {
            (*it)->Handler().FsFlush();
        }
        catch (AssertionFailed&) {
            throw;
        }
        catch (Exception&) {}
    }
}

IPowerManagerObserver* PowerManager::RegisterPowerHandler(IPowerHandler& aHandler, TUint aPriority, const TChar* aClientId)
{
    ASSERT(aPriority <= kPowerPriorityHighest)
    ASSERT(aPriority >= kPowerPriorityLowest); // shouldn't matter as lowest is 0, and parameter type is TUint

    AutoMutex a(iLock);
    if (iPowerDown) {
        return new PowerManagerObserverNull();
    }

    PowerManagerObserver* observer = new PowerManagerObserver(*this, aHandler, iNextPowerId++, aPriority, aClientId);

    PriorityList::iterator it;
    for (it = iPowerObservers.begin(); it != iPowerObservers.end(); ++it) {
        if ((*it)->Priority() < observer->Priority()) {
            iPowerObservers.insert(it, observer);
            break;
        }
    }

    if (it == iPowerObservers.cend()) {
        // Callback is lower priority than anything in list.
        iPowerObservers.push_back(observer);
    }

    aHandler.PowerUp();
    return observer;
}

IStandbyObserver* PowerManager::RegisterStandbyHandler(IStandbyHandler& aHandler, TUint aPriority, const TChar* aClientId)
{
    AutoMutex _(iLock);
    auto* observer = new StandbyObserver(*this, aHandler, iNextStandbyId++, aPriority, aClientId);

    std::vector<StandbyObserver*>::iterator it;
    for (it = iStandbyObservers.begin(); it != iStandbyObservers.end(); ++it) {
        if ((*it)->Priority() < observer->Priority()) {
            it = iStandbyObservers.insert(it, observer);
            break;
        }
    }

    if (it == iStandbyObservers.end()) {
        // Callback is lower priority than anything in list.
        iStandbyObservers.push_back(observer);
    }

    if (iStandby == Standby::On) {
        aHandler.StandbyEnabled();
    }
    else if (iStandby == Standby::Off) {
        aHandler.StandbyDisabled(iLastDisableReason);
    }
    return observer;
}

IFsFlushObserver* PowerManager::RegisterFsFlushHandler(IFsFlushHandler& aHandler)
{
    AutoMutex _(iLock);
    auto obs = new FsFlushObserver(*this, aHandler, iNextFsFlushId++);
    iFsFlushObservers.push_back(obs);
    return obs;
}

// Called from destructor of PowerManagerObserver.
void PowerManager::DeregisterPower(TUint aId)
{
    AutoMutex _(iLock);
    for (auto it = iPowerObservers.begin(); it != iPowerObservers.end(); ++it) {
        if ((*it)->Id() == aId) {
            // Call PowerDown() on handler under normal shutdown circumstances.
            // PowerDown() may have been invoked on the PowerManager itself,
            // and object destruction started before power failure. Don't want
            // to call PowerDown() on the handler again in that case.
            if (!iPowerDown) {
                IPowerHandler& handler = (*it)->PowerHandler();
                handler.PowerDown();
            }
            iPowerObservers.erase(it);
            return;
        }
    }
}

void PowerManager::DeregisterStandby(TUint aId)
{
    AutoMutex _(iLock);
    for (auto it = iStandbyObservers.begin(); it != iStandbyObservers.end(); ++it) {
        if ((*it)->Id() == aId) {
            LOG(kPowerManager, "PowerManager::DeregisterStandby %s\n", (*it)->ClientId());
            iStandbyObservers.erase(it);
            return;
        }
    }
}

void PowerManager::DeregisterFsFlush(TUint aId)
{
    AutoMutex _(iLock);
    for (auto it = iFsFlushObservers.begin(); it != iFsFlushObservers.end(); ++it) {
        if ((*it)->Id() == aId) {
            iFsFlushObservers.erase(it);
            return;
        }
    }
}

void PowerManager::StartupStandbyChanged(KeyValuePair<TUint>& aKvp)
{
    Standby standby = (aKvp.Value() == kConfigIdStartupStandbyEnabled ? Standby::On : Standby::Off);
    StartupStandbyExecute(standby);
}

void PowerManager::StartupStandbyExecute(Standby aMode)
{
    iLastDisableReason = StandbyDisableReason::Boot; // this callback only runs during PowerManager construction
    if (aMode == Standby::On) {
        StandbyEnable();
    }
    else {
        StandbyDisable(iLastDisableReason);
    }
}


// PowerManagerObserverNull

PowerManagerObserverNull::~PowerManagerObserverNull()
{
    // Not actually registered in PowerManager, so do nothing.
}


// PowerManagerObserver

PowerManagerObserver::PowerManagerObserver(PowerManager& aPowerManager, IPowerHandler& aHandler, TUint aId, TUint aPriority, const TChar* aClientId)
    : iPowerManager(aPowerManager)
    , iHandler(aHandler)
    , iId(aId)
    , iPriority(aPriority)
    , iClientId(aClientId)
{
}

PowerManagerObserver::~PowerManagerObserver()
{
    iPowerManager.DeregisterPower(iId);
}

IPowerHandler& PowerManagerObserver::PowerHandler() const
{
    return iHandler;
}

TUint PowerManagerObserver::Id() const
{
    return iId;
}

TUint PowerManagerObserver::Priority() const
{
    return iPriority;
}

const TChar* PowerManagerObserver::ClientId() const
{
    return iClientId;
}


// StandbyObserver

StandbyObserver::StandbyObserver(PowerManager& aPowerManager, IStandbyHandler& aHandler, TUint aId, TUint aPriority, const TChar* aClientId)
    : iPowerManager(aPowerManager)
    , iHandler(aHandler)
    , iId(aId)
    , iPriority(aPriority)
    , iClientId(aClientId)
{
}

StandbyObserver::~StandbyObserver()
{
    iPowerManager.DeregisterStandby(iId);
}

IStandbyHandler& StandbyObserver::Handler() const
{
    return iHandler;
}

TUint StandbyObserver::Id() const
{
    return iId;
}

const TChar* StandbyObserver::ClientId() const
{
    return iClientId;
}

TUint StandbyObserver::Priority() const
{
    return iPriority;
}


// FsFlushObserver

FsFlushObserver::FsFlushObserver(PowerManager& aPowerManager, IFsFlushHandler& aHandler, TUint aId)
    : iPowerManager(aPowerManager)
    , iHandler(aHandler)
    , iId(aId)
{
}

FsFlushObserver::~FsFlushObserver()
{
    iPowerManager.DeregisterFsFlush(iId);
}

IFsFlushHandler& FsFlushObserver::Handler() const
{
    return iHandler;
}

TUint FsFlushObserver::Id() const
{
    return iId;
}


// StoreVal

StoreVal::StoreVal(IStoreReadWrite& aStore, const Brx& aKey)
    : iObserver(nullptr)
    , iStore(aStore)
    , iKey(aKey)
    , iLock("STVM")
{
}

void StoreVal::RegisterPowerHandlers(IPowerManager& aPowerManager, TUint aPowerHandlerPriority)
{
    /*
     * Unsafe to register standby/power handlers in constructor of this base
     * class, as the callbacks may call into overridden virtual functions which
     * cannot yet be resolved.
     *
     * So, deriving classes must call this during their initialisation.
     */

    // our standby observer is relatively unimportant
    // priority enum describes importance when exiting Standby - we only do any work when we enter Standby
    iStandbyObserver.reset(aPowerManager.RegisterStandbyHandler(*this, kStandbyHandlerPriorityHighest - 1, "StoreVal"));
    iObserver = aPowerManager.RegisterPowerHandler(*this, aPowerHandlerPriority, "StoreVal");
    iFsFlushObserver.reset(aPowerManager.RegisterFsFlushHandler(*this));
}

void StoreVal::PowerDown()
{
    Write();
}

void StoreVal::StandbyEnabled()
{
    Write();
}

void StoreVal::StandbyTransitioning()
{
}

void StoreVal::StandbyDisabled(StandbyDisableReason /*aReason*/)
{
}

void StoreVal::FsFlush()
{
    Write();
}


// StoreInt

StoreInt::StoreInt(IStoreReadWrite& aStore, IPowerManager& aPowerManager, TUint aPriority, const Brx& aKey, TInt aDefault)
    : StoreVal(aStore, aKey)
    , iVal(aDefault)
    , iLastWritten(aDefault)
    , iChanged(false)
{
    RegisterPowerHandlers(aPowerManager, aPriority);
}

StoreInt::~StoreInt()
{
    delete iObserver;
}

TInt StoreInt::Get() const
{
    AutoMutex a(iLock);
    return iVal;
}

void StoreInt::Set(TInt aValue)
{
    AutoMutex a(iLock);
    if (iVal == aValue) {
        return;
    }
    iVal = aValue;
    iChanged = true;
}

void StoreInt::Write(const Brx& aKey, TInt aValue, Configuration::IStoreReadWrite& aStore)
{ // static
    Bws<sizeof(TInt)> buf;
    WriterBuffer writerBuf(buf);
    WriterBinary writerBin(writerBuf);
    writerBin.WriteUint32Be(aValue);
    aStore.Write(aKey, buf);
}

void StoreInt::PowerUp()
{
    AutoMutex _(iLock);
    Bws<sizeof(TInt)> buf;
    try {
        iStore.Read(iKey, buf);
        iVal = Converter::BeUint32At(buf, 0);
        iLastWritten = iVal;
    }
    catch (StoreKeyNotFound&) {}
}

void StoreInt::Write()
{
    AutoMutex _(iLock);
    if (iChanged) {
        if (iVal != iLastWritten) {
            Write(iKey, iVal, iStore);
            iLastWritten = iVal;
        }
        iChanged = false;
    }
}


// StoreText

StoreText::StoreText(IStoreReadWrite& aStore, IPowerManager& aPowerManager, TUint aPriority, const Brx& aKey, const Brx& aDefault, TUint aMaxLength)
    : StoreVal(aStore, aKey)
    , iVal(aMaxLength)
    , iLastWritten(aMaxLength)
    , iChanged(false)
{
    iVal.Replace(aDefault);
    iLastWritten.Replace(aDefault);
    RegisterPowerHandlers(aPowerManager, aPriority);
}

StoreText::~StoreText()
{
    delete iObserver;
}

void StoreText::Get(Bwx& aBuf) const
{
    AutoMutex a(iLock);
    aBuf.Replace(iVal);
}

void StoreText::Set(const Brx& aValue)
{
    AutoMutex a(iLock);
    if (iVal == aValue) {
        return;
    }
    iVal.Replace(aValue);
    iChanged = true;
}

void StoreText::PowerUp()
{
    AutoMutex _(iLock);
    try {
        iStore.Read(iKey, iVal);
        iLastWritten.Replace(iVal);
    }
    catch (StoreKeyNotFound&) {}
}

void StoreText::Write()
{
    AutoMutex a(iLock);
    if (iChanged) {
        if (iVal != iLastWritten) {
            iStore.Write(iKey, iVal);
            iLastWritten.Replace(iVal);
        }
        iChanged = false;
    }
}


// StoreTextDynamic

StoreTextDynamic::StoreTextDynamic(IStoreReadWrite& aStore, IPowerManager& aPowerManager, TUint aPriority, const Brx& aKey, const Brx& aDefault, TUint aGranularity)
    : StoreVal(aStore, aKey)
    , iVal(aGranularity)
    , iLastWritten(aGranularity)
    , iChanged(false)
{
    iVal.Write(aDefault);
    iLastWritten.Write(aDefault);
    RegisterPowerHandlers(aPowerManager, aPriority);
}

StoreTextDynamic::~StoreTextDynamic()
{
    delete iObserver;
}

void StoreTextDynamic::Read(IWriter& aWriter) const
{
    AutoMutex a(iLock);
    aWriter.Write(iVal.Buffer());
}

void StoreTextDynamic::Set(const Brx& aValue)
{
    AutoMutex a(iLock);
    if (iVal.Buffer() == aValue) {
        return;
    }
    iVal.Reset();
    iVal.Write(aValue);
    iChanged = true;
}

void StoreTextDynamic::PowerUp()
{
    AutoMutex _(iLock);
    try {
        iStore.Read(iKey, iVal);
        iLastWritten.Write(iVal.Buffer());
    }
    catch (StoreKeyNotFound&) {}
}

void StoreTextDynamic::Write()
{
    AutoMutex a(iLock);
    if (iChanged) {
        if (iVal.Buffer() != iLastWritten.Buffer()) {
            iStore.Write(iKey, iVal.Buffer());
            iLastWritten.Write(iVal.Buffer());
        }
        iChanged = false;
    }
}

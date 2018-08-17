#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Configuration/ConfigManager.h>

#include <list>
#include <vector>
#include <memory>

namespace OpenHome {

enum PowerDownPriority {
    kPowerPriorityLowest = 0
   ,kPowerPriorityNormal = 50
   ,kPowerPriorityHighest = 100
};

enum StandbyHandlerPriority {
    kStandbyHandlerPriorityLowest = 0    // first to be called for StandbyEnabled(), last for StandbyDisabled()
   ,kStandbyHandlerPriorityNormal = 50
   ,kStandbyHandlerPriorityHighest = 100 // last to be called for StandbyEnabled(), first for StandbyDisabled()
};

enum class StandbyDisableReason {
    /*
     * An event has occured where the product is expected to exit standby and
     * perform normal post-standby events.
     *
     * Normal post-standby events should happen when this reason seen.
     */
    Product
    /*
     * The product is being taken out of standby at boot time, as the
     * boot-out-of-standby option has been set.
     *
     * Normal post-standby events should happen when this reason seen.
     */
    ,Boot
    /*
     * A user-set alarm clock mode has taken the product out of standby.
     *
     * Only post-standby events related to activating the appropriate alarm
     * mode should happen when this reason seen (e.g., setting and playing the
     * appropriate source and/or content).
     */
    ,Alarm
    /*
     * Interaction directly with a source has taken the product out of standby.
     * (e.g., a network-based source module has received an event from a client
     * that it should activate itself).
     *
     * Only post-standby events related to activating the appropriate mode
     * should happen when this reason seen (i.e., the source that is taking the
     * product out of standby should NOT be overridden by any user-defined
     * startup source).
     */
    ,SourceActivation
};

class IPowerHandler
{
public:
    virtual void PowerUp() = 0;
    virtual void PowerDown() = 0;
    virtual ~IPowerHandler() {}
};

class IStandbyHandler
{
public:
    virtual void StandbyEnabled() = 0;
    virtual void StandbyDisabled(StandbyDisableReason aReason) = 0;
    virtual ~IStandbyHandler() {}
};

class IFsFlushHandler
{
public:
    virtual void FsFlush() = 0;
    virtual ~IFsFlushHandler() {}
};

/**
 * Interface that IPowerHandlers will be returned when they register with an
 * IPowerManager.
 *
 * Deleting an instance of a class implementing this interface causes the class
 * to be deregistered from the IPowerManager.
 */
class IPowerManagerObserver
{
public:
    virtual ~IPowerManagerObserver() {}
};

class IStandbyObserver
{
public:
    virtual ~IStandbyObserver() {}
};

class IFsFlushObserver
{
public:
    virtual ~IFsFlushObserver() {}
};

class IPowerManager
{
public:
    virtual void NotifyPowerDown() = 0;
    virtual void StandbyEnable() = 0;
    virtual void StandbyDisable(StandbyDisableReason aReason) = 0;
    virtual void FsFlush() = 0;
    virtual IPowerManagerObserver* RegisterPowerHandler(IPowerHandler& aHandler, TUint aPriority) = 0;
    virtual IStandbyObserver* RegisterStandbyHandler(IStandbyHandler& aHandler, TUint aPriority, const TChar* aClientId) = 0;
    virtual IFsFlushObserver* RegisterFsFlushHandler(IFsFlushHandler& aHandler) = 0;
    virtual ~IPowerManager() {}
};

class PowerManagerObserver;
class StandbyObserver;
class FsFlushObserver;

class PowerManager : public IPowerManager
{
    friend class PowerManagerObserver;
    friend class StandbyObserver;
    friend class FsFlushObserver;
    static const Brn kConfigKey;
    static const TUint kConfigIdStartupStandbyEnabled;
    static const TUint kConfigIdStartupStandbyDisabled;
public:
    PowerManager(Configuration::IConfigInitialiser& aConfigInit);
    ~PowerManager();
    void Start();
public: // from IPowerManager
    void NotifyPowerDown() override;
    void StandbyEnable() override;
    void StandbyDisable(StandbyDisableReason aReason) override;
    void FsFlush() override;
    IPowerManagerObserver* RegisterPowerHandler(IPowerHandler& aHandler, TUint aPriority) override;
    IStandbyObserver* RegisterStandbyHandler(IStandbyHandler& aHandler, TUint aPriority, const TChar* aClientId) override;
    IFsFlushObserver* RegisterFsFlushHandler(IFsFlushHandler& aHandler) override;
private:
    void DeregisterPower(TUint aId);
    void DeregisterStandby(TUint aId);
    void DeregisterFsFlush(TUint aId);
    void StartupStandbyChanged(Configuration::KeyValuePair<TUint>& aKvp);
private:
    enum class Standby {
        On,
        Off,
        Undefined
    };
private:
    typedef std::list<PowerManagerObserver*> PriorityList;  // efficient insertion and removal
    PriorityList iPowerObservers;
    std::vector<StandbyObserver*> iStandbyObservers;
    std::vector<FsFlushObserver*> iFsFlushObservers;
    TUint iNextPowerId;
    TUint iNextStandbyId;
    TUint iNextFsFlushId;
    TBool iPowerDown;
    Standby iStandby;
    StandbyDisableReason iLastDisableReason;
    mutable Mutex iLock;
    Configuration::ConfigChoice* iConfigStartupStandby;
};

/**
 * Class that is returned by IPowerManager::Register if registration of an
 * IPowerHandler fails.
 */
class PowerManagerObserverNull : public IPowerManagerObserver
{
public:
    ~PowerManagerObserverNull();
};

/**
 * Class that is returned by IPowerManager::Register if registration of an
 * IPowerHandler succeeds.
 */
class PowerManagerObserver : public IPowerManagerObserver, public INonCopyable
{
public:
    PowerManagerObserver(PowerManager& aPowerManager, IPowerHandler& aHandler, TUint aId, TUint aPriority);
    ~PowerManagerObserver();
    IPowerHandler& PowerHandler() const;
    TUint Id() const;
    TUint Priority() const;
private:
    PowerManager& iPowerManager;
    IPowerHandler& iHandler;
    const TUint iId;
    const TUint iPriority;
};

class StandbyObserver : public IStandbyObserver, private INonCopyable
{
public:
    StandbyObserver(PowerManager& aPowerManager, IStandbyHandler& aHandler, TUint aId, TUint aPriority, const TChar* aClientId);
    ~StandbyObserver();
    IStandbyHandler& Handler() const;
    TUint Id() const;
    TUint Priority() const;
    const TChar* ClientId() const;
private:
    PowerManager& iPowerManager;
    IStandbyHandler& iHandler;
    const TUint iId;
    const TUint iPriority;
    const TChar* iClientId;
};

class FsFlushObserver : public IFsFlushObserver, private INonCopyable
{
public:
    FsFlushObserver(PowerManager& aPowerManager, IFsFlushHandler& aHandler, TUint aId);
    ~FsFlushObserver();
    IFsFlushHandler& Handler() const;
    TUint Id() const;
private:
    PowerManager & iPowerManager;
    IFsFlushHandler& iHandler;
    const TUint iId;
};

/*
 * Abstract class that only writes its value out to store at power down.
 */
class StoreVal : protected IPowerHandler
               , protected IStandbyHandler
               , protected IFsFlushHandler
{
public:
    static const TUint kMaxIdLength = 32;
protected:
    StoreVal(Configuration::IStoreReadWrite& aStore, const Brx& aKey);
    void RegisterPowerHandlers(IPowerManager& aPowerManager, TUint aPowerHandlerPriority);
protected: // from IPowerHandler
    virtual void PowerUp() override = 0;
    void PowerDown() override;
private: // from IStandbyHandler
    void StandbyEnabled() override;
    void StandbyDisabled(StandbyDisableReason aReason) override;
private: // from IFsFlushHandler
    void FsFlush() override;
public:
    virtual void Write() = 0;
protected:
    IPowerManagerObserver* iObserver;
    Configuration::IStoreReadWrite& iStore;
    const Bws<kMaxIdLength> iKey;
    mutable Mutex iLock;
private:
    std::unique_ptr<IStandbyObserver> iStandbyObserver;
    std::unique_ptr<IFsFlushObserver> iFsFlushObserver;
};

/*
 * Int class that only writes its value out to store at power down.
 */
class StoreInt : public StoreVal
{
public:
    StoreInt(Configuration::IStoreReadWrite& aStore, IPowerManager& aPowerManager, TUint aPriority, const Brx& aKey, TInt aDefault);
    ~StoreInt();
    TInt Get() const;
    void Set(TInt aValue); // owning class knows limits
    static void Write(const Brx& aKey, TInt aValue, Configuration::IStoreReadWrite& aStore);
private: // from StoreVal
    void PowerUp() override;
public: // from StoreVal
    void Write() override;
private:
    TInt iVal;
    TInt iLastWritten;
    TBool iChanged;
};

/*
 * Text class that only writes its value out to store at power down.
 */
class StoreText : public StoreVal
{
public:
    StoreText(Configuration::IStoreReadWrite& aStore, IPowerManager& aPowerManager, TUint aPriority, const Brx& aKey, const Brx& aDefault, TUint aMaxLength);
    ~StoreText();
    void Get(Bwx& aVal) const;
    void Set(const Brx& aValue);
private: // from StoreVal
    void PowerUp() override;
public: // from StoreVal
    void Write() override;
private:
    Bwh iVal;
    Bwh iLastWritten;
    TBool iChanged;
};

} // namespace OpenHome


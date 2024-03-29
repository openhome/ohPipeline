#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/OAuth.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Functor.h>
#include <OpenHome/Private/Fifo.h>
#include <OpenHome/Private/Standard.h>
#include <OpenHome/Private/Thread.h>

#include <vector>

EXCEPTION(CredentialsIdNotFound);
EXCEPTION(CredentialsTooLong);
EXCEPTION(CredentialsLoginFailed);

#define ENABLED_NO  0
#define ENABLED_YES 1

namespace OpenHome {
    class Environment;
    class IWriter;
    class Timer;
    class ThreadFunctor;
    class IPowerManager;
namespace Net {
    class DvDevice;
}
namespace Configuration {
    class IStoreReadWrite;
    class IConfigInitialiser;
}
namespace Av {

class ICredentialConsumer
{
public:
    virtual ~ICredentialConsumer() {}
    virtual const Brx& Id() const = 0;
    virtual void CredentialsChanged(const Brx& aUsername, const Brx& aPassword) = 0; // password is decrypted
    virtual void UpdateStatus() = 0;
    virtual void Login(Bwx& aToken) = 0;
    virtual void ReLogin(const Brx& aCurrentToken, Bwx& aNewToken) = 0;
};

class ICredentialState
{
public:
    virtual ~ICredentialState() {}
    virtual void Unlock() = 0;
    virtual void Username(IWriter& aWriter) = 0;
    virtual void Password(IWriter& aWriter) = 0;
    virtual TBool Enabled() const = 0;
    virtual void Status(IWriter& aWriter) = 0;
    virtual void Data(IWriter& aWriter) = 0;
};

class AutoCredentialState : private INonCopyable
{
public:
    AutoCredentialState(ICredentialState& aState);
    ~AutoCredentialState();
private:
    ICredentialState& iState;
};

class ICredentials
{
public:
    static const TUint kMaxTokenBytes = 256;
public:
    virtual void Set(const Brx& aId, const Brx& aUsername, const Brx& aPassword) = 0;
    virtual void Clear(const Brx& aId) = 0;
    virtual void Enable(const Brx& aId, TBool aEnable) = 0;
    virtual ICredentialState& State(const Brx& aId) = 0;
    virtual void Login(const Brx& aId, Bwx& aToken) = 0;
    virtual void ReLogin(const Brx& aId, const Brx& aCurrentToken, Bwx& aNewToken) = 0;
};

class ICredentialsState
{
public:
    virtual void SetState(const Brx& aId, const Brx& aStatus, const Brx& aData) = 0;
};

class ICredentialObserver
{
public:
    virtual void CredentialChanged() = 0;
};

class IRsaProvider
{
public:
    virtual void* RsaPrivateKey() = 0; // Type is RSA. Ownership not passed.
    virtual void GetRsaPublicKey(Bwx& aKey) = 0;
    virtual ~IRsaProvider() {}
};

class IRsaObservable
{
public:
    static const TUint kObserverIdNull;
public:
    virtual TUint AddObserver(FunctorGeneric<IRsaProvider&> aCb) = 0;
    virtual void RemoveObserver(TUint aId) = 0; // cannot be called from callback registered with AddObserver
    virtual ~IRsaObservable() {}
};

class ProviderCredentials;
class Credential;

class Credentials : public ICredentials
                  , public ICredentialsState
                  , public IRsaObservable
                  , private ICredentialObserver
                  , private IRsaProvider
{
    static const Brn kKeyRsaPrivate;
    static const Brn kKeyRsaPublic;
    static const TUint kModerationTimeMs = 500;
    static const TUint kNumFifoElements = 100;
public:
    Credentials(Environment& aEnv,
                Net::DvDevice& aDevice,
                Configuration::IStoreReadWrite& aStore,
                const Brx& aEntropy,
                Configuration::IConfigInitialiser& aConfigInitialiser,
                IPowerManager& aPowerManager,
                TUint aKeyBits = 2048);
    virtual ~Credentials();
    void Add(ICredentialConsumer* aConsumer);
    void Start();
    void GetPublicKey(Bwx& aKey); // test use only
private: // from IRsaObservable
    TUint AddObserver(FunctorGeneric<IRsaProvider&> aCb) override;
    void RemoveObserver(TUint aId) override;
//    void GetKey(FunctorGeneric<IRsaProvider&> aCb);
private: // from ICredentials
    void Set(const Brx& aId, const Brx& aUsername, const Brx& aPassword) override; // password must be encrypted
    void Clear(const Brx& aId) override;
    void Enable(const Brx& aId, TBool aEnable) override;
    ICredentialState& State(const Brx& aId) override;
    void Login(const Brx& aId, Bwx& aToken) override;
    void ReLogin(const Brx& aId, const Brx& aCurrentToken, Bwx& aNewToken) override;
public: // from ICredentialsState
    void SetState(const Brx& aId, const Brx& aStatus, const Brx& aData) override;
private: // from ICredentialObserver
    void CredentialChanged() override;
private: // from IRsaProvider
    void* RsaPrivateKey() override;
    void GetRsaPublicKey(Bwx& aKey) override;
private:
    Credential* Find(const Brx& aId) const;
    void CreateKey(Configuration::IStoreReadWrite& aStore, const Brx& aEntropy, TUint aKeyBits);
    void CurrentAdapterChanged();
    void DnsChanged();
    void ScheduleStatusCheck();
    void ModerationTimerCallback();
    void CredentialsThread();
private:
    class KeyParams
    {
    public:
        KeyParams(Configuration::IStoreReadWrite& aStore, const Brx& aEntropy, TUint aKeyBits);
        Configuration::IStoreReadWrite& Store() const;
        const Brx& Entropy() const;
        TUint KeyBits() const;
    private:
        Configuration::IStoreReadWrite& iStore;
        Bwh iEntropy;
        TUint iKeyBits;
    };
private:
    Mutex iLock;
    Environment& iEnv;
    Configuration::IConfigInitialiser& iConfigInitialiser;
    Configuration::IStoreReadWrite& iStore;
    IPowerManager& iPowerManager;
    ProviderCredentials* iProvider;
    void* iKey; /* Type is RSA but don't want to include openssl headers.
                   These define lower case macros which can conflict with functions in our code. */
    std::vector<Credential*> iCredentials;
    Mutex iLockRsaConsumers;
    std::vector<std::pair<TUint, FunctorGeneric<IRsaProvider&>>> iRsaConsumers;
    TUint iNextObserverId;
    Timer* iModerationTimer;
    TBool iModerationTimerStarted;
    Bws<2048> iKeyBuf;
    KeyParams iKeyParams;
    ThreadFunctor* iThread;
    Fifo<Credential*> iFifo;
    TUint iAdapterChangeListenerId;
    TUint iDnsChangeListenerId;
    TBool iStarted;
};

} // namespace Av
} // namespace OpenHome


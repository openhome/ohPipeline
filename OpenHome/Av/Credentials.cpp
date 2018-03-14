#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Exception.h>
#include <OpenHome/PowerManager.h>
#include <OpenHome/Private/NetworkAdapterList.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Private/Timer.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Private/Fifo.h>
#include <OpenHome/Av/Debug.h>
#include <OpenHome/Configuration/IStore.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <OpenHome/Av/ProviderCredentials.h>

#include <vector>
#include "openssl/bio.h"
#include "openssl/pem.h"
#include "openssl/rand.h"

namespace OpenHome {
namespace Av {

class Credential : private ICredentialState
{
    friend class Credentials;

    static const TUint kEventModerationMs = 500;
    static const TUint kEnableNo;
    static const TUint kEnableYes;
    static const TUint kGranularityStatus = 512;
    static const TUint kGranularityData   = 128;
    static const TUint kUsernameMaxBytes  = 512;
    static const TUint kPasswordMaxBytes  = 512;
public:
    Credential(Environment& aEnv, ICredentialConsumer* aConsumer, ICredentialObserver& aObserver,
               Fifo<Credential*>& aFifoCredentialsChanged, Configuration::IConfigInitialiser& aConfigInitialiser,
               Configuration::IStoreReadWrite& aStore, IPowerManager& aPowerManager);
    ~Credential();
    void SetKey(RSA* aKey);
    const Brx& Id() const;
    void Set(const Brx& aUsername, const Brx& aPassword);
    void Clear();
    void Enable(TBool aEnable);
    void SetState(const Brx& aStatus, const Brx& aData);
    void Login(Bwx& aToken);
    void ReLogin(const Brx& aCurrentToken, Bwx& aNewToken);
    ICredentialState& GetState();
private: // from ICredentialState
    void Unlock() override;
    void Username(IWriter& aWriter) override;
    void Password(IWriter& aWriter) override;
    TBool Enabled() const override;
    void Status(IWriter& aWriter) override;
    void Data(IWriter& aWriter) override;
private:
    void DecryptPasswordLocked();
    void EnableChanged(Configuration::KeyValuePair<TUint>& aKvp);
    void ModerationTimerCallback();
    void CheckStatus();
    void ReportChangesLocked();
private:
    Mutex iLock;
    ICredentialConsumer* iConsumer;
    ICredentialObserver& iObserver;
    RSA* iRsa;
    Fifo<Credential*>& iFifoCredentialsChanged;
    Configuration::IStoreReadWrite& iStore;
    StoreText* iStoreUsername;
    StoreText* iStorePasswordEncrypted;
    Configuration::ConfigChoice* iConfigEnable;
    TUint iSubscriberIdEnable;
    Timer* iModerationTimer;
    Bws<kUsernameMaxBytes> iUsername;
    Bws<kPasswordMaxBytes> iPassword;
    Bws<kPasswordMaxBytes> iPasswordEncrypted;
    WriterBwh iStatus;
    WriterBwh iData;
    TBool iEnabled;
    TBool iModerationTimerStarted;
    TBool iStatusUpdatePending;
};

} // namespace Av
} // namespace OpenHome

using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Configuration;


// AutoCredentialState

AutoCredentialState::AutoCredentialState(ICredentialState& aState)
    : iState(aState)
{
}

AutoCredentialState::~AutoCredentialState()
{
    iState.Unlock();
}


// Credential

const TUint Credential::kEnableNo  = 0;
const TUint Credential::kEnableYes = 1;

Credential::Credential(Environment& aEnv,
                       ICredentialConsumer* aConsumer, ICredentialObserver& aObserver,
                       Fifo<Credential*>& aFifoCredentialsChanged,
                       Configuration::IConfigInitialiser& aConfigInitialiser,
                       IStoreReadWrite& aStore,
                       IPowerManager& aPowerManager)
    : iLock("CRED")
    , iConsumer(aConsumer)
    , iObserver(aObserver)
    , iRsa(nullptr)
    , iFifoCredentialsChanged(aFifoCredentialsChanged)
    , iSubscriberIdEnable(IConfigManager::kSubscriptionIdInvalid)
    , iStore(aStore)
    , iStatus(kGranularityStatus)
    , iData(kGranularityData)
    , iEnabled(true)
    , iModerationTimerStarted(false)
    , iStatusUpdatePending(false)
{
    iModerationTimer = new Timer(aEnv, MakeFunctor(*this, &Credential::ModerationTimerCallback), "Credential");
    Bws<64> key(aConsumer->Id());
    key.Append('.');
    key.Append(Brn("Username"));
    iStoreUsername = new StoreText(aStore, aPowerManager, kPowerPriorityNormal, key, Brx::Empty(), kUsernameMaxBytes);
    key.Replace(iConsumer->Id());
    key.Append('.');
    key.Append(Brn("Password"));
    iStorePasswordEncrypted = new StoreText(aStore, aPowerManager, kPowerPriorityNormal, key, Brx::Empty(), kPasswordMaxBytes);
    key.Replace(iConsumer->Id());
    key.Append('.');
    key.Append(Brn("Enabled"));
    std::vector<TUint> choices;
    choices.push_back(kEnableNo);
    choices.push_back(kEnableYes);
    iConfigEnable = new ConfigChoice(aConfigInitialiser, key, choices, kEnableYes);
}

Credential::~Credential()
{
    delete iModerationTimer;
    delete iStoreUsername;
    delete iStorePasswordEncrypted;
    if (iConfigEnable != nullptr) {
        iConfigEnable->Unsubscribe(iSubscriberIdEnable);
        delete iConfigEnable;
    }
    delete iConsumer;
}

void Credential::SetKey(RSA* aKey)
{
    {
        AutoMutex _(iLock);
        iRsa = aKey;
        iStoreUsername->Get(iUsername);
        iStorePasswordEncrypted->Get(iPasswordEncrypted);
        DecryptPasswordLocked();
    }
    iSubscriberIdEnable = iConfigEnable->Subscribe(MakeFunctorConfigChoice(*this, &Credential::EnableChanged));
}

const Brx& Credential::Id() const
{
    return iConsumer->Id();
}

void Credential::Set(const Brx& aUsername, const Brx& aPassword)
{
    TBool changed = false;
    AutoMutex _(iLock);
    if (aUsername != iUsername) {
        iUsername.ReplaceThrow(aUsername);
        changed = true;
        iStoreUsername->Set(aUsername);
        iStoreUsername->Write();
    }
    if (aPassword != iPasswordEncrypted) {
        iPasswordEncrypted.ReplaceThrow(aPassword);
        changed = true;
        iStorePasswordEncrypted->Set(aPassword);
        iStorePasswordEncrypted->Write();
        DecryptPasswordLocked();
    }
    changed = changed || !iEnabled;
    iEnabled = true;
    iStatus.Reset();

    if (changed) {
        iObserver.CredentialChanged();
        if (!iModerationTimerStarted) {
            iModerationTimer->FireIn(kEventModerationMs);
            iModerationTimerStarted = true;
        }
    }
}

void Credential::Clear()
{
    TBool changed;
    {
        AutoMutex _(iLock);
        changed =    iStatus.Buffer().Bytes() > 0
                  || iUsername.Bytes() > 0
                  || iPassword.Bytes() > 0;
        iStatus.Reset();
        iUsername.Replace(Brx::Empty());
        iPassword.Replace(Brx::Empty());
        iPasswordEncrypted.Replace(Brx::Empty());
    }
    if (changed) {
        iObserver.CredentialChanged();
        if (!iModerationTimerStarted) {
            iModerationTimer->FireIn(kEventModerationMs);
            iModerationTimerStarted = true;
        }
    }
}

void Credential::Enable(TBool aEnable)
{
    AutoMutex _(iLock);
    if (iEnabled == aEnable) {
        return;
    }
    iEnabled = aEnable;
    iObserver.CredentialChanged();
    ReportChangesLocked();
}

void Credential::SetState(const Brx& aStatus, const Brx& aData)
{
    AutoMutex _(iLock);
    if (iStatus.Buffer() == aStatus && iData.Buffer() == aData) {
        return;
    }
    iStatus.Reset();
    iStatus.Write(aStatus);
    iData.Reset();
    iData.Write(aData);
    iObserver.CredentialChanged();
}

void Credential::Login(Bwx& aToken)
{
    iConsumer->Login(aToken);
}

void Credential::ReLogin(const Brx& aCurrentToken, Bwx& aNewToken)
{
    iConsumer->ReLogin(aCurrentToken, aNewToken);
}

ICredentialState& Credential::GetState()
{
    iLock.Wait();
    return *this;
}

void Credential::Unlock()
{
    iLock.Signal();
}

void Credential::Username(IWriter& aWriter)
{
    aWriter.Write(iUsername);
}

void Credential::Password(IWriter& aWriter)
{
    aWriter.Write(iPasswordEncrypted);
}

TBool Credential::Enabled() const
{
    return iEnabled;
}

void Credential::Status(IWriter& aWriter)
{
    aWriter.Write(iStatus.Buffer());
}

void Credential::Data(IWriter& aWriter)
{
    aWriter.Write(iData.Buffer());
}

void Credential::DecryptPasswordLocked()
{
    const auto encLen = iPasswordEncrypted.Bytes();
    if (encLen == 0) {
        iPassword.SetBytes(0);
    }
    else {
        ASSERT(iRsa != nullptr);
        const int decryptedLen = RSA_private_decrypt(encLen,
                                                     iPasswordEncrypted.Ptr(),
                                                     const_cast<TByte*>(iPassword.Ptr()),
                                                     iRsa,
                                                     RSA_PKCS1_OAEP_PADDING);
        if (decryptedLen < 0) {
            const Brx& id = Id();
            LOG_ERROR(kApplication6, "Failed to decrypt password for %.*s\n", PBUF(id));
            iPassword.SetBytes(0);
        }
        else {
            iPassword.SetBytes((TUint)decryptedLen);
        }
    }
}

void Credential::EnableChanged(Configuration::KeyValuePair<TUint>& aKvp)
{
    TBool requestTimer = false;
    {
        AutoMutex _(iLock);
        iEnabled = (aKvp.Value() == kEnableYes);
        iObserver.CredentialChanged();
        if (!iModerationTimerStarted) {
            requestTimer = true;
            iModerationTimerStarted = true;
        }
    }
    if (requestTimer) {
        iModerationTimer->FireIn(kEventModerationMs);
    }
}

void Credential::ModerationTimerCallback()
{
    AutoMutex _(iLock);
    iModerationTimerStarted = false;
    ReportChangesLocked();
    if (iEnabled && !iStatusUpdatePending) {
        iFifoCredentialsChanged.Write(this);
        iStatusUpdatePending = true;
    }
}

void Credential::CheckStatus()
{
    {
        AutoMutex _(iLock);
        iStatusUpdatePending = false;
    }
    iConsumer->UpdateStatus();
}

void Credential::ReportChangesLocked()
{
    if (iEnabled) {
        iConsumer->CredentialsChanged(iUsername, iPassword);
    }
    else {
        iConsumer->CredentialsChanged(Brx::Empty(), Brx::Empty());
    }
}


// Credentials

const Brn Credentials::kKeyRsaPrivate("RsaPrivateKey");
const Brn Credentials::kKeyRsaPublic("RsaPublicKey");

Credentials::Credentials(Environment& aEnv,
                         Net::DvDevice& aDevice,
                         IStoreReadWrite& aStore,
                         const Brx& aEntropy,
                         Configuration::IConfigInitialiser& aConfigInitialiser,
                         IPowerManager& aPowerManager,
                         TUint aKeyBits)
    : iLock("CRD1")
    , iEnv(aEnv)
    , iConfigInitialiser(aConfigInitialiser)
    , iStore(aStore)
    , iPowerManager(aPowerManager)
    , iKey(nullptr)
    , iLockRsaConsumers("CRD2")
    , iModerationTimerStarted(false)
    , iKeyParams(aStore, aEntropy, aKeyBits)
    , iThread(nullptr)
    , iFifo(kNumFifoElements)
    , iStarted(false)
{
    iProvider = new ProviderCredentials(aDevice, *this);
    iModerationTimer = new Timer(aEnv, MakeFunctor(*this, &Credentials::ModerationTimerCallback), "Credentials");
    Functor f = MakeFunctor(*this, &Credentials::CurrentAdapterChanged);
    iAdapterChangeListenerId = iEnv.NetworkAdapterList().AddCurrentChangeListener(f, "Credentials", false);
}

Credentials::~Credentials()
{
    iModerationTimerStarted = true; // prevent further callbacks being queued
    iEnv.NetworkAdapterList().RemoveCurrentChangeListener(iAdapterChangeListenerId);
    delete iModerationTimer;
    iFifo.ReadInterrupt();
    delete iThread;
    delete iProvider;
    for (auto it=iCredentials.begin(); it!=iCredentials.end(); ++it) {
        delete *it;
    }
    RSA_free((RSA*)iKey);
}

void Credentials::Add(ICredentialConsumer* aConsumer)
{
    AutoMutex _(iLock);
    Credential* credential = new Credential(iEnv, aConsumer, *this, iFifo, iConfigInitialiser, iStore, iPowerManager);
    iCredentials.push_back(credential);
    if (iStarted) {
        credential->SetKey((RSA*)iKey);
    }
    iProvider->AddId(credential->Id());
}

void Credentials::Start()
{
    if (iCredentials.size() > 0) {
        iThread = new ThreadFunctor("Credentials", MakeFunctor(*this, &Credentials::CredentialsThread), kPriorityLow);
        iThread->Start();
    }
}

void Credentials::GetKey(FunctorGeneric<IRsaProvider&> aCb)
{
    AutoMutex _(iLockRsaConsumers);
    if (iKey != nullptr) {
        aCb(*this);
    }
    else {
        iRsaConsumers.push_back(aCb);
    }
}

void Credentials::SetState(const Brx& aId, const Brx& aStatus, const Brx& aData)
{
    Credential* credential = Find(aId);
    credential->SetState(aStatus, aData);
}

void Credentials::GetPublicKey(Bwx& aKey)
{
    aKey.Replace(iKeyBuf);
}

void Credentials::Set(const Brx& aId, const Brx& aUsername, const Brx& aPassword)
{
    Credential* credential = Find(aId);
    credential->Set(aUsername, aPassword);
}

void Credentials::Clear(const Brx& aId)
{
    Credential* credential = Find(aId);
    credential->Clear();
}

void Credentials::Enable(const Brx& aId, TBool aEnable)
{
    Credential* credential = Find(aId);
    credential->Enable(aEnable);
}

ICredentialState& Credentials::State(const Brx& aId)
{
    Credential* credential = Find(aId);
    return credential->GetState();
}

void Credentials::Login(const Brx& aId, Bwx& aToken)
{
    Credential* credential = Find(aId);
    credential->Login(aToken);
}

void Credentials::ReLogin(const Brx& aId, const Brx& aCurrentToken, Bwx& aNewToken)
{
    Credential* credential = Find(aId);
    credential->ReLogin(aCurrentToken, aNewToken);
}

void Credentials::CredentialChanged()
{
    if (!iModerationTimerStarted) {
        iModerationTimer->FireIn(kModerationTimeMs);
        iModerationTimerStarted = true;
    }
}

void* Credentials::RsaPrivateKey()
{
    return iKey;
}

void Credentials::GetRsaPublicKey(Bwx& aKey)
{
    iKeyParams.Store().Read(kKeyRsaPublic, aKey);
}

Credential* Credentials::Find(const Brx& aId) const
{
    for (auto it=iCredentials.begin(); it!=iCredentials.end(); ++it) {
        if ((*it)->Id() == aId) {
            return *it;
        }
    }
    THROW(CredentialsIdNotFound);
}

static void WriteToStore(IStoreReadWrite& aStore, const Brx& aKey, BIO* aBio)
{
    const int len = BIO_pending(aBio);
    char* val = (char*)calloc(len+1, 1); // +1 for nul terminator
    ASSERT(val != nullptr);
    BIO_read(aBio, val, len);
    Brn valBuf(val);
    aStore.Write(aKey, valBuf);
    free(val);
}

void Credentials::CreateKey(IStoreReadWrite& aStore, const Brx& aEntropy, TUint aKeyBits)
{
    try {
        aStore.Read(kKeyRsaPrivate, iKeyBuf);
        BIO *bio = BIO_new_mem_buf((void*)iKeyBuf.Ptr(), iKeyBuf.Bytes());
        AutoMutex _(iLockRsaConsumers);
        iKey = (void*)PEM_read_bio_RSAPrivateKey(bio, nullptr, 0, nullptr);
        BIO_free(bio);
        for (auto cb : iRsaConsumers) {
            cb(*this);
        }
        return;
    }
    catch (StoreKeyNotFound&) {
    }

    RAND_seed(aEntropy.Ptr(), aEntropy.Bytes());
    BIGNUM *bn = BN_new();
    ASSERT(BN_set_word(bn, RSA_F4));
    RSA* rsa = RSA_new();
    ASSERT(rsa != nullptr);
    ASSERT(RSA_generate_key_ex(rsa, aKeyBits, bn, nullptr));
    BN_free(bn);

    BIO* bio = BIO_new(BIO_s_mem());
    ASSERT(bio != nullptr);
    ASSERT(1 == PEM_write_bio_RSAPrivateKey(bio, rsa, nullptr, nullptr, 0, nullptr, nullptr));
    WriteToStore(aStore, kKeyRsaPrivate, bio);
    ASSERT(1 == PEM_write_bio_RSAPublicKey(bio, rsa));
    WriteToStore(aStore, kKeyRsaPublic, bio);
    BIO_free(bio);

    AutoMutex _(iLockRsaConsumers);
    iKey = (void*)rsa;

    for (auto cb : iRsaConsumers) {
        cb(*this);
    }
}

void Credentials::CurrentAdapterChanged()
{
    for (auto it=iCredentials.begin(); it!=iCredentials.end(); ++it) {
        iFifo.Write(*it);
    }
}

void Credentials::ModerationTimerCallback()
{
    iModerationTimerStarted = false;
    iProvider->NotifyCredentialsChanged();
}

void Credentials::CredentialsThread()
{
    // create private key
    CreateKey(iKeyParams.Store(), iKeyParams.Entropy(), iKeyParams.KeyBits());
    iLock.Wait();
    iStarted = true;
    for (auto it=iCredentials.begin(); it!=iCredentials.end(); ++it) {
        (*it)->SetKey((RSA*)iKey);
    }
    iLock.Signal();
    iKeyParams.Store().Read(kKeyRsaPublic, iKeyBuf);
    iProvider->SetPublicKey(iKeyBuf);

    // run any NotifyCredentialsChanged() callbacks
    // these are potentially slow so can't be run directly from the timer thread
    try {
        for (;;) {
            Credential* c = iFifo.Read();
            c->CheckStatus();
        }
    }
    catch (FifoReadError&) {
    }
}


// Credentials::KeyParams

Credentials::KeyParams::KeyParams(Configuration::IStoreReadWrite& aStore, const Brx& aEntropy, TUint aKeyBits)
    : iStore(aStore)
    , iEntropy(aEntropy)
    , iKeyBits(aKeyBits)
{
}

Configuration::IStoreReadWrite& Credentials::KeyParams::Store() const
{
    return iStore;
}

const Brx& Credentials::KeyParams::Entropy() const
{
    return iEntropy;
}

TUint Credentials::KeyParams::KeyBits() const
{
    return iKeyBits;
}

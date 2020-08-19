#include <OpenHome/Av/Debug.h>
#include <OpenHome/Av/Credentials.h>
#include <OpenHome/Av/ProviderOAuth.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Configuration/ConfigManager.h>

#include <vector>

#include "openssl/rsa.h"
#include "openssl/aes.h"


using namespace OpenHome;
using namespace OpenHome::Av;
using namespace OpenHome::Net;
using namespace OpenHome::Configuration;

namespace OpenHome {
namespace Av {

class ServiceProvider
{
    public:
        ServiceProvider(const Brx& aServiceId,
                        Environment&,
                        IThreadPool&,
                        const TUint aNumTokens,
                        IOAuthAuthenticator&,
                        IConfigManager&,
                        IStoreReadWrite&,
                        ITokenManagerObserver&);
        ~ServiceProvider();

    public:
        const Brx& ServiceId() const { return iServiceId; }

        ITokenProvider* TokenProvider() { return iTokenManager; }

        void AddToken(const Brx& aId,
                      const Brx& aRefreshToken) { iTokenManager->AddToken(aId, aRefreshToken); }

        void RemoveToken(const Brx& aId) { iTokenManager->RemoveToken(aId); }

        void ClearTokens() { iTokenManager->ClearTokens(); }

        void ToJson(WriterJsonObject& aWriter);

    private:
        ConfigChoice& GetEnabledConfigValue();
        void EnabledChanged(Configuration::KeyValuePair<TUint>& aConfigValue);

    private:
        const Brx& iServiceId;
        TokenManager* iTokenManager;
        ITokenManagerObserver& iObserver;
        IConfigManager& iConfigManager;
        TUint iConfigEnabledSubscription;
        TBool iServiceEnabled;
};

} //namespace AV
} //namespace OpenHome





/* ***************
 * ServiceProvider
 * *************** */
ServiceProvider::ServiceProvider(const Brx& aServiceId,
                                 Environment& aEnv,
                                 IThreadPool& aThreadPool,
                                 const TUint aNumTokens,
                                 IOAuthAuthenticator& aServiceAuthenticator,
                                 IConfigManager& aConfigManager,
                                 IStoreReadWrite& aStore,
                                 ITokenManagerObserver& aObserver)
    : iServiceId(aServiceId),
      iObserver(aObserver),
      iConfigManager(aConfigManager)
{
    ConfigChoice& choice = GetEnabledConfigValue();
    iConfigEnabledSubscription = choice.Subscribe(MakeFunctorConfigChoice(*this, &ServiceProvider::EnabledChanged));

    // Subscribe to it's value changes!!!
    //  - When a value does change, then we should grab a copy of the value
    //  - Notify observes we've changed :)


    iTokenManager = new TokenManager(aServiceId, aNumTokens, aEnv, aThreadPool, aServiceAuthenticator, aStore, aObserver);
}

ServiceProvider::~ServiceProvider()
{
    ConfigChoice& choice = GetEnabledConfigValue();
    choice.Unsubscribe(iConfigEnabledSubscription);

    delete iTokenManager;
}

void ServiceProvider::ToJson(WriterJsonObject& aWriter)
{
    aWriter.WriteString("id" ,iServiceId);
    aWriter.WriteBool("visible", iServiceEnabled);
    aWriter.WriteInt("tokenMax", iTokenManager->Capacity());

    iTokenManager->TokenStateToJson(aWriter);

    aWriter.WriteEnd();
}


ConfigChoice& ServiceProvider::GetEnabledConfigValue()
{
    Bws<256> key(iServiceId);
    key.Append('.');
    key.Append("Enabled");

    return iConfigManager.GetChoice(key);
}

void ServiceProvider::EnabledChanged(Configuration::KeyValuePair<TUint>& aConfigValue)
{
    iServiceEnabled = aConfigValue.Value() == ENABLED_YES;
    iObserver.OnTokenChanged(); //Notify observer so things can happen! :)
}


/* *************
 * ProviderOAuth
 * ************* */

static const TUint kServiceIdNotFoundCode = 800;
static const Brn kServiceIdNotFoundMsg("Service with matching Id not found");

static const TUint kTokenInvalidCode = 801;
static const Brn kTokenInvalidMsg("Token invalid");

static const TUint kParameterInvalidCode = 802;
static const Brn kParameterTokenIdInvalidMsg("Parameter invalid. (TokenId)");
static const Brn kParameterAesKeyInvalidMsg("Parameter invalid. (AESKey)");
static const Brn kParameterInitVectoryMsg("Parameter invalid. (InitVector)");
static const Brn kParameterTokenMsg("Parameter invalid. (Token)");

static const TUint kDecryptionFailedCode = 803;
static const Brn kDecryptionFailedMsg("Failed to decrypt provided token");

static const TUint kTokenIdNotFoundCode = 804;
static const Brn kTokenIdNotFoundMsg("Token with matching Id not found");


ProviderOAuth::ProviderOAuth(Net::DvDevice& aDevice,
                             Environment& aEnv,
                             IThreadPool& aThreadPool,
                             IRsaObservable& aRsaObservable,
                             Configuration::IConfigManager& aConfigManager,
                             Configuration::IStoreReadWrite& aStore)
    : DvProviderAvOpenhomeOrgOAuth1(aDevice),
      iEnv(aEnv),
      iThreadPool(aThreadPool),
      iRsaObservable(aRsaObservable),
      iConfigManager(aConfigManager),
      iStore(aStore),
      iLockRsa("OAuth::RSA"),
      iLockProviders("OAuth::PVD"),
      iLockModerator("OAuth::MOD"),
      iRsa(nullptr),
      iUpdateId(0)
{
    EnablePropertyPublicKey();
    EnablePropertyUpdateId();
    EnablePropertySupportedServices();

    EnableActionGetPublicKey();
    EnableActionSetToken();
    EnableActionClearToken();
    EnableActionClearTokens();
    EnableActionGetServiceStatusUpdateId();
    EnableActionGetServiceStatus();
    EnableActionGetSupportedServices();

    SetPropertyPublicKey(Brx::Empty());
    SetPropertyUpdateId(0);
    SetPropertySupportedServices(Brn("[]"));

    iKeyObserver = iRsaObservable.AddObserver(MakeFunctorGeneric(*this, &ProviderOAuth::RsaKeySet));

    iModeratorTimer = new Timer(aEnv, MakeFunctor(*this, &ProviderOAuth::UpdateIdSet), "OAuthModerator");
}

ProviderOAuth::~ProviderOAuth()
{
    delete iModeratorTimer;

    iRsaObservable.RemoveObserver(iKeyObserver);

    for(auto p : iProviders)
    {
        delete p;
    }
}


void ProviderOAuth::AddService(const Brx& aServiceId,
                               const TUint aMaxTokens,
                               IOAuthAuthenticator& aAuthenticator)
{
   AutoMutex m(iLockProviders);

    ServiceProvider* newProvider = new ServiceProvider(aServiceId, 
                                                       iEnv,
                                                       iThreadPool, 
                                                       aMaxTokens,
                                                       aAuthenticator,
                                                       iConfigManager,
                                                       iStore,
                                                       *this);

    iProviders.push_back(newProvider);

    Bwh buf(2048);
    WriterBuffer writer(buf);
    WriterJsonArray json(writer);

    for(auto val : iProviders)
        json.WriteString(val->ServiceId());

    json.WriteEnd();

    SetPropertySupportedServices(buf);
}



ITokenProvider* ProviderOAuth::GetTokenProvider(const Brx& aServiceId)
{
    AutoMutex m(iLockProviders);

    ServiceProvider* provider = GetProviderLocked(aServiceId);
    if (provider == nullptr)
        return nullptr;

    return provider->TokenProvider();
}



void ProviderOAuth::GetPublicKey(Net::IDvInvocation& aInvocation,
                                 IDvInvocationResponseString& aPublicKey)
{
    Brhz key;
    GetPropertyPublicKey(key);

    aInvocation.StartResponse();
    aPublicKey.Write(key);
    aPublicKey.WriteFlush();
    aInvocation.EndResponse();
}



void ProviderOAuth::SetToken(IDvInvocation& aInvocation, 
                             const Brx& aServiceId,
                             const Brx& aTokenId,
                             const Brx& aAesKeyRsaEncrypted,
                             const Brx& aInitVectorRsaEncrypted,
                             const Brx& aTokenAesEncrypted)
{
    ValidateSetTokenParams(aInvocation,
                           aTokenId,
                           aAesKeyRsaEncrypted,
                           aInitVectorRsaEncrypted,
                           aTokenAesEncrypted);

     AutoMutex mRsa(iLockRsa);

    if (iRsa == nullptr)
    {
        LOG_ERROR(kOAuth, "ProviderOAuth::SetToken failed - no RSA key\n");
        aInvocation.Error(kDecryptionFailedCode, kDecryptionFailedMsg);
    }

    unsigned char aesKeyData[16];
    int decryptedLen = RSA_private_decrypt(aAesKeyRsaEncrypted.Bytes(),
                                           aAesKeyRsaEncrypted.Ptr(),
                                           aesKeyData,
                                           static_cast<RSA*>(iRsa),
                                           RSA_PKCS1_OAEP_PADDING);
    if (decryptedLen < 0) 
    {
        LOG_ERROR(kOAuth, "ProviderOAuth::SetToken failed - could not decode AES key\n");
        aInvocation.Error(kDecryptionFailedCode, kDecryptionFailedMsg);
    }

    AES_KEY aesKey;
    AES_set_decrypt_key(aesKeyData, 128, &aesKey);

    unsigned char initVector[16];
    decryptedLen = RSA_private_decrypt(aInitVectorRsaEncrypted.Bytes(),
                                       aInitVectorRsaEncrypted.Ptr(),
                                       initVector,
                                       static_cast<RSA*>(iRsa),
                                       RSA_PKCS1_OAEP_PADDING);
    if (decryptedLen < 0) 
    {
        LOG_ERROR(kOAuth, "ProviderOAuth::SetToken failed - could not decode initVector\n");
        aInvocation.Error(kDecryptionFailedCode, kDecryptionFailedMsg);
    }

    Bws<OAuth::kMaxTokenBytes> tokenBuf;

    AES_cbc_encrypt(aTokenAesEncrypted.Ptr(),
                    const_cast<TByte*>(tokenBuf.Ptr()),
                    aTokenAesEncrypted.Bytes(),
                    &aesKey,
                    initVector,
                    AES_DECRYPT);

    tokenBuf.SetBytes(aTokenAesEncrypted.Bytes());

    const TUint len = Converter::BeUint16At(tokenBuf, 0);
    if (len > tokenBuf.Bytes() - 2) 
    {
        LOG_ERROR(kOAuth, "ProviderOAuth::SetAssociated failed - token begins with length %u\n", len);
        aInvocation.Error(kDecryptionFailedCode, kDecryptionFailedMsg);
    }

    Brn token;
    token.Set(tokenBuf.Ptr() + 2, len);


    try
    {
        AutoMutex mProviders(iLockProviders);

        ServiceProvider* provider = GetProviderLocked(aServiceId);

        if (provider == nullptr)
        {
            THROW(ServiceIdNotFound);
        }

        provider->AddToken(aTokenId, token);
    }
    catch (ServiceIdNotFound&)
    {
        aInvocation.Error(kServiceIdNotFoundCode, kServiceIdNotFoundMsg);
    }
    catch (OAuthTokenInvalid&)
    {
        aInvocation.Error(kTokenInvalidCode, kTokenInvalidMsg);
    }

    aInvocation.StartResponse();
    aInvocation.EndResponse();
}


void ProviderOAuth::ClearToken(IDvInvocation& aInvocation,
                               const Brx& aServiceId,
                               const Brx& aTokenId)
{
    try 
    {
        AutoMutex m(iLockProviders);

        ServiceProvider* provider = GetProviderLocked(aServiceId);

        if (provider == nullptr)
            THROW(ServiceIdNotFound);

        provider->RemoveToken(aTokenId);
    }
    catch (ServiceIdNotFound&)
    {
        aInvocation.Error(kServiceIdNotFoundCode, kServiceIdNotFoundMsg);
    }
    catch (OAuthTokenIdNotFound&)
    {
        aInvocation.Error(kTokenIdNotFoundCode, kTokenIdNotFoundMsg);
    }

    aInvocation.StartResponse();
    aInvocation.EndResponse();
}


void ProviderOAuth::ClearTokens(IDvInvocation& aInvocation,
                                const Brx& aServiceId)
{
    try
    {
        AutoMutex m(iLockProviders);

        ServiceProvider* provider = GetProviderLocked(aServiceId);

        if (provider == nullptr)
            THROW(ServiceIdNotFound);

        provider->ClearTokens();
    }
    catch (ServiceIdNotFound&)
    {
        aInvocation.Error(kServiceIdNotFoundCode, kServiceIdNotFoundMsg);
    }
    catch (OAuthTokenIdNotFound&)
    {
        aInvocation.Error(kTokenIdNotFoundCode, kTokenIdNotFoundMsg);
    }

    aInvocation.StartResponse();
    aInvocation.EndResponse();
}


void ProviderOAuth::GetServiceStatusUpdateId(Net::IDvInvocation& aInvocation,
                                             Net::IDvInvocationResponseUint& aUpdateId)
{
    AutoMutex m(iLockModerator);

    aInvocation.StartResponse();
    aUpdateId.Write(iUpdateId);
    aInvocation.EndResponse();
}

void ProviderOAuth::GetSupportedServices(Net::IDvInvocation& aInvocation, Net::IDvInvocationResponseString& aSupportedServices)
{
    Brhz buf;
    GetPropertySupportedServices(buf);

    aInvocation.StartResponse();
    aSupportedServices.Write(buf);
    aSupportedServices.WriteFlush();
    aInvocation.EndResponse();
}


void ProviderOAuth::RsaKeySet(IRsaProvider& aRsaProvider)
{
    AutoMutex _(iLockRsa);

    iRsa = aRsaProvider.RsaPrivateKey();

    aRsaProvider.GetRsaPublicKey(iKeyBuf);

    SetPropertyPublicKey(iKeyBuf);
}



void ProviderOAuth::ValidateSetTokenParams(IDvInvocation& aInvocation,
                                           const Brx& aTokenId,
                                           const Brx& aAesKeyRsaEncrypted,
                                           const Brx& aInitVectorRsaEncrypted,
                                           const Brx& aTokenAesEncrypted)
{
    if (aTokenId == Brx::Empty())
    {
        aInvocation.Error(kParameterInvalidCode, kParameterTokenIdInvalidMsg);
    }

    if (aAesKeyRsaEncrypted == Brx::Empty())
    {
        aInvocation.Error(kParameterInvalidCode, kParameterAesKeyInvalidMsg);
    }

    if (aInitVectorRsaEncrypted == Brx::Empty())
    {
        aInvocation.Error(kParameterInvalidCode, kParameterInitVectoryMsg);
    }

    if (aTokenAesEncrypted == Brx::Empty())
    {
        aInvocation.Error(kParameterInvalidCode, kParameterTokenMsg);
    }
}


void ProviderOAuth::GetServiceStatus(IDvInvocation& aInvocation,
                                     IDvInvocationResponseString& aServiceStatusJson)
{
    AutoMutex m(iLockProviders);

    aInvocation.StartResponse();

    /* This JSON could get quite big in the future, with multiple services
     * each having many tokens that are quite large. Rather than try and
     * buffer it on the DS, we simply write it everytime a CP requests it */
    WriterJsonObject jsonWriter(aServiceStatusJson);

    jsonWriter.WriteInt("updateId", iUpdateId);

    WriterJsonArray serviceWriter = jsonWriter.CreateArray("services");

    for(auto val : iProviders)
    {
        WriterJsonObject providerWriter = serviceWriter.CreateObject();
        val->ToJson(providerWriter);
    }

    serviceWriter.WriteEnd();
    jsonWriter.WriteEnd();

    aServiceStatusJson.WriteFlush();

    aInvocation.EndResponse();
}



ServiceProvider* ProviderOAuth::GetProviderLocked(const Brx& aServiceId)
{
    for (auto p : iProviders)
    {
        if (p->ServiceId() == aServiceId)
            return p;
    }

    return nullptr;
}


void ProviderOAuth::UpdateIdSet()
{
    AutoMutex m(iLockModerator);

    iUpdateId++;

    SetPropertyUpdateId(iUpdateId);

}


void ProviderOAuth::OnTokenChanged()
{
    AutoMutex m(iLockModerator);

    iModeratorTimer->Cancel();
    iModeratorTimer->FireIn(kModerationTimeout);
}

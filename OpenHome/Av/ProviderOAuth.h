#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/OAuth.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Timer.h>
#include <OpenHome/Configuration/IStore.h>
#include <Generated/DvAvOpenhomeOrgOAuth1.h>
#include <OpenHome/Net/Core/DvInvocationResponse.h>


EXCEPTION(ServiceIdNotFound);

namespace OpenHome {

namespace Configuration {

    class IConfigManager;

} //namespace Configuration

namespace Av {

class IRsaProvider;
class IRsaObservable; //From Credentials

class ServiceProvider;


class ProviderOAuth: public Net::DvProviderAvOpenhomeOrgOAuth1,
                     public ITokenManagerObserver
{
    public:
        static const TUint kModerationTimeout = 1000;

    public:
        ProviderOAuth(Net::DvDevice&,
                      Environment&,
                      IThreadPool&,
                      IRsaObservable& aRsaProvider,
                      Configuration::IConfigManager&,
                      Configuration::IStoreReadWrite&);
        ~ProviderOAuth();

    public:
        void AddService(const Brx& aServiceId,
                        const TUint aMaxTokens,
                        IOAuthAuthenticator& aAuthenticator);

        ITokenProvider* GetTokenProvider(const Brx& aServiceId);

    private: // ITokenManagerObserver
        void OnTokenChanged() override;

    private: // from Net::DvProviderAvOpenhomeOrgOAuth1
        void GetPublicKey(Net::IDvInvocation& aInvocation,
                          Net::IDvInvocationResponseString& aPublicKey) override;

        void GetSupportedServices(Net::IDvInvocation& aInvocation,
                                  Net::IDvInvocationResponseString& aSupportedServices) override;

        void SetToken(Net::IDvInvocation& aInvocation,
                      const Brx& aServiceId,
                      const Brx& aTokenId,
                      const Brx& aAesKeyRsaEncrypted,
                      const Brx& aInitVectorRsaEncrypted,
                      const Brx& aTokenAesEncrypted) override;

        void ClearToken(Net::IDvInvocation& aInvocation,
                        const Brx& aServiceId,
                        const Brx& aTokenId) override;

        void ClearTokens(Net::IDvInvocation& aInvocation,
                         const Brx& aServiceId) override;

        void GetUpdateId(Net::IDvInvocation& aInvocation,
                         Net::IDvInvocationResponseUint& aUpdateId) override;

        void GetServiceStatus(Net::IDvInvocation& aInvocation,
                              Net::IDvInvocationResponseString& aServiceStatusJson) override;

    private:
        void ValidateSetTokenParams(Net::IDvInvocation& aInvocation,
                                    const Brx& aTokenId,
                                    const Brx& aAesKeyRsaEncrypted,
                                    const Brx& aInitVectorRsaEncrypted,
                                    const Brx& aTokenAesEncrypted);

        ServiceProvider* GetProviderLocked(const Brx& aServiceId);

        void RsaKeySet(IRsaProvider&);

        void UpdateIdSet();
        void OnModerationTimerExpired();

    private:
        Environment& iEnv;
        IThreadPool& iThreadPool;
        IRsaObservable& iRsaObservable;
        Configuration::IConfigManager& iConfigManager;
        Configuration::IStoreReadWrite& iStore;
        Mutex iLockRsa;
        Mutex iLockProviders;
        Mutex iLockModerator;
        void* iRsa; /* Type is RSA but don't want to include openssl headers.
                       These define lower case macros which can conflict with functions in our code. */
        std::vector<ServiceProvider*> iProviders;
        Bws<1024> iKeyBuf;
        Timer* iModeratorTimer;
        TUint iUpdateId;
        TUint iKeyObserver;
};

} // namespace Av
} // namespace OpenHome


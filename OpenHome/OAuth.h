#pragma once

#include <OpenHome/Json.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Stream.h>
#include <OpenHome/Configuration/IStore.h>

#include <list>

EXCEPTION(OAuthTokenInvalid);
EXCEPTION(OAuthTokenIdNotFound);


namespace OpenHome
{
    class IThreadPool;
    class IThreadPoolHandle;

namespace Av
{
    class Credentials;

namespace TestOAuth
{
    class SuiteOAuthToken;
    class SuiteTokenManager;
} //namespace TestOAuth
} //namespace Av



class OAuth
{
    public:
        static const TUint kMaxTokenBytes = 2048;

        // OAuth request parameters
        static const Brn kParameterRefreshToken;
        static const Brn kParameterClientId;
        static const Brn kParameterClientSecret;
        static const Brn kParameterScope;
        static const Brn kParameterGrantType;

        // OAuth Grant types
        static const Brn kGrantTypeRefreshToken;

        // OAuth Token Response fields
        static const Brn kTokenResponseFieldTokenType;
        static const Brn kTokenResponseFieldTokenExpiry;
        static const Brn kTokenResponseFieldAccessToken;
        static const Brn kTokenResponseFieldRefreshToken;

        // OAuth Error Response fields
        static const Brn kErrorResponseFieldError;
        static const Brn kErrorResponseFieldErrorDescription;

    public:
        static void WriteAccessTokenHeader(WriterHttpHeader&,
                                           const Brx& aAccessToken);

        static void ConstructRefreshTokenRequestBody(IWriter&,
                                                     const Brx& aRefreshToken,
                                                     const Brx& aClientId,
                                                     const Brx& aClientSecret,
                                                     const Brx& aTokenScope);
};


enum class TokenType : TByte
{
    UsernamePassword,
    OAuth,
};


/* Response fields used when requesting an access token.
 * RefreshToken field may not be populated if the service 
 * doesn't provide one. */
struct AccessTokenResponse
{
    TUint tokenExpiry;
    Brn  accessToken;
    Brn  refreshToken;
};


struct ServiceToken
{
    TokenType type;
    Brn       token;
};



class IOAuthAuthenticator
{
    public:     
        virtual ~IOAuthAuthenticator() {}


        /* Will be called by token management when token is added, to
         * confirm that given refresh token is valid.
         * 
         * Subsequent store refreshes will also call this method. */
        virtual TBool TryGetAccessToken(const Brx& aTokenId,
                                        const Brx& aRefreshToken,
                                        AccessTokenResponse& aResponse) = 0;

        /* Called by token management to assign a username to a token.
         * This helps Control Points differentiate between tokens
         * as hopefully this'll resolve to something human readable like
         * an email address or nickname.
         * This will be called each time a token has successfully updated. */
        virtual TBool TryGetUsernameFromToken(const Brx& aTokenId,
                                              const Brx& aAccessToken,
                                              IWriter& aUsername) = 0;


        /* Events called by the token manager to notify when tokens have
         * changed or been removed entirely.
         * Can be used by services that need to fetch additional information on
         * an change or removal. */
        virtual void OnTokenRemoved(const Brx& aTokenId,
                                    const Brx& aAccessToken) = 0;
};
    
class ITokenObserver
{
    public:
        virtual ~ITokenObserver() {}


        virtual void TokenExpired(const Brx& aId) = 0;        
};

class ITokenProvider
{
    public:
        virtual ~ITokenProvider() {}


        virtual TBool HasToken(const Brx& aId) = 0;

        virtual TBool EnsureTokenIsValid(const Brx& aId) = 0;

        virtual TBool TryGetToken(const Brx& aId,
                                  ServiceToken& aToken) = 0;

        virtual TBool TryGetFirstValidTokenId(IWriter& writer) = 0;

};

class ITokenManagerObserver
{
    public:
        virtual ~ITokenManagerObserver() {}


        virtual void OnTokenChanged() = 0;
};


/* Stores information regarding a single access token for 
 * a service. Internal timer will call observer when the 
 * token has expired. */
class OAuthToken
{
    friend class TokenManager;
    friend class Av::TestOAuth::SuiteOAuthToken;

    static const TUint kIdGranularity = 128;
    static const TUint kUsernameGranularity = 64;

    public:
        OAuthToken(Environment& aEnv,
                   ITokenObserver& aObserver);
        ~OAuthToken();

    public:
        const Brx& Id();

        const Brx& AccessToken() const;
        const Brx& RefreshToken() const;
        const Brx& Username() const;

        TBool IsPresent() const;
        TBool IsLongLived() const;
        TBool HasExpired() const;
        const TByte RetryCount() const;
        TBool CanRefresh(TUint aMaxRetryCount) const;

        void UpdateToken(const Brx& aNewAccessToken,
                         TUint aTokenExpiry,
                         const Brx& aUsername);

        void Set(const Brx& aId,
                 const Brx& aRefreshToken,
                 TBool aIsLongLived);

        void SetWithAccessToken(const Brx& aId,
                                const Brx& aRefreshToken,
                                TBool aIsLongLived,
                                const Brx& aAccessToken,
                                TUint aTokenExpiry,
                                const Brx& aUsername);

        void Clear();
        void NotifyFailedRefresh();

    private:
        void OnTokenExpired();
        void ToJson(WriterJsonObject& aWriter);

    private:
        TBool iHasExpired;
        TBool iIsLongLived;
        TByte iRetryCount;
        WriterBwh iId;
        WriterBwh iUsername;
        Bws<OAuth::kMaxTokenBytes> iAccessToken;
        Bws<OAuth::kMaxTokenBytes> iRefreshToken;
        ITokenObserver& iObserver;
        Timer* iTimer;
};



/* Stores a collection of tokens and their refresh tokens.
 * Schedules a refresh of a token that has expired and stored
 * refresh tokens allowing them to survive between reboots */
class TokenManager : public ITokenObserver,
                     public ITokenProvider
{
    friend class Av::TestOAuth::SuiteTokenManager;

    public:
        static const TUint kMaxShortLivedTokens = 10;
        static const TUint kMaxLongLivedTokens = 5;

    public:
        enum ETokenTypeSelection
        {
            ShortLived,
            LongLived,
            All,
        };

    public:
        TokenManager(const Brx& aServiceId,
                     TUint aMaxShortLivedCapacity,
                     TUint aMaxLongLivedCapacity,
                     Environment&,
                     IThreadPool&,
                     IOAuthAuthenticator&,
                     Configuration::IStoreReadWrite&,
                     ITokenManagerObserver&);
        virtual ~TokenManager();

    public:
        const Brx& ServiceId() const;
        TUint ShortLivedCapacity() const;
        TUint LongLivedCapacity() const;

        void AddToken(const Brx& aId,
                      const Brx& aRefreshToken,
                      TBool aIsLongLived);

        void RemoveToken(const Brx& aId, ETokenTypeSelection tokenType);

        void ClearShortLivedTokens();
        void ClearLongLivedTokens();
        void ClearAllTokens();

        TUint NumberOfStoredTokens() const;

        void TokenStateToJson(WriterJsonObject& aWriter);

    public: //ITokenObserver
        void TokenExpired(const Brx& aId) override;

    public: // ITokenProvider
        TBool HasToken(const Brx& aId) override;

        TBool EnsureTokenIsValid(const Brx& aId) override;

        TBool TryGetToken(const Brx& aId,
                          ServiceToken& aToken) override;

        TBool TryGetFirstValidTokenId(IWriter& writer) override;

    private:
        void RefreshTokens();
        TBool CheckSpaceAvailableLocked(TBool aIsLongLoved) const;
        TBool InsertTokenLocked(const Brx& aId,
                                TBool aIsLongLived,
                                const Brx& aRefreshToken,
                                const Brx& aAccessToken = Brx::Empty(),
                                TUint aTokenExpiry = 0,
                                const Brx& aUsername = Brx::Empty());
        void RemoveTokenLocked(OAuthToken* aToken);

        OAuthToken* FindTokenLocked(const Brx& aTokenId) const;
        OAuthToken* FindTokenLocked(const Brx& aTokenId, TBool aIsLongLived) const;
        TBool IsTokenPtrPresentLocked(OAuthToken* aTokenPtr) const;

        void MoveTokenToFrontOfList(OAuthToken*);
        void MoveTokenToEndOfList(OAuthToken*);

        TBool ValidateToken(const Brx& aId,
                            const Brx& aRefreshToken,
                            AccessTokenResponse& aResponse,
                            IWriter& aUsername);

        void DoClearTokens(ETokenTypeSelection operation);

        // Token Storage
        void LoadStoredTokens(ETokenTypeSelection operation);
        void StoreTokenIdsLocked(ETokenTypeSelection operation);
        void StoreTokenLocked(const Brx& aTokenId,
                              const Brx& aRefreshToken);
        void RemoveStoredTokenLocked(const Brx& aTokenId);

        // Testing helpers
        void ExpireToken(const Brx& aId);

    private:
        const Brx& iServiceId;
        const TUint iMaxShortLivedCapacity;
        const TUint iMaxLongLivedCapacity;
        mutable Mutex iLock;
        Environment& iEnv;
        WriterBwh iUsernameBuffer;
        WriterBwh iStoreKeyBuffer;
        WriterBwh iTokenIdsBuffer;
        std::list<OAuthToken*> iShortLivedTokens;
        std::list<OAuthToken*> iLongLivedTokens;
        IThreadPoolHandle* iRefresherHandle;
        IOAuthAuthenticator& iAuthenticator;
        Configuration::IStoreReadWrite& iStore;
        ITokenManagerObserver& iObserver;
};


} // namespace OpenHome

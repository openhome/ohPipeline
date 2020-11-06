#pragma once

#include <OpenHome/Json.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Exception.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Timer.h>
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
        enum class PollResult
        {
            Poll,      // Token not ready, try again
            SlowDown,  // We're polling too quickly, slow down and try again.
            Failed,    // Something went wrong when attempting to poll
            Success,   // Login process completed and we now have a token!
        };

    public:
        static const TUint kMaxTokenBytes = 2048;

        // OAuth request parameters
        static const Brn kParameterRefreshToken;
        static const Brn kParameterClientId;
        static const Brn kParameterClientSecret;
        static const Brn kParameterScope;
        static const Brn kParameterGrantType;
        static const Brn kParameterDeviceCode;

        // OAuth Grant types
        static const Brn kGrantTypeRefreshToken;
        static const Brn kGrantTypeDeviceCode;

        // OAuth Token Response fields
        static const Brn kTokenResponseFieldTokenType;
        static const Brn kTokenResponseFieldTokenExpiry;
        static const Brn kTokenResponseFieldAccessToken;
        static const Brn kTokenResponseFieldRefreshToken;

        // OAuth Error Response fields
        static const Brn kErrorResponseFieldError;
        static const Brn kErrorResponseFieldErrorDescription;

        // OAuth Limited Input Flow Polling responses
        static const Brn kPollingStateTryAgain;
        static const Brn kPollingStateSlowDown;

    public:
        static void WriteAccessTokenHeader(WriterHttpHeader&,
                                           const Brx& aAccessToken);

        static void ConstructRefreshTokenRequestBody(IWriter&,
                                                     const Brx& aRefreshToken,
                                                     const Brx& aClientId,
                                                     const Brx& aClientSecret,
                                                     const Brx& aTokenScope);

        static void WriteRequestToStartLimitedInputFlowBody(IWriter& aWriter,
                                                            const Brx& aClientId,
                                                            const Brx& aTokenScope);

        static void WriteTokenPollRequestBody(IWriter& aWriter,
                                              const Brx& aClientId,
                                              const Brx& aClientSecret,
                                              const Brx& aTokenScope,
                                              const Brx& aDeviceCode);
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


/* Internal details for limited input flow, returned from a service */
class LimitedInputFlowDetails
{
public:
    const Brx& UserUrl() const { return iUserUrl; }
    const Brx& AuthCode() const { return iAuthCode; }
    const Brx& DeviceCode() const { return iDeviceCode; }
    const TUint SuggestedPollingInterval() const { return iSuggestedPollingInterval; }

    void Set(const Brx& aUserUrl,
             const Brx& aAuthCode,
             const Brx& aDeviceCode,
             TUint aSuggestedPollingInterval)
    {
        iUserUrl.Set(aUserUrl);
        iAuthCode.Replace(aAuthCode);
        iDeviceCode.Set(aDeviceCode);
        iSuggestedPollingInterval = aSuggestedPollingInterval;
    }

private:
    Brh      iUserUrl;                   // Link for client to visit to authorise the DS
    Bws<16>  iAuthCode;                  // Code for user to enter
    Brh      iDeviceCode;                // Internal device code used to poll
    TUint    iSuggestedPollingInterval;  // Suggested polling interval to use
};

/* Information required by a client in order to conduct Limited Input
 * Flow.
 * If there are multiple urls given by a service (i.e: TIDAL provide
 * an auto-complete and non-autocomplete option) then the DS should
 * pick a suitable one to return to clients. */
class PublicLimitedInputFlowDetails
{
    public:
        const Brx& JobId() const { return iJobId; }
        const Brx& UserUrl() const { return iUserUrl; }
        const Brx& AuthCode() const { return iAuthCode; }

        void Set(const Brx& aJobId,
                 const Brx& aUserUrl,
                 const Brx& aAuthCode)
        {
            iJobId.Set(aJobId);
            iUserUrl.Set(aUserUrl);
            iAuthCode.Replace(aAuthCode);
        }

    private:

        Brn     iJobId;      // JobId, and later the given TokenId clients can use
        Brh     iUserUrl;    // Link for client to visit to authorise the DS
        Bws<16> iAuthCode;   // Code for user to enter
};


/* Internal struct used by polling sub-system to request
 * a poll for a given service.
 * Using 'Brn' here as the backing memory for the job
 * will always be present */
class OAuthPollRequest
{
    public:
        OAuthPollRequest()
            : OAuthPollRequest(Brx::Empty(), Brx::Empty())
        { }
        OAuthPollRequest(const Brx& aJobId,
                         const Brx& aDeviceCode)
            : iJobId(aJobId)
            , iDeviceCode(aDeviceCode)
        { }

        const Brx& JobId() const { return iJobId; }
        const Brx& DeviceCode() const { return iDeviceCode; }

    private:
        Brn iJobId;
        Brn iDeviceCode;
};

/* Internal struct used by service code to report a poll
 * completion.
 * Using 'Brn' here as the backing memory for the jobId will
 * always be present. Backing memory for the refreshToken
 * should be copied by result observer when recieved. */
class OAuthPollResult
{
    public:
        OAuthPollResult(const Brx& aJobId)
            : iJobId(aJobId)
        { }

        const Brx& JobId() const { return iJobId; }
        const Brx& RefreshToken() const { return iRefreshToken; }
        OAuth::PollResult PollResult() const { return iPollResult; }

        void Set(OAuth::PollResult aResult)
        {
            Set(aResult, Brx::Empty());
        }

        void Set(OAuth::PollResult aResult,
                 const Brx& aRefreshToken)
        {
            iPollResult = aResult;
            iRefreshToken = Brn(aRefreshToken);
        }

    private:
        Brn               iJobId;
        OAuth::PollResult iPollResult;
        Brn               iRefreshToken;
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

/* Limited Input support.
 *
 * A 'OAuthPollingManager' will construct a 'job' each time a
 * client requests we start this flow. We then request a 'Poller'
 * implementer to provide us with polling & client details.
 *
 * Internally this job has an assicated timer which will expire
 * after a polling interval. Upon expiry, we request a poll from
 * the 'Poller' implementer who will callback with a result some
 * point in the future.
 *
 * The result will indicate if we need to poll again, increase our
 * polling interval before trying again, stop polling due to an error
 * or add the fetched token to the token management sub-system. */


class IOAuthTokenPollResultListener
{
    public:
        virtual ~IOAuthTokenPollResultListener() {}

        // Called by a 'IOAuthTokenPoller' instance once a poll has been executed
        virtual void OnPollCompleted(OAuthPollResult&) = 0;
};


class IOAuthTokenPoller
{
    public:
        virtual ~IOAuthTokenPoller() {}

        virtual TUint MaxPollingJobs() const = 0;

        // Returns details required to poll and pass back to clients in order to complete the flow
        virtual TBool StartLimitedInputFlow(LimitedInputFlowDetails&) = 0;

        // Set a listener so that we have something to accept results after a poll has been executed
        // Ownership is not taken by the implementer. Since this method is called outside the constructor
        // then a pointer must be used.
        virtual void SetPollResultListener(IOAuthTokenPollResultListener*) = 0;

        // Called by OAuthPollingManager to request a poll for the given details.
        virtual TBool RequestPollForToken(OAuthPollRequest&) = 0;
};



/* Internal interface used by OAuthPollingManager to receieve expiry events
 * from the individual job timers. */
class IPollingJobObserver
{
    public:
        virtual ~IPollingJobObserver() { }
        virtual void OnPollRequested(const Brx& aJobId) = 0;
};

// Allows an implementer to be notified whenever a job is created or has changed state.
class IOAuthPollingManagerObserver
{
    public:
        virtual ~IOAuthPollingManagerObserver() { }
        virtual void OnJobStatusChanged() = 0;
};


class OAuthPollingManager : public IOAuthTokenPollResultListener
                          , private IPollingJobObserver
{
    private:
        enum class EPollingJobStatus
        {
            InProgress,
            Failed,
            Success,
        };

        class PollingJob
        {
            public:
                PollingJob(Environment& aEnv,
                           IPollingJobObserver& aObserver,
                           const Brx& aJobId,
                           const Brx& aDeviceCode,
                           TUint suggestedPollingInterval);
                ~PollingJob();

                const Brx& JobId() const { return iJobId; }
                const Brx& DeviceCode() const { return iDeviceCode; }
                const TUint PollingInterval() const { return iPollingInterval; }
                const EPollingJobStatus Status() const { return iStatus; }

                void StartPollTimer();
                void HandleOnRequestToPollAgain();
                void HandleOnRequestToSlowPollingDown();
                void HandleOnFailed();
                void HandleOnSuccess();
            private:
                void OnPollRequired();

            private:
                Bws<64>              iJobId;
                Bws<64>              iDeviceCode;
                TUint                iPollingInterval;
                EPollingJobStatus    iStatus;
                Timer*               iPollTimer;
                IPollingJobObserver& iObserver;
        };


    public:
        OAuthPollingManager(Environment& aEnv,
                            IOAuthTokenPoller&,
                            TokenManager&,
                            IOAuthPollingManagerObserver&);
        ~OAuthPollingManager();

    public:
        TUint MaxPollingJobs() const;
        TUint RunningPollingJobs() const;
        TBool CanRequestJob() const;
        TBool RequestNewJob(PublicLimitedInputFlowDetails& aDetails);
        void GetJobStatusJSON(WriterJsonObject& aJsonWriter);

    public: // IOAuthTokenPollResultListener
        void OnPollCompleted(OAuthPollResult&) override;

    private: // IPollingJobObserver
        void OnPollRequested(const Brx& aJobId) override;

    private:
        TBool GenerateJobId(Bwx& aBuffer);
        void GenerateGUID(Bwx& aBuffer);
        TUint NumberOfRunningJobsLocked() const;
        TBool HasJobWithMatchingId(const Brx& aJobId) const;

    private:
        mutable Mutex iLockJobs;
        Environment& iEnv;
        IOAuthTokenPoller& iPoller;
        TokenManager& iTokenManager;
        IOAuthPollingManagerObserver& iObserver;
        std::vector<PollingJob*> iJobs;
        Bws<OAuth::kMaxTokenBytes> iTokenBuffer;
};



} // namespace OpenHome

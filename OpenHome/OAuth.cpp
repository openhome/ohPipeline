#include <OpenHome/Json.h>
#include <OpenHome/OAuth.h>
#include <OpenHome/Av/Debug.h>
#include <OpenHome/OsWrapper.h>
#include <OpenHome/Exception.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Av/Utils/FormUrl.h>
#include <OpenHome/Private/Converter.h>

#include <algorithm>
#include <limits>

using namespace OpenHome;
using namespace OpenHome::Av;


/* *******************
 * OAuth Static class
 * ******************* */

const TUint OAuth::kMaxTokenBytes;

// OAuth request parameters
const Brn OAuth::kParameterRefreshToken("refresh_token");
const Brn OAuth::kParameterClientId("client_id");
const Brn OAuth::kParameterClientSecret("client_secret");
const Brn OAuth::kParameterScope("scope");
const Brn OAuth::kParameterGrantType("grant_type");
const Brn OAuth::kParameterDeviceCode("device_code");

// OAuth Grant types
const Brn OAuth::kGrantTypeRefreshToken("refresh_token");
const Brn OAuth::kGrantTypeDeviceCode("urn:ietf:params:oauth:grant-type:device_code");

// OAuth Token Response fields
const Brn OAuth::kTokenResponseFieldTokenType("token_type");
const Brn OAuth::kTokenResponseFieldTokenExpiry("expires_in");
const Brn OAuth::kTokenResponseFieldAccessToken("access_token");
const Brn OAuth::kTokenResponseFieldRefreshToken("refresh_token");

// OAuth Error Response fields
const Brn OAuth::kErrorResponseFieldError("error");
const Brn OAuth::kErrorResponseFieldErrorDescription("error_description");

// OAuth Polling Errors
const Brn OAuth::kPollingStateSlowDown("slow_down");
const Brn OAuth::kPollingStateTryAgain("authorization_pending");



void OAuth::ConstructRefreshTokenRequestBody(IWriter& aWriter,
                                             const Brx& aRefreshToken,
                                             const Brx& aClientId,
                                             const Brx& aClientSecret,
                                             const Brx& aScope)
{
    aWriter.Write(kParameterGrantType);
    aWriter.Write('=');
    FormUrl::Encode(aWriter, kGrantTypeRefreshToken);

    aWriter.Write('&');

    aWriter.Write(kParameterRefreshToken);
    aWriter.Write('=');
    FormUrl::Encode(aWriter, aRefreshToken);

    aWriter.Write('&');

    aWriter.Write(kParameterClientId);
    aWriter.Write('=');
    FormUrl::Encode(aWriter, aClientId);

    aWriter.Write('&');

    aWriter.Write(kParameterClientSecret);
    aWriter.Write('=');
    FormUrl::Encode(aWriter, aClientSecret);

    aWriter.Write('&');

    aWriter.Write(kParameterScope);
    aWriter.Write('=');
    FormUrl::Encode(aWriter, aScope);
}



void OAuth::WriteAccessTokenHeader(WriterHttpHeader& aWriter,
                                   const Brx& aAccessToken)
{
    IWriterAscii& headerWriter = aWriter.WriteHeaderField(Http::kHeaderAuthorization);

    headerWriter.Write(Brn("Bearer"));
    headerWriter.WriteSpace();
    headerWriter.Write(aAccessToken);
    headerWriter.WriteFlush();
}

void OAuth::WriteRequestToStartLimitedInputFlowBody(IWriter& aWriter,
                                                    const Brx& aClientId,
                                                    const Brx& aTokenScope)
{
    aWriter.Write(kParameterClientId);
    aWriter.Write('=');
    aWriter.Write(aClientId);

    aWriter.Write('&');

    aWriter.Write(kParameterScope);
    aWriter.Write('=');
    aWriter.Write(aTokenScope);
}


void OAuth::WriteTokenPollRequestBody(IWriter& aWriter,
                                      const Brx& aClientId,
                                      const Brx& aClientSecret,
                                      const Brx& aTokenScope,
                                      const Brx& aDeviceCode)
{
    aWriter.Write(kParameterGrantType);
    aWriter.Write('=');
    aWriter.Write(kGrantTypeDeviceCode);

    aWriter.Write('&');

    aWriter.Write(kParameterClientId);
    aWriter.Write('=');
    aWriter.Write(aClientId);

    aWriter.Write('&');

    aWriter.Write(kParameterClientSecret);
    aWriter.Write('=');
    aWriter.Write(aClientSecret);

    aWriter.Write('&');

    aWriter.Write(kParameterScope);
    aWriter.Write('=');
    aWriter.Write(aTokenScope);

    aWriter.Write('&');

    aWriter.Write(kParameterDeviceCode);
    aWriter.Write('=');
    aWriter.Write(aDeviceCode);
}


/* ************
 * OAuth Token
 * ************ */

OAuthToken::OAuthToken(Environment& aEnv,
                       ITokenObserver& aObserver)
    : iHasExpired(true)
    , iIsLongLived(false)
    , iId(kIdGranularity)
    , iUsername(kUsernameGranularity)
    , iObserver(aObserver)
{
    iTimer = new Timer(aEnv, MakeFunctor(*this, &OAuthToken::OnTokenExpired), "OAuthTokenExpiry");
}

OAuthToken::~OAuthToken()
{
    delete iTimer;
}


const Brx& OAuthToken::Id()
{
    return iId.Buffer();
}

const Brx& OAuthToken::AccessToken() const
{
    return iAccessToken;
}

const Brx& OAuthToken::RefreshToken() const
{
    return iRefreshToken;
}

const Brx& OAuthToken::Username() const
{
    return iUsername.Buffer();
}

TBool OAuthToken::HasExpired() const
{
    return iHasExpired;
}

TBool OAuthToken::IsPresent() const
{
    return iRefreshToken.Bytes() > 0;
}

TBool OAuthToken::IsLongLived() const
{
    return iIsLongLived;
}

const TByte OAuthToken::RetryCount() const
{
    return iRetryCount;
}

TBool OAuthToken::CanRefresh(TUint aMaxRetryCount) const
{
    return HasExpired() && (RetryCount() < aMaxRetryCount);
}


void OAuthToken::UpdateToken(const Brx& aNewAccessToken,
                             TUint aTokenExpiry,
                             const Brx& aUsername)
{
    iTimer->Cancel();

    iAccessToken.ReplaceThrow(aNewAccessToken);

    iUsername.Reset();
    iUsername.Write(aUsername);

    iHasExpired = false;
    iRetryCount = 0;
    iTimer->FireIn(aTokenExpiry * 1000); //Expiry reported in seconds, timer works in ms
}

void OAuthToken::Clear()
{
    iTimer->Cancel();

    iId.Reset();
    iUsername.Reset();

    iAccessToken.Replace(Brx::Empty());
    iRefreshToken.Replace(Brx::Empty());

    iHasExpired = true;
    iIsLongLived = false;
    iRetryCount = 0;
}

void OAuthToken::Set(const Brx& aId,
                     const Brx& aRefreshToken,
                     TBool aIsLongLived)
{
    SetWithAccessToken(aId, aRefreshToken, aIsLongLived, Brx::Empty(), 0, Brx::Empty());
}

void OAuthToken::SetWithAccessToken(const Brx& aId,
                                    const Brx& aRefreshToken,
                                    TBool aIsLongLived,
                                    const Brx& aAccessToken,
                                    TUint aTokenExpiry,
                                    const Brx& aUsername)
{
    Clear();

    iId.Write(aId);
    iIsLongLived = aIsLongLived;

    iRefreshToken.ReplaceThrow(aRefreshToken);

    if (aAccessToken.Bytes() > 0)
    {
        UpdateToken(aAccessToken, aTokenExpiry, aUsername);
    }
    else
    {
        // Schedule an expiry notification so that observer will
        // go aheead and refresh the token for us!
        OnTokenExpired();
    }
}


void OAuthToken::OnTokenExpired()
{
    iHasExpired = true;
    iObserver.TokenExpired(Id());
}

void OAuthToken::NotifyFailedRefresh()
{
    ++iRetryCount;
}


void OAuthToken::ToJson(WriterJsonObject& aWriter)
{
    aWriter.WriteString("id", Id());
    aWriter.WriteBool("isValid", !HasExpired());
    aWriter.WriteString("username", iUsername.Buffer());

    aWriter.WriteEnd();
}



/* *************
 * TokenManager
 * ************* */
static const Brn kShortLivedTokenIdsKey("Ids"); //Davaar 80 has this already so will be difficult to change without migration step.
static const Brn kLongLivedTokenIdsKey("llIds");

static const TUint kRefreshRetryCount = 5;

TokenManager::TokenManager(const Brx& aServiceId,
                           TUint aMaxShortLivedCapacity,
                           TUint aMaxLongLivedCapacity,
                           Environment& aEnv,
                           IThreadPool& aThreadPool,
                           IOAuthAuthenticator& aAuthenticator,
                           Configuration::IStoreReadWrite& aStore,
                           ITokenManagerObserver& aObserver)
    : iServiceId(aServiceId)
    , iMaxShortLivedCapacity(aMaxShortLivedCapacity)
    , iMaxLongLivedCapacity(aMaxLongLivedCapacity)
    , iLock("TKNMGR")
    , iEnv(aEnv)
    , iUsernameBuffer(OAuthToken::kUsernameGranularity)
    , iStoreKeyBuffer(128)
    , iTokenIdsBuffer(128)
    , iAuthenticator(aAuthenticator)
    , iStore(aStore)
    , iObserver(aObserver)
{
    iRefresherHandle = aThreadPool.CreateHandle(MakeFunctor(*this, &TokenManager::RefreshTokens), "OAuthTokenRefresher", ThreadPoolPriority::Medium);

    ASSERT_VA(aMaxShortLivedCapacity <= kMaxShortLivedTokens,
              "Exceeded maximum number of stored tokens supported (Short lived). (Requested %u, Max: %u)",
              iMaxShortLivedCapacity,
              kMaxShortLivedTokens);

    ASSERT_VA(aMaxLongLivedCapacity <= kMaxLongLivedTokens,
              "Exceeded maximum number of stored tokens supported (Long lived). (Requested %u, Max: %u)",
              iMaxLongLivedCapacity,
              kMaxLongLivedTokens)

    for(TUint i = 0; i < iMaxShortLivedCapacity; ++i)
    {
        OAuthToken* newToken = new OAuthToken(iEnv, *this);
        iShortLivedTokens.push_front(newToken);
    }

    for(TUint i = 0; i < iMaxLongLivedCapacity; ++i)
    {
        OAuthToken* newToken = new OAuthToken(iEnv, *this);
        iLongLivedTokens.push_front(newToken);
    }

    LoadStoredTokens(ETokenTypeSelection::ShortLived);
    LoadStoredTokens(ETokenTypeSelection::LongLived);
}


TokenManager::~TokenManager()
{
    iRefresherHandle->Destroy();

    for(auto val : iShortLivedTokens)
    {
        delete val;
    }

    for(auto val : iLongLivedTokens)
    {
        delete val;
    }

    iShortLivedTokens.clear();
    iLongLivedTokens.clear();
}


const Brx& TokenManager::ServiceId() const
{
    return iServiceId;
}

TUint TokenManager::ShortLivedCapacity() const
{
    return iMaxShortLivedCapacity;
}

TUint TokenManager::LongLivedCapacity() const
{
    return iMaxLongLivedCapacity;
}


//Useful in testing...
TUint TokenManager::NumberOfStoredTokens() const
{
    AutoMutex m(iLock);
    TUint numTokens = 0;

    for(auto val : iShortLivedTokens)
    {
        if (val->IsPresent())
        {
            ++numTokens;
        }
    }

    for(auto val : iLongLivedTokens)
    {
        if (val->IsPresent())
        {
            ++numTokens;
        }
    }

    return numTokens;
}

void TokenManager::ExpireToken(const Brx& aId)
{
    /* NOTE: This is used when testing.
     *       If the SyncThreadPool,we must trigger the TokenExpiry
     *       outside of the lock, otherwise tests will crash with
     *       a recursive lock take exception. */
    OAuthToken* token = nullptr;
    {
        AutoMutex m(iLock);
        token = FindTokenLocked(aId);
    }

    if (token != nullptr)
    {
        token->OnTokenExpired();
    }
}


void TokenManager::AddToken(const Brx& aTokenId,
                            const Brx& aRefreshToken,
                            TBool aIsLongLived)
{
    AutoMutex m(iLock);

    /* Check token already exists.
     * If so, and it's still valid, don't bother doing anything. */
    OAuthToken* existingToken = FindTokenLocked(aTokenId, aIsLongLived);
    if (existingToken != nullptr && !existingToken->HasExpired())
    {
        return;
    }

    /* Validate the new token to make sure it's usable! */
    AccessTokenResponse response;
    iUsernameBuffer.Reset();

    TBool tokenValid = ValidateToken(aTokenId,
                                     aRefreshToken,
                                     response,
                                     iUsernameBuffer);

    if (!tokenValid)
    {
        THROW(OAuthTokenInvalid);
    }

    /* Already have an existing token with same ID but it has expired.
     * Replace it with the newly given token. */
    if (existingToken != nullptr)
    {
        existingToken->SetWithAccessToken(existingToken->Id(),
                                          aRefreshToken,
                                          aIsLongLived,
                                          response.accessToken,
                                          response.tokenExpiry,
                                          iUsernameBuffer.Buffer());

        StoreTokenLocked(existingToken->Id(), aRefreshToken);
        iObserver.OnTokenChanged();

        return;
    }


    /* Otherwise, try and find a suitable space to store the token.
     * If there is no free space, we'll evict a token to make space */
    const TBool isFull = !CheckSpaceAvailableLocked(aIsLongLived);

    if (isFull)
    {
        /* If full, we must evict a token. The least recently used token
         * is found at the back of the list so we use that. */
        OAuthToken* tokenToEvict = aIsLongLived ? iLongLivedTokens.back()
                                                : iShortLivedTokens.back();

        ASSERT_VA(tokenToEvict != nullptr && tokenToEvict->IsPresent(),
                  "%.*s",
                  PBUF(Brn("TokenManager::AddToken - Token storage is full and a suitable token for eviction can't be found.")));


        RemoveTokenLocked(tokenToEvict);

        tokenToEvict->SetWithAccessToken(aTokenId,
                                         aRefreshToken,
                                         aIsLongLived,
                                         response.accessToken,
                                         response.tokenExpiry,
                                         iUsernameBuffer.Buffer());

        LOG(kOAuth,
            "TokenManager::AddToken (by eviction) - Added token '%.*s', expires in %us.\n",
            PBUF(tokenToEvict->Id()),
            response.tokenExpiry);

        // Since we have just added a token, then we should put it at the head of the list
        // as it's now the most recently used!
        MoveTokenToFrontOfList(tokenToEvict);
    }
    else
    {
        const TBool didAdd = InsertTokenLocked(aTokenId,
                                               aIsLongLived,
                                               aRefreshToken,
                                               response.accessToken,
                                               response.tokenExpiry,
                                               iUsernameBuffer.Buffer());

        ASSERT_VA(didAdd == true, "Assumed that token storage had space, but wasn't able to find a space to add OAuth token.\n", 0);
    }

    StoreTokenLocked(aTokenId, aRefreshToken);
    StoreTokenIdsLocked(aIsLongLived ? ETokenTypeSelection::LongLived
                                     : ETokenTypeSelection::ShortLived);

    iObserver.OnTokenChanged();
}



void TokenManager::RemoveToken(const Brx& aTokenId, ETokenTypeSelection type)
{
    const TBool isLongLived = type == ETokenTypeSelection::LongLived;

    AutoMutex m(iLock);

    OAuthToken* token = FindTokenLocked(aTokenId, isLongLived);
    if (token == nullptr)
    {
        THROW(OAuthTokenIdNotFound);
        return;
    }

    // Need to grab this value here before the token is removed
    // Otherwise, the 'IsLongLived' flag will be incorrect.
    ASSERT_VA(token->IsLongLived() == isLongLived, "%s\n", "Found token with matching ID, but it's long-lived property wasn't what we expected.");

    RemoveTokenLocked(token);
    MoveTokenToEndOfList(token);

    StoreTokenIdsLocked(isLongLived ? ETokenTypeSelection::LongLived
                                    : ETokenTypeSelection::ShortLived);

    iObserver.OnTokenChanged();
}


void TokenManager::ClearAllTokens()
{
    DoClearTokens(ETokenTypeSelection::All);
}

void TokenManager::ClearShortLivedTokens()
{
    DoClearTokens(ETokenTypeSelection::ShortLived);
}

void TokenManager::ClearLongLivedTokens()
{
    DoClearTokens(ETokenTypeSelection::LongLived);
}

void TokenManager::DoClearTokens(ETokenTypeSelection operation)
{
    const TBool clearLongLived =  operation == ETokenTypeSelection::All || operation == ETokenTypeSelection::LongLived;
    const TBool clearShortLived = operation == ETokenTypeSelection::All || operation == ETokenTypeSelection::ShortLived;

    TUint numberOfShortLivedRemoved = 0;
    TUint numberOfLongLivedRemoved = 0;

    AutoMutex m(iLock);

    if (clearShortLived)
    {
        for(auto val : iShortLivedTokens)
        {
            if (val->IsPresent())
            {
                RemoveTokenLocked(val);
                numberOfShortLivedRemoved++;
            }
        }
    }

    if (clearLongLived)
    {
        for(auto val : iLongLivedTokens)
        {
            if (val->IsPresent())
            {
                RemoveTokenLocked(val);
                numberOfLongLivedRemoved++;
            }
        }
    }

    LOG_TRACE(kOAuth,
              "TokenManager::DoClearTokens - Cleared: %d short lived & %d long lived token(s)\n.",
              numberOfShortLivedRemoved,
              numberOfLongLivedRemoved);

    // Since all tokens from one or both collections have
    // been cleared there is no need to rearrange the orders
    if (clearShortLived && numberOfShortLivedRemoved > 0)
    {
        StoreTokenIdsLocked(ETokenTypeSelection::ShortLived);
    }

    if (clearLongLived && numberOfLongLivedRemoved > 0)
    {
        StoreTokenIdsLocked(ETokenTypeSelection::LongLived);
    }

    iObserver.OnTokenChanged();
}


void TokenManager::TokenExpired(const Brx& /*aId*/)
{
    iRefresherHandle->TrySchedule();
}


void TokenManager::RefreshTokens()
{
    TBool notifyObserver = false;
    {
        AutoMutex m(iLock);
        OAuthToken* token = nullptr;

        for(auto val : iShortLivedTokens)
        {
            if (val->IsPresent() && val->CanRefresh(kRefreshRetryCount))
            {
                token = val;
                break;
            }
        }

        // If no token found, check the long lived collection
        if (token == nullptr)
        {
            for(auto val : iLongLivedTokens)
            {
                if (val->IsPresent() && val->CanRefresh(kRefreshRetryCount))
                {
                    token = val;
                    break;
                }
            }
        }

        // At this point, we've no token in either collection that needs
        // refreshed so our work here is done.
        if (token == nullptr)
        {
            return;
        }

        AccessTokenResponse response;

        iUsernameBuffer.Reset();
        TBool success = ValidateToken(token->Id(),
                                      token->RefreshToken(),
                                      response,
                                      iUsernameBuffer);

        if (success)
        {
            LOG(kOAuth,
                "TokenManager(%.*s) - Refreshed token '%.*s', expires in %us\n",
                PBUF(iServiceId),
                PBUF(token->Id()),
                response.tokenExpiry);

            token->UpdateToken(response.accessToken,
                               response.tokenExpiry,
                               iUsernameBuffer.Buffer());

            notifyObserver = true;
        }
        else
        {
            LOG(kOAuth,
                "TokenManager(%.*s) - Failed to refresh token '%.*s'.\n",
                PBUF(iServiceId),
                PBUF(token->Id()));

            token->NotifyFailedRefresh();
        }
    }

    // Schedule another pass and notify outside holding onto the lock
    if (notifyObserver)
    {
        iObserver.OnTokenChanged();
    }

    iRefresherHandle->TrySchedule();
}


TBool TokenManager::HasToken(const Brx& aId)
{
    AutoMutex m(iLock);
    return FindTokenLocked(aId) != nullptr;
}

TBool TokenManager::TryGetFirstValidTokenId(IWriter& writer)
{
    AutoMutex m(iLock);

    for(auto val : iShortLivedTokens)
    {
        if (val->IsPresent() && !val->HasExpired())
        {
            writer.Write(val->Id());
            return true;
        }
    }

    // Try long lived tokens if we've yet to find a usable one.
    for(auto val : iLongLivedTokens)
    {
        if (val->IsPresent() && !val->HasExpired())
        {
            writer.Write(val->Id());
            return true;
        }
    }

    return false;
}


TBool TokenManager::EnsureTokenIsValid(const Brx& aId)
{
    AutoMutex m(iLock);

    OAuthToken* token = FindTokenLocked(aId);
    if(token == nullptr)
        return false;

    if (token->HasExpired())
    {
        AccessTokenResponse response;

        iUsernameBuffer.Reset();
        if (ValidateToken(token->Id(),
                          token->RefreshToken(),
                          response,
                          iUsernameBuffer))
        {
            token->UpdateToken(response.accessToken,
                               response.tokenExpiry,
                               iUsernameBuffer.Buffer());

            return true;
        }

        //TODO: If something goes wrong here, then should we evict the token??
        //TODO: What if instead of ensuring a token is valid or not, we simply
        //      try and use it, otherwise refresh it, and then fail to stream
        return false;
    }

    return true;
}


TBool TokenManager::TryGetToken(const Brx& aId,
                                ServiceToken& aToken)
{
    AutoMutex m(iLock);
    OAuthToken* token = FindTokenLocked(aId);
    if (token == nullptr)
    {
        return false;
    }

    MoveTokenToFrontOfList(token);

    aToken.type = TokenType::OAuth;
    aToken.token.Set(token->AccessToken());

    return true;    
}


TBool TokenManager::CheckSpaceAvailableLocked(TBool aIsLongLived) const
{
    const auto& tokenList = aIsLongLived ? iLongLivedTokens
                                         : iShortLivedTokens;

    for(auto val : tokenList)
    {
        if (!val->IsPresent())
        {
            return true;
        }
    }

    return false;
}

OAuthToken* TokenManager::FindTokenLocked(const Brx& aTokenId) const
{
    OAuthToken* token = FindTokenLocked(aTokenId, false);
    if (token == nullptr)
    {
        token = FindTokenLocked(aTokenId, true);
    }

    return token;
}


OAuthToken* TokenManager::FindTokenLocked(const Brx& aId, TBool aIsLongLived) const
{
    const auto& tokenList = aIsLongLived ? iLongLivedTokens
                                         : iShortLivedTokens;

    for (auto val : tokenList)
    {
        if (val->Id() == aId)
        {
            return val;
        }
    }

    return nullptr;
}


TBool TokenManager::InsertTokenLocked(const Brx& aId,
                                      TBool aIsLongLived,
                                      const Brx& aRefreshToken,
                                      const Brx& aAccessToken,
                                      TUint aTokenExpiry,
                                      const Brx& aUsername)
{
    const TBool hasAccessToken = aAccessToken.Bytes() > 0;
    const auto& tokenList = aIsLongLived ? iLongLivedTokens
                                         : iShortLivedTokens;

    for(auto val : tokenList)
    {
        if (!val->IsPresent())
        {
            if (hasAccessToken)
            {
                LOG(kOAuth,
                    "TokenManager(%.*s) - Added token '%.*s', expires in %us\n",
                    PBUF(iServiceId),
                    PBUF(aId),
                    aTokenExpiry);

                val->SetWithAccessToken(aId, aRefreshToken, aIsLongLived, aAccessToken, aTokenExpiry, aUsername);
            }
            else
            {
                LOG(kOAuth,
                    "TokenManager(%.*s) - Added token '%.*s'. Fetch has been scheduled.\n",
                    PBUF(iServiceId),
                    PBUF(aId));

                // Token will automatically schedule a refresh upon set
                // since no access token has been provided
                val->Set(aId, aRefreshToken, aIsLongLived);
            }

            return true;
        }
    }

    return false;
}

TBool TokenManager::IsTokenPtrPresentLocked(OAuthToken* aTokenPtr) const
{
    for(auto val : iShortLivedTokens)
    {
        if (val == aTokenPtr)
        {
            return true;
        }
    }

    for(auto val : iLongLivedTokens)
    {
        if (val == aTokenPtr)
        {
            return true;
        }
    }

    return false;
}


void TokenManager::RemoveTokenLocked(OAuthToken* aToken)
{
    if (aToken == nullptr)
        return;

    const Brx& tokenId = aToken->Id();

    iAuthenticator.OnTokenRemoved(tokenId, aToken->AccessToken());

    LOG(kOAuth, "TokenManager(%.*s) - Removed token '%.*s'\n", PBUF(iServiceId), PBUF(tokenId));

    RemoveStoredTokenLocked(aToken->Id());

    // Make sure to Clear() last, as this will remove any
    // stored Id() that might be used for storage
    aToken->Clear();
}


void TokenManager::MoveTokenToFrontOfList(OAuthToken* aToken)
{
    auto& tokenList = aToken->IsLongLived() ? iLongLivedTokens
                                            : iShortLivedTokens;

    for(auto it = tokenList.begin(); it != tokenList.end(); ++it)
    {
        if (*it == aToken)
        {
            tokenList.splice(tokenList.begin(), tokenList, it);
        }
    }
}

void TokenManager::MoveTokenToEndOfList(OAuthToken* aToken)
{
    auto& tokenList = aToken->IsLongLived() ? iLongLivedTokens
                                            : iShortLivedTokens;

    for(auto it = tokenList.begin(); it != tokenList.end(); ++it)
    {
        if (*it == aToken)
        {
            tokenList.splice(tokenList.end(), tokenList, it);
            break;
        }
    }
}



/* TokenIds are stored in a space-seperated value
 * in the store.
 * Each refresh token is stored individually.
 * Store Keys are in the form <serviceId>.Ids / <serviceId>.<tokenId>
 * In the case of a "long-lived" key, kLongLivedPostfix (.ll) is appended
 * so we know which list to insert it into*/
void TokenManager::LoadStoredTokens(ETokenTypeSelection operation)
{
    Bwh tokenReadBuffer(OAuth::kMaxTokenBytes);

    const TBool isLongLived = operation == ETokenTypeSelection::LongLived;
    const Brx& tokenIdKey = isLongLived ? kLongLivedTokenIdsKey
                                        : kShortLivedTokenIdsKey;

    iTokenIdsBuffer.Reset();

    // Read in list of all the stored token Ids
    try
    {
        iStoreKeyBuffer.Reset();

        iStoreKeyBuffer.Write(iServiceId);
        iStoreKeyBuffer.Write('.');
        iStoreKeyBuffer.Write(tokenIdKey);

        iStore.Read(iStoreKeyBuffer.Buffer(), iTokenIdsBuffer);
    }
    catch (StoreKeyNotFound&)
    {
        LOG(kOAuth, "TokenManager(%.*s)::LoadStoredTokens() - no stored token keys found.\n", PBUF(iServiceId));
        return;
    }


    TBool parsingComplete = false;
    TBool tokenIdsChanged = false;
    Parser p(iTokenIdsBuffer.Buffer());
    do
    {
        Brn id = p.Next(' ');

        if (id.Bytes() == 0)
        {
            id.Set(p.Remaining());
            parsingComplete = true;
        }

        if (id.Bytes() > 0)
        {
            iStoreKeyBuffer.Reset();
            iStoreKeyBuffer.Write(iServiceId);
            iStoreKeyBuffer.Write('.');
            iStoreKeyBuffer.Write(id);

            try
            {
                iStore.Read(iStoreKeyBuffer.Buffer(), tokenReadBuffer);
            }
            catch (StoreKeyNotFound&)
            {
                LOG_WARNING(kOAuth,
                            "TokenManager(%.*s)::LoadStoredTokens() - Stored token '%.*s' has been referenced but can't be loaded.\n",
                            PBUF(iServiceId),
                            PBUF(id));

                continue;
            }

            if (InsertTokenLocked(id, isLongLived, tokenReadBuffer))
            {
                LOG(kOAuth,
                    "TokenManager(%.*s)::LoadStoredTokens() - Loaded token '%.*s'.\n",
                    PBUF(iServiceId),
                    PBUF(id));
            }
            else
            {
                LOG_WARNING(kOAuth,
                            "TokenManager(%.*s)::LoadStoredTokens() - Failed to store token '%.*s'. Removing from store...\n",
                            PBUF(iServiceId),
                            PBUF(id));

                // Failed to add token, remove it from backing storage...
                tokenIdsChanged = true;
                RemoveStoredTokenLocked(id);
            }
        }

    } while( !parsingComplete );

    /* If any token couldn't be correctly loaded then it will have been removed
     * from the store. Make sure to update our ID list so we don't try and load
     * it in the future. */
    if (tokenIdsChanged)
    {
        StoreTokenIdsLocked(isLongLived ? ETokenTypeSelection::LongLived
                                        : ETokenTypeSelection::ShortLived);
    }

    iObserver.OnTokenChanged();
}

void TokenManager::StoreTokenIdsLocked(ETokenTypeSelection operation)
{
    iTokenIdsBuffer.Reset();

    const Brx& storeKey = operation == ETokenTypeSelection::LongLived ? kLongLivedTokenIdsKey
                                                                      : kShortLivedTokenIdsKey;

    const auto& tokenList = operation == ETokenTypeSelection::LongLived ? iLongLivedTokens
                                                                        : iShortLivedTokens;

    for(const auto val : tokenList)
    {
        if (val->IsPresent())
        {
            iTokenIdsBuffer.Write(val->Id());
            iTokenIdsBuffer.Write(' ');
        }
    }

    iStoreKeyBuffer.Reset();
    iStoreKeyBuffer.Write(iServiceId);
    iStoreKeyBuffer.Write('.');
    iStoreKeyBuffer.Write(storeKey);

    if (iTokenIdsBuffer.Buffer()
                       .Bytes() == 0)
    {
        iStore.Delete(iStoreKeyBuffer.Buffer());
    }
    else
    {
        iStore.Write(iStoreKeyBuffer.Buffer(), iTokenIdsBuffer.Buffer());
    }
}

void TokenManager::StoreTokenLocked(const Brx& aTokenId,
                                    const Brx& aRefreshToken)
{
    iStoreKeyBuffer.Reset();
    iStoreKeyBuffer.Write(iServiceId);
    iStoreKeyBuffer.Write('.');
    iStoreKeyBuffer.Write(aTokenId);

    iStore.Write(iStoreKeyBuffer.Buffer(), aRefreshToken);
}

void TokenManager::RemoveStoredTokenLocked(const Brx& aTokenId)
{
    iStoreKeyBuffer.Reset();
    iStoreKeyBuffer.Write(iServiceId);
    iStoreKeyBuffer.Write('.');
    iStoreKeyBuffer.Write(aTokenId);

    iStore.Delete(iStoreKeyBuffer.Buffer());
}


void TokenManager::TokenStateToJson(WriterJsonObject& aWriter)
{
    AutoMutex m(iLock);

    {
        WriterJsonArray arrayWriter = aWriter.CreateArray("shortLivedTokens");
        for(auto val : iShortLivedTokens)
        {
            if (val->IsPresent())
            {
                WriterJsonObject objWriter = arrayWriter.CreateObject();
                val->ToJson(objWriter); //Consumes writer and calls WriteEnd();
            }
        }
        arrayWriter.WriteEnd();
    }

    {
        WriterJsonArray arrayWriter = aWriter.CreateArray("longLivedTokens");
        for(auto val : iLongLivedTokens)
        {
            if (val->IsPresent())
            {
                WriterJsonObject objWriter = arrayWriter.CreateObject();
                val->ToJson(objWriter); //Consume writer and calls WriteEnd();
            }
        }
        arrayWriter.WriteEnd();
    }
}


TBool TokenManager::ValidateToken(const Brx& aTokenId,
                                  const Brx& aRefreshToken,
                                  AccessTokenResponse& aResponse,
                                  IWriter& aUsername)
{
    TBool success = iAuthenticator.TryGetAccessToken(aTokenId,
                                                     aRefreshToken,
                                                     aResponse);

    if (success)
    {
        /* Will attempt to use the newly fetched accessToken to get a username
         * for this token. Should this fail, then we'll assume that the provided
         * refreshToken is invalid */
        return iAuthenticator.TryGetUsernameFromToken(aTokenId,
                                                      aResponse.accessToken,
                                                      aUsername);
    }

    return false;
}

/************************************
 * OAuthPollingManager :: Polling Job
 ************************************/
static const TUint kBasePollingRate = 1000; // In seconds.
static const TUint kPollingRateIncrease = 1000; // Increase polling by this amount (in seconds) if we're told to slow down.


OAuthPollingManager::PollingJob::PollingJob(Environment& aEnv,
                                            IPollingJobObserver& aObserver,
                                            const Brx& aJobId,
                                            const Brx& aDeviceCode,
                                            TUint suggestedPollingInterval)
    : iJobId(aJobId)
    , iDeviceCode(aDeviceCode)
    , iObserver(aObserver)
{
    iStatus = EPollingJobStatus::InProgress;
    iPollingInterval = std::max<TUint>(kBasePollingRate, suggestedPollingInterval);

    iPollTimer = new Timer(aEnv, MakeFunctor(*this, &OAuthPollingManager::PollingJob::OnPollRequired), "PollingJobTimer");
}

OAuthPollingManager::PollingJob::~PollingJob()
{
    iPollTimer->Cancel();
    delete iPollTimer;
}

void OAuthPollingManager::PollingJob::StartPollTimer()
{
    iPollTimer->FireIn(iPollingInterval);
}

void OAuthPollingManager::PollingJob::HandleOnFailed()
{
    iStatus = EPollingJobStatus::Failed;

    LOG_TRACE(kOAuth,
              "PollingJob::HandleOnRequestToPollAgain - Job: %.*s: Handling case where we've failed to poll. Not requesting a new poll.\n",
              PBUF(iJobId));

}

void OAuthPollingManager::PollingJob::HandleOnSuccess()
{
    iStatus = EPollingJobStatus::Success;

    LOG_TRACE(kOAuth,
              "PollingJob::HandleOnRequestToPollAgain - Job: %.*s: Polling was successful. Adding token...\n",
              PBUF(iJobId));

}

void OAuthPollingManager::PollingJob::HandleOnRequestToPollAgain()
{
    iStatus = EPollingJobStatus::InProgress;
    StartPollTimer();

    LOG_TRACE(kOAuth,
              "PollingJob::HandleOnRequestToPollAgain - Job: %.*s: Handling case where user hasn't logged in. Poll requested in %udms\n",
              PBUF(iJobId),
              iPollingInterval);

}

void OAuthPollingManager::PollingJob::HandleOnRequestToSlowPollingDown()
{
    iStatus = EPollingJobStatus::InProgress;

    iPollingInterval += kPollingRateIncrease;
    StartPollTimer();

    LOG_TRACE(kOAuth,
              "PollingJob::HandleOnRequestToPollAgain - Job: %.*s: Handling case where we're polling too quickly. Poll requested in %udms\n",
              PBUF(iJobId),
              iPollingInterval);

}

void OAuthPollingManager::PollingJob::OnPollRequired()
{
    iObserver.OnPollRequested(iJobId);
}


/*********************
 * OAuthPollingManager
 *********************/

OAuthPollingManager::OAuthPollingManager(Environment& aEnv,
                                         IOAuthTokenPoller& aPoller,
                                         TokenManager& aTokenManager,
                                         IOAuthPollingManagerObserver& aObserver)
    : iLockJobs("PLCK")
    , iEnv(aEnv)
    , iPoller(aPoller)
    , iTokenManager(aTokenManager)
    , iObserver(aObserver)
{

}

OAuthPollingManager::~OAuthPollingManager()
{
    for(auto val : iJobs)
    {
        delete val;
    }
}

TUint OAuthPollingManager::MaxPollingJobs() const
{
    return iPoller.MaxPollingJobs();
}

TUint OAuthPollingManager::RunningPollingJobs() const
{
    AutoMutex m(iLockJobs);
    return NumberOfRunningJobsLocked();
}

TBool OAuthPollingManager::CanRequestJob() const
{
    return RunningPollingJobs() < MaxPollingJobs();
}

TUint OAuthPollingManager::NumberOfRunningJobsLocked() const
{
    TUint runningJobs = 0;

    for(const auto v : iJobs)
    {
        if (v->Status() == EPollingJobStatus::InProgress)
        {
            runningJobs++;
        }
    }

    return runningJobs;
}


TBool OAuthPollingManager::RequestNewJob(PublicLimitedInputFlowDetails& aDetails)
{
    TBool success = false;

    {
        AutoMutex m(iLockJobs);
        LimitedInputFlowDetails privateDetails;

        if (NumberOfRunningJobsLocked() >= MaxPollingJobs())
        {
            LOG_WARNING(kOAuth, "OAuthPollingManager::RequestNewJob - Failed request job as we are currently at capacity.");
            return false;
        }

        if (iPoller.StartLimitedInputFlow(privateDetails))
        {
            // Create new job....
            Bws<64> newJobId;
            if (!GenerateJobId(newJobId))
            {
                LOG_ERROR(kOAuth, "OAuthPollingManager::RequestNewJob - Failed to generate a suitable ID for new job.\n");
                return false;
            }

            PollingJob* newJob = new PollingJob(iEnv,
                                                *this,
                                                newJobId,
                                                privateDetails.DeviceCode(),
                                                privateDetails.SuggestedPollingInterval());

            newJob->StartPollTimer();

            // Add to job list...
            iJobs.push_back(newJob);

            // Populate details for caller...
            aDetails.Set(newJob->JobId(),
                         privateDetails.UserUrl(),
                         privateDetails.AuthCode());

            success = true;
        }
    }

    if (success)
    {
        iObserver.OnJobStatusChanged();
    }

    return true;
}

void OAuthPollingManager::GetJobStatusJSON(WriterJsonObject& aJsonWriter)
{
    AutoMutex m(iLockJobs);

    WriterJsonArray arrayWriter = aJsonWriter.CreateArray("jobs");
    for(auto v : iJobs)
    {
        auto w = arrayWriter.CreateObject();
        w.WriteString("id", v->JobId());

        switch(v->Status())
        {
            case EPollingJobStatus::InProgress:
            {
                w.WriteString("status", "inProgress");
                break;
            }

            case EPollingJobStatus::Failed:
            {
                w.WriteString("status", "failed");
                break;
            }

            case EPollingJobStatus::Success:
            {
                w.WriteString("status", "success");
                break;
            }
        }

        w.WriteEnd();
    }

    arrayWriter.WriteEnd();
}

void OAuthPollingManager::OnPollCompleted(OAuthPollResult& aResult)
{
    TBool notifyStatusChanged = false;

    {
        AutoMutex m(iLockJobs);

        PollingJob* job = nullptr;
        for(const auto v : iJobs)
        {
            if (v->JobId() == aResult.JobId())
            {
                job = v;
                break;
            }
        }

        ASSERT_VA(job != nullptr,
                  "%s %.*s\n",
                  "OAuthPollanager::OnPollCompleted - Unable to find job with id:",
                  PBUF(aResult.JobId()));


        switch(aResult.PollResult())
        {
            case OAuth::PollResult::Poll:
            {
                job->HandleOnRequestToPollAgain();

                // Don't notify every poll loop to avoid flooding the network with updates that aren't visible to clients
                notifyStatusChanged = false;
                break;
            }

            case OAuth::PollResult::SlowDown:
            {
                job->HandleOnRequestToSlowPollingDown();

                // Don't notify every poll loop to avoid flooding the network with updates that aren't visible to clients
                notifyStatusChanged = false;
                break;
            }

            case OAuth::PollResult::Failed:
            {
                job->HandleOnFailed();

                notifyStatusChanged = true;
                break;
            }

            case OAuth::PollResult::Success:
            {
                job->HandleOnSuccess();

                // Copy token to prevent the underlying memory being reused in later network requests...
                iTokenBuffer.Replace(aResult.RefreshToken());

                iTokenManager.AddToken(job->JobId(), iTokenBuffer, false);

                notifyStatusChanged = true;
                break;
            }
        }
    }

    if (notifyStatusChanged)
    {
        iObserver.OnJobStatusChanged();
    }
}


void OAuthPollingManager::OnPollRequested(const Brx& jobId)
{
    AutoMutex m(iLockJobs);

    PollingJob* job = nullptr;
    for(const auto v : iJobs)
    {
        if (v->JobId() == jobId)
        {
            job = v;
            break;
        }
    }

    ASSERT_VA(job != nullptr,
              "%s %.*s\n",
              "OAuthPollingManager::OnPollRequested - Failed to find job with id:",
              PBUF(jobId));


    OAuthPollRequest request (jobId, job->DeviceCode());

    LOG_TRACE(kOAuth,
              "OAuthPollingManager::OnPollRequested - Requesting poll for job: %.*s\n",
              PBUF(jobId));

    if (!iPoller.RequestPollForToken(request))
    {
        LOG_ERROR(kOAuth,
                  "OAuthPollingManager::OnPollRequested - Failed to request poll for job: %.*s\n",
                  PBUF(jobId));

        // TODO: What's best here? Set the job to failed and leave it?
        //       Reschedule??
    }
}


TBool OAuthPollingManager::GenerateJobId(Bwx& aBuffer)
{
    // Prevents clashing GUIDs
    const TUint kMaxRetries = 5;
    TUint tryCount = 0;

    do
    {
        GenerateGUID(aBuffer);
        tryCount++;
    } while((iTokenManager.HasToken(aBuffer) || HasJobWithMatchingId(aBuffer)) && tryCount < kMaxRetries);

    return tryCount < kMaxRetries;
}

TBool OAuthPollingManager::HasJobWithMatchingId(const Brx& aJobId) const
{
    for(const auto v: iJobs)
    {
        if (v->JobId() == aJobId)
        {
            return true;
        }
    }

    return false;
}

void OAuthPollingManager::GenerateGUID(Bwx& aBuffer)
{
    // Build unique GUID. See: http://msdn.microsoft.com/en-us/library/cc251279%28PROT.10%29.aspx
    // GUID is of form (we don't use the enclosing curly brackets)
    // 8HEXDIG "-" 4HEXDIG "-" 4HEXDIG "-" 4HEXDIG "-" 12HEXDIG
    // Based on ProtocolMMS in 'ds'

    // 8 hex digits.
    TUint rand = iEnv.Random(std::numeric_limits<TUint>::max(), 0);
    Ascii::AppendHex(aBuffer, rand);
    aBuffer.Append(Brn("-"));

    // 4 hex digits.
    TUint versionMajor = 0;
    TUint versionMinor = 0;
    iEnv.GetVersion(versionMajor, versionMinor);
    Ascii::AppendHex(aBuffer, static_cast<TByte>(versionMajor & 0xff));
    Ascii::AppendHex(aBuffer, static_cast<TByte>(versionMinor & 0xff));
    aBuffer.Append(Brn("-"));

    // 4 hex digits.
    // Take 2 least significant bytes.
    TUint timeInMs = Os::TimeInMs(iEnv.OsCtx());
    Ascii::AppendHex(aBuffer, static_cast<TByte>((timeInMs>>8) & 0xff));
    Ascii::AppendHex(aBuffer, static_cast<TByte>(timeInMs & 0xff));
    aBuffer.Append(Brn("-"));

    // 4 hex digits.
    TUint seqNo = iEnv.SequenceNumber();
    Ascii::AppendHex(aBuffer, static_cast<TByte>((seqNo>>8) & 0xff));
    Ascii::AppendHex(aBuffer, static_cast<TByte>(seqNo & 0xff));
    aBuffer.Append(Brn("-"));

    // 12 hex digits.
    TUint64 timeInUs = Os::TimeInUs(iEnv.OsCtx());
    Bws<12> timeUsBuf;
    // Take 6 least significant bytes.
    Ascii::AppendHex(timeUsBuf, static_cast<TUint>((timeInUs>>16) & 0xffffffff));
    Ascii::AppendHex(timeUsBuf, static_cast<TByte>((timeInUs>>8) & 0xff));
    Ascii::AppendHex(timeUsBuf, static_cast<TByte>(timeInUs & 0xff));
    aBuffer.Append(timeUsBuf);
}



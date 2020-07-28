#include <OpenHome/Json.h>
#include <OpenHome/OAuth.h>
#include <OpenHome/Av/Debug.h>
#include <OpenHome/Exception.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/Private/Http.h>
#include <OpenHome/Private/Timer.h>
#include <OpenHome/Private/Parser.h>
#include <OpenHome/Av/Utils/FormUrl.h>
#include <OpenHome/Private/Converter.h>

using namespace OpenHome;
using namespace OpenHome::Av;


/* *******************
 * OAuth Static class
 * ******************* */

// OAuth request parameters
const Brn OAuth::kParameterRefreshToken("refresh_token");
const Brn OAuth::kParameterClientId("client_id");
const Brn OAuth::kParameterClientSecret("client_secret");
const Brn OAuth::kParameterScope("scope");
const Brn OAuth::kParameterGrantType("grant_type");

// OAuth Grant types
const Brn OAuth::kGrantTypeRefreshToken("refresh_token");

// OAuth Token Response fields
const Brn OAuth::kTokenResponseFieldTokenType("token_type");
const Brn OAuth::kTokenResponseFieldTokenExpiry("expires_in");
const Brn OAuth::kTokenResponseFieldAccessToken("access_token");
const Brn OAuth::kTokenResponseFieldRefreshToken("refresh_token");

// OAuth Error Response fields
const Brn OAuth::kErrorResponseFieldError("error");
const Brn OAuth::kErrorResponseFieldErrorDescription("error_description");



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


/* ************
 * OAuth Token
 * ************ */

OAuthToken::OAuthToken(Environment& aEnv,
                       ITokenObserver& aObserver)
    : iHasExpired(true),
      iId(kIdGranularity),
      iUsername(kUsernameGranularity),
      iObserver(aObserver)
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

const TByte OAuthToken::RetryCount() const
{
    return iRetryCount;
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
    iRetryCount = 0;
}

void OAuthToken::Set(const Brx& aId,
                     const Brx& aRefreshToken)
{
    SetWithAccessToken(aId, aRefreshToken, Brx::Empty(), 0, Brx::Empty());
}

void OAuthToken::SetWithAccessToken(const Brx& aId,
                                    const Brx& aRefreshToken,
                                    const Brx& aAccessToken,
                                    TUint aTokenExpiry,
                                    const Brx& aUsername)
{
    Clear();

    iId.Write(aId);
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
static const Brn kTokenIdsKey("Ids");
static const TUint kRefreshRetryCount = 5;

TokenManager::TokenManager(const Brx& aServiceId,
                           TUint aMaxCapacity,
                           Environment& aEnv,
                           IThreadPool& aThreadPool,
                           IOAuthAuthenticator& aAuthenticator,
                           Configuration::IStoreReadWrite& aStore,
                           ITokenManagerObserver& aObserver)
    : iServiceId(aServiceId),
      iMaxCapacity(aMaxCapacity),
      iLock("TKNMGR"),
      iEnv(aEnv),
      iUsernameBuffer(OAuthToken::kUsernameGranularity),
      iStoreKeyBuffer(128),
      iTokenIdsBuffer(128),
      iAuthenticator(aAuthenticator),
      iStore(aStore),
      iObserver(aObserver)
{
    iRefresherHandle = aThreadPool.CreateHandle(MakeFunctor(*this, &TokenManager::RefreshTokens), "OAuthTokenRefresher", ThreadPoolPriority::Medium);

    ASSERT_VA(aMaxCapacity <= kMaxSupportedTokens,
              "Exceeded maximum number of stored tokens supported. (Requested %u, Max: %u)",
              iMaxCapacity,
              kMaxSupportedTokens);

    for(TUint i = 0; i < iMaxCapacity; ++i)
    {
        OAuthToken* newToken = new OAuthToken(iEnv, *this);
        iTokens.push_front(newToken);
    }

    LoadStoredTokens();
}


TokenManager::~TokenManager()
{
    iRefresherHandle->Destroy();

    for(auto val : iTokens)
    {
        delete val;
    }

    iTokens.clear();
}


const Brx& TokenManager::ServiceId() const
{
    return iServiceId;
}

TUint TokenManager::Capacity() const
{
    return iMaxCapacity;
}


//Useful in testing...
TUint TokenManager::NumberOfStoredTokens()
{
    AutoMutex m(iLock);
    TUint numTokens = 0;

    for(auto val : iTokens)
    {
        if (val->IsPresent())
        {
            ++numTokens;
        }
    }

    return numTokens;
}


void TokenManager::AddToken(const Brx& aTokenId,
                            const Brx& aRefreshToken)
{
    AutoMutex m(iLock);

    /* Check token already exists.
     * If so, and it's still valid, don't bother doing anything. */
    OAuthToken* existingToken = FindTokenLocked(aTokenId);
    if (existingToken != nullptr)
    {
        if (!existingToken->HasExpired())
        {
            return;
        }
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
                                          response.accessToken,
                                          response.tokenExpiry,
                                          iUsernameBuffer.Buffer());

        StoreTokenLocked(existingToken->Id(), aRefreshToken);
        iObserver.OnTokenChanged();

        return;
    }


    /* Otherwise, try and find a suitable space to store the token.
     * If there is no free space, we'll evict a token to make space */
    const TBool isFull = !CheckSpaceAvailableLocked();

    if (isFull)
    {
        // Evicit a token...
        // Token that has been least recently used will be found at the
        // end of the collection
        OAuthToken* tokenToEvict = iTokens.back();

        ASSERT_VA(tokenToEvict != nullptr && tokenToEvict->IsPresent(),
                  "%.*s",
                  PBUF(Brn("TokenManager::AddToken - Token storage is full and a suitable token for eviction can't be found.")));

        RemoveTokenLocked(tokenToEvict);

        tokenToEvict->SetWithAccessToken(aTokenId,
                                         aRefreshToken,
                                         response.accessToken,
                                         response.tokenExpiry,
                                         iUsernameBuffer.Buffer());

        // TODO: If a token has been evicited, then should we place it at the head of the list??
    }
    else
    {
        const TBool didAdd = InsertTokenLocked(aTokenId,
                                               aRefreshToken,
                                               response.accessToken,
                                               response.tokenExpiry,
                                               iUsernameBuffer.Buffer());

        ASSERT_VA(didAdd == true, "Assumed that token storage had space, but wasn't able to find a space to add OAuth token.\n", 0);
    }

    StoreTokenIdsLocked();
    StoreTokenLocked(aTokenId, aRefreshToken);

    iObserver.OnTokenChanged();
}



void TokenManager::RemoveToken(const Brx& aTokenId)
{
    AutoMutex m(iLock);

    OAuthToken* token = FindTokenLocked(aTokenId);
    if (token == nullptr)
    {
        THROW(OAuthTokenIdNotFound);
        return;
    }

    RemoveTokenLocked(token);
    MoveTokenToEndOfList(token);

    StoreTokenIdsLocked();
    iObserver.OnTokenChanged();
}

void TokenManager::ClearTokens()
{
    AutoMutex m(iLock);

    for(auto val : iTokens)
    {
        if (val->IsPresent())
        {
            RemoveTokenLocked(val);
        }
    }

    // All tokens are cleared, so don't obhter rearranging the elements
    // in the token collection

    StoreTokenIdsLocked();
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

        for(auto val : iTokens)
        {
            if (val->IsPresent())
            {
                TBool expired = val->HasExpired();
                TBool needRefresh = val->RetryCount() < kRefreshRetryCount;

                if (expired && needRefresh)
                {
                    token = val;
                    break;
                }
            }
        }

        if (token == nullptr)
        {
            return; //Nothing to refresh. We're finished...
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

    for(auto val : iTokens)
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


TBool TokenManager::CheckSpaceAvailableLocked() const
{
    for(auto val : iTokens)
    {
        if (!val->IsPresent())
        {
            return true;
        }
    }

    return false;
}


OAuthToken* TokenManager::FindTokenLocked(const Brx& aId) const
{
    for (auto val : iTokens)
    {
        if(val->Id() == aId)
        {
            return val;
        }
    }

    return nullptr;
}


TBool TokenManager::InsertTokenLocked(const Brx& aId,
                                      const Brx& aRefreshToken,
                                      const Brx& aAccessToken,
                                      TUint aTokenExpiry,
                                      const Brx& aUsername)
{
    const TBool hasAccessToken = aAccessToken.Bytes() > 0;

    for(auto it = iTokens.begin(); it != iTokens.end(); ++it)
    {
        OAuthToken* val = *it;

        if (!val->IsPresent())
        {
            if (hasAccessToken)
            {
                LOG(kOAuth,
                    "TokenManager(%.*s) - Added token '%.*s', expires in %us\n",
                    PBUF(iServiceId),
                    PBUF(aId),
                    aTokenExpiry);

                val->SetWithAccessToken(aId, aRefreshToken, aAccessToken, aTokenExpiry, aUsername);
            }
            else
            {
                LOG(kOAuth,
                    "TokenManager(%.*s) - Added token '%.*s'. Fetch has been scheduled.\n",
                    PBUF(iServiceId),
                    PBUF(aId));

                // Token will automatically schedule a refresh upon set
                // since no access token has been provided
                val->Set(aId, aRefreshToken);
            }

            return true;
        }
    }

    return false;
}

TBool TokenManager::IsTokenPtrPresentLocked(OAuthToken* aTokenPtr) const
{
    for(auto val : iTokens)
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
    for(auto it = iTokens.begin(); it != iTokens.end(); ++it)
    {
        if (*it == aToken)
        {
            iTokens.splice(iTokens.begin(), iTokens, it);
        }
    }
}

void TokenManager::MoveTokenToEndOfList(OAuthToken* aToken)
{
    for(auto it = iTokens.begin(); it != iTokens.end(); ++it)
    {
        if (*it == aToken)
        {
            iTokens.splice(iTokens.end(), iTokens, it);
            break;
        }
    }
}



/* TokenIds are stored in a space-seperated value
 * in the store.
 * Each refresh token is stored individually.
 * Store Keys are in the form <serviceId>.Ids / <serviceId>.<tokenId> */
void TokenManager::LoadStoredTokens()
{
    Bwh tokenReadBuffer(OAuth::kMaxTokenBytes);

    // Read in list of all the stored token Ids
    try
    {
        iStoreKeyBuffer.Reset();

        iStoreKeyBuffer.Write(iServiceId);
        iStoreKeyBuffer.Write('.');
        iStoreKeyBuffer.Write(kTokenIdsKey);

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

            if (InsertTokenLocked(id, tokenReadBuffer))
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
        StoreTokenIdsLocked();
    }

    iObserver.OnTokenChanged();
}

void TokenManager::StoreTokenIdsLocked()
{
    iTokenIdsBuffer.Reset();

    for(auto val : iTokens)
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
    iStoreKeyBuffer.Write(kTokenIdsKey);

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

    WriterJsonArray arrayWriter = aWriter.CreateArray("tokens");
    for(auto val : iTokens)
    {
        if (val->IsPresent())
        {
            WriterJsonObject objWriter = arrayWriter.CreateObject();
            val->ToJson(objWriter); //Consumes writer and calls WriteEnd();
        }
    }

    arrayWriter.WriteEnd();
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




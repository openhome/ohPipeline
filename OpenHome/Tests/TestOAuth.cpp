#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/OAuth.h>
#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Av/Debug.h>
#include <OpenHome/Exception.h>
#include <OpenHome/ThreadPool.h>
#include <OpenHome/PowerManager.h>
#include <OpenHome/Private/Thread.h>
#include <OpenHome/Net/Private/Error.h>
#include <OpenHome/Configuration/IStore.h>
#include <OpenHome/Configuration/ConfigManager.h>
#include <Generated/CpAvOpenhomeOrgCredentials1.h>
#include <OpenHome/Configuration/Tests/ConfigRamStore.h>


#include "openssl/bio.h"
#include "openssl/pem.h"

using namespace OpenHome;
using namespace OpenHome::Net;
using namespace OpenHome::TestFramework;
using namespace OpenHome::Configuration;

#define TEST_THROWS_PROXYERROR(aExp, aCode) \
    do { \
        const TChar* file = __FILE__; \
        try { \
            aExp; \
            OpenHome::TestFramework::Fail(file, __LINE__, #aExp, "ProxyError expected but not thrown"); \
        } \
        catch(ProxyError& aPe) { \
            if (aPe.Level() != Error::eUpnp) { \
                OpenHome::TestFramework::Fail(file, __LINE__, #aExp, "Wrong error level"); \
            } \
            else if (aPe.Code() == aCode) { \
                OpenHome::TestFramework::Succeed(file, __LINE__); \
            } \
            else { \
                char str[128]; \
                (void)sprintf(str, "Expected error code %d, got %d", aCode, (int)aPe.Code()); \
                OpenHome::TestFramework::Fail(file, __LINE__, #aExp, str); \
            } \
        } \
    } while(0)


namespace OpenHome {
namespace Av {
namespace TestOAuth {

class SuiteTokenManager : public Suite
{
    public:
        SuiteTokenManager(Environment& aEnv);
        ~SuiteTokenManager();

    private: // Suite
        void Test() override;

    private:
        void TestTokenStorage();
        void TestAddRemove();
        void TestContains();
        void TestAddingInvalidToken();
        void TestTokenRefreshes(TBool aIsLongLived);
        void TestTokenEviction(TBool aIsLongLived);
        void TestTokenClears();

    private:
        Environment& iEnv;
        IThreadPool* iThreadPool;
};

class SuiteOAuthToken : public Suite
{
    public:
        SuiteOAuthToken(Environment& aEnv);

    private:
        void Test() override;

        void DoTest(TBool aIsLongLived);

    private:
        Environment& iEnv;
};


class InvalidOAuthAuthenticator : public IOAuthAuthenticator
{
    public: //IOAuthAuthenticator
        TBool TryGetAccessToken(const Brx& /*aTokenId*/,
                                const Brx& /*aRefreshToken*/,
                                AccessTokenResponse& /*aResponse*/) override { return false; }

        TBool TryGetUsernameFromToken(const Brx& /*aTokenId*/,
                                      const Brx& /*aAccessToken*/,
                                      IWriter& /*aUsername*/) override { return false; }

        void OnTokenRemoved(const Brx& /*aTokenId*/, const Brx& /*aAccessToken*/) override { };
};

class ValidOAuthAuthenticator : public IOAuthAuthenticator
{
    public:
        ValidOAuthAuthenticator(const Brx& aAccessToken,
                                const TUint aTokenExpiry);

    public: //IOAuthAuthenticator
        TBool TryGetAccessToken(const Brx& aTokenId,
                                const Brx& aRefreshToken,
                                AccessTokenResponse& aResponse) override;

        TBool TryGetUsernameFromToken(const Brx& aTokenId,
                                      const Brx& aAccessToken,
                                      IWriter& aUsername) override;

        void OnTokenRemoved(const Brx& /*aTokenId*/, const Brx& /*aAccessToken*/) override { };

    private:
        TUint iTokenExpiry;
        Bws<OAuth::kMaxTokenBytes> iAccessToken;
};

class AlternatingValidAuthenticator : public IOAuthAuthenticator
{
    public:
        AlternatingValidAuthenticator(const Brx& aAccessTokenA,
                                      const TUint aTokenExpiryA,
                                      const Brx& aAccessTokenB,
                                      const TUint aTokenExpiryB);

    public: // IOAuthAuthenticator
        TBool TryGetAccessToken(const Brx& aTokenId,
                                const Brx& aRefreshToken,
                                AccessTokenResponse& aResponse) override;

        TBool TryGetUsernameFromToken(const Brx& aTokenId,
                                      const Brx& aAccessToken,
                                      IWriter& aUsername) override;

        void OnTokenRemoved(const Brx& /*aTokenId*/, const Brx& /*aAccessToken*/) override { };

    private:
        TUint iCallCount;
        ValidOAuthAuthenticator iAuthA;
        ValidOAuthAuthenticator iAuthB;
};


class DummyTokenObserver : public ITokenObserver
{
    public:
        const TBool HasCalledBack() const { return iCallbackCount > 0; }
        const TUint CallbackCount() const { return iCallbackCount; }

    public: //ITokenObserver
        void TokenExpired(const Brx& /*aId*/) override { iCallbackCount++; }

    private:
        TUint iCallbackCount = 0;
};

class DummyTokenManagerObserver : public ITokenManagerObserver
{
    public: // ITokenManagerObserver
        void OnTokenChanged() override { };
};


} // namespace TestOAuth
} // namespace Av
} // namespace OpenHome

using namespace OpenHome::Av::TestOAuth;


/* ***********************
 * ValudOAuthAuthenticator
 * *********************** */

ValidOAuthAuthenticator::ValidOAuthAuthenticator(const Brx& aAccessToken,
                                                 const TUint aTokenExpiry)
    : iTokenExpiry(aTokenExpiry),
      iAccessToken(aAccessToken)
{

}


TBool ValidOAuthAuthenticator::TryGetAccessToken(const Brx& /*aTokenId*/,
                                                 const Brx& /*aRefreshToken*/,
                                                 AccessTokenResponse& aResponse)
{
    aResponse.accessToken.Set(iAccessToken);
    aResponse.tokenExpiry = iTokenExpiry;

    return true;
}

TBool ValidOAuthAuthenticator::TryGetUsernameFromToken(const Brx& /*aTokenId*/,
                                                       const Brx& /*aAccessToken*/,
                                                       IWriter& aUsername)
{
    aUsername.Write(Brn("username"));
    return true;
}


/* *****************************
 * AlternatingValidAuthenticator
 * ***************************** */

AlternatingValidAuthenticator::AlternatingValidAuthenticator(const Brx& aAccessTokenA,
                                                             const TUint aTokenExpiryA,
                                                             const Brx& aAccessTokenB,
                                                             const TUint aTokenExpiryB)
    : iCallCount(0),
      iAuthA(aAccessTokenA, aTokenExpiryA),
      iAuthB(aAccessTokenB, aTokenExpiryB)
{ }


TBool AlternatingValidAuthenticator::TryGetAccessToken(const Brx& aTokenId,
                                                       const Brx& aRefreshToken,
                                                       AccessTokenResponse& aResponse)
{
    TBool isOdd = iCallCount & 0b1;
    iCallCount++;

    return isOdd ? iAuthB.TryGetAccessToken(aTokenId, aRefreshToken, aResponse)
                 : iAuthA.TryGetAccessToken(aTokenId, aRefreshToken, aResponse);
}

TBool AlternatingValidAuthenticator::TryGetUsernameFromToken(const Brx& aTokenId,
                                                             const Brx& aAccessToken,
                                                             IWriter& aUsername)
{
    TBool isOdd = iCallCount & 0b1;

    // Don't increment the call count here.
    // TryGetAccessToken / TryGetUsername always called in a pair

    return isOdd ? iAuthB.TryGetUsernameFromToken(aTokenId, aAccessToken, aUsername)
                 : iAuthA.TryGetUsernameFromToken(aTokenId, aAccessToken, aUsername);
}


/* *****************
 * SuiteTokenManager
 * ***************** */

static const Brn kServiceId("id");

SuiteTokenManager::SuiteTokenManager(Environment& aEnv)
    : Suite("TokenManager Tests"),
      iEnv(aEnv)
{
    iThreadPool = new MockThreadPoolSync();
}

SuiteTokenManager::~SuiteTokenManager()
{
    delete iThreadPool;
}


void SuiteTokenManager::Test()
{
    TestTokenStorage();

    TestAddingInvalidToken();

    TestContains();

    TestAddRemove();

    TestTokenEviction(true);
    TestTokenEviction(false);

    TestTokenRefreshes(true);
    TestTokenRefreshes(false);

    TestTokenClears();
}

void SuiteTokenManager::TestTokenStorage()
{
    ConfigRamStore store;
    DummyTokenManagerObserver observer;
    ValidOAuthAuthenticator auth(Brn("at"), 1);

    // Test TokenManager doesn't crash if there's nothing present in the store
    // Below code will throw if the fact that the key isn't present.
    {
        TokenManager manager(kServiceId, TokenManager::kMaxShortLivedTokens, TokenManager::kMaxLongLivedTokens, iEnv, *iThreadPool, auth, store, observer);
    }

    Bws<32> storeKey;
    storeKey.Replace(kServiceId);
    storeKey.Append('.');
    storeKey.Append("Ids");

    store.Write(storeKey, Brn("KeyA KeyB"));

    storeKey.Replace(kServiceId);
    storeKey.Append('.');
    storeKey.Append("KeyA");

    store.Write(storeKey, Brn("TOKEN"));

    // Also add a long-lived token..
    storeKey.Replace(kServiceId);
    storeKey.Append('.');
    storeKey.Append("llIds");

    store.Write(storeKey, Brn("KeyC"));

    storeKey.Replace(kServiceId);
    storeKey.Append('.');
    storeKey.Append("KeyC");

    store.Write(storeKey, Brn("TOKEN FOR C"));


    TokenManager manager(kServiceId, 5, 1, iEnv, *iThreadPool, auth, store, observer);

    TEST(manager.NumberOfStoredTokens() == 2);

    TEST(manager.HasToken(Brn("KeyA")));
    TEST(manager.HasToken(Brn("KeyB")) == false);
    TEST(manager.HasToken(Brn("KeyC")));

    manager.AddToken(Brn("TEST-KEY"), Brn("anotherToken"), false);

    TEST(manager.NumberOfStoredTokens() == 3);

    Bws<32> storeBuffer;
    storeKey.Replace(kServiceId);
    storeKey.Append('.');
    storeKey.Append("TEST-KEY");

    store.Read(storeKey, storeBuffer);
    TEST(storeBuffer == Brn("anotherToken"));

    storeKey.Replace(kServiceId);
    storeKey.Append('.');
    storeKey.Append("Ids");

    store.Read(storeKey, storeBuffer);
    TEST(storeBuffer == Brn("KeyA TEST-KEY ")); //Trailing space is required
}


void SuiteTokenManager::TestAddingInvalidToken()
{
    ConfigRamStore store;
    InvalidOAuthAuthenticator auth;
    DummyTokenManagerObserver observer;
    TokenManager manager(kServiceId, 1, 1, iEnv, *iThreadPool, auth, store, observer);

    try
    {
        manager.AddToken(Brn("key"), Brn("invalid-token"), true);
    }
    catch (OAuthTokenInvalid)
    {
        TEST(true);
    }

    // All other exceptions will throw and fail this test :)
}


void SuiteTokenManager::TestContains()
{
    ConfigRamStore store;
    DummyTokenManagerObserver observer;
    ValidOAuthAuthenticator auth(Brn("access-token"), 1);

    TokenManager manager(kServiceId, 1, 1, iEnv, *iThreadPool, auth, store, observer);

    manager.AddToken(Brn("id"), Brn("refresh-token"), false);
    manager.AddToken(Brn("id-ll"), Brn("refresh-token"), true);

    TEST(manager.HasToken(Brn("id")));
    TEST(manager.HasToken(Brn("id-ll")));

    TEST(manager.HasToken(Brx::Empty()) == false);
    TEST(manager.HasToken(Brn("another-id")) == false);
    TEST(manager.HasToken(Brn("yet-another")) == false);
}


void SuiteTokenManager::TestAddRemove()
{
    const Brn idA("A");
    const Brn idB("B");
    const Brn refreshToken("rf");

    ConfigRamStore store;
    DummyTokenManagerObserver observer;
    ValidOAuthAuthenticator auth(Brn("access-token"), 1);
    TokenManager manager(kServiceId, 1, 1, iEnv, *iThreadPool, auth, store, observer);

    manager.AddToken(idA, refreshToken, false);

    TEST(manager.HasToken(idA));
    TEST(manager.HasToken(idB) == false);


    manager.RemoveToken(idA, TokenManager::ETokenTypeSelection::ShortLived);

    TEST(manager.HasToken(idA) == false);
    TEST(manager.HasToken(idB) == false);


    // Attempt to remove an ID that doesn't exist
    try
    {
        manager.RemoveToken(idA, TokenManager::ETokenTypeSelection::ShortLived);
    }
    catch(OAuthTokenIdNotFound)
    {
        TEST(true);
    }

    try
    {
        manager.RemoveToken(idA, TokenManager::ETokenTypeSelection::LongLived);
    }
    catch(OAuthTokenIdNotFound)
    {
        TEST(true);
    }
}


void SuiteTokenManager::TestTokenRefreshes(TBool aIsLongLived)
{
    const Brn id("A");
    const Brn refreshToken("rf");

    const Brn accessTokenA("ata");
    const Brn accessTokenB("atb");

    ConfigRamStore store;
    DummyTokenManagerObserver observer;
    AlternatingValidAuthenticator auth(accessTokenA, 1,
                                       accessTokenB, 1);    // Expiries are treated as seconds
    TokenManager manager(kServiceId, 5, 2, iEnv, *iThreadPool, auth, store, observer);

    /* Adding multiple times should only result in one call
     * to get AccessTokens/Username. If not, then the number
     * of calls here will put the AlternatingValidAuthenticator
     * out of sync and fail the rest of the tests */
    manager.AddToken(id, refreshToken, aIsLongLived);
    manager.AddToken(id, refreshToken, aIsLongLived);
    manager.AddToken(id, refreshToken, aIsLongLived);

    TEST(manager.NumberOfStoredTokens() == 1);

    ServiceToken tokenA;
    manager.TryGetToken(id, tokenA);

    TEST(tokenA.token == accessTokenA);

    manager.ExpireToken(id);
    ServiceToken tokenB;
    manager.TryGetToken(id, tokenB);
    TEST(tokenB.token == accessTokenB);

    manager.ExpireToken(id);

    ServiceToken tokenC;
    manager.TryGetToken(id, tokenC);

    TEST(tokenC.token == accessTokenA);
}

void SuiteTokenManager::TestTokenEviction(TBool aIsLongLived)
{
    const Brn id("id");
    const Brn accessToken("at");
    const Brn refreshToken("rf");
    const Brn id1("id1");
    const Brn id2("id2");
    const Brn id3("id3");

    ConfigRamStore store;
    DummyTokenManagerObserver observer;
    ValidOAuthAuthenticator auth(accessToken, 10);
    TokenManager manager(id, 2, 2, iEnv, *iThreadPool, auth, store, observer);

    manager.AddToken(id1, refreshToken, aIsLongLived);
    manager.AddToken(id2, refreshToken, aIsLongLived);

    TEST(manager.NumberOfStoredTokens() == 2);

    ServiceToken __;
    manager.TryGetToken(id2, __); //Should put "id2" at the front of token list
    manager.TryGetToken(id1, __); //Should put "id1" at the front of token list

    // Adding token here should evict the LRU, which in this case is 'id2'
    manager.AddToken(id3, refreshToken, aIsLongLived);

    TEST(manager.NumberOfStoredTokens() == 2);

    TEST(manager.HasToken(id1));
    TEST(manager.HasToken(id2) == false);
    TEST(manager.HasToken(id3));

    manager.TryGetToken(id1, __); // Should put "id1" at the front of the token list
    manager.TryGetToken(id3, __); // Should put "id3" at the front of the token list

    // Adding token here should evict the LRU, which in this case is 'id1'
    manager.AddToken(id2, refreshToken, aIsLongLived);

    TEST(manager.NumberOfStoredTokens() == 2);

    TEST(manager.HasToken(id1) == false);
    TEST(manager.HasToken(id2));
    TEST(manager.HasToken(id3));
}

void SuiteTokenManager::TestTokenClears()
{
    const Brn idA("A");
    const Brn idB("B");
    const Brn idC("C");
    const Brn rt("RT");

    ConfigRamStore store;
    DummyTokenManagerObserver observer;
    ValidOAuthAuthenticator auth(Brn("at"), 10000);
    TokenManager manager(kServiceId, 2, 2, iEnv, *iThreadPool, auth, store, observer);

    manager.AddToken(idA, rt, false);
    manager.AddToken(idB, rt, false);
    manager.AddToken(idC, rt, true);

    manager.ClearShortLivedTokens();

    TEST(manager.HasToken(idA) == false);
    TEST(manager.HasToken(idB) == false);
    TEST(manager.HasToken(idC));

    manager.ClearLongLivedTokens();

    TEST(manager.HasToken(idC) == false);

    manager.AddToken(idA, rt, false);
    manager.AddToken(idB, rt, false);
    manager.AddToken(idC, rt, true);

    manager.ClearAllTokens();

    TEST(manager.HasToken(idA) == false);
    TEST(manager.HasToken(idB) == false);
    TEST(manager.HasToken(idC) == false);
}



/* ***************
 * SuiteOAuthToken
 * *************** */

SuiteOAuthToken::SuiteOAuthToken(Environment& aEnv)
    : Suite("OAuthToken Tests"),
      iEnv(aEnv)
{ }

void SuiteOAuthToken::Test()
{
    DoTest(true);
    DoTest(false);
}

void SuiteOAuthToken::DoTest(TBool aIsLongLived)
{
    const Brn id("id");
    const Brn accessToken("at");
    const Brn refreshToken("rf");
    const Brn username("uname");

    DummyTokenObserver observer;
    OAuthToken token(iEnv, observer);


    TEST(token.IsPresent() == false);
    TEST(token.Id() == Brx::Empty());
    TEST(token.AccessToken() == Brx::Empty());
    TEST(token.RefreshToken() == Brx::Empty());


    token.Set(id, refreshToken, aIsLongLived);

    TEST(token.IsPresent());
    TEST(token.Id() == id);
    TEST(token.RefreshToken() == refreshToken);

    TEST(observer.HasCalledBack());
    TEST(observer.CallbackCount() == 1);


    token.Clear();

    TEST(token.IsPresent() == false);
    TEST(token.Id() == Brx::Empty());
    TEST(token.RefreshToken() == Brx::Empty());


    token.SetWithAccessToken(id, refreshToken, aIsLongLived, accessToken, 1, username); // Expiries are treated as seconds

    TEST(token.IsPresent());

    TEST(token.Id() == id);
    TEST(token.Username() == username);
    TEST(token.AccessToken() == accessToken);
    TEST(token.RefreshToken() == refreshToken);
    TEST(token.HasExpired() == false);

    // Expire token...
    token.OnTokenExpired();

    TEST(token.HasExpired() == true);

    TEST(observer.HasCalledBack());
    TEST(observer.CallbackCount() == 2); //Initial set callback and refresh on expiry


    token.UpdateToken(accessToken, 1, username); // Expiries are treated as seconds

    TEST(token.HasExpired() == false);


    token.Clear();

    TEST(token.Id() == Brx::Empty());
    TEST(token.Username() == Brx::Empty());
    TEST(token.AccessToken() == Brx::Empty());
    TEST(token.RefreshToken() == Brx::Empty());
}



void TestOAuth(Environment& aEnv)
{
    Debug::SetLevel(Debug::kOAuth);
    Debug::SetSeverity(Debug::kSeverityError);
    //Debug::SetSeverity(Debug::kSeverityTrace);

    Runner runner("OAuth & related service tests\n");
    runner.Add(new SuiteOAuthToken(aEnv));
    runner.Add(new SuiteTokenManager(aEnv));
    runner.Run();
}

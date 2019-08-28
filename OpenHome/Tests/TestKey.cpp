#include <stdio.h>
#include "openssl/bio.h"
#include "openssl/pem.h"
#include "openssl/rand.h"

static RSA* createRsaKey()
{
    static const char buf[] = "moderate sized string, created to give the illusion of entropy.  Not for commit but hopefully good enough for an initial test.";
    RAND_seed(buf, sizeof(buf));
    BIGNUM *bn = BN_new();
    if(!BN_set_word(bn, RSA_F4)) {
        printf("BN_set_word error\n");
        return nullptr;
    }
    RSA* rsa = RSA_new();
    if (RSA_generate_key_ex(rsa, 2048, bn, nullptr) == 0) {
        printf("RSA_generate_key_ex error\n");
        RSA_free(rsa);
        rsa = nullptr;
    }
    BN_free(bn);
    return rsa;
}

static void printKey(RSA* key)
{
    if (key == nullptr) {
        printf("Failed to create RSA for key\n");
        return;
    }
    BIO *mem = BIO_new(BIO_s_mem()); 
    RSA_print(mem, key, 0);
    static unsigned char buf[1024*10];
    BIO_read(mem, buf, sizeof(buf));
    printf("%s \n",buf);
    BIO_free(mem);
}

static void printPemKey(BIO* bio)
{
    int keylen = BIO_pending(bio);
    char* pem_key = (char*)calloc(keylen+1, 1);
    BIO_read(bio, pem_key, keylen);
    printf("\n%s\n", pem_key);
    free(pem_key);
}


static const unsigned char bigStr[] =
    "1234567890123456789012345678901234567890"
/*    "1234567890123456789012345678901234567890"
    "1234567890123456789012345678901234567890"
    "1234567890123456789012345678901234567890"
    "1234567890123456789012345678901234567890"
    "1234567890123456789012345678901234567890"
    "1234567890123456789012345678901234567890"
    "1234567890123456789012345678901234567890"
    "1234567890123456789012345678901234567890"
    "1234567890123456789012345678901234567890"
    "1234567890123456789012345678901234567890"
    "1234567890123456789012345678901234567890"
    "1234567890123456789012345678901234567890"
    "1234567890123456789012345678901234567890"
    "1234567890123456789012345678901234567890"
    "1234567890123456789012345678901234567890"
    "1234567890123456789012345678901234567890"
    "1234567890123456789012345678901234567890"
    "1234567890123456789012345678901234567890"
    "1234567890123456789012345678901234567890"
    "1234567890123456789012345678901234567890"
    "1234567890123456789012345678901234567890"*/
    "1234567890123456789012345678901234567890"
    "1234567890123456789012345678901234567890"
    "1234567890123456789012345678901234567890"
    "1234567890123456789012345678901234567890"
    "1234567890123456789012345678901234567890";
static const size_t bigStrLen = sizeof bigStr - 1;

static unsigned char encStr[2048] ={};
static unsigned char decStr[sizeof bigStr] ={};

static void testEncypt(RSA* key)
{
    printf("RSA_size(key) = %d\n", RSA_size(key));
    int encLen = RSA_public_encrypt(bigStrLen, bigStr, encStr, key, RSA_PKCS1_OAEP_PADDING);
    if (encLen < 0) {
        printf("failed to encrypt %u bytes\n", bigStrLen);
        return;
    }
    int decLen = RSA_private_decrypt(encLen, encStr, decStr, key, RSA_PKCS1_OAEP_PADDING);
    if (decLen < 0) {
        printf("failed to decrypt %d bytes\n", encLen);
        return;
    }
    if (bigStrLen != decLen) {
        printf("Original (%d) and decrypted (%d) strings have different lengths\n", bigStrLen, decLen);
        return;
    }
    if (strncmp((const char*)bigStr, (const char*)decStr, decLen) != 0) {
        printf("Original and decrypted strings are different\n");
    }
}

int main(int /*argc*/, char* /*argv*/[])
{
    RSA* rsa = createRsaKey();
    printf("RSA_size(rsa) = %d\n", RSA_size(rsa));
    if (rsa == nullptr) {
        goto cleanup;
    }
    printKey(rsa);

    BIO* bio = BIO_new(BIO_s_mem());
    if (1 != PEM_write_bio_RSAPrivateKey(bio, rsa, nullptr, nullptr, 0, nullptr, nullptr)) {
        printf("PEM_write_bio_RSAPrivateKey error\n");
        goto cleanup2;
    }
    printPemKey(bio);

    if (1 != PEM_write_bio_RSAPublicKey(bio, rsa)) {
        printf("PEM_write_bio_RSAPublicKey error\n");
        goto cleanup2;
    }
    printPemKey(bio);

    testEncypt(rsa);

cleanup2:
    BIO_free(bio);
cleanup:
    RSA_free(rsa);

    return 0;
}

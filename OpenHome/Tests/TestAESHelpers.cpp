#include <OpenHome/Buffer.h>
#include <OpenHome/AESHelpers.h>
#include <OpenHome/Private/Converter.h>
#include <OpenHome/Private/TestFramework.h>
#include <OpenHome/Private/SuiteUnitTest.h>

using namespace OpenHome;
using namespace OpenHome::TestFramework;

namespace OpenHome
{
    class SuiteAESHelpers : public Suite
    {
        public:
            SuiteAESHelpers();
        private: // from Suite
            void Test() override;
        private:
            void TestEncryptDecrypt(const Brx& aValue);
            void TestEncryptDecryptWithContentLength(const Brx& aValue);
        private:
            Bws<1024> iRawValueBuf;
            Bws<1024> iEncryptionBuf;
            Bws<1024> iDecryptionBuf;
    };

}


// SuiteAESHelpers
SuiteAESHelpers::SuiteAESHelpers()
    : Suite("AES")
{ }


void SuiteAESHelpers::Test()
{
    auto valA = Brn("Hello world");
    auto valB = Brn("A longer message which will need some amount of padding.");
    auto valC = Brn("0123456789098765"); //AES_KEY_SIZE (16bytes) in length to handle case were full block of padding required.
    auto valD = Brn("09876543212345678");
    auto valE = Brn("1234567890987654321");
    auto valF = Brn("{'key':'value', 'array': [ 'an', 'array', 'values' ], 'key2':'value2' }");

    TestEncryptDecrypt(valA);
    TestEncryptDecrypt(valB);
    TestEncryptDecrypt(valC);
    TestEncryptDecrypt(valD);
    TestEncryptDecrypt(valE);
    TestEncryptDecrypt(valF);

    TestEncryptDecryptWithContentLength(valA);
    TestEncryptDecryptWithContentLength(valB);
    TestEncryptDecryptWithContentLength(valC);
    TestEncryptDecryptWithContentLength(valD);
    TestEncryptDecryptWithContentLength(valE);
    TestEncryptDecryptWithContentLength(valF);
}


void SuiteAESHelpers::TestEncryptDecrypt(const Brx& aValue)
{
    iRawValueBuf.Replace(aValue);
    iEncryptionBuf.SetBytes(0);
    iDecryptionBuf.SetBytes(0);

    unsigned char aesKey[AESHelpers::kKeySizeInBytes] = { 'a', 'b', 'c', 'd',
                                                          'e', 'f', 'g', 'h',
                                                          'i', 'j', 'k', 'l',
                                                          'm', 'n', 'o', 'p' }; //16 bytes = 128bits for AES128 encryption

    unsigned char initKey[AESHelpers::kKeySizeInBytes] = { 'p', 'o', 'n', 'm',
                                                           'l', 'k', 'j', 'i',
                                                           'h', 'g', 'f', 'e',
                                                           'd', 'c', 'b', 'a' };

    // Encrypt trashes the initKey so we need to make a copy
    // for use when decrypting the value
    unsigned char initKeyB[AESHelpers::kKeySizeInBytes];
    memcpy(initKeyB, initKey, AESHelpers::kKeySizeInBytes);


    TEST(AESHelpers::Encrypt(aesKey, initKey, iRawValueBuf, iEncryptionBuf));
    TEST(iEncryptionBuf.Bytes() > 0);

    TEST(AESHelpers::Decrypt(aesKey, initKeyB, iEncryptionBuf, iDecryptionBuf));
    TEST(iDecryptionBuf.Bytes() > 0);

    TEST(iRawValueBuf.Equals(iDecryptionBuf));
}

void SuiteAESHelpers::TestEncryptDecryptWithContentLength(const Brx& aValue)
{
    iRawValueBuf.SetBytes(0);
    {
        WriterBuffer writer(iRawValueBuf);
        WriterBinary binaryWriter(writer);

        binaryWriter.WriteUint16Be(aValue.Bytes());
        writer.Write(aValue);
        writer.WriteFlush();
    }

    iEncryptionBuf.SetBytes(0);
    iDecryptionBuf.SetBytes(0);

    unsigned char aesKey[AESHelpers::kKeySizeInBytes] = { 'a', 'b', 'c', 'd',
                                                          'e', 'f', 'g', 'h',
                                                          'i', 'j', 'k', 'l',
                                                          'm', 'n', 'o', 'p' }; //16 bytes = 128bits for AES128 encryption

    unsigned char initKey[AESHelpers::kKeySizeInBytes] = { 'p', 'o', 'n', 'm',
                                                           'l', 'k', 'j', 'i',
                                                           'h', 'g', 'f', 'e',
                                                           'd', 'c', 'b', 'a' };

    // Encrypt trashes the initKey so we need to make a copy
    // for use when decrypting the value
    unsigned char initKeyB[AESHelpers::kKeySizeInBytes];
    memcpy(initKeyB, initKey, AESHelpers::kKeySizeInBytes);


    TEST(AESHelpers::Encrypt(aesKey, initKey, iRawValueBuf, iEncryptionBuf));
    TEST(iEncryptionBuf.Bytes() > 0);

    TEST(AESHelpers::DecryptWithContentLengthPrefix(aesKey, initKeyB, iEncryptionBuf, iDecryptionBuf));
    TEST(iDecryptionBuf.Bytes() > 0);

    TEST(aValue.Equals(iDecryptionBuf));
}




void TestAESHelpers()
{
    Runner runner("AESHelper tests.\n");
    runner.Add(new SuiteAESHelpers());
    runner.Run();
}

#include <OpenHome/Av/Debug.h>
#include <OpenHome/AESHelpers.h>
#include <OpenHome/Private/Converter.h>

#include "openssl/aes.h"

using namespace OpenHome;

// Define this for additional logging output to help debug issues
//#define AES_LOG_TRACE


namespace OpenHome
{

TBool AESHelpers::DecryptWithContentLengthPrefix(unsigned char* aAesKeyData,
                                                 unsigned char* aInitVec,
                                                 const Brx& aEncrypted,
                                                 Bwx& aDecrypted)
{
    if (!Decrypt(aAesKeyData, aInitVec, aEncrypted, aDecrypted))
    {
        return false;
    }

    const TUint len = Converter::BeUint16At(aDecrypted, 0);
    if (len > aDecrypted.Bytes() - 2)
    {
        Log::Print("AESHelpers::DecryptWithContentLengthPrefix failed. Token begins with length: %u\n", len);
        return false;
    }

    aDecrypted.Replace(aDecrypted.Ptr() + 2, len);
    return true;
}

TBool AESHelpers::Decrypt(unsigned char* aAesKeyData,
                          unsigned char* aInitVec,
                          const Brx& aEncrypted,
                          Bwx& aDecrypted)
{
    AES_KEY aesKey;
    AES_set_decrypt_key(aAesKeyData, 128, &aesKey);

    AES_cbc_encrypt(aEncrypted.Ptr(),
                    (unsigned char*)aDecrypted.PtrZ(),
                    aEncrypted.Bytes(),
                    &aesKey,
                    &aInitVec[0],
                    AES_DECRYPT);

    aDecrypted.SetBytes(aEncrypted.Bytes());
    return true;
}


TBool AESHelpers::Encrypt(unsigned char* aAesKeyData,
                          unsigned char* aInitVec,
                          Bwx& aValue,
                          Bwx& aEncryptedValue)
{
    // Assumes value passed in is not padded.
    if (!PKCSPad(aValue))
    {
        Log::Print("AESHelpers::Encrypt - Failed to pad value correctly.\n");
        return false;
    }

    AES_KEY aesKey;
    AES_set_encrypt_key(&aAesKeyData[0], 128, &aesKey);

    AES_cbc_encrypt(aValue.Ptr(),
                    (unsigned char*)aEncryptedValue.PtrZ(),
                    aValue.Bytes(),
                    &aesKey,
                    &aInitVec[0],
                    AES_ENCRYPT);

    aEncryptedValue.SetBytes(aValue.Bytes());

#ifdef AES_LOG_TRACE
    Log::Print("Encrypted Value: ");
    for(TUint i = 0; i < aEncryptedValue.Bytes(); ++i)
    {
        Log::Print("%02x ", aEncryptedValue.At(i));
    }
    Log::Print("\n");
#endif

    return true;
}




TBool AESHelpers::PKCSPad(Bwx& aValue)
{
    /* AES Encryption requires values to be padded into 8 byte blocks.
     * This has to be done using PKCS#5/#7 padding.
     *
     * See: https://tools.ietf.org/html/rfc5652#section-6.3
     * NOTE: RFC defines PKCS#5. PKCS#7 has been later defined to work
     *       on inputs over 256bytes in length. */
    const TUint currentLength = aValue.Bytes();
    TUint paddingRequired = AES_BLOCK_SIZE - (currentLength % AES_BLOCK_SIZE);

    // If we're already aligned to AES_BLOCK_SIZE we need to append a full block
    if (paddingRequired == 0)
    {
        paddingRequired = AES_BLOCK_SIZE;
    }

    // PKCS requires you to set any padding bytes to the number
    // of padding bytes added.
    const TByte paddingValue = (const TByte)paddingRequired;

#ifdef AES_LOG_TRACE
    Log::Print("ProviderOAuth::PKCSPad - Current Length: %d byte(s), padding required: %d bytes(s). Padding Value: %d\n",
               currentLength,
               paddingRequired,
               paddingValue);
#endif


    if (aValue.BytesRemaining() < paddingRequired)
    {
        Log::Print("AESHelpers::PKCSPad - Need %d bytes of padding, but only %d byte(s) are available.\n", paddingRequired, aValue.BytesRemaining());
        return false;
    }
    else
    {
        for(TUint i = 0; i < paddingRequired; ++i)
        {
            aValue.Append(paddingValue);
        }

        return true;
    }
}

} //namespace OpenHome


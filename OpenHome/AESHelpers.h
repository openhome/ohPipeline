#pragma once

#include <OpenHome/Types.h>
#include <OpenHome/Buffer.h>
#include <OpenHome/Private/Debug.h>

namespace OpenHome {

class AESHelpers
{
    public:
        static const TInt kKeySizeInBytes = 16; //Means we use AES128. Other options: AES256 & AES512 are available

        // Decrypt a value using provided AESKeys
        static TBool Decrypt(unsigned char* aAesKeyData,
                             unsigned char* aInitVec,
                             const Brx& aEncrypted,
                             Bwx& aDecrypted);

        // Decrypt a value using the provided AESKeys
        // assuming the decrypted value is prefixed with the content length
        static TBool DecryptWithContentLengthPrefix(unsigned char* aAesKeyData,
                                                    unsigned char* aInitVec,
                                                    const Brx& aEncrypted,
                                                    Bwx& aDecrypted);

        /* Encrypt a value using provided AESKeys
         * Encrypted value must be provided in a writable buffer (with at least AES_BLOCK_SIZE
         * space available) as value may need to be padded prior to encryption) */
        static TBool Encrypt(unsigned char* aAesKeyData,
                             unsigned char* aInitVec,
                             Bwx& aValue,
                             Bwx& aEncryptedValue);

    private:
        static TBool PKCSPad(Bwx& aValue);
};


} // namespace OpenHome


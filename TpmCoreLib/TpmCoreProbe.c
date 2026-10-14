#include "Tpm.h"
#include "Marshal_fp.h"
#include "AlgorithmCap_fp.h"
#include <TpmCoreProbe.h>

UINT32
EFIAPI
VtpmCoreSmokeTest (VOID)
{
    UINT32 passed = 0;
    UINT32 value = 0x12345678;
    UINT32 decoded = 0;
    BYTE buffer[4];
    BYTE *cursor = buffer;
    INT32 remaining = sizeof(buffer);
    TPMS_ALG_PROPERTY property;
    SHA256_CTX hash;
    BYTE digest[32];
    AES_KEY aes;
    BYTE encrypted[16];
    static const BYTE expectedHash[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,
        0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,
        0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
    };
    static const BYTE key[16] = {
        0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
        0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f
    };
    static const BYTE plaintext[16] = {
        0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,
        0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff
    };
    static const BYTE expectedAes[16] = {
        0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,
        0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a
    };

    if(UINT32_Marshal(&value, &cursor, &remaining) == 4 &&
       remaining == 0 && buffer[0] == 0x12 && buffer[3] == 0x78)
    {
        cursor = buffer;
        remaining = sizeof(buffer);
        if(UINT32_Unmarshal(&decoded, &cursor, &remaining) == TPM_RC_SUCCESS &&
           decoded == value && remaining == 0)
            passed |= VTPM_CORE_TEST_MARSHAL;
    }
    cursor = buffer;
    remaining = 3;
    decoded = 0xabcdef01;
    if(UINT32_Unmarshal(&decoded, &cursor, &remaining) == TPM_RC_INSUFFICIENT &&
       cursor == buffer && decoded == 0xabcdef01)
        passed |= VTPM_CORE_TEST_BOUNDS;

    if(AlgorithmCapGetOneImplemented(TPM_ALG_SHA256, &property) &&
       property.alg == TPM_ALG_SHA256 &&
       AlgorithmCapGetOneImplemented(TPM_ALG_AES, &property) &&
       property.alg == TPM_ALG_AES)
        passed |= VTPM_CORE_TEST_PROFILE;

    // Known-answer checks of the backend selected by the upstream adapters.
    if(SHA256_Init(&hash) == 1 && SHA256_Update(&hash, "abc", 3) == 1 &&
       SHA256_Final(digest, &hash) == 1 &&
       memcmp(digest, expectedHash, sizeof(digest)) == 0)
        passed |= VTPM_CORE_TEST_SHA256;
    if(AES_set_encrypt_key(key, 128, &aes) == 0)
    {
        AES_encrypt(plaintext, encrypted, &aes);
        if(memcmp(encrypted, expectedAes, sizeof(encrypted)) == 0)
            passed |= VTPM_CORE_TEST_AES128;
    }
    return passed;
}

// Firmware adaptation of Platform/src/Entropy.c. No host CRT or time seed.
#include "Tpm.h"
#include <TpmCoreProbe.h>

static BOOL mEntropyInitialized;
static BOOL mEntropyFailed;
static UINT32 mLastEntropy;

static BOOL EntropyWord(UINT32 *word)
{
    UINTN attempt;
    for(attempt = 0; attempt < 10; ++attempt)
    {
        if(AsmRdRand32(word))
            return TRUE;
        CpuPause();
    }
    return FALSE;
}

int32_t _plat__GetEntropy(unsigned char *entropy, uint32_t amount)
{
    UINT32 ecx;
    UINT32 word;
    UINT32 count;
    if(mEntropyFailed)
        return -1;
    if(!mEntropyInitialized)
    {
        AsmCpuid(1, NULL, NULL, &ecx, NULL);
        // Never execute RDRAND on a processor which does not advertise it.
        if((ecx & BIT30) == 0 || !EntropyWord(&mLastEntropy))
        {
            mEntropyFailed = TRUE;
            return -1;
        }
        // Discard the first block, retain it for the continuous comparison.
        mEntropyInitialized = TRUE;
    }
    if(amount == 0)
        return 0;
    if(entropy == NULL || !EntropyWord(&word) || word == mLastEntropy)
    {
        // Latched for this module lifetime; initialization cannot clear failure.
        mEntropyFailed = TRUE;
        return -1;
    }
    mLastEntropy = word;
    count = amount < sizeof(word) ? amount : sizeof(word);
    CopyMem(entropy, &word, count);
    // Partial results follow the upstream contract; DRBG collects more if needed.
    return (int32_t)count;
}

UINT32 EFIAPI VtpmEntropySmokeTest(void)
{
    UINT32 passed = 0;
    BYTE buffer[8];
    UINTN index;
    BOOL bounded = TRUE;
    if(_plat__GetEntropy(NULL, 0) != 0)
        return 0;
    passed |= VTPM_ENTROPY_TEST_INIT;
    SetMem(buffer, sizeof(buffer), 0xa5);
    if(_plat__GetEntropy(&buffer[2], 1) == 1 &&
       buffer[0] == 0xa5 && buffer[1] == 0xa5 && buffer[3] == 0xa5 &&
       buffer[4] == 0xa5 && buffer[7] == 0xa5)
        passed |= VTPM_ENTROPY_TEST_SHORT;
    SetMem(buffer, sizeof(buffer), 0xa5);
    if(_plat__GetEntropy(&buffer[2], 6) == 4 &&
       buffer[0] == 0xa5 && buffer[1] == 0xa5 &&
       buffer[6] == 0xa5 && buffer[7] == 0xa5)
        passed |= VTPM_ENTROPY_TEST_PARTIAL;
    for(index = 0; index < 64; ++index)
    {
        if(_plat__GetEntropy(buffer, 4) != 4)
        {
            bounded = FALSE;
            break;
        }
    }
    if(bounded && _plat__GetEntropy(NULL, 0) == 0)
        passed |= VTPM_ENTROPY_TEST_REPEAT;
    // No random bytes, keys or seeds are logged. This is not an RNG certification.
    SetMem(buffer, sizeof(buffer), 0);
    return passed;
}
